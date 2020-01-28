
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
	upnp

EXTRA_DIST = \
	libnpupnp.pc.in \
	README.md \
	bootstrap


# This variable must have 'exec' in its name, in order to be installed 
# by 'install-exec' target (instead of default 'install-data')
pkgconfigexecdir = $(libdir)/pkgconfig
pkgconfigexec_DATA = libnpupnp.pc


$(pkgconfigexec_DATA): config.status

CLEANFILES = 

# Ensure that the working copy is clean
dist-hook:
	test ! -d $(top_srcdir)/.git || cd $(top_srcdir) && git diff-index --quiet HEAD
	vers=`echo $(VERSION) | sed -e 's/~/_/g'`;\
	git tag -a libnpupnp-v$$vers -m "version $$vers"
