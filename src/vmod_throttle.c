#include <stdlib.h>
#include <pthread.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

//Individual call
struct vmodth_call {
  //Call time
  double time;
  //next call
  struct vmodth_call *next;
  //prev call
  struct vmodth_call *prev;
};

//Call window
struct vmodth_call_win {
  //Def: Win length
  int length;
  //Def: Max nb of calls in this time win
  int max_calls;
  //Status: Pointer to the older call in this win
  struct vmodth_call *last_call;
  //Cache: Nb of calls done in this win
  int nb_calls;
};

//Call set, identified by a key
struct vmodth_calls {
  //Def: Key
  char* key;
  //Def: Limit windows
  struct vmodth_call_win* wins;
  //Def: Nb of windows
  int nb_wins;
  //Status: Linked list, first call
  struct vmodth_call* first;
  //Status: Linked list, last call
  struct vmodth_call* last;
  //Status: Next call set
  struct vmodth_calls* next;
  //Cache: Nb of calls
  int nb_calls;
};

//Set of all the call sets
struct vmodth_priv {
  //Status: Hashmap of the call sets
  struct vmodth_calls* hashmap[4096];
  //status: Total number of calls to is_allowed(), which an answer of 0.0s. Will 
  unsigned long total_accepted_calls;
};

//Global rwlock, for all read/modification operations on the data structure
pthread_rwlock_t vmodth_rwlock;
#define LOCK_READ() assert(pthread_rwlock_rdlock(&vmodth_rwlock) == 0);
#define LOCK_WRITE() assert(pthread_rwlock_wrlock(&vmodth_rwlock) == 0);
#define UNLOCK() pthread_rwlock_unlock(&vmodth_rwlock);


// Private: the djb2 string hash algorithm
unsigned long
_vmod_hash(unsigned char *str) {
  unsigned long hash = 5381;
  int c;

  while (c = *str++)
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

// Private: Parse a window limit list, return the current window limit, put the cursor at
// the beginning of the next one.
// A result window with a length of 0 means it failed to parse.
struct vmodth_call_win _vmod_parse_win(char** wins) {
    struct vmodth_call_win result;
    int parsed_max = 0;
    int parsed_length = 0;
    char c;

    //Init the result win
    result.length = 0;
    result.max_calls = 0;
    result.nb_calls = 0;
    result.last_call = NULL;

    if(c = *(*wins)++) {
      //Get rid of any whitespaces
      while(c == ' ') {
        c = *(*wins)++;
      }

      //Expecting a number
      while(48 <= c && c <= 57) {
        parsed_max = parsed_max*10 + (c - 48);
        c = *(*wins)++;
      }
      if(parsed_max == 0) {
        return result;
      }

      //Get rid of any whitespaces
      while(c == ' ') {
        c = *(*wins)++;
      }

      //Expecting the string "req/"
      if(strncmp(--(*wins), "req/", 4) != 0) {
        return result;
      }
      (*wins) += 4;
      c = *(*wins)++;

      //Expecting either a number and a duration qualitifer (s,m,h,d), or a duration qualitifer
      while(48 <= c && c <= 57) {
        parsed_length = parsed_length*10 + (c - 48);
        c = *(*wins)++;
      }
      if(parsed_length == 0) {
        parsed_length = 1;
      }
      if(c == 'm') {
        parsed_length *= 60;
      }
      else if(c == 'h') {
        parsed_length *= 3600;
      }
      else if(c == 'd') {
        parsed_length *= 24*3600;
      }

      //Then expecting the next char to be either NULL or the ',' separator
      c = *(*wins)++;
      if(c != 0 && c != ',') {
        return result;
      }

      //Now let's create the new win struct
      result.length = parsed_length;
      result.max_calls = parsed_max;
      result.nb_calls = 0;
      result.last_call = NULL;
    }

    return result;
}

// Private: Fetch or create the call set from the given key
struct vmodth_calls* _vmod_get_call_set_from_key(struct vmodth_priv* priv, char* key, int create_if_not_existing, char* window_limits) {
  struct vmodth_calls* result = NULL;
  int hash_key;

  //Get the hash key from the key
  hash_key = _vmod_hash(key) & 0xfff;

  //Search in the hash bucket
  struct vmodth_calls* cur = priv->hashmap[hash_key];
  while(cur) {
    if(strcmp(cur->key, key) == 0) {
      result = cur;
      break;
    }
    cur = cur->next;
  }

  //If not found, and we are allowed to create, we have to create this new call set
  if(!result && create_if_not_existing) {
    //First we do the parsing of the windows. If it fails, we will stop.
    struct vmodth_call_win* parsed_wins = NULL;
    int parsed_win_count = 0;
    struct vmodth_call_win parsed_win;

    //We parse windows until either we reached the end of string, or a parsing failed.
    while(*window_limits) {
      //Parse a window
      parsed_win = _vmod_parse_win(&window_limits);

      if(!parsed_win.length) {
        //Parsing failed. Quit.
        break;
      }

      //Now let's copy the result in the window list
      parsed_win_count++;
      if(parsed_win_count == 1) {
        parsed_wins = malloc(sizeof(struct vmodth_call_win));
      }
      else {
        parsed_wins = realloc(parsed_wins, sizeof(struct vmodth_call_win) * parsed_win_count);
      }
      memcpy(parsed_wins + (parsed_win_count - 1), &parsed_win, sizeof(struct vmodth_call_win));
    }

    //If we failed to parse any window, return NULL
    if(parsed_win_count == 0) {
      return result;
    }

    //Create the actual vmodth_calls structure
    result = malloc(sizeof(struct vmodth_calls));
    AN(result);
    result->key = malloc(strlen(key) + 1);
    strncpy(result->key, key, strlen(key) + 1);
    result->first = NULL;
    result->last = NULL;
    result->nb_calls = 0;
    result->nb_wins = parsed_win_count;
    result->wins = parsed_wins;

    //Place it, in first position
    result->next = priv->hashmap[hash_key];
    priv->hashmap[hash_key] = result;
  }

  return result;
}

// Private: Update the window counter (e.g. the last minute calls counter)
void 
_vmod_update_window_counter(struct vmodth_call_win* call_win, double now) {
  while(call_win->last_call && call_win->last_call->time < now - call_win->length) {
    call_win->last_call = call_win->last_call->prev;
    call_win->nb_calls--;
  }
}

// Private: Return the amount of time to wait to be allowed in this time window
double 
_vmod_get_time_wait_for_window_compliance(struct vmodth_call_win* call_win, double now) {
  double result = 0.0;

  if(call_win->nb_calls >= call_win->max_calls) {
    result = call_win->length - (now - call_win->last_call->time);
  }

  return result;
}

// Private: Update the window counter (e.g. the last minute calls counter)
void 
_vmod_increment_window_counter(struct vmodth_call* new_call, struct vmodth_call_win* call_win) {
  call_win->nb_calls++;
  if(call_win->last_call == NULL) {
    call_win->last_call = new_call;
  }
}

// Private: Remove older entries
void 
_vmod_remove_older_entries(struct vmodth_calls* calls, double now) {
  struct vmodth_call *prev;
  int max_win_max_calls = 0;
  int max_win_length = 0;

  //Get the biggest of the max nb calls of the different time windows, and the
  //biggest window length
  for(int i = 0; i < calls->nb_wins; i++) {
    if(calls->wins[i].max_calls > max_win_max_calls) {
      max_win_max_calls = calls->wins[i].max_calls;
    }
    if(calls->wins[i].length > max_win_length) {
      max_win_length = calls->wins[i].length;
    }
  }

  while(calls->last && 
    (calls->nb_calls > max_win_max_calls || calls->last->time < now - max_win_length)) {
    prev = calls->last->prev;
    free(calls->last);
    calls->last = prev;
    if(calls->last) {
      calls->last->next = NULL;
    }
    calls->nb_calls--;
  }
}

// Private: Garbage collector
void
_vmod_garbage_collector(struct vmodth_priv* priv, int hashmap_slot, double now) {
  struct vmodth_calls *calls;
  struct vmodth_calls *prev_calls;
  struct vmodth_calls *tmp;

  prev_calls = NULL;
  calls = priv->hashmap[hashmap_slot];
  //Let's look at each vmodth_calls: remove older vmodth_call, and if then empty,
  //remove the vmodth_calls itself.
  while(calls) {
    //First update the windows counters and pointers
    for(int i = 0; i < calls->nb_wins; i++) {
      _vmod_update_window_counter(&calls->wins[i], now);
    }

    //Then update the list of vmodth_call
    _vmod_remove_older_entries(calls, now);
    
    //Finally, if there is no longer any vmodth_call, delete this vmodth_calls, and goto next
    if(calls->last == NULL) {
      if(prev_calls == NULL) {
        priv->hashmap[hashmap_slot] = calls->next;
      }
      else {
        prev_calls->next = calls->next;
      }
      tmp = calls->next;
      free(calls);
      calls = tmp;
    }
    //Else let's just move on to the next one
    else {
      prev_calls = calls;
      calls = calls->next;
    }
  }
}

// Private: Free all memory
void
_vmod_free_all(void* data) {
  struct vmodth_priv *priv;
  struct vmodth_calls *calls;
  struct vmodth_call *call;
  
  //Seek and destroy. Mwahahahahh
  priv = ((struct vmodth_priv*)data);
  for(int i = 0; i < 4096; i++) {
    calls = priv->hashmap[i];
    while(calls) {
      //Free call entries
      call = calls->last;
      while(call) {
        calls->last = call->prev;
        free(call);
        call = calls->last;
      }

      //Free wins
      free(calls->wins);

      //Free calls itself
      priv->hashmap[i] = calls->next;
      free(calls);
      calls = priv->hashmap[i];
    }
  }
  free(data);
}

// Public: Vmod init function, initialize the data structure
int
init_function(struct vmod_priv *pc, const struct VCL_conf *conf) {
  struct vmodth_priv *priv;

  //Init the rwlock
  pthread_rwlock_init(&vmodth_rwlock, NULL);

  //Initialize the data structure
  LOCK_WRITE();
  priv = ((struct vmodth_priv*)pc->priv);
  if (!priv) {
    pc->priv = malloc(sizeof(struct vmodth_priv));
    AN(pc->priv);
    //Is this ever called?
    pc->free = _vmod_free_all;
    priv = ((struct vmodth_priv*)pc->priv); 
    memset(priv->hashmap, 0, sizeof(priv->hashmap));
    priv->total_accepted_calls = 0;
  }
  UNLOCK();

  return (0);
}

// Public: is_allowed VCL command
double
vmod_is_allowed(struct sess *sp, struct vmod_priv *pc, const char* key, const char* window_limits) {
  struct vmodth_priv *priv;
  struct vmodth_calls *calls;
	double result = 0;

  //Our persistent data structure
  priv = ((struct vmodth_priv*)pc->priv);
  AN(priv);

  LOCK_WRITE();

  //Get the call set for this given key
  calls = _vmod_get_call_set_from_key(priv, key, 1, window_limits);
  //calls can be NULL if the parsing of the windows failed
  if(calls == NULL) {
    UNLOCK();
    return -1.0;
  }

  //Get time
  //TODO: first a faster one, let's avoid a syscall. Find and use the request time.
  struct timeval tv;
  gettimeofday(&tv, NULL);
  double now = tv.tv_sec+tv.tv_usec/1000000.0;

  //Update the windows counters and pointers, with the current time
  for(int i = 0; i < calls->nb_wins; i++) {
    _vmod_update_window_counter(&calls->wins[i], now);
  }

  //Now lets check if we are allowed
  //If we are not, let's return how much time we should wait before being allowed
  double win_wait;
  for(int i = 0; i < calls->nb_wins; i++) {
    win_wait = _vmod_get_time_wait_for_window_compliance(&calls->wins[i], now);
    if(win_wait > result) {
      result = win_wait;
    }
  }
  if(result > 0.0) {
    UNLOCK();
    return result;
  }

  //We are authorized
  //Add the call in the call list
  struct vmodth_call* new_call = malloc(sizeof(struct vmodth_call));
  new_call->time = now;
  new_call->next = calls->first;
  new_call->prev = NULL;
  if(calls->first) {
    calls->first->prev = new_call;
  }
  calls->first = new_call;
  if(calls->last == NULL) {
    calls->last = new_call;
  }
  calls->nb_calls++;

  //Increment the total accepted calls counter
  priv->total_accepted_calls++;

  //Increment the windows counters and update if necessary their pointers
  for(int i = 0; i < calls->nb_wins; i++) {
    _vmod_increment_window_counter(new_call, &calls->wins[i]);
  }

  //Remove the older vmodthcall that are no longer usefull
  _vmod_remove_older_entries(calls, now);

  //Every 16 calls, call the garbage collector on one of the hashmap slot
  //This is necessary for clients that stopped requesting, we need to clear them
  //So everything is cleaned every 4096*16=65536 calls
  if((priv->total_accepted_calls & 0xf) == 0) {
    _vmod_garbage_collector(priv, (priv->total_accepted_calls >> 4) & 0xfff, now);
  }

  UNLOCK();

  return result;
}

// Public: is_allowed VCL command
int
vmod_remaining_calls(struct sess *sp, struct vmod_priv *pc, const char* key, const char* window_limit) {
  int result = -1;
  char* window_limit_str;
  struct vmodth_priv *priv;
  struct vmodth_calls *calls;
  struct vmodth_call_win win;

  LOCK_READ();

  //Our persistent data structure
  priv = ((struct vmodth_priv*)pc->priv);
  AN(priv);

  //Get the call set for this given key
  calls = _vmod_get_call_set_from_key(priv, key, 0, NULL);
  if(calls == NULL) {
    //calls not found, we return an error (-1)
    UNLOCK();
    return result;
  }

  //Now parse the window_limit
  window_limit_str = window_limit;
  win = _vmod_parse_win(&window_limit_str);

  if(!win.length) {
    //Parsing failed.
    UNLOCK();
    return result;
  }

  //Ok now let's iterate within the windows and find ours
  for(int i = 0; i < calls->nb_wins; i++) {
    if(calls->wins[i].max_calls == win.max_calls && calls->wins[i].length == win.length) {
      //Found it! Return the remaining calls.
      result = calls->wins[i].max_calls - calls->wins[i].nb_calls;
    }
  }

  UNLOCK();

  return result;
}

// Public: memory_usage VCL command, used for debugging
int
vmod_memory_usage(struct sess *sp, struct vmod_priv *pc) {
  int result = 0;
  struct vmodth_priv *priv;
  struct vmodth_calls *calls;
  struct vmodth_call *call;

  LOCK_READ();

  //First the size of the call_set
  result += sizeof(struct vmodth_priv);
  
  //Then the size of the calls
  priv = ((struct vmodth_priv*)pc->priv);
  for(int i = 0; i < 4096; i++) {
    calls = priv->hashmap[i];
    while(calls) {
      //result += calls->nb_calls * sizeof(struct vmodth_call);
      call = calls->last;
      while(call) {
        result += sizeof(struct vmodth_call);
        call = call->prev;
      }
      result += calls->nb_wins * sizeof(struct vmodth_call_win);
      calls = calls->next;
    }
  }

  UNLOCK();

  return result;
}