# if VARNISHSRC is defined on the command-line, use that. Otherwise, build
# this the same as the modules that come with varnish (i.e. we're building
# within the varnish src dir itself, and $(top_srcdir) is the varnish source).
#
if HAVE_VARNISHSRC
SRC = $(VARNISHSRC)
DIR_PREFIX = /
else
SRC = $(top_srcdir)
DIR_PREFIX = lib/libvmod-statsd/
endif

INCLUDES = -I$(SRC)/include -I$(SRC)

if HAVE_VMODDIR
vmoddir = $(VMODDIR)
else
vmoddir = $(pkglibdir)/vmods
endif

vmod_LTLIBRARIES = libvmod_throttle.la

libvmod_throttle_la_LDFLAGS = -module -export-dynamic -avoid-version

libvmod_throttle_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_throttle.c

vcc_if.c vcc_if.h: $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod_throttle.vcc
	@PYTHON@ $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod_throttle.vcc

VMOD_TESTS = tests/*.vtc
.PHONY: $(VMOD_TESTS)

tests/*.vtc:
	$(VARNISHSRC)/bin/varnishtest/varnishtest -Dvarnishd=$(VARNISHSRC)/bin/varnishd/varnishd -Dvmod_topbuild=$(abs_top_builddir) $@

check: $(VMOD_TESTS)

EXTRA_DIST = \
	vmod_throttle.vcc \
	$(VMOD_TESTS)

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
