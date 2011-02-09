AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2011 Varnish Software AS])
AC_INIT([libvmod_example], [0.1])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(src/vmod.vcc)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE([foreign])

AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_CC_STDC
if test "x$ac_cv_prog_cc_c99" = xno; then
	AC_MSG_ERROR([Could not find a C99 compatible compiler])
fi
AC_PROG_CPP

AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([sys/stdlib.h])

# Check for python
AC_CHECK_PROGS(PYTHON, [python3 python3.1 python3.2 python2.7 python2.6 python2.5 python2 python], [AC_MSG_ERROR([Python is needed to build this vmod, please install python.])])

# Varnish source tree
AC_ARG_VAR([VARNISHSRC], [path to Varnish source tree (mandatory)])
if test "x$VARNISHSRC" = x; then
	AC_MSG_ERROR([No Varnish source tree specified])
fi
AC_CHECK_FILE([$VARNISHSRC/include/varnishapi.h],
	[],
	[AC_MSG_FAILURE(["$VARNISHSRC" is not a Varnish source directory])]
)

# vmod installation dir
AC_ARG_VAR([VMODDIR], [vmod installation directory @<:@LIBDIR/varnish/vmods@:>@])
if test "x$VMODDIR" = x; then
	VMODDIR=$libdir/varnish/vmods
fi

AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
