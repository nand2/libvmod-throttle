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

NO PRODUCTION READY YET! Work as defined, but API not totally frozen, and some memory management remains to be done.

This vmod most obvious uses would be to handle denial of services by a single user (or bot) punching through the cache, or to set rate limits to API calls you provide.

This vmod will allow you to set rate limiting, on several different time windows, per path/IP/whatever you want. If a time window limit was reached, this vmod will return you the time to wait before this call will be authorized.
With this return information, you can handle rate limit overflow by either abruptly returning an error, or by actually waiting the necessary time if you deems it reasonable, for a smoother rate limit overflow handling.

Please note that at the moment, there is no native way (AFAIK) to wait within Varnish (and no, using sleep() in the VCL is a *bad* idea), so you'll have to hack your way to it, e.g. redirect to a nodejs server that will wait for you and come back later.
I'll try to see if I can hack a way within Varnish, e.g. with adding a new state in the caching state machine, which would be sleepAndRestart;

FUNCTIONS
=========

is_allowed
----------

Prototype
        ::

                is_allowed(STRING key, STRING window_limits)
Arguments
    key: A unique key that will identify what you are throttling. Can be used in may ways. See the examples below.
    
    window_limits: A list of different rate limits you want to put on this call. The syntax is the following: "[nb_of_calls]req/[duration][durations_size], ...". Example: "3req/s, 10req/30s, 30req/5m, 100req/h". Please note that we are using the Varnish duration size identifiers: s, m (and not mn/min), h, d.
Return value
	DURATION
Description
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

    API rate limiting: limit calls by IP and API call, max 2 req/s, 100 req/2h, 1000 req/d.::

            sub vcl_recv {
                if(req.url ~ "^/my_api_path/") {
                    if(throttle.is_allowed("ip:" + client.ip" + ":api:" + req.url, "2req/s, 100req/2h, 1000req/d") > 0s) {
                           error 429 "Calm down";
                    }
                }
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

* Concurrency issues (mutexes)
* Garbage collector
* Test files