# libnpupnp

Copyright (c) 2000-2003 Intel Corporation - All Rights Reserved.

Copyright (c) 2005-2006 Rémi Turboult <r3mi@users.sourceforge.net>

Copyright (c) 2006 Michel Pfeiffer and others <virtual_worlds@gmx.de>

Copyright (c) 2020 Jean-Francois Dockes <jf@dockes.org>

See LICENSE for details.

npupnp (new pupnp or not pupnp ?) is an UPnP library derived from the
venerable pupnp (https://github.com/pupnp/pupnp), based on its 1.6.x
branch (around 1.6.25).

Main modifications:

 - Use libcurl for HTTP client functions.
 - Use libmicrohttpd for HTTP server functions (GENA SOAP and Webserver).
 - Use C++ STL as needed to eliminate local-grown data structures as
   needed.

The changes reduce the library from around 40000 lines of code to around
20000, replacing difficult to maintain (and sometimes weird) code with well
supported and maintained libraries.

The C API has been almost fully preserved, except for a few calls which
passed IXML DOM tree objects as parameters, where they have been replaced
by either XML string documents or C++ STL structures.


