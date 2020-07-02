# libnpupnp

Copyright (c) 2000-2003 Intel Corporation - All Rights Reserved.  
Copyright (c) 2005-2006 Rémi Turboult <r3mi@users.sourceforge.net>  
Copyright (c) 2006 Michel Pfeiffer and others <virtual_worlds@gmx.de>  
Copyright (c) 2020 Jean-Francois Dockes <jf@dockes.org>  
See LICENSE for details.

npupnp (new pupnp or not pupnp ?) is an UPnP library derived from the
venerable pupnp (https://github.com/pupnp/pupnp), based on its 1.6.x
branch (around 1.6.25).

Have a look at the
[DOC](https://www.lesbonscomptes.com/upmpdcli/npupnp-doc/libnpupnp.html) !

Main modifications:

 - Support multiple network interfaces
 - Support multiple root devices (already in the late pupnp versions).
 - Use libcurl for HTTP client functions.
 - Use libmicrohttpd for HTTP server functions (GENA, SOAP, and WEB server).
 - Vastly cleaned-up code. Use C++ STL as needed to eliminate locally-grown
   data structures.

The changes reduce the library from around 40000 lines of code to around
20000, replacing difficult to maintain (and sometimes weird) code with well
supported and maintained libraries.

The C API has been largely preserved, except for a few calls which
passed IXML DOM tree objects as parameters, where they have been replaced
by either XML string documents or C++ STL structures.

At this point the API is C++. It would not be impossible to add a pure C
API if this was needed.


