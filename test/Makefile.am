CXXFLAGS ?= @CXXFLAGS@

AM_CPPFLAGS = -Wall -Wno-unused -std=c++11 \
            -I${top_srcdir} -I${top_srcdir}/inc -I ${top_srcdir}/src/inc


noinst_PROGRAMS = testnetif

testnetif_SOURCES = test_netif.cpp
testnetif_LDADD = ../libnpupnp.la
