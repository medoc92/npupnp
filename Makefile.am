#
# Top-level "Makefile.am" for libupnp
#
# Copyright (C) 2005 Rémi Turboult <r3mi@users.sourceforge.net>
#

ACLOCAL_AMFLAGS = -I m4

DISTCHECK_CONFIGURE_FLAGS = --enable-debug

SUBDIRS = \
	upnp

EXTRA_DIST = \
	.gitignore \
	libnpupnp.pc.in \
	README.md \
	bootstrap \
	libupnp.spec


# This variable must have 'exec' in its name, in order to be installed 
# by 'install-exec' target (instead of default 'install-data')
pkgconfigexecdir = $(libdir)/pkgconfig
pkgconfigexec_DATA = libnpupnp.pc


$(pkgconfigexec_DATA): config.status

CLEANFILES = 

# Ensure that the working copy is clean
dist-hook:
	test ! -d $(top_srcdir)/.git || cd $(top_srcdir) && git diff-index --quiet HEAD
