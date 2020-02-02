#ifndef UPNPINET_H
#define UPNPINET_H

/*!
 * \brief Provides a platform independent way to include TCP/IP types
 *  and functions.
 */

#ifdef WIN32
	#include <stdarg.h>
	#ifndef UPNP_USE_MSVCPP
		/* Removed: not required (and cause compilation issues) */
		#include <winbase.h>
		#include <windef.h>
	#endif
	#include <winsock2.h>
	#include <iphlpapi.h>
	#include <ws2tcpip.h>

	#define UpnpCloseSocket closesocket

	#if(_WIN32_WINNT < 0x0600)
		typedef short sa_family_t;
	#else
		typedef ADDRESS_FAMILY sa_family_t;
	#endif
    #define UPNP_SOCK_GET_LAST_ERROR() WSAGetLastError()

#else /* ! WIN32 -> */
	#include <syslog.h>
	#ifndef __APPLE__
		#include <netinet/in_systm.h>
		#include <netinet/ip.h>
		#include <netinet/ip_icmp.h>
	#endif /* __APPLE__ */
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

	/*! This typedef makes the code slightly more WIN32 tolerant.
	 * On WIN32 systems, SOCKET is unsigned and is not a file
	 * descriptor. */
	typedef int SOCKET;

	/*! INVALID_SOCKET is unsigned on win32. */
	#define INVALID_SOCKET (-1)

	/*! select() returns SOCKET_ERROR on win32. */
	#define SOCKET_ERROR (-1)

    #define UPNP_SOCK_GET_LAST_ERROR() errno

	/*! Alias to close() to make code more WIN32 tolerant. */
	#define UpnpCloseSocket close
#endif /* WIN32 */

/* @} Sock */

#endif /* UPNPINET_H */
