#
# "Makefile.am" for "libupnp/upnp"
#
# Copyright (C) 2005 Rémi Turboult <r3mi@users.sourceforge.net>
#

AM_CPPFLAGS = \
	-I$(srcdir)/inc

# For thread stats
#if ENABLE_DEBUG
#    AM_CPPFLAGS		+= -DSTATS
#endif

LDADD = \
	libupnp.la \
	$(top_builddir)/ixml/libixml.la 


upnpincludedir = $(includedir)/upnp
upnpinclude_HEADERS = \
	inc/upnp.h \
	inc/upnpdebug.h \
	inc/UpnpGlobal.h \
	inc/UpnpInet.h \
	inc/UpnpIntTypes.h \
	inc/UpnpStdInt.h \
	inc/UpnpUniStd.h

nodist_upnpinclude_HEADERS = inc/upnpconfig.h 
if ENABLE_TOOLS
upnpinclude_HEADERS += inc/upnptools.h 
endif


lib_LTLIBRARIES = libupnp.la

libupnp_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/src/inc 

libupnp_la_LDFLAGS = \
	-version-info $(LT_VERSION_UPNP) \
	-export-symbols-regex '^Upnp.*' \
	-lixml -lcurl -lmicrohttpd

libupnp_la_SOURCES = \
	src/inc/FreeList.h \
	src/inc/LinkedList.h \
	src/inc/ThreadPool.h \
	src/inc/TimerThread.h \
	src/inc/VirtualDir.h \
	src/inc/client_table.h \
	src/inc/config.h \
	src/inc/gena.h \
	src/inc/gena_ctrlpt.h \
	src/inc/gena_device.h \
	src/inc/httputils.h \
	src/inc/ithread.h \
	src/inc/md5.h \
	src/inc/miniserver.h \
	src/inc/netall.h \
	src/inc/service_table.h \
	src/inc/smallut.h \
	src/inc/soaplib.h \
	src/inc/sock.h \
	src/inc/ssdplib.h \
	src/inc/ssdpparser.h \
	src/inc/statcodes.h \
	src/inc/statuscodes.h \
	src/inc/sysdep.h \
	src/inc/upnp_timeout.h \
	src/inc/upnpapi.h \
	src/inc/upnputil.h \
	src/inc/uri.h \
	src/inc/uuid.h \
	src/inc/webserver.h

# ssdp
if ENABLE_SSDP
libupnp_la_SOURCES += \
	src/ssdp/ssdp_ResultData.h \
	src/ssdp/ssdp_device.c \
	src/ssdp/ssdp_ctrlpt.c \
	src/ssdp/ssdpparser.cpp \
	src/ssdp/ssdp_server.c
endif

# soap
if ENABLE_SOAP
libupnp_la_SOURCES += \
	src/soap/soap_device.c \
	src/soap/soap_ctrlpt.c
endif

# genlib
libupnp_la_SOURCES += \
	src/dispatcher/miniserver.c \
	src/utils/smallut.cpp \
	src/utils/util.c \
	src/utils/sock.c \
	src/utils/httputils.c \
	src/utils/statcodes.c \
	src/webserver/webserver.c \
	src/utils/uri.c

libupnp_la_SOURCES += \
	src/threadutil/FreeList.c \
	src/threadutil/LinkedList.c \
	src/threadutil/ThreadPool.c \
	src/threadutil/TimerThread.c

# gena
if ENABLE_GENA
libupnp_la_SOURCES += \
	src/gena/gena_device.c \
	src/gena/gena_ctrlpt.c \
	src/gena/service_table.c \
	src/gena/client_table.c \
	src/gena/gena_callback2.c
endif

# api
libupnp_la_SOURCES += \
	src/api/upnpapi.c

if ENABLE_TOOLS
libupnp_la_SOURCES += src/api/upnptools.c
endif

if ENABLE_DEBUG
libupnp_la_SOURCES += src/api/upnpdebug.c 
endif


# uuid
if ENABLE_UUID
libupnp_la_SOURCES += \
	src/utils/uuid/md5.c \
	src/utils/uuid/sysdep.c \
	src/utils/uuid/uuid.c
endif


# inet_pton (needed on Win32, compiles to nothing elsewhere)
libupnp_la_SOURCES += \
	src/utils/inet_pton.c \
	src/inc/inet_pton.h


# check / distcheck tests
check_PROGRAMS = test_init test_url
TESTS = test_init test_url
test_init_SOURCES = test/test_init.c
test_url_SOURCES = test/test_url.c


EXTRA_DIST = \
	m4/libupnp.m4 \
	src/win_dll.c


CLEANFILES = \
	IUpnpErrFile.txt \
	IUpnpInfoFile.txt

