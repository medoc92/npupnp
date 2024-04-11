#!/bin/sh

VERSION=`grep "version:" meson.build | head -1 | awk '{print $2}' | tr -d "',"`
MAJOR=`echo $VERSION | cut -d. -f 1`
MINOR=`echo $VERSION | cut -d. -f 2`
PATCH=`echo $VERSION | cut -d. -f 3`


sed -i -E -e '/^#define[ \t]+NPUPNP_VERSION_STRING/c\'\
"#define NPUPNP_VERSION_STRING \"$VERSION\"" \
-e '/^#define[ \t]+NPUPNP_VERSION_MAJOR/c\'\
"#define NPUPNP_VERSION_MAJOR $MAJOR" \
-e '/^#define[ \t]+NPUPNP_VERSION_MINOR/c\'\
"#define NPUPNP_VERSION_MINOR $MINOR" \
-e '/^#define[ \t]+NPUPNP_VERSION_PATCH/c\'\
"#define NPUPNP_VERSION_PATCH $PATCH" \
windows/upnpconfig-windows.h macos/upnpconfig-macos.h
