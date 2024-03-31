/* Windows version, hand-edited */

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <ws2tcpip.h>
#endif

/* Define to 1 to compile debug code */
#define DEBUG 1

/* Define to 1 to prevent compilation of assert() */
/* #undef NDEBUG */

/* Name of package */
#define PACKAGE "libnpupnp"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "jfd@recoll.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "libnpupnp"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "libnpupnp 6.2.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libnpupnp"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "6.2.0"

/* see upnpconfig.h */
#define UPNP_ENABLE_IPV6 1

/* see upnpconfig.h */
/* #undef UPNP_ENABLE_UNSPECIFIED_SERVER */

/* see upnpconfig.h */
#define UPNP_HAVE_CLIENT 1

/* see upnpconfig.h */
#define UPNP_HAVE_DEBUG 1

/* see upnpconfig.h */
#define UPNP_HAVE_DEVICE 1

/* see upnpconfig.h */
#define UPNP_HAVE_GENA 1

/* see upnpconfig.h */
#define UPNP_HAVE_OPTSSDP 1

/* see upnpconfig.h */
#define UPNP_HAVE_SOAP 1

/* see upnpconfig.h */
#define UPNP_HAVE_SSDP 1

/* see upnpconfig.h */
#define UPNP_HAVE_TOOLS 1

/* see upnpconfig.h */
#define UPNP_HAVE_WEBSERVER 1

/* Use expat */
#define USE_EXPAT 1

/* File Offset size */
#define _FILE_OFFSET_BITS 64

/* Large files support */
#define _LARGE_FILE_SOURCE /**/
