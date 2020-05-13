#ifndef UPNPINET_H
#define UPNPINET_H

/* Provide a platform independent way to include TCP/IP types and functions. */

#ifdef _WIN32

#include <stdarg.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#if(_WIN32_WINNT < 0x0600)
typedef short sa_family_t;
#else
typedef ADDRESS_FAMILY sa_family_t;
#endif

#define UpnpCloseSocket(s) {closesocket(s); s = INVALID_SOCKET;}
#define UPNP_SOCK_GET_LAST_ERROR() WSAGetLastError()

#else /* ! _WIN32 -> */

#define UpnpCloseSocket(s) {close(s); s = -1;}
#define UPNP_SOCK_GET_LAST_ERROR() errno
/* SOCKET is typedefd by the system and unsigned on Win32 */
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

#ifndef __APPLE__
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#endif /* ! __APPLE__ */

#include <sys/time.h>
#include <sys/param.h>

#if defined(__sun)

#include <fcntl.h>
#include <sys/sockio.h>

#elif (defined(BSD) && BSD >= 199306) || defined (__FreeBSD_kernel__)

#include <ifaddrs.h>
/* Do not move or remove the include below for "sys/socket"!
 * Will break FreeBSD builds. */
#include <sys/socket.h>

#endif

#include <arpa/inet.h>  /* for inet_pton() */
#include <net/if.h>
#include <netinet/in.h>

#endif /* _WIN32 */

/* @} Sock */

#endif /* UPNPINET_H */
