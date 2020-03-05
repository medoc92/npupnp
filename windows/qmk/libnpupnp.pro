QT       -= core gui

TARGET = npupnp
TEMPLATE = lib

CONFIG += object_parallel_to_source
CONFIG  += qt warn_on thread
CONFIG += staticlib

DEFINES += UPNP_STATIC_LIB
DEFINES += CURL_STATICLIB
DEFINES += WIN32
DEFINES -= UNICODE
DEFINES -= _UNICODE
DEFINES += _MBCS
DEFINES += PSAPI_VERSION=1

INCLUDEPATH += ../../
INCLUDEPATH += c:/users/bill/documents/upnp/expat-2.1.0/lib
INCLUDEPATH += c:/users/bill/documents/upnp/curl-7.43.0/include
# Comes from the official libmicrohttpd downloads
INCLUDEPATH += c:/users/bill/documents/upnp/libmicrohttpd-0.9.65-w32-bin/x86/MinGW/static/mingw32/include

LIBS += c:/users/bill/documents/upnp/expat-2.1.0/.libs/libexpat.a
LIBS += c:/users/bill/documents/upnp/curl-7.43.0/lib/libcurl.a
LIBS += c:/users/bill/documents/upnp/pupnp/threadutil/.libs/libthreadutil.a
LIBS += c:/users/bill/documents/upnp/libmicrohttpd-0.9.65-w32-bin/x86/MinGW/static/mingw32/lib/libmicrohttpd.a
LIBS += -liphlpapi
LIBS += -lwldap32
LIBS += -lws2_32

contains(QMAKE_CC, gcc){
    # MingW
    QMAKE_CXXFLAGS += -std=c++11 -Wno-unused-parameter
}
contains(QMAKE_CC, cl){
    # Visual Studio
}

SOURCES += \
	src/ssdp/ssdp_device.cpp \
	src/ssdp/ssdp_ctrlpt.cpp \
	src/ssdp/ssdpparser.cpp \
	src/ssdp/ssdp_server.cpp
	src/soap/soap_device.cpp \
	src/soap/soap_ctrlpt.cpp \
	src/dispatcher/miniserver.cpp \
	src/utils/description.cpp \
	src/utils/smallut.cpp \
	src/utils/httputils.cpp \
	src/utils/statcodes.cpp \
	src/webserver/webserver.cpp \
	src/utils/md5.cpp \
	src/utils/uri.cpp \
	src/threadutil/ThreadPool.cpp \
	src/threadutil/TimerThread.cpp \
	src/gena/gena_device.cpp \
	src/gena/gena_ctrlpt.cpp \
	src/gena/gena_sids.cpp \
	src/gena/service_table.cpp \
	src/gena/client_table.cpp \
	src/gena/gena_callback2.cpp \
	src/api/upnpapi.cpp \
        src/api/upnptools.cpp \
        src/api/upnpdebug.cpp \


