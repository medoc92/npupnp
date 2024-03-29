
if COND_TESTPROGS
  MAYBE_TESTPROGS = test
endif
SUBDIRS = . $(MAYBE_TESTPROGS)
DIST_SUBDIRS = .

ACLOCAL_AMFLAGS = -I m4

MICROHTTPD_CPPFLAGS = @MICROHTTPD_CFLAGS@
CURL_CPPFLAGS = @CURL_CFLAGS@
EXPAT_CPPFLAGS = @EXPAT_CFLAGS@

AM_CPPFLAGS = $(MICROHTTPD_CPPFLAGS) $(CURL_CPPFLAGS) $(EXPAT_CPPFLAGS) \
	-Wall -Wextra -I$(srcdir)/inc \
        -DSMALLUT_NO_REGEX -DSMALLUT_EXTERNAL_INSTANTIATIONS

CPP_STD=@CPP_STD@

AM_CXXFLAGS = $(CPP_STD)

LIBNPUPNP_LIBS=@LIBNPUPNP_LIBS@

npupnpincludedir = $(includedir)/npupnp
npupnpinclude_HEADERS = \
	inc/upnp.h \
	inc/upnpdebug.h \
	inc/UpnpGlobal.h \
	inc/netif.h \
	inc/upnpdescription.h

nodist_npupnpinclude_HEADERS = inc/upnpconfig.h 
if ENABLE_TOOLS
npupnpinclude_HEADERS += inc/upnptools.h 
endif


lib_LTLIBRARIES = libnpupnp.la

libnpupnp_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/src/inc 
if LINUX
libnpupnp_la_CXXFLAGS = -fvisibility=hidden $(CPP_STD)
else
libnpupnp_la_CXXFLAGS = $(AM_CXXFLAGS)
endif

if MINGW
MORELDFLAGS = -no-undefined
endif

libnpupnp_la_LDFLAGS = \
	$(MORELDFLAGS) \
	-version-info $(LT_VERSION_NPUPNP) \
	$(LIBNPUPNP_LIBS)

libnpupnp_la_SOURCES = \
	src/inc/ThreadPool.h \
	src/inc/TimerThread.h \
	src/inc/VirtualDir.h \
	src/inc/config.h \
	src/inc/expatmm.h \
	src/inc/gena.h \
	src/inc/gena_ctrlpt.h \
	src/inc/gena_device.h \
	src/inc/gena_sids.h \
	src/inc/genut.h \
	src/inc/httputils.h \
	src/inc/md5.h \
	src/inc/miniserver.h \
	src/inc/picoxml.h \
	src/inc/service_table.h \
	src/inc/smallut.h \
	src/inc/smallut_instantiate.h \
	src/inc/soaplib.h \
	src/inc/ssdplib.h \
	src/inc/ssdpparser.h \
	src/inc/statcodes.h \
        src/inc/upnpinet.h \
	src/inc/upnpapi.h \
	src/inc/uri.h \
	src/inc/webserver.h \
        src/inc/utf8iter.h

# ssdp
if ENABLE_SSDP
libnpupnp_la_SOURCES += \
	src/ssdp/ssdp_device.cpp \
	src/ssdp/ssdp_ctrlpt.cpp \
	src/ssdp/ssdpparser.cpp \
	src/ssdp/ssdp_server.cpp
endif

# soap
if ENABLE_SOAP
libnpupnp_la_SOURCES += \
	src/soap/soap_device.cpp \
	src/soap/soap_ctrlpt.cpp
endif

# genlib
libnpupnp_la_SOURCES += \
	src/dispatcher/miniserver.cpp \
	src/threadutil/ThreadPool.cpp \
	src/threadutil/TimerThread.cpp \
	src/utils/description.cpp \
	src/utils/genut.cpp \
	src/utils/httputils.cpp \
	src/utils/md5.cpp \
	src/utils/netif.cpp \
	src/utils/smallut.cpp \
	src/utils/statcodes.cpp \
	src/utils/uri.cpp \
	src/webserver/webserver.cpp \
        src/utils/utf8iter.cpp

# gena
if ENABLE_GENA
libnpupnp_la_SOURCES += \
	src/gena/gena_device.cpp \
	src/gena/gena_ctrlpt.cpp \
	src/gena/gena_sids.cpp \
	src/gena/service_table.cpp \
	src/gena/gena_callback2.cpp
endif

# api
libnpupnp_la_SOURCES += \
	src/api/upnpapi.cpp

if ENABLE_TOOLS
libnpupnp_la_SOURCES += src/api/upnptools.cpp
endif

if ENABLE_DEBUG
libnpupnp_la_SOURCES += src/api/upnpdebug.cpp 
endif


EXTRA_DIST = \
	VER \
	meson.build \
	meson_options.txt \
	libnpupnp.pc.in \
	README.md \
	autogen.sh \
	src/utils/win_dll.cpp \
        test/Makefile.am

# This variable must have 'exec' in its name, in order to be installed 
# by 'install-exec' target (instead of default 'install-data')
if FREEBSD
pkgconfigexecdir = $(prefix)/libdata/pkgconfig
else
pkgconfigexecdir = $(libdir)/pkgconfig
endif
pkgconfigexec_DATA = libnpupnp.pc

$(pkgconfigexec_DATA): config.status

CLEANFILES = 


# Dist: ensure that the working copy is clean and create tag.
# On Linux also check that nothing changed in the symbols table: changes
# must be explicit, may need an solib version change, and an update of the
# reference file.
dist-hook:
if LINUX
	nm -g --defined-only --demangle .libs/libnpupnp.so | grep ' T ' | \
           awk '{$$1=$$2="";print $$0}' | diff $(top_srcdir)/symbols-reference -
endif
	if test -z "$(NOTAG)";then \
	    test ! -d $(top_srcdir)/.git||cd $(top_srcdir) && git diff-index --quiet HEAD; \
	fi 
	if test -z "$(NOTAG)";then \
	    vers=`echo $(VERSION) | sed -e 's/~/_/g'`; \
		git tag -a libnpupnp-v$$vers -m "version $$vers"; \
	fi
