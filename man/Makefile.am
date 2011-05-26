#

dist_man_MANS = vmod_example.3
MAINTAINERCLEANFILES = $(dist_man_MANS)

vmod_example.3: vmod_example.rst
if HAVE_RST2MAN
	${RST2MAN} vmod_example.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
