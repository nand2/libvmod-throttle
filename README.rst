=============
vmod_throttle
=============

-------------------------
Varnish Throttling Module
-------------------------

:Author: Nicolas Deschildre
:Date: 2012-10-06
:Version: 0.1
:Manual section: 3

SYNOPSIS
========

import throttle;

DESCRIPTION
===========

Current status: Finished, going through tests before going to production. You're welcome to test it, but note that it was not used in production yet, AFAIK.

This vmod most obvious uses are to handle denial of services by a single user (or bot) punching through the cache, or to set rate limits to API calls you provide.

It will allow you to set rate limiting, on several different time windows, per path/IP/whatever you want. If a time window limit was reached, this vmod will return you the time to wait before this call will be authorized.
With this return information, you can handle rate limit overflow by either abruptly returning an error, or by actually waiting the necessary time if you deems it reasonable, for a smoother rate limit overflow handling.

Please note that at the moment, there is no native way (AFAIK) to wait within Varnish (and no, using sleep() in the VCL is a *bad* idea), so you'll have to hack your way to it, e.g. redirect to a separate server that will wait for you and come back later.

Please read carefully the "Memory management and denial of service" section below to avoid misuses that could lead to denial of services.

FUNCTIONS
=========

is_allowed
----------

Prototype
        ::

                is_allowed(STRING key, STRING rate_limits)
Arguments
    key: A unique key that will identify what you are throttling. Can be used in may ways. See the examples below.

    rate_limits: A list of different rate limits you want to put on this call. The syntax is the following: "[nb_of_calls]req/[duration][durations_size], ...". Example: "3req/s, 10req/30s, 30req/5m, 100req/h". Please note that we are using the Varnish duration size identifiers: s, m (and not mn/min), h, d. WARNING: You cannot define different rate_limits for a same key. If you do, only the one given in the first call will be used. See API rate limiting example below.
Return value
	DURATION
Description
    Increments the call counters.
    Returns 0.0 if the call was authorized, or the time to wait if one of the time window limit was reached.
Example
    Prevent a single user (or crazy googlebot) to make a denial of service by punching through the cache: limit MISS calls on non-static assets by IP, max 3 req/s, 10 req/30s, 30 req/5m::

            sub vcl_miss {
                if(req.url !~ "\.(jpg|jpeg|png|gif|ico|swf|css|js|html|htm)$") {
                    if(throttle.is_allowed("ip:" + client.ip, "3req/s, 10req/30s, 30req/5m") > 0s) {
                            error 429 "Calm down";
                    }
                }
            }

    API rate limiting: limit calls by IP and API call: max 2 req/s, 100 req/2h, 1000 req/d. If an API-key is given, allow more.::

            sub vcl_recv {
                if(req.url ~ "^/my_api_path/my_api_name") {
                    //Consider using libvmod-redis to share apikey infos between Varnish && your api key management app
                    if(req.http.X-apikey *is valid*) {
                        if(throttle.is_allowed("ip:" + client.ip" + ":api:[my_api_name]:auth", "5req/s, 10000req/d") > 0s) {
                           error 429 "Calm down";
                        }
                    else
                    {
                        if(throttle.is_allowed("ip:" + client.ip" + ":api:[my_api_name]" + req.url, "2req/s, 100req/2h, 1000req/d") > 0s) {
                           error 429 "Calm down";
                        }
                    }
                }
            }

    Please note: You cannot set 2 differents set of rate limits for a same key. (If you do, only one will be used, and the other will be ignored). In this example, simply add some extra text to the key to differentiate the authentificated calls from the non-authentificated ones.


remaining_calls
---------------

Prototype
        ::

                remaining_calls(STRING key, STRING rate_limit)
Arguments
    key: The unique key that will identify what you are throttling, used with the is_allowed() function.

    rate_limit: A single rate limit, with the same syntax than in the is_allowed() function. It has to be one of the rate limits you defined in the is_allowed() function.
Return value
    INT
Description
    Return the number of remaining allowed calls for a given key, and for a given rate limitation.
Example
    In the API example above, show in a header the remaining calls for the hour::

            sub vcl_recv {
                if(req.url ~ "^/my_api_path/my_api_name") {
                    if(throttle.is_allowed("ip:" + client.ip" + ":api:[my_api_name]", "5req/s, 100req/h") > 0s) {
                       error 429 "Calm down";
                    }
                }
            }

            sub vlc_deliver {
                if(req.url ~ "^/my_api_path/my_api_name") {
                    set resp.http.X-throttle-remaining-calls = throttle.remaining_calls("ip:" + client.ip" + ":api:[my_api_name]", "100req/h");
                }
            }

MEMORY MANAGEMENT AND DENIAL OF SERVICE
=======================================

If used incorrectly, this tool could let an attacker force Varnish to consume all available memory and crash. It would be too bad to be DoS'ed by a tool that prevents DoS!
What you need to know is that this vmod will keep in memory the time of the revelant last requests for each key you provide. And this memory is *outside* of the memory you specify to Varnish for caching. (So if you specify 4G of RAM to varnish, this vmod memory will be on top of it.)

For a given key, the amount of necessary memory is at its maximum fixed to the maximum number of request limit you give to this key, multiplied by 16 bytes. For example:: 

        if(throttle.is_allowed("pouet", "2req/s, 100req/h, 1000req/d") > 0s)

For the key "pouet", the maximum memory usage will be 1000 (the maximum number between 2, 100, and 1000) multiplied by 16 bytes = 16 kbytes. Now, with a more advanced key::

        if(throttle.is_allowed("ip:" + client.ip, "2req/s, 100req/3h, 1000req/d") > 0s)

We now have one key per client IP, which will each consume 16kbytes maximum. That is potentially unlimited. So what you also need to know is that the request times are kept in memory until they get older than the biggest time window: here one day (the biggest between 1s, 3 hours and 1 day).
So if you take an average of 10,000 differents IP per day, that would cost at the maximum (if every IP was making 1000 calls), 10,000 * 16kbytes = 160 mbytes. That begins to be quite a number. So one can reduce this number by keeping request limits lower. For example::

        if(throttle.is_allowed("ip:" + client.ip, "2req/s, 30req/h") > 0s)

This would reduce the maximum memory consumption, with 10,000 differents IP per day, to 10,000 * 30 * 16 = 4.8 mbytes. Much better. But wait! Now that we no longer have the 1 day window, the request times will only be kept for the new largest window, 1 hour. So if we have around 1,000 different IP per hour, that makes a maximum memory consumption of 1,000 * 30 * 16 = 480 kbytes. Muuch better! So we see that the time window sizes and lengths has a big impact on memory consumption.

With the following example, we are theorically still open to distributed denial of service due to this vmod, but with the required number of necessary clients to consume all memory, it is much more likely that your backend services will fall and crash first. (And remember, we only use at maximum a fixed amount of memory per key, whatever the number of calls for this key).

When we begin to be vulnerable to denial of service by a single user is when a single user can have an unlimited number of keys::

        if(throttle.is_allowed("ip:" + client.ip + ":path:" + req.url, "2req/s, 30req/h") > 0s)

With this example, you would limit the request rate per IP and per URL. A single user can thus create an unlimited number of keys, and thus consume an unlimited amount of memory, and make a denial of service by crashing varnish. So if you are in a case when you want to have different rate limits per path, it is a good idea to normalize the paths, and have a limited number of them only. For example::

        if(req.url ~ "^/my_api_path/my_api_name") {
            if(throttle.is_allowed("ip:" + client.ip + ":api:api_name", "2req/s, 30req/h") > 0s)

Finally, if you want to want to track the memory usage of this throttle vmod , you can use this command::

        if(req.url == "/my_admin_page") {
            set resp.http.X-throttle-memusage = throttle.memory_usage();
        }


INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the varnishtest tool.

Usage::

 ./configure VARNISHSRC=DIR [VMODDIR=DIR]

`VARNISHSRC` is the directory of the Varnish source tree for which to
compile your vmod. Both the `VARNISHSRC` and `VARNISHSRC/include`
will be added to the include search paths for your module.

Optionally you can also set the vmod install directory by adding
`VMODDIR=DIR` (defaults to the pkg-config discovered directory from your
Varnish installation).

Make targets:

* make - builds the vmod
* make install - installs your vmod in `VMODDIR`
* make check - runs the unit tests in ``src/tests/*.vtc``

In your VCL you could then use this vmod along the following lines::
        
        import throttle;

        sub vcl_miss {
                # This sets resp.http.hello to "Hello, World"
                set resp.http.X-throttle-wait = throttle.is_allowed("ip:" + client.ip + ":api:/path", 2, 20, 200);
        }

HISTORY
=======

This module use libvmod-example as a base.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-example project. See LICENSE for details.

* Copyright (c) 2012 Nicolas Deschildre
* Copyright (c) 2011 Varnish Software

TODO
====

* Test files