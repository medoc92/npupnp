/* autoconfig.h.  Generated from autoconfig.h.in by configure.  */
/* autoconfig.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to 1 to compile debug code */
#define DEBUG 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `getifaddrs' function. */
#define HAVE_GETIFADDRS 1

/* Define to 1 if you have the `pthread' library (-lpthread). */
#define HAVE_LIBPTHREAD 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <syslog.h> header file. */
#define HAVE_SYSLOG_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 to prevent compilation of assert() */
/* #undef NDEBUG */

/* Name of package */
#define PACKAGE "libnpupnp"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "jfd@recoll.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "libnpupnp"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "libnpupnp 6.1.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libnpupnp"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "6.1.1"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

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

/* see upnpconfig.h */
#define UPNP_MINISERVER_REUSEADDR 1

/* Use expat */
#define USE_EXPAT 1

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* File Offset size */
#define _FILE_OFFSET_BITS 64

/* Large files support */
#define _LARGE_FILE_SOURCE /**/

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `long int' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Type for storing the length of struct sockaddr */
/* #undef socklen_t */
