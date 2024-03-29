QT       -= core gui

TARGET = npupnp
TEMPLATE = lib

CONFIG += qt warn_on thread
CONFIG += staticlib

INCLUDEPATH += ../
INCLUDEPATH += ../inc
INCLUDEPATH += ../src/inc

SOURCES += \
../src/api/upnpapi.cpp \
../src/api/upnpdebug.cpp \
../src/api/upnptools.cpp \
../src/dispatcher/miniserver.cpp \
../src/gena/gena_callback2.cpp \
../src/gena/gena_ctrlpt.cpp \
../src/gena/gena_device.cpp \
../src/gena/gena_sids.cpp \
../src/gena/service_table.cpp \
../src/soap/soap_ctrlpt.cpp \
../src/soap/soap_device.cpp \
../src/ssdp/ssdp_ctrlpt.cpp \
../src/ssdp/ssdp_device.cpp \
../src/ssdp/ssdp_server.cpp \
../src/ssdp/ssdpparser.cpp \
../src/threadutil/ThreadPool.cpp \
../src/threadutil/TimerThread.cpp \
../src/utils/description.cpp \
../src/utils/genut.cpp \
../src/utils/httputils.cpp \
../src/utils/md5.cpp \
../src/utils/netif.cpp \
../src/utils/statcodes.cpp \
../src/utils/uri.cpp \
../src/utils/utf8iter.cpp \
../src/webserver/webserver.cpp


windows {
    DEFINES += WIN32_MEAN_AND_LEAN
    DEFINES += UPNP_STATIC_LIB
    DEFINES += CURL_STATICLIB
    DEFINES += PSAPI_VERSION=1

    ## W7 with mingw
    contains(QMAKE_CC, gcc){
      INCLUDEPATH += $$PWD/../../expat-2.1.0/lib
      INCLUDEPATH += $$PWD/../../curl-7.70.0/include
      INCLUDEPATH += $$PWD/../../libmicrohttpd-0.9.65/src/include
      QMAKE_CXXFLAGS += -std=c++17 -Wno-unused-parameter
    }

    # W10 with msvc 2017
    contains(QMAKE_CC, cl){
      QMAKE_CXXFLAGS += /std:c++17
      DEFINES += NOMINMAX
      INCLUDEPATH += $$PWD/../../expat.v140.2.4.1.1/build/native/include
      INCLUDEPATH += $$PWD/../../curl-7.70.0/include
      INCLUDEPATH += $$PWD/../../libmicrohttpd-0.9.65-w32-bin/x86_64/VS2019/Release-dll/
    }
    LIBS += -liphlpapi
    LIBS += -lwldap32
    LIBS += -lws2_32
}

mac {
    QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64
    INCLUDEPATH += ../../libmicrohttpd-0.9.71/src/include
}

