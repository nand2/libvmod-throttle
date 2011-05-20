SUBDIRS = tests

INCLUDES = -I$(VARNISHSRC)/include -I$(VARNISHSRC)

vmoddir = $(VMODDIR)
vmod_LTLIBRARIES = libvmod_example.la

libvmod_example_la_LDFLAGS = -avoid-version

libvmod_example_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_example.c

vcc_if.c vcc_if.h: $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod_example.vcc
	@PYTHON@ $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod_example.vcc

EXTRA_DIST = \
	vmod_example.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
