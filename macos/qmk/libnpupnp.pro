QT       -= core gui

TARGET = npupnp
TEMPLATE = lib

CONFIG += qt warn_on thread
CONFIG += staticlib

INCLUDEPATH += ../../
INCLUDEPATH += ../../inc
INCLUDEPATH += ../../src/inc
INCLUDEPATH += ../../../libmicrohttpd-0.9.71/src/include

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
../../src/utils/md5.cpp \
../../src/utils/netif.cpp \
../../src/utils/statcodes.cpp \
../../src/utils/uri.cpp \
../../src/utils/utf8iter.cpp \
../../src/webserver/webserver.cpp
