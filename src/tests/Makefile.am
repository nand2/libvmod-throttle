VMOD_TESTS = *.vtc
.PHONY: $(VMOD_TESTS)

*.vtc:
	cd $(VARNISHSRC)/bin/varnishtest && ./varnishtest -D vmod_topbuild=$(abs_top_builddir) $(abs_srcdir)/$@

check: $(VMOD_TESTS)

EXTRA_DIST = \
	$(VMOD_TESTS)
