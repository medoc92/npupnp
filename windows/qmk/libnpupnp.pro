QT       -= core gui

TARGET = npupnp
TEMPLATE = lib

CONFIG += object_parallel_to_source
CONFIG += qt warn_on thread
CONFIG += staticlib

DEFINES += WIN32_MEAN_AND_LEAN
DEFINES += UPNP_STATIC_LIB
DEFINES += CURL_STATICLIB
DEFINES -= UNICODE
DEFINES -= _UNICODE
DEFINES += _MBCS
DEFINES += PSAPI_VERSION=1
DEFINES += DISABLE_SMALLUT

INCLUDEPATH += ../../
INCLUDEPATH += ../../inc
INCLUDEPATH += ../../src/inc
INCLUDEPATH += c:/users/bill/documents/upnp/expat-2.1.0/lib
INCLUDEPATH += c:/users/bill/documents/upnp/curl-7.70.0/include
# Comes from the official libmicrohttpd downloads
#INCLUDEPATH += c:/users/bill/documents/upnp/libmicrohttpd-0.9.65-w32-bin/x86/MinGW/static/mingw32/include
INCLUDEPATH += c:/users/bill/documents/upnp/libmicrohttpd-0.9.65/src/include

LIBS += c:/users/bill/documents/upnp/expat-2.1.0/.libs/libexpat.a
LIBS += c:/users/bill/documents/upnp/curl-7.70.0/lib/libcurl.a
#LIBS += -Lc:/users/bill/documents/upnp/libmicrohttpd-0.9.65-w32-bin/x86/MinGW/static/mingw32/lib/ -llibmicrohttpd
LIBS += -Lc:/users/bill/documents/upnp/libmicrohttpd-0.9.65/.libs/ -lmicrohttpd

LIBS += -liphlpapi
LIBS += -lwldap32
LIBS += -lws2_32

contains(QMAKE_CC, gcc){
    # MingW
    QMAKE_CXXFLAGS += -std=c++11 -Wno-unused-parameter
}

SOURCES += \
../../src/api/upnpapi.cpp \
../../src/api/upnpdebug.cpp \
../../src/api/upnptools.cpp \
../../src/dispatcher/miniserver.cpp \
../../src/gena/client_table.cpp \
../../src/gena/gena_callback2.cpp \
../../src/gena/gena_ctrlpt.cpp \
../../src/gena/gena_device.cpp \
../../src/gena/gena_sids.cpp \
../../src/gena/service_table.cpp \
../../src/soap/soap_ctrlpt.cpp \
../../src/soap/soap_device.cpp \
../../src/ssdp/ssdp_ctrlpt.cpp \
../../src/ssdp/ssdp_device.cpp \
../../src/ssdp/ssdp_server.cpp \
../../src/ssdp/ssdpparser.cpp \
../../src/threadutil/ThreadPool.cpp \
../../src/threadutil/TimerThread.cpp \
../../src/utils/description.cpp \
../../src/utils/genut.cpp \
../../src/utils/httputils.cpp \
../../src/utils/inet_pton.cpp \
../../src/utils/md5.cpp \
../../src/utils/netif.cpp \
../../src/utils/statcodes.cpp \
../../src/utils/uri.cpp \
../../src/webserver/webserver.cpp

