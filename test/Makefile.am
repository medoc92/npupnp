CXXFLAGS ?= @CXXFLAGS@

AM_CPPFLAGS = -Wall -Wno-unused -std=c++14 \
            -I${top_srcdir} -I${top_srcdir}/inc -I ${top_srcdir}/src/inc


noinst_PROGRAMS = testnetif testdescription

testnetif_SOURCES = test_netif.cpp
testnetif_LDADD = ../libnpupnp.la

testdescription_SOURCES = test_description.cpp
testdescription_LDADD = ../libnpupnp.la
