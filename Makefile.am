
ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = \
	-I$(srcdir)/inc

LIBNPUPNP_LIBS=@LIBNPUPNP_LIBS@

npupnpincludedir = $(includedir)/npupnp/upnp
npupnpinclude_HEADERS = \
	inc/upnp.h \
	inc/upnpdebug.h \
	inc/UpnpGlobal.h \
	inc/UpnpInet.h

nodist_npupnpinclude_HEADERS = inc/upnpconfig.h 
if ENABLE_TOOLS
npupnpinclude_HEADERS += inc/upnptools.h 
endif


lib_LTLIBRARIES = libnpupnp.la

libnpupnp_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/src/inc 


libnpupnp_la_LDFLAGS = \
	-version-info $(LT_VERSION_NPUPNP) \
	$(LIBNPUPNP_LIBS)

libnpupnp_la_SOURCES = \
	src/inc/FreeList.h \
	src/inc/ThreadPool.h \
	src/inc/TimerThread.h \
	src/inc/VirtualDir.h \
	src/inc/client_table.h \
	src/inc/config.h \
	src/inc/description.h \
	src/inc/expatmm.hxx \
	src/inc/gena.h \
	src/inc/gena_ctrlpt.h \
	src/inc/gena_device.h \
	src/inc/httputils.h \
	src/inc/ithread.h \
	src/inc/md5.h \
	src/inc/miniserver.h \
	src/inc/service_table.h \
	src/inc/smallut.h \
	src/inc/soaplib.h \
	src/inc/ssdplib.h \
	src/inc/ssdpparser.h \
	src/inc/statcodes.h \
	src/inc/statuscodes.h \
	src/inc/sysdep.h \
	src/inc/upnp_timeout.h \
	src/inc/upnpapi.h \
	src/inc/uri.h \
	src/inc/uuid.h \
	src/inc/webserver.h

# ssdp
if ENABLE_SSDP
libnpupnp_la_SOURCES += \
	src/ssdp/ssdp_ResultData.h \
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
	src/utils/description.cpp \
	src/utils/smallut.cpp \
	src/utils/httputils.cpp \
	src/utils/statcodes.cpp \
	src/webserver/webserver.cpp \
	src/utils/uri.cpp

libnpupnp_la_SOURCES += \
	src/threadutil/ThreadPool.cpp \
	src/threadutil/TimerThread.cpp

# gena
if ENABLE_GENA
libnpupnp_la_SOURCES += \
	src/gena/gena_device.cpp \
	src/gena/gena_ctrlpt.cpp \
	src/gena/service_table.cpp \
	src/gena/client_table.cpp \
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


# uuid
if ENABLE_UUID
libnpupnp_la_SOURCES += \
	src/utils/uuid/md5.cpp \
	src/utils/uuid/sysdep.cpp \
	src/utils/uuid/uuid.cpp
endif


# inet_pton (needed on Win32, compiles to nothing elsewhere)
libnpupnp_la_SOURCES += \
	src/utils/inet_pton.cpp \
	src/inc/inet_pton.h


EXTRA_DIST = \
	libnpupnp.pc.in \
	README.md \
	bootstrap \
	src/utils/win_dll.cpp


# This variable must have 'exec' in its name, in order to be installed 
# by 'install-exec' target (instead of default 'install-data')
pkgconfigexecdir = $(libdir)/pkgconfig
pkgconfigexec_DATA = libnpupnp.pc

$(pkgconfigexec_DATA): config.status

CLEANFILES = 

# Ensure that the working copy is clean
dist-hook:
	test ! -d $(top_srcdir)/.git || cd $(top_srcdir) && \
			 git diff-index --quiet HEAD
	vers=`echo $(VERSION) | sed -e 's/~/_/g'`;\
			   git tag -a libnpupnp-v$$vers -m "version $$vers"
