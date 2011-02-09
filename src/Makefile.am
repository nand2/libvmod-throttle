INCLUDES = -I$(VARNISHSRC)/include -I$(VARNISHSRC)

vmoddir = $(VMODDIR)
vmod_LTLIBRARIES = libvmod_example.la

libvmod_example_la_LDFLAGS = -version-info 1:0:0

libvmod_example_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod.c

vcc_if.c vcc_if.h: $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod.vcc
	@PYTHON@ $(VARNISHSRC)/lib/libvmod_std/vmod.py $(top_srcdir)/src/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)vcc_if.c $(builddir)/vcc_if.h
