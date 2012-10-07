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
  //Status: Nb of calls done in this win
  int nb_calls;
  //Status: Pointer to the older call in this win
  struct vmodth_call *last_call;
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
struct vmodth_calls_set {
  //Hashmap of the call sets
  struct vmodth_calls* hashmap[4096];
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

// Private: Fetch or create the call set from the given key
struct vmodth_calls* _vmod_get_call_set_from_key(struct vmodth_calls_set* calls_set, char* key, char* window_limits) {
  struct vmodth_calls* result = NULL;
  int hash_key;

  //Get the hash key from the key
  hash_key = _vmod_hash(key) & 0xfff;

  //Search in the hash bucket
  LOCK_READ();
  struct vmodth_calls* cur = calls_set->hashmap[hash_key];
  while(cur) {
    if(strcmp(cur->key, key) == 0) {
      result = cur;
      break;
    }
    cur = cur->next;
  }
  UNLOCK();

  //If not found, we have to create this new call set
  if(!result) {
    //First we do the parsing of the windows. If it fails, we will stop.
    struct vmodth_call_win* parsed_wins = NULL;
    int parsed_win_count = 0;
    int parsed_max = 0;
    int parsed_length = 0;
    char c;
    while(c = *window_limits++) {
      //Init variables
      parsed_max = 0;
      parsed_length = 0;

      //Get rid of any whitespaces
      while(c == ' ') {
        c = *window_limits++;
      }

      //Expecting a number
      while(48 <= c && c <= 57) {
        parsed_max = parsed_max*10 + (c - 48);
        c = *window_limits++;
      }
      if(parsed_max == 0) {
        break;
      }

      //Get rid of any whitespaces
      while(c == ' ') {
        c = *window_limits++;
      }

      //Expecting the string "req/"
      if(strncmp(--window_limits, "req/", 4) != 0) {
        break;
      }
      window_limits+=4;
      c = *window_limits++;

      //Expecting either a number and a duration qualitifer (s,m,h,d), or a duration qualitifer
      while(48 <= c && c <= 57) {
        parsed_length = parsed_length*10 + (c - 48);
        c = *window_limits++;
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
      c = *window_limits++;
      if(c != 0 && c != ',') {
        break;
      }

      //Now let's create the new win struct
      parsed_win_count++;
      if(parsed_win_count == 0) {
        parsed_wins = malloc(sizeof(struct vmodth_call_win));
      }
      else {
        parsed_wins = realloc(parsed_wins, sizeof(struct vmodth_call_win) * parsed_win_count);
      }
      parsed_wins[parsed_win_count - 1].length = parsed_length;
      parsed_wins[parsed_win_count - 1].max_calls = parsed_max;
      parsed_wins[parsed_win_count - 1].nb_calls = 0;
      parsed_wins[parsed_win_count - 1].last_call = NULL;

      //Finally, if the current character is null, just stop here
      if(c == 0) {
        break;
      }
    }

    //If we failed to parse any window, return NULL
    if(parsed_win_count == 0) {
      return result;
    }

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
    LOCK_WRITE();
    result->next = calls_set->hashmap[hash_key];
    calls_set->hashmap[hash_key] = result;
    UNLOCK();
  }

  return result;
}

// Private: Update the window counter (e.g. the last minute calls counter)
void 
_vmod_update_window_counter(struct vmodth_call_win* call_win, double now) {
  LOCK_WRITE();
  while(call_win->last_call && call_win->last_call->time < now - call_win->length) {
    call_win->last_call = call_win->last_call->prev;
    call_win->nb_calls--;
  }
  UNLOCK();
}

// Private: Return the amount of time to wait to be allowed in this time window
double 
_vmod_get_time_wait_for_window_compliance(struct vmodth_call_win* call_win, double now) {
  double result = 0.0;

  LOCK_READ();
  if(call_win->nb_calls >= call_win->max_calls) {
    result = call_win->length - (now - call_win->last_call->time);
  }
  UNLOCK();

  return result;
}

// Private: Update the window counter (e.g. the last minute calls counter)
void 
_vmod_increment_window_counter(struct vmodth_call* new_call, struct vmodth_call_win* call_win) {
  LOCK_WRITE();
  call_win->nb_calls++;
  if(call_win->last_call == NULL) {
    call_win->last_call = new_call;
  }
  UNLOCK();
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

  LOCK_WRITE();
  while(calls->nb_calls > max_win_max_calls || calls->last->time < now - max_win_length) {
    prev = calls->last->prev;
    free(calls->last);
    calls->last = prev;
    prev->next = NULL;
    calls->nb_calls--;
  }
  UNLOCK();
}

// Public: Vmod init function, initialize the data structure
int
init_function(struct vmod_priv *pc, const struct VCL_conf *conf)
{
  struct vmodth_calls_set *calls_set;

  //Init the rwlock
  pthread_rwlock_init(&vmodth_rwlock, NULL);

  //Initialize the data structure
  LOCK_WRITE();
  calls_set = ((struct vmodth_calls_set*)pc->priv);
  if (!calls_set) {
    pc->priv = malloc(sizeof(struct vmodth_calls_set));
    AN(pc->priv);
    pc->free = free;
    calls_set = ((struct vmodth_calls_set*)pc->priv); 
    memset(calls_set->hashmap, 0, sizeof(calls_set->hashmap));
  }
  UNLOCK();

  return (0);
}

// Public: is_allowed VCL command
double
vmod_is_allowed(struct sess *sp, struct vmod_priv *pc, const char* key, const char* window_limits)
{
  struct vmodth_calls_set *calls_set;
  struct vmodth_calls *calls;
	double result = 0;

  //Get the call set for this given key
  calls_set = ((struct vmodth_calls_set*)pc->priv);
  AN(calls_set);
  calls = _vmod_get_call_set_from_key(calls_set, key, window_limits);
  //calls can be NULL if the parsing of the windows failed
  if(calls == NULL) {
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
    return result;
  }

  //We are authorized
  //Add the call in the call list
  LOCK_WRITE();
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
  UNLOCK();

  //Increment the windows counters and update if necessary their pointers
  for(int i = 0; i < calls->nb_wins; i++) {
    _vmod_increment_window_counter(new_call, &calls->wins[i]);
  }

  //Remove the older entries
  _vmod_remove_older_entries(calls, now);

  return result;
}
