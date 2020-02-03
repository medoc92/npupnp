/**************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 * Copyright (C) 2012 France Telecom All rights reserved.
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

#if EXCLUDE_MINISERVER == 0

/*!
 * \file
 *
 * \brief Implements the functionality and utility functions
 * used by the miniserver/dispatcher module.
 *
 * The miniserver is a central point for processing all network requests.
 * It is made of:
 *	 - The SSDP sockets for discovery.
 *	 - The HTTP listeners for description / control / eventing.
 */

#include "miniserver.h"

#include "httputils.h"
#include "ithread.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "ThreadPool.h"
#include "upnpapi.h"
#include "smallut.h"
#include "uri.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <iostream>

#include <microhttpd.h>

#if MHD_VERSION < 0x00095300
#define MHD_USE_INTERNAL_POLLING_THREAD MHD_USE_SELECT_INTERNALLY
#endif

/*! . */
#define APPLICATION_LISTENING_PORT 49152

/*! . */
typedef enum {
	/*! . */
	MSERV_IDLE,
	/*! . */
	MSERV_RUNNING,
	/*! . */
	MSERV_STOPPING
} MiniServerState;

/*!
 * module vars
 */
static MiniServerSockArray *miniSocket;
static MiniServerState gMServState = MSERV_IDLE;
static struct MHD_Daemon *mhd;

#ifdef INTERNAL_WEB_SERVER
static MiniServerCallback gGetCallback = NULL;
static MiniServerCallback gSoapCallback = NULL;
static MiniServerCallback gGenaCallback = NULL;

void SetHTTPGetCallback(MiniServerCallback callback)
{
	gGetCallback = callback;
}

#ifdef INCLUDE_DEVICE_APIS
void SetSoapCallback(MiniServerCallback callback)
{
	gSoapCallback = callback;
}
#endif /* INCLUDE_DEVICE_APIS */

void SetGenaCallback(MiniServerCallback callback)
{
	gGenaCallback = callback;
}

/*!
 * \brief Send Error Message.
 */
static UPNP_INLINE void handle_error(
	MHDTransaction *mhdt,
	/*! [in] HTTP Error Code. */
	int http_error_code)
{
	http_SendStatusResponse(mhdt, http_error_code);
}

#endif /* INTERNAL_WEB_SERVER */

static UPNP_INLINE void fdset_if_valid(SOCKET sock, fd_set *set)
{
	if (sock != INVALID_SOCKET) {
		FD_SET(sock, set);
	}
}

static int headers_cb(void *cls, enum MHD_ValueKind kind, 
					  const char *k, const char *value)
{
	MHDTransaction *mhtt = (MHDTransaction *)cls;
	std::string key(k);
	stringtolower(key);
	mhtt->headers[key] = value;
	UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
			   "miniserver:gather_header: [%s: %s]\n",	key.c_str(), value);
	return MHD_YES;
}

static int queryvalues_cb(void *cls, enum MHD_ValueKind kind, 
						  const char *key, const char *value)
{
	MHDTransaction *mhdt = (MHDTransaction *)cls;
	if (mhdt) {
		//std::cerr << "qvalues_cb:	 " << key << " -> " << value << std::endl;
		mhdt->queryvalues[key] = value;
	}
	return MHD_YES;
}

static const std::map<std::string, http_method_t> strmethtometh {
	{"get", HTTPMETHOD_GET},
	{"head", HTTPMETHOD_HEAD},
	{"m-post", HTTPMETHOD_MPOST},
	{"m-search", HTTPMETHOD_MSEARCH},
	{"notify", HTTPMETHOD_NOTIFY},
	{"post", HTTPMETHOD_POST},
	{"subscribe", HTTPMETHOD_SUBSCRIBE},
	{"unsubscribe", HTTPMETHOD_UNSUBSCRIBE},
		};

void request_completed_cb(
	void *cls, struct MHD_Connection *conn,
	void **con_cls, enum MHD_RequestTerminationCode toe)
{
	if (nullptr == con_cls)
		return;
	MHDTransaction *mhdt = (MHDTransaction *)*con_cls;
	delete mhdt;
}

static int answer_to_connection(
	void *cls, struct MHD_Connection *conn, 
	const char *url, const char *method, const char *version, 
	const char *upload_data, size_t *upload_data_size,
	void **con_cls)
{
	if (NULL == *con_cls) {
		UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
				   "answer_to_connection1: url [%s] method [%s]"
				   " version [%s]\n", url, method, version);
		// First call, allocate and set context, get the headers, etc.
		MHDTransaction *mhdt = new MHDTransaction;
		*con_cls = mhdt;
		MHD_get_connection_values(conn, MHD_HEADER_KIND, headers_cb, mhdt);
		mhdt->client_address =
			(struct sockaddr_storage*)MHD_get_connection_info (
				conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;

		MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND,
								  queryvalues_cb, mhdt);
		mhdt->conn = conn;
		mhdt->url = url;
		mhdt->version = version;
		std::string lmeth(method);
		stringtolower(lmeth);
		const auto it = strmethtometh.find(lmeth);
		if (it == strmethtometh.end()) {
			mhdt->method = HTTPMETHOD_UNKNOWN;
		} else {
			if (it->second == HTTPMETHOD_POST &&
				mhdt->headers.find("soapaction") != mhdt->headers.end()) {
				mhdt->method = SOAPMETHOD_POST;
			} else {
				mhdt->method = it->second;
			}
		}
		return MHD_YES;
	}

	MHDTransaction *mhdt = (MHDTransaction *)*con_cls;
	if (*upload_data_size) {
		mhdt->postdata.append(upload_data, *upload_data_size);
		*upload_data_size = 0;
		return MHD_YES;
	}
	UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
			   "answer_to_connection: end of upload, postdata:\n[%s]\n",
			   mhdt->postdata.c_str());
	
	/* We now have the full request */
	
	MiniServerCallback callback;
	switch (mhdt->method) {
		/* Soap Call */
	case SOAPMETHOD_POST:
	case HTTPMETHOD_MPOST:
		callback = gSoapCallback;
		break;
		/* Gena Call */
	case HTTPMETHOD_NOTIFY:
	case HTTPMETHOD_SUBSCRIBE:
	case HTTPMETHOD_UNSUBSCRIBE:
		callback = gGenaCallback;
		break;
		/* HTTP server call */
	case HTTPMETHOD_GET:
	case HTTPMETHOD_POST:
	case HTTPMETHOD_HEAD:
		callback = gGetCallback;
		break;
	default:
		callback = NULL;
	}
	if (callback == NULL) {
		return MHD_NO;
	}

	callback(mhdt);

	if (nullptr == mhdt->response) {
		std::cerr << "answer_to_connection: NULL response !!\n";
		return MHD_NO;
	} else {
		int ret = MHD_queue_response(conn, mhdt->httpstatus, mhdt->response);
		MHD_destroy_response(mhdt->response);
		return ret;
	}
}

static void web_server_accept(SOCKET lsock, fd_set *set)
{
#ifdef INTERNAL_WEB_SERVER
	SOCKET asock;
	socklen_t clientLen;
	struct sockaddr_storage clientAddr;
	char errorBuffer[ERROR_BUFFER_LEN];

	if (lsock != INVALID_SOCKET && FD_ISSET(lsock, set)) {
		clientLen = sizeof(clientAddr);
		asock = accept(lsock, (struct sockaddr *)&clientAddr, &clientLen);
		if (asock == INVALID_SOCKET) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
					   "miniserver: accept(): %s\n", errorBuffer);
		} else {
			if (MHD_add_connection(mhd,asock, (struct sockaddr*)&clientAddr,
								   clientLen) != MHD_YES) {
				std::cerr << "web_server_accept: MHD add_connection failed\n";
			}

#if 1
			const union MHD_DaemonInfo *info = 
				MHD_get_daemon_info(mhd, MHD_DAEMON_INFO_CURRENT_CONNECTIONS);
			if (info) {
				UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
						   "MHD connection count: %d\n", info->num_connections);
			}
#endif
		}
	}
#endif /* INTERNAL_WEB_SERVER */
}

static void ssdp_read(SOCKET rsock, fd_set *set)
{
	if (rsock != INVALID_SOCKET && FD_ISSET(rsock, set)) {
		readFromSSDPSocket(rsock);
	}
}

static int receive_from_stopSock(SOCKET ssock, fd_set *set)
{
	ssize_t byteReceived;
	socklen_t clientLen;
	struct sockaddr_storage clientAddr;
	char requestBuf[256];
	char buf_ntop[INET6_ADDRSTRLEN];

	if (FD_ISSET(ssock, set)) {
		clientLen = sizeof(clientAddr);
		memset((char *)&clientAddr, 0, sizeof(clientAddr));
		byteReceived = recvfrom(ssock, requestBuf, (size_t)25, 0,
								(struct sockaddr *)&clientAddr, &clientLen);
		if (byteReceived > 0) {
			requestBuf[byteReceived] = '\0';
			inet_ntop(AF_INET, &((struct sockaddr_in*)&clientAddr)->sin_addr,
					  buf_ntop, sizeof(buf_ntop));
			UpnpPrintf( UPNP_INFO, MSERV, __FILE__, __LINE__,
						"Received response: %s From host %s \n",
						requestBuf, buf_ntop );
			UpnpPrintf( UPNP_INFO, MSERV, __FILE__, __LINE__,
						"Received multicast packet: \n %s\n",
						requestBuf);
			if (NULL != strstr(requestBuf, "ShutDown")) {
				return 1;
			}
		}
	}

	return 0;
}

/* MHD has a bug where dead connections are never cleaned up if we use
   it with NO_LISTEN_SOCKET and add_connection(). So we let it listen
   on the webserver socket (and possibly still use add_connection()
   for ipv6 connections */
#define LET_MHD_LISTEN_ON_SOCK4 1

/*!
 * \brief Run the miniserver.
 *
 * The MiniServer accepts a new request and schedules a thread to handle the
 * new request. Checks for socket state and invokes appropriate read and
 * shutdown actions for the Miniserver and SSDP sockets.
 */
static void RunMiniServer(void *)
{
	char errorBuffer[ERROR_BUFFER_LEN];
	fd_set expSet;
	fd_set rdSet;
	SOCKET maxMiniSock;
	int ret = 0;
	int stopSock = 0;

	maxMiniSock = 0;
	maxMiniSock = MAX(maxMiniSock, miniSocket->miniServerSock4);
	maxMiniSock = MAX(maxMiniSock, miniSocket->miniServerSock6);
	maxMiniSock = MAX(maxMiniSock, miniSocket->miniServerStopSock);
	maxMiniSock = MAX(maxMiniSock, miniSocket->ssdpSock4);
	maxMiniSock = MAX(maxMiniSock, miniSocket->ssdpSock6);
	maxMiniSock = MAX(maxMiniSock, miniSocket->ssdpSock6UlaGua);
#ifdef INCLUDE_CLIENT_APIS
	maxMiniSock = MAX(maxMiniSock, miniSocket->ssdpReqSock4);
	maxMiniSock = MAX(maxMiniSock, miniSocket->ssdpReqSock6);
#endif /* INCLUDE_CLIENT_APIS */
	++maxMiniSock;

	gMServState = MSERV_RUNNING;
	while (!stopSock) {
		FD_ZERO(&rdSet);
		FD_ZERO(&expSet);
		/* FD_SET()'s */
		FD_SET(miniSocket->miniServerStopSock, &expSet);
		FD_SET(miniSocket->miniServerStopSock, &rdSet);
#if LET_MHD_LISTEN_ON_SOCK4
		fdset_if_valid(miniSocket->miniServerSock4, &rdSet);
#endif
		fdset_if_valid(miniSocket->miniServerSock6, &rdSet);
		fdset_if_valid(miniSocket->ssdpSock4, &rdSet);
		fdset_if_valid(miniSocket->ssdpSock6, &rdSet);
		fdset_if_valid(miniSocket->ssdpSock6UlaGua, &rdSet);
#ifdef INCLUDE_CLIENT_APIS
		fdset_if_valid(miniSocket->ssdpReqSock4, &rdSet);
		fdset_if_valid(miniSocket->ssdpReqSock6, &rdSet);
#endif /* INCLUDE_CLIENT_APIS */
		/* select() */
		ret = select((int) maxMiniSock, &rdSet, NULL, &expSet, NULL);
		if (ret == SOCKET_ERROR && errno == EINTR) {
			continue;
		}
		if (ret == SOCKET_ERROR) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
					   "miniserver: select(): %s\n", errorBuffer);
			continue;
		} else {
			web_server_accept(miniSocket->miniServerSock4, &rdSet);
			web_server_accept(miniSocket->miniServerSock6, &rdSet);
#ifdef INCLUDE_CLIENT_APIS
			ssdp_read(miniSocket->ssdpReqSock4, &rdSet);
			ssdp_read(miniSocket->ssdpReqSock6, &rdSet);
#endif /* INCLUDE_CLIENT_APIS */
			ssdp_read(miniSocket->ssdpSock4, &rdSet);
			ssdp_read(miniSocket->ssdpSock6, &rdSet);
			ssdp_read(miniSocket->ssdpSock6UlaGua, &rdSet);
			stopSock = receive_from_stopSock(
				miniSocket->miniServerStopSock, &rdSet);
		}
	}

	delete miniSocket;
	miniSocket = nullptr;
	gMServState = MSERV_IDLE;
	return;
}

/*!
 * \brief Returns port to which socket, sockfd, is bound.
 *
 * \return -1 on error; check errno. 0 if successfull.
 */
static int get_port(
	/*! [in] Socket descriptor. */
	SOCKET sockfd,
	/*! [out] The port value if successful, otherwise, untouched. */
	uint16_t *port)
{
	struct sockaddr_storage sockinfo;
	socklen_t len;
	int code;

	len = sizeof(sockinfo);
	code = getsockname(sockfd, (struct sockaddr *)&sockinfo, &len);
	if (code == -1) {
		return -1;
	}
	if (sockinfo.ss_family == AF_INET) {
		*port = ntohs(((struct sockaddr_in*)&sockinfo)->sin_port);
	} else if(sockinfo.ss_family == AF_INET6) {
		*port = ntohs(((struct sockaddr_in6*)&sockinfo)->sin6_port);
	}
	UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
			   "sockfd = %d, .... port = %d\n", sockfd, (int)*port);

	return 0;
}

#ifdef INTERNAL_WEB_SERVER
/*!
 * \brief Create and bind our listening sockets.
 * Returns the actual port which the sockets sub-system returned. 
 *
 * \return
 *	\li UPNP_E_OUTOF_SOCKET: Failed to create a socket.
 *	\li UPNP_E_SOCKET_BIND: Bind() failed.
 *	\li UPNP_E_LISTEN: Listen() failed. 
 *	\li UPNP_E_INTERNAL_ERROR: Port returned by the socket layer is < 0.
 *	\li UPNP_E_SUCCESS: Success.
 */
static int get_miniserver_sockets(
	/*! [in] Socket Array. */
	MiniServerSockArray *out,
	/*! [in] port on which the server is listening for incoming IPv4
	 * connections. */
	uint16_t listen_port4
#ifdef UPNP_ENABLE_IPV6
	,
	/*! [in] port on which the server is listening for incoming IPv6
	 * connections. */
	uint16_t listen_port6
#endif
	)
{
	int ret = UPNP_E_INTERNAL_ERROR;
	char errorBuffer[ERROR_BUFFER_LEN];
	struct sockaddr_storage __ss_v4;
	struct sockaddr_in* serverAddr4 = (struct sockaddr_in*)&__ss_v4;
#ifdef UPNP_ENABLE_IPV6
	struct sockaddr_storage __ss_v6;
	struct sockaddr_in6* serverAddr6 = (struct sockaddr_in6*)&__ss_v6;
#endif

	/* Create listen socket for IPv4/IPv6. An error here may indicate
	 * that we don't have an IPv4/IPv6 stack. */
	out->miniServerSock4 = socket(AF_INET, SOCK_STREAM, 0);
	if (out->miniServerSock4 == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
				   "miniserver: IPv4 socket(): %s\n",  errorBuffer);
	}
#ifdef UPNP_ENABLE_IPV6
	out->miniServerSock6 = socket(AF_INET6, SOCK_STREAM, 0);
	if (out->miniServerSock6 == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
				   "miniserver: IPv6 socket(): %s\n", errorBuffer);
	} else {
		int onOff = 1;
		int sockError = setsockopt(out->miniServerSock6, IPPROTO_IPV6,
								   IPV6_V6ONLY, (char *)&onOff, sizeof(onOff));
		if (sockError == SOCKET_ERROR) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
					   "miniserver: IPv6 setsockopt(): %s\n", errorBuffer);
			UpnpCloseSocket(out->miniServerSock6);
			out->miniServerSock6 = INVALID_SOCKET;
		}
	}
#endif

	if (out->miniServerSock4 == INVALID_SOCKET
#ifdef UPNP_ENABLE_IPV6
		&& out->miniServerSock6 == INVALID_SOCKET
#endif
		) {
		UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
				   "miniserver: no protocols available\n");
		return UPNP_E_OUTOF_SOCKET;
	}

	/* Got at list one listening socket */
	
	/* As per the IANA specifications for the use of ports by applications
	 * override the listen port passed in with the first available. */
	if (listen_port4 < APPLICATION_LISTENING_PORT) {
		listen_port4 = (uint16_t)APPLICATION_LISTENING_PORT;
	}
#ifdef UPNP_ENABLE_IPV6
	if (listen_port6 < APPLICATION_LISTENING_PORT) {
		listen_port6 = (uint16_t)APPLICATION_LISTENING_PORT;
	}
#endif
	memset(&__ss_v4, 0, sizeof (__ss_v4));
	serverAddr4->sin_family = (sa_family_t)AF_INET;
	inet_pton(AF_INET, gIF_IPV4, &serverAddr4->sin_addr);
#ifdef UPNP_ENABLE_IPV6
	memset(&__ss_v6, 0, sizeof (__ss_v6));
	serverAddr6->sin6_family = (sa_family_t)AF_INET6;
	inet_pton(AF_INET6, gIF_IPV6, &serverAddr6->sin6_addr);
	serverAddr6->sin6_scope_id = gIF_INDEX;
#endif

#ifdef UPNP_MINISERVER_REUSEADDR
	UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
			   "miniserver: resuseaddr is set.\n");
	if (out->miniServerSock4 != INVALID_SOCKET) {
		if (setsockopt(
				out->miniServerSock4, SOL_SOCKET, SO_REUSEADDR,
				(const char *)&reuseaddr_on, sizeof(int)) == SOCKET_ERROR) {
			ret = UPNP_E_SOCKET_BIND;
			goto out;
		}
	}
#ifdef UPNP_ENABLE_IPV6
	if (out->miniServerSock6 != INVALID_SOCKET) {
		if (setsockopt(
				out->miniServerSock6, SOL_SOCKET, SO_REUSEADDR,
				(const char *)&reuseaddr_on, sizeof(int)) == SOCKET_ERROR) {
			ret = UPNP_E_SOCKET_BIND;
			goto out;
		}
	}
#endif	/* IPv6 */
#endif /* REUSEADDR */

	if (out->miniServerSock4 != INVALID_SOCKET) {
		uint16_t orig_listen_port4 = listen_port4;
		int sockError, errCode = 0;
		do {
			serverAddr4->sin_port = htons(listen_port4++);
			sockError = bind(
				out->miniServerSock4,
				(struct sockaddr *)serverAddr4, sizeof(*serverAddr4));
			if (sockError == SOCKET_ERROR) {
				errCode = UPNP_SOCK_GET_LAST_ERROR();
				if (errno == EADDRINUSE) {
					errCode = 1;
				}
			} else {
				errCode = 0;
			}
		} while (errCode != 0 && listen_port4 >= orig_listen_port4);
		if (sockError == SOCKET_ERROR) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
					   "miniserver: ipv4 bind(): %s\n", errorBuffer);
			ret = UPNP_E_SOCKET_BIND;
			goto out;
		}
	}
#ifdef UPNP_ENABLE_IPV6
	if (out->miniServerSock6 != INVALID_SOCKET) {
		uint16_t orig_listen_port6 = listen_port6;
		int sockError, errCode = 0;
		do {
			serverAddr6->sin6_port = htons(listen_port6++);
			sockError = bind(
				out->miniServerSock6,
				(struct sockaddr *)serverAddr6,sizeof(*serverAddr6));
			if (sockError == SOCKET_ERROR) {
				errCode = UPNP_SOCK_GET_LAST_ERROR();
				if (errno == EADDRINUSE) {
					errCode = 1;
				}
			} else {
				errCode = 0;
			}
		} while (errCode != 0 && listen_port6 >= orig_listen_port6);
		if (sockError == SOCKET_ERROR) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
					   "miniserver: ipv6 bind(): %s\n", errorBuffer);
			ret = UPNP_E_SOCKET_BIND;
			goto out;
		}
	}
#endif

	if (out->miniServerSock4 != INVALID_SOCKET) {
		int ret_code = listen(out->miniServerSock4, SOMAXCONN);
		if (ret_code == SOCKET_ERROR) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
					   "miniserver: ipv4 listen(): %s\n", errorBuffer);
			ret = UPNP_E_LISTEN;
			goto out;
		}
		if (get_port(out->miniServerSock4, &out->miniServerPort4) < 0) {
			ret = UPNP_E_INTERNAL_ERROR;
			goto out;
		}
	}
#ifdef UPNP_ENABLE_IPV6
	if (out->miniServerSock6 != INVALID_SOCKET) {
		int ret_code = listen(out->miniServerSock6, SOMAXCONN);
		if (ret_code == SOCKET_ERROR) {
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
					   "miniserver: ipv6 listen(): %s\n", errorBuffer);
			ret = UPNP_E_LISTEN;
			goto out;
		}
		if (get_port(out->miniServerSock6, &out->miniServerPort6) < 0) {
			ret = UPNP_E_INTERNAL_ERROR;
			goto out;
		}
	}
#endif

	ret = UPNP_E_SUCCESS;

out:
/* No need to cleanup, our caller will do it */
	return ret;
}
#endif /* INTERNAL_WEB_SERVER */

/*!
 * \brief Creates the miniserver STOP socket. This socket is created and 
 *	listened on to know when it is time to stop the Miniserver.
 *
 * \return 
 * \li \c UPNP_E_OUTOF_SOCKET: Failed to create a socket.
 * \li \c UPNP_E_SOCKET_BIND: Bind() failed.
 * \li \c UPNP_E_INTERNAL_ERROR: Port returned by the socket layer is < 0.
 * \li \c UPNP_E_SUCCESS: Success.
 */
static int get_miniserver_stopsock(
	/*! [in] Miniserver Socket Array. */
	MiniServerSockArray *out)
{
	char errorBuffer[ERROR_BUFFER_LEN];
	struct sockaddr_in stop_sockaddr;
	int ret = 0;

	out->miniServerStopSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (out->miniServerStopSock == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
				   "miniserver: stopsock: socket(): %s\n", errorBuffer);
		return UPNP_E_OUTOF_SOCKET;
	}
	/* Bind to local socket. */
	memset(&stop_sockaddr, 0, sizeof (stop_sockaddr));
	stop_sockaddr.sin_family = (sa_family_t)AF_INET;
	stop_sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	ret = bind(out->miniServerStopSock, (struct sockaddr *)&stop_sockaddr,
			   sizeof(stop_sockaddr));
	if (ret == SOCKET_ERROR) {
		UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
				   "Error in binding localhost!!!\n");
		return UPNP_E_SOCKET_BIND;
	}
	ret = get_port(out->miniServerStopSock, &out->stopPort);
	if (ret < 0) {
		return UPNP_E_INTERNAL_ERROR;
	}
	return UPNP_E_SUCCESS;
}


int StartMiniServer(
	/*! [in,out] Port on which the server listens for incoming IPv4
	 * connections. */
	uint16_t *listen_port4, 
	/*! [in,out] Port on which the server listens for incoming IPv6
	 * connections. */
	uint16_t *listen_port6)
{
	int ret_code = UPNP_E_OUTOF_MEMORY;

	switch (gMServState) {
	case MSERV_IDLE:
		break;
	default:
		/* miniserver running. */
		return UPNP_E_INTERNAL_ERROR;
	}

	miniSocket = new MiniServerSockArray;
	if (NULL == miniSocket) {
		return UPNP_E_OUTOF_MEMORY;
	}

#ifdef INTERNAL_WEB_SERVER
	/* V4 and V6 http listeners. */
	ret_code = get_miniserver_sockets(miniSocket, *listen_port4
#ifdef UPNP_ENABLE_IPV6
									  , *listen_port6
#endif
		);
	if (ret_code != UPNP_E_SUCCESS) {
		goto out;
	}
#endif

	/* Stop socket (To end miniserver processing). */
	ret_code = get_miniserver_stopsock(miniSocket);
	if (ret_code != UPNP_E_SUCCESS) {
		goto out;
	}

	/* SSDP socket for discovery/advertising. */
	ret_code = get_ssdp_sockets(miniSocket);
	if (ret_code != UPNP_E_SUCCESS) {
		goto out;
	}

	ThreadPoolJob job;
	memset(&job, 0, sizeof(job));
	TPJobInit(&job, (start_routine)RunMiniServer, (void *)miniSocket);
	TPJobSetPriority(&job, MED_PRIORITY);
	TPJobSetFreeFunction(&job, (free_routine)free);
	ret_code = ThreadPoolAddPersistent(&gMiniServerThreadPool, &job, NULL);
	if (ret_code < 0) {
		ret_code = UPNP_E_OUTOF_MEMORY;
		goto out;
	}

	/* Wait for miniserver to start. */
	for (int count = 0; count < 10000; count++) {
		if (gMServState == (MiniServerState)MSERV_RUNNING)
			break;
		/* 0.05s */
		usleep(50u * 1000u);
	}
	if (gMServState != (MiniServerState)MSERV_RUNNING) {
		/* Took it too long to start that thread. */
		ret_code = UPNP_E_INTERNAL_ERROR;
		goto out;
	}

#ifdef INTERNAL_WEB_SERVER
	*listen_port4 = miniSocket->miniServerPort4;
	*listen_port6 = miniSocket->miniServerPort6;
#endif

	mhd = MHD_start_daemon(
		MHD_USE_THREAD_PER_CONNECTION |
#ifndef LET_MHD_LISTEN_ON_SOCK4
		MHD_USE_NO_LISTEN_SOCKET |
#endif
		MHD_USE_INTERNAL_POLLING_THREAD |
		MHD_USE_DEBUG,
		-1, /* No port because we supply the listen fd */
		NULL, NULL, /* Accept policy callback and arg */
		/* handler and arg */
		&answer_to_connection, NULL,
		MHD_OPTION_NOTIFY_COMPLETED, request_completed_cb, nullptr,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)UPNP_TIMEOUT,
#if LET_MHD_LISTEN_ON_SOCK4
		MHD_OPTION_LISTEN_SOCKET, miniSocket->miniServerSock4,
#endif
		MHD_OPTION_END);
	if (NULL == mhd) {
		ret_code = UPNP_E_OUTOF_MEMORY;
		goto out;
	}

out:
	if (ret_code != UPNP_E_SUCCESS) {
		delete miniSocket;
		miniSocket = nullptr;
	}
	return ret_code;
}

int StopMiniServer()
{
	char errorBuffer[ERROR_BUFFER_LEN];
	socklen_t socklen = sizeof (struct sockaddr_in);
	SOCKET sock;
	struct sockaddr_in ssdpAddr;
	char buf[256] = "ShutDown";
	size_t bufLen = strlen(buf);

	switch(gMServState) {
	case MSERV_RUNNING:
		gMServState = MSERV_STOPPING;
		break;
	default:
		return 0;
	}
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
				   "StopMiniserver: socket(): %s\n", errorBuffer);
		return 0;
	}
	while(gMServState != (MiniServerState)MSERV_IDLE) {
		ssdpAddr.sin_family = (sa_family_t)AF_INET;
		ssdpAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
		ssdpAddr.sin_port = htons(miniSocket->stopPort);
		sendto(sock, buf, bufLen, 0, (struct sockaddr *)&ssdpAddr, socklen);
		usleep(1000u);
		if (gMServState == (MiniServerState)MSERV_IDLE) {
			break;
		}
		isleep(1u);
	}
	UpnpCloseSocket(sock);

	return 0;
}
#endif /* EXCLUDE_MINISERVER */
