/**************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (C) 2011-2012 France Telecom All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **************************************************************************/


#include "config.h"

#if EXCLUDE_SSDP == 0

#include "ssdplib.h"
#include "ssdpparser.h"
#include "httputils.h"
#include "miniserver.h"
#include "ThreadPool.h"
#include "upnpapi.h"
#include "smallut.h"
#include "netif.h"

#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <thread>

#ifdef INCLUDE_CLIENT_APIS
SOCKET gSsdpReqSocket4 = INVALID_SOCKET;
#ifdef UPNP_ENABLE_IPV6
SOCKET gSsdpReqSocket6 = INVALID_SOCKET;
#endif /* UPNP_ENABLE_IPV6 */
#endif /* INCLUDE_CLIENT_APIS */

// Extract criteria from ssdp packet. Cmd can come from either an USN,
// NT, or ST field. The possible forms are:
//   ssdp:all
//   upnp:rootdevice
//   urn:domain-name:device:deviceType:v
//   urn:domain-name:service:serviceType:v
//   urn:schemas-upnp-org:device:deviceType:v
//   urn:schemas-upnp-org:service:serviceType:v
//   uuid:device-UUID
//   uuid:device-UUID::upnp:rootdevice
//   uuid:device-UUID::urn:domain-name:device:deviceType:v
//   uuid:device-UUID::urn:domain-name:service:serviceType:v
//   uuid:device-UUID::urn:schemas-upnp-org:device:deviceType:v
//   uuid:device-UUID::urn:schemas-upnp-org:service:serviceType:v
// We get the UDN, device or service type as available.
int unique_service_name(const char *cmd, SsdpEvent *Evt)
{
	int CommandFound = 0;

	if (strstr(const_cast<char*>(cmd), "uuid:") == cmd) {
		char *theend = strstr(const_cast<char*>(cmd), "::");
		if (nullptr != theend) {
			size_t n = theend - cmd;
			if (n >= sizeof(Evt->UDN))
				n = sizeof(Evt->UDN) - 1;
			memcpy(Evt->UDN, cmd, n);
			Evt->UDN[n] = 0;
		} else {
			upnp_strlcpy(Evt->UDN, cmd, sizeof(Evt->UDN));
		}
		CommandFound = 1;
	}

	char *urncp = strstr(const_cast<char*>(cmd), "urn:");
	if (urncp && strstr(const_cast<char*>(cmd), ":service:")) {
		upnp_strlcpy(Evt->ServiceType, urncp, sizeof(Evt->ServiceType));
		CommandFound = 1;
	}
	if (urncp && strstr(const_cast<char*>(cmd), ":device:")) {
		upnp_strlcpy(Evt->DeviceType, urncp, sizeof(Evt->DeviceType));
		CommandFound = 1;
	}

	if (CommandFound == 0)
		return -1;

	return 0;
}

enum SsdpSearchType ssdp_request_type1(const char *cmd)
{
	if (strstr(cmd, ":all"))
		return SSDP_ALL;
	if (strstr(cmd, ":rootdevice"))
		return SSDP_ROOTDEVICE;
	if (strstr(cmd, "uuid:"))
		return SSDP_DEVICEUDN;
	if (strstr(cmd, "urn:") && strstr(cmd, ":device:"))
		return SSDP_DEVICETYPE;
	if (strstr(cmd, "urn:") && strstr(cmd, ":service:"))
		return SSDP_SERVICE;
	return SSDP_SERROR;
}

int ssdp_request_type(const char *cmd, SsdpEvent *Evt)
{
	/* clear event */
	memset(Evt, 0, sizeof(SsdpEvent));
	unique_service_name(cmd, Evt);
	Evt->ErrCode = NO_ERROR_FOUND;
	if ((Evt->RequestType = ssdp_request_type1(cmd)) == SSDP_SERROR) {
		Evt->ErrCode = E_HTTP_SYNTEX;
		return -1;
	}
	return 0;
}


#define BUFSIZE   (size_t)2500
struct ssdp_thread_data {
	// The data packet is transferred to the parser, keep as pointer
	char *packet{nullptr};
	struct sockaddr_storage dest_addr;
};

/*!
 * \brief Frees the ssdp request.
 * arg is cast to *ssdp_thread_data pointer
 */
static void free_ssdp_event_handler_data(void *arg)
{
	auto data = static_cast<ssdp_thread_data *>(arg);

	if (nullptr == data) {
		return;
	}
	if (data->packet) {
		free(data->packet);
	}
	free(data);
}

/*!
 * \brief Does some quick checking of an ssdp request msg.
 *
 * \return HTTPMETHOD_UNKNOWN if packet is invalid, else method.
 */
static http_method_t valid_ssdp_msg(SSDPPacketParser& parser)
{
	http_method_t method = HTTPMETHOD_UNKNOWN;
	if (!parser.isresponse) {
		if (!parser.method) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "NULL method "
					   "in SSDP request????\n");
			return HTTPMETHOD_UNKNOWN;
		}
		method = httpmethod_str2enum(parser.method);
		if (method != HTTPMETHOD_NOTIFY && method != HTTPMETHOD_MSEARCH) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "Invalid method "
					   "in SSDP message: [%s] \n", parser.method);
			return HTTPMETHOD_UNKNOWN;
		}
		/* check PATH == "*" */
		if (!parser.url || strcmp(parser.url, "*") != 0) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "Invalid URI "
					   "in SSDP message NOTIFY or M-SEARCH: [%s] \n",
					   (parser.url?parser.url:"(null)"));
			return HTTPMETHOD_UNKNOWN;
		}
		/* check HOST header */
		if (!parser.host ||
		    (strcmp(parser.host, "239.255.255.250:1900") != 0 &&
		     strcasecmp(parser.host, "[FF02::C]:1900") != 0 &&
		     strcasecmp(parser.host, "[FF05::C]:1900") != 0)) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "Invalid HOST "
					   "header [%s] from SSDP message\n", parser.host);
			return HTTPMETHOD_UNKNOWN;
		}
	} else {
		// We return HTTPMETHOD_MSEARCH + isresponse for response packets.
		// Analog to what the original code did.
		method = HTTPMETHOD_MSEARCH;
	}

	return method;
}

/* Thread routine to process one received SSDP message */
static void *thread_ssdp_event_handler(void *the_data)
{
	auto data = static_cast<ssdp_thread_data *>(the_data);

	std::cerr << "thread_ssdp_event: got data:\n" << data->packet << "\n";
	// The parser takes ownership of the buffer
	SSDPPacketParser parser(data->packet);
	data->packet = nullptr;
	if (!parser.parse()) {
		std::cerr << "thread_ssdp_event: parser failed\n";
		UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,	"SSDP parser error\n");
		return nullptr;
	}

	http_method_t method = valid_ssdp_msg(parser);
	if (method == HTTPMETHOD_UNKNOWN) {
		std::cerr << "thread_ssdp_event: unknown method\n";
		return nullptr;
	}

	/* Dispatch message to device or ctrlpt */
	if (method == HTTPMETHOD_NOTIFY ||
		(parser.isresponse && method == HTTPMETHOD_MSEARCH)) {
#ifdef INCLUDE_CLIENT_APIS
		ssdp_handle_ctrlpt_msg(parser, &data->dest_addr, 0, nullptr);
#endif /* INCLUDE_CLIENT_APIS */
	} else {
		ssdp_handle_device_request(parser, &data->dest_addr);
	}
	return nullptr;
}

void readFromSSDPSocket(SOCKET socket)
{
	ssdp_thread_data *data = 
		static_cast<ssdp_thread_data*>(malloc(sizeof(ssdp_thread_data)));
	if (!data) {
		std::cerr << "Out of memory in readFromSSDPSocket\n";
		abort();
	}
	data->packet = static_cast<char*>(malloc(BUFSIZE));
	if (!data->packet) {
		std::cerr << "Out of memory in readFromSSDPSocket\n";
		abort();
	}
	
	struct sockaddr_storage saddr;
	struct sockaddr *sap = reinterpret_cast<struct sockaddr *>(&saddr);
	socklen_t socklen = sizeof(saddr);
	ssize_t cnt = recvfrom(socket, data->packet, BUFSIZE - 1, 0, sap, &socklen);
	if (cnt > 0) {
		data->packet[cnt] = '\0';
		NetIF::IPAddr nipa(sap);
		UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__,
				   "\nSSDP message from host %s --------------------\n"
				   "%s\n"
				   "End of received data -----------------------------\n",
				   nipa.straddr().c_str(), data->packet);
		/* add thread pool job to handle request */
		memcpy(&data->dest_addr, &saddr, sizeof(saddr));
		if (gRecvThreadPool.addJob(thread_ssdp_event_handler, data,
								   free_ssdp_event_handler_data) != 0) {
			free_ssdp_event_handler_data(data);
		}
	} else {
		free_ssdp_event_handler_data(data);
	}
}

static int create_ssdp_sock_v4(SOCKET *ssdpSock)
{
	char errorBuffer[ERROR_BUFFER_LEN];
	char ttl = 4;
	int onOff;
	struct ip_mreq ssdpMcastAddr;
	struct sockaddr_storage __ss;
	auto ssdpAddr4 = reinterpret_cast<struct sockaddr_in *>(&__ss);
	int ret = 0;
	struct in_addr addr;

	*ssdpSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (*ssdpSock == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "socket(): %s\n", errorBuffer);

		return UPNP_E_OUTOF_SOCKET;
	}
	onOff = 1;
	ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEADDR,
					 reinterpret_cast<char *>(&onOff), sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() SO_REUSEADDR: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
#if (defined(BSD) && !defined(__GNU__)) || defined(__OSX__) || defined(__APPLE__)
	onOff = 1;
	ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEPORT,
					 (char *)&onOff, sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() SO_REUSEPORT: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
#endif /* BSD, __OSX__, __APPLE__ */
	memset(&__ss, 0, sizeof(__ss));
	ssdpAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
	ssdpAddr4->sin_addr.s_addr = htonl(INADDR_ANY);
	ssdpAddr4->sin_port = htons(SSDP_PORT);
	ret = bind(*ssdpSock, reinterpret_cast<struct sockaddr *>(ssdpAddr4), sizeof(*ssdpAddr4));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "bind(), addr=0x%08lX, port=%d: %s\n",
				   static_cast<unsigned long>INADDR_ANY, SSDP_PORT, errorBuffer);
		ret = UPNP_E_SOCKET_BIND;
		goto error_handler;
	}
	memset((void *)&ssdpMcastAddr, 0, sizeof(struct ip_mreq));
	ssdpMcastAddr.imr_interface.s_addr = inet_addr(gIF_IPV4);
	ssdpMcastAddr.imr_multiaddr.s_addr = inet_addr(SSDP_IP);
	ret = setsockopt(*ssdpSock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
					 reinterpret_cast<char *>(&ssdpMcastAddr), sizeof(struct ip_mreq));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() IP_ADD_MEMBERSHIP: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
	/* Set multicast interface. */
	memset((void *)&addr, 0, sizeof(struct in_addr));
	addr.s_addr = inet_addr(gIF_IPV4);
	ret = setsockopt(*ssdpSock, IPPROTO_IP, IP_MULTICAST_IF,
					 reinterpret_cast<char *>(&addr), sizeof addr);
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__,
				   "setsockopt() IP_MULTICAST_IF: %s\n", errorBuffer);
		/* This is probably not a critical error, so let's continue. */
	}
	/* result is not checked becuase it will fail in WinMe and Win9x. */
	setsockopt(*ssdpSock, IPPROTO_IP,
			   IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	onOff = 1;
	ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_BROADCAST,
					 reinterpret_cast<char *>(&onOff), sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() SO_BROADCAST: %s\n", errorBuffer);
		ret = UPNP_E_NETWORK_ERROR;
		goto error_handler;
	}
	ret = UPNP_E_SUCCESS;

error_handler:
	if (ret != UPNP_E_SUCCESS) {
		UpnpCloseSocket(*ssdpSock);
	}

	return ret;
}

#ifdef INCLUDE_CLIENT_APIS

static int sock_make_no_blocking(SOCKET sock)
{
#ifdef _WIN32
	u_long val = 1;
	return ioctlsocket(sock, FIONBIO, &val);
#else /* ! _WIN32 ->*/
	int val = fcntl(sock, F_GETFL, 0);
	return fcntl(sock, F_SETFL, val | O_NONBLOCK);
#endif /* !_WIN32 */
}

/* Create the SSDP IPv4 socket to be used by the control point. */
static int create_ssdp_sock_reqv4(SOCKET *ssdpReqSock)
{
	char errorBuffer[ERROR_BUFFER_LEN];

	*ssdpReqSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (*ssdpReqSock == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "socket(): %s\n", errorBuffer);
		return UPNP_E_OUTOF_SOCKET;
	}
	char ttl = 4;
	setsockopt(*ssdpReqSock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	/* just do it, regardless if fails or not. */
	sock_make_no_blocking(*ssdpReqSock);

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */

#ifdef UPNP_ENABLE_IPV6

static int create_ssdp_sock_v6(bool isulagua, SOCKET *ssdpSock)
{
	char errorBuffer[ERROR_BUFFER_LEN];
	struct ipv6_mreq ssdpMcastAddr;
	struct sockaddr_storage __ss;
	auto ssdpAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&__ss);
	int onOff;
	int ret = 0;

	*ssdpSock = socket(AF_INET6, SOCK_DGRAM, 0);
	if (*ssdpSock == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "socket(): %s\n", errorBuffer);

		return UPNP_E_OUTOF_SOCKET;
	}
	onOff = 1;
	ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEADDR,
					 reinterpret_cast<char *>(&onOff), sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() SO_REUSEADDR: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
#if (defined(BSD) && !defined(__GNU__)) || defined(__OSX__) || defined(__APPLE__)
	onOff = 1;
	ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEPORT,
					 (char *)&onOff, sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() SO_REUSEPORT: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
#endif /* BSD, __OSX__, __APPLE__ */
	onOff = 1;
	ret = setsockopt(*ssdpSock, IPPROTO_IPV6, IPV6_V6ONLY,
					 reinterpret_cast<char *>(&onOff), sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() IPV6_V6ONLY: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
	memset(&__ss, 0, sizeof(__ss));
	ssdpAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
	ssdpAddr6->sin6_addr = in6addr_any;
	ssdpAddr6->sin6_scope_id = gIF_INDEX;
	ssdpAddr6->sin6_port = htons(SSDP_PORT);
	ret = bind(*ssdpSock, reinterpret_cast<struct sockaddr *>(ssdpAddr6),
			   sizeof(*ssdpAddr6));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "bind(): addr=0x%032lX, port=%d: %s\n", 0LU, SSDP_PORT,
				   errorBuffer);
		ret = UPNP_E_SOCKET_BIND;
		goto error_handler;
	}
	memset((void *)&ssdpMcastAddr, 0, sizeof(ssdpMcastAddr));
	ssdpMcastAddr.ipv6mr_interface = gIF_INDEX;
	if (isulagua) {
		/* ULAGUA SITE LOCAL */
		inet_pton(AF_INET6, SSDP_IPV6_SITELOCAL,
				  &ssdpMcastAddr.ipv6mr_multiaddr);
	} else {
		inet_pton(AF_INET6, SSDP_IPV6_LINKLOCAL,
				  &ssdpMcastAddr.ipv6mr_multiaddr);
	}
	ret = setsockopt(*ssdpSock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
					 reinterpret_cast<char *>(&ssdpMcastAddr),
					 sizeof(ssdpMcastAddr));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() IPV6_JOIN_GROUP: %s\n", errorBuffer);
		ret = UPNP_E_SOCKET_ERROR;
		goto error_handler;
	}
	onOff = 1;
	ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_BROADCAST,
					 reinterpret_cast<char *>(&onOff), sizeof(onOff));
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "setsockopt() SO_BROADCAST: %s\n", errorBuffer);
		ret = UPNP_E_NETWORK_ERROR;
		goto error_handler;
	}
	ret = UPNP_E_SUCCESS;

error_handler:
	if (ret != UPNP_E_SUCCESS) {
		UpnpCloseSocket(*ssdpSock);
	}

	return ret;
}

#ifdef INCLUDE_CLIENT_APIS
/* Create the SSDP IPv6 socket to be used by the control point. */
static int create_ssdp_sock_reqv6(SOCKET *ssdpReqSock)
{
	char errorBuffer[ERROR_BUFFER_LEN];
	char hops = 1;

	*ssdpReqSock = socket(AF_INET6, SOCK_DGRAM, 0);
	if (*ssdpReqSock == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "socket(): %s\n", errorBuffer);
		return UPNP_E_OUTOF_SOCKET;
	}
	/* MUST use scoping of IPv6 addresses to control the propagation os SSDP
	 * messages instead of relying on the Hop Limit (Equivalent to the TTL
	 * limit in IPv4). */
	setsockopt(*ssdpReqSock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
			   &hops, sizeof(hops));
	/* just do it, regardless if fails or not. */
	sock_make_no_blocking(*ssdpReqSock);

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */

#endif /* UPNP_ENABLE_IPV6 */


static void maybeCLoseAndInvalidate(SOCKET *s, int doclose)
{
	if (doclose && *s != INVALID_SOCKET) {
		UpnpCloseSocket(*s);
	}
	*s = INVALID_SOCKET;
}

static void closeSockets(MiniServerSockArray *out, int doclose)
{
#ifdef INCLUDE_CLIENT_APIS
	maybeCLoseAndInvalidate(&out->ssdpReqSock4, doclose);
	maybeCLoseAndInvalidate(&out->ssdpReqSock6, doclose);
#endif	
	maybeCLoseAndInvalidate(&out->ssdpSock4, doclose);
	maybeCLoseAndInvalidate(&out->ssdpSock6, doclose);
	maybeCLoseAndInvalidate(&out->ssdpSock6UlaGua, doclose);
}

int get_ssdp_sockets(MiniServerSockArray * out)
{
	int retVal = UPNP_E_SOCKET_ERROR;

	closeSockets(out, 0);

#ifdef INCLUDE_CLIENT_APIS
	/* Create the IPv4 socket for SSDP REQUESTS */
	if (strlen(gIF_IPV4) > static_cast<size_t>(0)) {
		if ((retVal = create_ssdp_sock_reqv4(&out->ssdpReqSock4))
			!= UPNP_E_SUCCESS) {
			goto out;
		}
		/* For use by ssdp control point. */
		gSsdpReqSocket4 = out->ssdpReqSock4;
	}
#ifdef UPNP_ENABLE_IPV6
	/* Create the IPv6 socket for SSDP REQUESTS */
	if (strlen(gIF_IPV6) > static_cast<size_t>(0)) {
		if ((retVal = create_ssdp_sock_reqv6(&out->ssdpReqSock6))
			!= UPNP_E_SUCCESS) {
			goto out;
		}
		/* For use by ssdp control point. */
		gSsdpReqSocket6 = out->ssdpReqSock6;
	}
#endif /* IPv6 */
#endif /* INCLUDE_CLIENT_APIS */

	/* Create the IPv4 socket for SSDP */
	if (strlen(gIF_IPV4) > static_cast<size_t>(0)) {
		if ((retVal = create_ssdp_sock_v4(&out->ssdpSock4)) != UPNP_E_SUCCESS) {
			goto out;
		}
	}

#ifdef UPNP_ENABLE_IPV6
	/* Create the IPv6 socket for SSDP */
	if (strlen(gIF_IPV6) > static_cast<size_t>(0)) {
		if ((retVal = create_ssdp_sock_v6(false, &out->ssdpSock6))
			!= UPNP_E_SUCCESS) {
			goto out;
		}
	}
	if (strlen(gIF_IPV6_ULA_GUA) > static_cast<size_t>(0)) {
		if ((retVal = create_ssdp_sock_v6(true, &out->ssdpSock6UlaGua))
			!= UPNP_E_SUCCESS) {
			goto out;
		}
	}
#endif /* UPNP_ENABLE_IPV6 */

	retVal = UPNP_E_SUCCESS;
out:
	if (retVal != UPNP_E_SUCCESS) {
		closeSockets(out, 1);
	}
	return retVal;
}
#endif /* EXCLUDE_SSDP */

/* @} SSDPlib */
