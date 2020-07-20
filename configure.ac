AC_PREREQ(2.60)

AC_INIT([libnpupnp], [4.0.10], [jfd@recoll.org])

# - Start with version information of ‘0:0:0’ for each libtool library.
# - Update the version information only immediately before a public release
# - If the library source code has changed at all since the last update, then
#   increment revision (‘c:r:a’ becomes ‘c:r+1:a’).
# - If any interfaces have been added, removed, or changed since the last
#   update, increment current, and set revision to 0.
# - If any interfaces have been added since the last public release, then
#   increment age.
# - If any interfaces have been removed or changed since the last public
#   release, then set age to 0. Change package name if it is needed to have
#   several installed versions of the lib. 
# npupnp packages will be named libupnpX at some point, where X is the .so
# major number (c-a). This will allow packages for multiple incompatible ABIs
# to be installed. Not necessary for now.
# Previous version: 2.1.2 was [2:0:0]
# Previous version: 2.2.1 was [3:0:0]
AC_SUBST([LT_VERSION_NPUPNP],       [4:2:0])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([inc/upnp.h])

AM_INIT_AUTOMAKE([1.8 -Wall foreign subdir-objects])
AM_PROG_AR
AC_DISABLE_STATIC
LT_INIT

AC_PROG_CXX
AC_PROG_LIBTOOL
AC_CHECK_PROG([pkg_config], [pkg-config], [yes], [no])
if test X$pkg_config != Xyes ; then
   AC_MSG_ERROR([pkg-config program needed and not found])
fi

AC_CHECK_LIB([pthread], [pthread_create], [], [])

AC_CANONICAL_HOST

#
# There are 3 configuration files :
# 1) "./autoconfig.h" is auto-generated and used only internally during build
#    (usually named "config.h" but conflicts with the file below)
# 2) "./src/inc/config.h" is static and contains some compile-time 
#    parameters. This file was previously in "./upnp/inc" but is no longer 
#    installed (contains internal definitions only).
# 3) "./inc/upnpconfig.h" is auto-generated and installed with the 
#    libraries : it contains information on the configuration of the
#    installed libraries.
#
AC_CONFIG_HEADERS([autoconfig.h inc/upnpconfig.h])

upnpmaj=`echo "$PACKAGE_VERSION" | sed 's/\..*//' `
upnpmin=[`echo "$PACKAGE_VERSION" | sed 's/^[^.]\.// ; s/[^0-9].*$//' `]
upnppatch=[`echo "$PACKAGE_VERSION" | sed 's/^[^.]\.[^.]\.// ; s/[^0-9].*$//' `]
AC_DEFINE_UNQUOTED([NPUPNP_VERSION_STRING], "$PACKAGE_VERSION", [see upnpconfig.h])
AC_DEFINE_UNQUOTED([NPUPNP_VERSION_MAJOR], $upnpmaj, [see upnpconfig.h])
AC_DEFINE_UNQUOTED([NPUPNP_VERSION_MINOR], $upnpmin, [see upnpconfig.h])
AC_DEFINE_UNQUOTED([NPUPNP_VERSION_PATCH], $upnppatch, [see upnpconfig.h])



AC_ARG_ENABLE([debug], [extra debugging code])
if test "x$enable_debug" != xno ; then
	AC_DEFINE(UPNP_HAVE_DEBUG, 1, [see upnpconfig.h])
	AC_DEFINE(DEBUG, 1, [Define to 1 to compile debug code])
else
	AC_DEFINE(NDEBUG, 1, [Define to 1 to prevent compilation of assert()])
fi
AM_CONDITIONAL(ENABLE_DEBUG, test x"$enable_debug" != xno)

#
# Check for libupnp subsets
#
AC_ARG_ENABLE([client], AC_HELP_STRING([control point code (client)]))
if test "x$enable_client" != xno ; then
	AC_DEFINE(UPNP_HAVE_CLIENT, 1, [see upnpconfig.h])
fi

AC_ARG_ENABLE([device], [device specific code (implies --disable-webserver if disabled)])
if test "x$enable_device" != xno ; then
	AC_DEFINE(UPNP_HAVE_DEVICE, 1, [see upnpconfig.h])
fi

if test "x$enable_device" = xno ; then 
	enable_webserver=no
else 
	AC_ARG_ENABLE([webserver], [integrated web server])
fi
AM_CONDITIONAL(ENABLE_WEBSERVER, test x"$enable_webserver" != xno)
if test "x$enable_webserver" != xno ; then
	AC_DEFINE(UPNP_HAVE_WEBSERVER, 1, [see upnpconfig.h])
fi

AC_ARG_ENABLE([ssdp], [SSDP part])
if test "x$enable_ssdp" != xno ; then
        AC_DEFINE(UPNP_HAVE_SSDP, 1, [see upnpconfig.h])
fi
AM_CONDITIONAL(ENABLE_SSDP, test x"$enable_ssdp" != xno)

AC_ARG_ENABLE([optssdp], [optional SSDP headers support)])
if test "x$enable_optssdp" != xno ; then
        AC_DEFINE(UPNP_HAVE_OPTSSDP, 1, [see upnpconfig.h])
	enable_uuid=yes
fi

AC_ARG_ENABLE([soap], [SOAP part])
if test "x$enable_soap" != xno ; then
        AC_DEFINE(UPNP_HAVE_SOAP, 1, [see upnpconfig.h])
fi
AM_CONDITIONAL(ENABLE_SOAP, test x"$enable_soap" != xno)

AC_ARG_ENABLE([gena], [GENA part])
if test "x$enable_gena" != xno ; then
        AC_DEFINE(UPNP_HAVE_GENA, 1, [see upnpconfig.h])
	enable_uuid=yes
fi
AM_CONDITIONAL(ENABLE_GENA, test x"$enable_gena" != xno)

AM_CONDITIONAL(ENABLE_UUID, test x"$enable_uuid" != xno)

AC_ARG_ENABLE([tools], [helper APIs in upnptools.h])
if test "x$enable_tools" != xno ; then
	AC_DEFINE(UPNP_HAVE_TOOLS, 1, [see upnpconfig.h])
fi
AM_CONDITIONAL(ENABLE_TOOLS, test x"$enable_tools" != xno)


AC_ARG_ENABLE([unspecified_server], [unspecified SERVER header])
if test "x$enable_unspecified_server" = xyes ; then
        AC_DEFINE(UPNP_ENABLE_UNSPECIFIED_SERVER, 1, [see upnpconfig.h])
fi

AC_ARG_ENABLE([ipv6], [--disable-ipv6 disable ipv6 support.],
    enable_ipv6=$enableval, enable_ipv6="yes")
if test "x$enable_ipv6" = xyes ; then
    AC_DEFINE(UPNP_ENABLE_IPV6, 1, [see upnpconfig.h])
fi

AC_ARG_ENABLE([reuseaddr],
	[--disable-reuseaddr do not bind the miniserver socket with	reuseaddr.],
	enable_reuseaddr=$enableval, enable_reuseaddr="yes")
if test "x$enable_reuseaddr" = xyes ; then
        AC_DEFINE(UPNP_MINISERVER_REUSEADDR, 1, [see upnpconfig.h])
fi

AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_DEFINE([_LARGE_FILE_SOURCE], [], [Large files support])
AC_DEFINE([_FILE_OFFSET_BITS], [64], [File Offset size])


#
# Are we targetting Win32?
#
AC_MSG_CHECKING([for Win32])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#ifdef _WIN32
	#error Yup
#endif
],[])], [ac_cv_win32="no"], [ac_cv_win32="yes"])
if test "$ac_cv_win32" = "yes"; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi


#
# Checks for header files (which aren't needed on Win32)
#
AC_HEADER_STDC
if test "$ac_cv_win32" = "no"; then
AC_CHECK_HEADERS(
	[ \
	arpa/inet.h \
	fcntl.h \
	inttypes.h \
	limits.h \
	netdb.h \
	netinet/in.h \
	stdlib.h \
	string.h \
	sys/ioctl.h \
	sys/socket.h \
	sys/time.h \
	syslog.h \
	unistd.h \
	],
	[],
	[AC_MSG_ERROR([required header file missing])])
fi


#
# Checks for typedefs, structures, and compiler characteristics
#	
AC_C_CONST
AC_C_BIGENDIAN

AC_CHECK_HEADERS([sys/types.h sys/socket.h ws2tcpip.h])
AC_MSG_CHECKING(for socklen_t)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#ifdef HAVE_SYS_TYPES_H
	#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
	#include <sys/socket.h>
#endif
#ifdef HAVE_WS2TCPIP_H
	#include <ws2tcpip.h>
#endif
],[ socklen_t t = 0; return t; ])
],[ac_cv_socklen_t="yes"],[ac_cv_socklen_t="no"])
if test "$ac_cv_socklen_t" = "yes"; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no, using int])
	AC_DEFINE(socklen_t, int, [Type for storing the length of struct sockaddr])
fi


AC_FUNC_VPRINTF
AC_CHECK_FUNCS([getifaddrs inet_pton])
# Solaris needs -lsocket -lnsl -lrt
AC_SEARCH_LIBS([bind],           [socket])
AC_SEARCH_LIBS([gethostbyname],  [nsl])
AC_SEARCH_LIBS([sched_getparam], [rt])

PKG_CHECK_MODULES([curl], [libcurl], [], AC_MSG_ERROR([libcurl not found]))
PKG_CHECK_MODULES([libmicrohttpd], [libmicrohttpd], [],
	[AC_MSG_ERROR([libmicrohttpd not found])])

AC_ARG_ENABLE(expat,
    AC_HELP_STRING([--disable-expat],
    [Use a small internal module for parsing XML instead of libexpat.]),
    useexpat=$enableval, useexpat=yes)
AM_CONDITIONAL([COND_EXPAT], [test "$useexpat" = yes])
if test X$useexpat = Xyes ; then
   PKG_CHECK_MODULES([expat], [expat], [], AC_MSG_ERROR([libexpat not found]))
   AC_DEFINE(USE_EXPAT, 1, [Use expat])
fi

LIBNPUPNP_LIBS="$curl_LIBS $libmicrohttpd_LIBS $expat_LIBS $LIBS"
echo "LIBNPUPNP_LIBS[" $LIBNPUPNP_LIBS "]"
AC_SUBST(LIBNPUPNP_LIBS)

AC_CONFIG_FILES([
	Makefile
	libnpupnp.pc
	])


AC_OUTPUT
