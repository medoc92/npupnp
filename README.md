# libnpupnp

npupnp (new pupnp or not pupnp ?) is a base UPnP library derived from the venerable pupnp
(https://github.com/pupnp/pupnp), based on its 1.6.x branch (around 1.6.25). It provides the
fundamental layer for implementing UPnP devices or Control Points.

Main modifications:

 - Support multiple network interfaces
 - Support multiple root devices (already in the late pupnp versions).
 - Use libcurl for HTTP client functions.
 - Use libmicrohttpd for HTTP server functions (GENA, SOAP, and WEB server).
 - Vastly cleaned-up code, moved from C to C++, using the C++ STL to eliminate
   locally-grown data structures.

The changes reduce the library from around 40000 lines of code to around
20000, replacing difficult to maintain (and sometimes weird) code with well
supported and maintained libraries.

The C API has been largely preserved, except for a few calls which
passed IXML DOM tree objects as parameters, where they have been replaced
by either XML string documents or C++ STL structures.

At this point the API is C++. It would not be impossible to add a pure C
API if this was needed.

[Documentation](https://www.lesbonscomptes.com/upmpdcli/npupnp-doc/libnpupnp.html) !

Tar archives for the releases are stored on the [upmpdcli downloads
page](https://www.lesbonscomptes.com/upmpdcli/pages/downloads.html)

Build dependancies (as Debian package names, you may need to translate): `pkg-config`,
`libexpat1-dev`, `libmicrohttpd-dev`, `libcurl4-gnutls-dev`.

At the moment, the tar archives builds are based on the GNU autotools. The build sequence is the
usual one:

    ./autogen.sh # Only for a git clone, no need for a tar release file
    configure --prefix=/usr
    make
    sudo make install

The development code has switched to using meson/ninja-build. The new way is something like:

    cd [somewhere]
    mkdir builddir
    meson setup --prefix=usr builddir /path/to/npupnp/meson.build
    cd builddir
    ninja 
    sudo meson install
    
