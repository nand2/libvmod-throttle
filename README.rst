=============
vmod_throttle
=============

-------------------------
Varnish Throttling Module
-------------------------

NOT FINISHED YET! No per-key support yet, and the API is not frozen yet. Please come back later.

:Author: Nicolas Deschildre
:Date: 2012-10-06
:Version: 
:Manual section: 3

SYNOPSIS
========

import throttle;

DESCRIPTION
===========

This vmod will allow you to set rate limiting, on several different time windows. If a time window limit was reached, this vmod will return you the time to wait before this call will be authorized.

FUNCTIONS
=========

is_allowed
----------

Prototype
        ::

                is_allowed(STRING key, INT max_calls_per_sec, INT max_calls_per_min, INT max_calls_per_hour)
Return value
	DURATION
Description
	Returns 0.0 if the call was authorized, or the time to wait if one of the time window limit was reached.
Example
        ::

                set resp.http.X-throttle-wait = throttle.is_allowed("ip:" + client.ip + ":api:/path", 2, 20, 200);

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
