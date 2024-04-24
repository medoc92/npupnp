#ifndef UPNPINET_H
#define UPNPINET_H

/* Provide a platform independent way to include TCP/IP types and functions. */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#define UpnpCloseSocket(s)  do {closesocket(s); s = INVALID_SOCKET;} while(0)

#else /* ! _WIN32 -> */

/*** Windows compatibility macros */
#define UpnpCloseSocket(s) do {close(s); s = -1;} while(0)
/* SOCKET is typedefd by the system and unsigned on Win32 */
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
/*** End Windows compat */

#if defined(__sun)
#  include <fcntl.h>
#  include <sys/sockio.h>
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>


#endif /* ! _WIN32 */

#endif /* UPNPINET_H */
