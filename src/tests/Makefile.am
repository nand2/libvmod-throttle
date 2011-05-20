MYTESTS = \
	test01.vtc
.PHONY: $(MYTESTS)

*.vtc:
	cd $(VARNISHSRC)/bin/varnishtest && ./varnishtest -D vmod_topbuild=$(abs_top_builddir) $(abs_srcdir)/$@

check: $(MYTESTS)
