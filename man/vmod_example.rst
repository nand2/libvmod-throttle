============
vmod_example
============

----------------------
Varnish Example Module
----------------------

:Author: Martin Blix Grydeland
:Date: 2011-05-26
:Version: 1.0
:Manual section: 3

SYNOPSIS
========

import example;

DESCRIPTION
===========

Example Varnish vmod demonstrating how to write an out-of-tree Varnish vmod.

Implements the traditional Hello World as a vmod.

FUNCTIONS
=========

hello
-----

Prototype
	hello(STRING S)
Return value
	STRING
Description
	Returns "Hello, " prepended to S
Example
	set resp.http.hello = example.hello("World");

HISTORY
=======

This manual page was released as part of the libvmod-example package,
demonstrating how to create an out-of-tree Varnish vmod.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-example project. See LICENSE for details.

* Copyright (c) 2011 Varnish Software
