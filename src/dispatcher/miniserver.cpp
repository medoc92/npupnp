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
 *     - The SSDP sockets for discovery.
 *     - The HTTP listeners for description / control / eventing.
 */

#include "miniserver.h"

#include "httputils.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "ThreadPool.h"
#include "upnpapi.h"
#include "genut.h"
#include "uri.h"
#include "netif.h"
#include "upnp.h"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <thread>
#include <algorithm>

#include <microhttpd.h>

#if MHD_VERSION < 0x00095300
#define MHD_USE_INTERNAL_POLLING_THREAD MHD_USE_SELECT_INTERNALLY
#endif

#if MHD_VERSION <= 0x00097000
#define MHD_Result int
#endif

/*! . */
#define APPLICATION_LISTENING_PORT 49152

/*! . */
using MiniServerState = enum {
    /*! . */
    MSERV_IDLE,
    /*! . */
    MSERV_RUNNING,
    /*! . */
    MSERV_STOPPING
};

/*!
 * module vars
 */
static MiniServerSockArray *miniSocket;
static MiniServerState gMServState = MSERV_IDLE;
static struct MHD_Daemon *mhd;

#ifdef INTERNAL_WEB_SERVER
static MiniServerCallback gGetCallback = nullptr;
static MiniServerCallback gSoapCallback = nullptr;
static MiniServerCallback gGenaCallback = nullptr;

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

#endif /* INTERNAL_WEB_SERVER */

static UPNP_INLINE void fdset_if_valid(SOCKET sock, fd_set *set)
{
    if (sock != INVALID_SOCKET) {
        FD_SET(sock, set);
    }
}

static MHD_Result headers_cb(void *cls, enum MHD_ValueKind,
                             const char *k, const char *value)
{
    auto mhtt = static_cast<MHDTransaction *>(cls);
    std::string key(k);
    stringtolower(key);
    // It is always possible to combine multiple identically named headers
    // name into one comma separated list. See HTTP 1.1 section 4.2
    if (mhtt->headers.find(key) != mhtt->headers.end()) {
        mhtt->headers[key] = mhtt->headers[key] + "," + value;
    } else {
        mhtt->headers[key] = value;
    }
    UpnpPrintf(UPNP_ALL, MSERV, __FILE__, __LINE__,
               "miniserver:gather_header: [%s: %s]\n",    key.c_str(), value);
    return MHD_YES;
}

static MHD_Result queryvalues_cb(void *cls, enum MHD_ValueKind,
                                 const char *key, const char *value)
{
    auto mhdt = static_cast<MHDTransaction *>(cls);
    if (mhdt) {
        UpnpPrintf(UPNP_ALL, MSERV, __FILE__, __LINE__,
                   "miniserver:request value: [%s: %s]\n", key, value);
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
    void *, struct MHD_Connection *,
    void **con_cls, enum MHD_RequestTerminationCode)
{
    if (nullptr == con_cls)
        return;
    auto mhdt = static_cast<MHDTransaction *>(*con_cls);
    delete mhdt;
}


// We listen on INADDR_ANY, but only accept connections from our
// configured interfaces
static MHD_Result filter_connections(
    void *, const sockaddr *addr, socklen_t)
{
    if (g_use_all_interfaces) {
        return MHD_YES;
    }
    NetIF::IPAddr incoming{addr};
    NetIF::IPAddr ifaddr;
    if (NetIF::Interfaces::interfaceForAddress(
            incoming, g_netifs, ifaddr) == nullptr) {
        UpnpPrintf(UPNP_ERROR, MSERV, __FILE__, __LINE__,
                   "Refusing connection from %s\n", incoming.straddr().c_str());
        return MHD_NO;
    }
    return MHD_YES;
}

static MHD_Result answer_to_connection(
    void *, struct MHD_Connection *conn, 
    const char *url, const char *method, const char *version, 
    const char *upload_data, size_t *upload_data_size,
    void **con_cls)
{
    if (nullptr == *con_cls) {
        UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__,
                   "answer_to_connection1: url [%s] method [%s]"
                   " version [%s]\n", url, method, version);
        // First call, allocate and set context, get the headers, etc.
        auto mhdt = new MHDTransaction;
        *con_cls = mhdt;
        MHD_get_connection_values(conn, MHD_HEADER_KIND, headers_cb, mhdt);
        mhdt->client_address =
            reinterpret_cast<struct sockaddr_storage*>(
                MHD_get_connection_info (conn,MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr);

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

    auto mhdt = static_cast<MHDTransaction *>(*con_cls);
    if (*upload_data_size) {
        mhdt->postdata.append(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }
    UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__,
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
        callback = nullptr;
    }
    if (callback == nullptr) {
        return MHD_NO;
    }

    callback(mhdt);

    if (nullptr == mhdt->response) {
        UpnpPrintf(UPNP_ERROR, MSERV, __FILE__, __LINE__,
                   "answer_to_connection: NULL response !!\n");
        return MHD_NO;
    }

    MHD_Result ret = MHD_queue_response(conn, mhdt->httpstatus, mhdt->response);
    MHD_destroy_response(mhdt->response);
    return ret;
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
    socklen_t len;
    struct sockaddr_storage ss;
    auto fromaddr = reinterpret_cast<struct sockaddr *>(&ss);
    char requestBuf[100];

    if (FD_ISSET(ssock, set)) {
        len = sizeof(ss);
        memset(&ss, 0, sizeof(ss));
        byteReceived = recvfrom(
            ssock, requestBuf, static_cast<size_t>(25), 0, fromaddr, &len);
        
        if (byteReceived > 0) {
            requestBuf[byteReceived] = '\0';
            NetIF::IPAddr ipa{fromaddr};
            UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                       "Received response: %s From host %s.\n data: %s\n",
                       requestBuf, ipa.straddr().c_str(), requestBuf);
            if (nullptr != strstr(requestBuf, "ShutDown")) {
                return 1;
            }
        }
    }

    return 0;
}

/*!
 * \brief Run the miniserver.
 *
 * The MiniServer accepts a new request and schedules a thread to handle the
 * new request. Checks for socket state and invokes appropriate read and
 * shutdown actions for the Miniserver and SSDP sockets.
 */
static void *thread_miniserver(void *)
{
    char errorBuffer[ERROR_BUFFER_LEN];
    fd_set expSet;
    fd_set rdSet;
    SOCKET maxMiniSock;
    int ret = 0;
    int stopSock = 0;

    maxMiniSock = 0;
    maxMiniSock = std::max(maxMiniSock, miniSocket->miniServerStopSock);
    maxMiniSock = std::max(maxMiniSock, miniSocket->ssdpSock4);
    if (using_ipv6()) {
        maxMiniSock = std::max(maxMiniSock, miniSocket->ssdpSock6);
        maxMiniSock = std::max(maxMiniSock, miniSocket->ssdpSock6UlaGua);
    }
#ifdef INCLUDE_CLIENT_APIS
    maxMiniSock = std::max(maxMiniSock, miniSocket->ssdpReqSock4);
    if (using_ipv6())
        maxMiniSock = std::max(maxMiniSock, miniSocket->ssdpReqSock6);
#endif /* INCLUDE_CLIENT_APIS */
    ++maxMiniSock;

    gMServState = MSERV_RUNNING;
    while (!stopSock) {
        FD_ZERO(&rdSet);
        FD_ZERO(&expSet);
        /* FD_SET()'s */
        FD_SET(miniSocket->miniServerStopSock, &expSet);
        FD_SET(miniSocket->miniServerStopSock, &rdSet);
        fdset_if_valid(miniSocket->ssdpSock4, &rdSet);
        if (using_ipv6()) {
            fdset_if_valid(miniSocket->ssdpSock6, &rdSet);
            fdset_if_valid(miniSocket->ssdpSock6UlaGua, &rdSet);
        }
#ifdef INCLUDE_CLIENT_APIS
        fdset_if_valid(miniSocket->ssdpReqSock4, &rdSet);
        if (using_ipv6())
            fdset_if_valid(miniSocket->ssdpReqSock6, &rdSet);
#endif /* INCLUDE_CLIENT_APIS */
        /* select() */
        ret = select(static_cast<int>(maxMiniSock), &rdSet, nullptr, &expSet, nullptr);
        if (ret == SOCKET_ERROR && errno == EINTR) {
            continue;
        }
        if (ret == SOCKET_ERROR) {
            posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
            UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
                       "miniserver: select(): %s\n", errorBuffer);
            continue;
        }

#ifdef INCLUDE_CLIENT_APIS
        ssdp_read(miniSocket->ssdpReqSock4, &rdSet);
        if (using_ipv6())
            ssdp_read(miniSocket->ssdpReqSock6, &rdSet);
#endif /* INCLUDE_CLIENT_APIS */
        ssdp_read(miniSocket->ssdpSock4, &rdSet);
        if (using_ipv6()) {
            ssdp_read(miniSocket->ssdpSock6, &rdSet);
            ssdp_read(miniSocket->ssdpSock6UlaGua, &rdSet);
        }
        stopSock = receive_from_stopSock(
            miniSocket->miniServerStopSock, &rdSet);
    }

    delete miniSocket;
    miniSocket = nullptr;
    gMServState = MSERV_IDLE;
    return nullptr;
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
    code = getsockname(
        sockfd, reinterpret_cast<struct sockaddr *>(&sockinfo), &len);
    if (code == -1) {
        return -1;
    }
    if (sockinfo.ss_family == AF_INET) {
        *port = ntohs(((struct sockaddr_in*)&sockinfo)->sin_port);
    } else if(sockinfo.ss_family == AF_INET6) {
        *port = ntohs(((struct sockaddr_in6*)&sockinfo)->sin6_port);
    }
    UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
               "sockfd = %d, .... port = %d\n", sockfd, static_cast<int>(*port));

    return 0;
}

/*!
 * \brief Creates the miniserver STOP socket. This socket is created and 
 *    listened on to know when it is time to stop the Miniserver.
 *
 * \return 
 * \li \c UPNP_E_OUTOF_SOCKET: Failed to create a socket.
 * \li \c UPNP_E_SOCKET_BIND: Bind() failed.
 * \li \c UPNP_E_INTERNAL_ERROR: Port returned by the socket layer is < 0.
 * \li \c UPNP_E_SUCCESS: Success.
 */
static int get_miniserver_stopsock(MiniServerSockArray *out)
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
    stop_sockaddr.sin_family = static_cast<sa_family_t>(AF_INET);
    stop_sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = bind(out->miniServerStopSock, reinterpret_cast<struct sockaddr *>(&stop_sockaddr),
               sizeof(stop_sockaddr));
    if (ret == SOCKET_ERROR) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "Error in binding localhost!!!\n");
        return UPNP_E_SOCKET_BIND;
    }
    ret = get_port(out->miniServerStopSock, &out->stopPort);
    if (ret < 0) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "get_port failed for stop socket\n");
        return UPNP_E_INTERNAL_ERROR;
    }
    return UPNP_E_SUCCESS;
}

static int available_port(int reqport)
{
    char errorBuffer[ERROR_BUFFER_LEN];
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: socket(): %s\n", errorBuffer);
        return UPNP_E_OUTOF_SOCKET;
    }
    int onOff = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<char *>(&onOff), sizeof(onOff)) < 0) {
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: reuseaddr: %s\n", errorBuffer);
    }

    int port = std::max(APPLICATION_LISTENING_PORT, reqport);
    int ret = UPNP_E_SOCKET_BIND;
    struct sockaddr_storage saddr;
    memset(&saddr, 0, sizeof(saddr));
    auto ip = reinterpret_cast<struct sockaddr_in*>(&saddr);
    ip->sin_family = AF_INET;
    ip->sin_addr.s_addr = htonl(INADDR_ANY);
    for (int i = 0; i < 20; i++) {
        ip->sin_port = htons(static_cast<uint16_t>(port));
        if (bind(sock, reinterpret_cast<struct sockaddr*>(&saddr),
                 sizeof(struct sockaddr_in)) == 0) {
            ret = port;
            break;
        }
        bool eaddrinuse{false};
#if defined(_WIN32)
        int error = WSAGetLastError();
        eaddrinuse = (error == WSAEADDRINUSE || error == WSAEACCES);
#else
        eaddrinuse = (errno == EADDRINUSE);
#endif
        if (eaddrinuse) {
            port++;
            continue;
        }
#ifdef _WIN32
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: bind(): WSA error %d\n", error);
#else
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: bind(): %s\n", errorBuffer);
#endif
        ret = UPNP_E_SOCKET_BIND;
        break;
    }

    if (sock != INVALID_SOCKET) {
        UpnpCloseSocket(sock);
    }
    return ret;
}

/* @param[input,output] listen_port4/6 listening ports for incoming HTTP. */
int StartMiniServer(uint16_t *listen_port4, uint16_t *listen_port6)
{
    int port=0;
    int ret_code = UPNP_E_OUTOF_MEMORY;
    unsigned int mhdflags = 0;
    
    switch (gMServState) {
    case MSERV_IDLE:
        break;
    default:
        /* miniserver running. */
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: ALREADY RUNNING !\n");
        return UPNP_E_INTERNAL_ERROR;
    }

    miniSocket = new MiniServerSockArray;
    if (nullptr == miniSocket) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: OUT OF MEMORY !\n");
        return UPNP_E_OUTOF_MEMORY;
    }

    /* Stop socket (To end miniserver processing). */
    ret_code = get_miniserver_stopsock(miniSocket);
    if (ret_code != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: get_miniserver_stopsock() failed\n");
        goto out;
    }

    /* SSDP socket for discovery/advertising. */
    ret_code = get_ssdp_sockets(miniSocket);
    if (ret_code != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: get_ssdp_sockets() failed\n");
        goto out;
    }

    ret_code = gMiniServerThreadPool.addPersistent(thread_miniserver,miniSocket);
    if (ret_code != 0) {
        ret_code = UPNP_E_OUTOF_MEMORY;
        goto out;
    }
    /* Wait for miniserver to start. */
    for (int count = 0; count < 10000; count++) {
        if (gMServState == static_cast<MiniServerState>(MSERV_RUNNING))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (gMServState != static_cast<MiniServerState>(MSERV_RUNNING)) {
        /* Took it too long to start that thread. */
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: thread_miniserver not starting !\n");
        ret_code = UPNP_E_INTERNAL_ERROR;
        goto out;
    }

#ifdef INTERNAL_WEB_SERVER
    port = available_port(static_cast<int>(*listen_port4));
    if (port < 0) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "miniserver: available_port() failed !\n");
        return port;
    }
    *listen_port4 = port;
    *listen_port6 = port;

    mhdflags = MHD_USE_THREAD_PER_CONNECTION |
        MHD_USE_INTERNAL_POLLING_THREAD |
        MHD_USE_DEBUG;

#ifdef UPNP_ENABLE_IPV6
    if (using_ipv6()) {
        mhdflags |= MHD_USE_IPv6 | MHD_USE_DUAL_STACK;
    }
#endif /* UPNP_ENABLE_IPV6 */
    
    mhd = MHD_start_daemon(
        mhdflags, port,
        filter_connections, nullptr, /* Accept policy callback and arg */
        &answer_to_connection, nullptr, /* Request handler and arg */
        MHD_OPTION_NOTIFY_COMPLETED, request_completed_cb, nullptr,
        MHD_OPTION_CONNECTION_TIMEOUT, static_cast<unsigned int>(UPNP_TIMEOUT),
        MHD_OPTION_END);
    if (nullptr == mhd) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "MHD_start_daemon failed\n");
        ret_code = UPNP_E_OUTOF_MEMORY;
        goto out;
    }
#endif

out:
    if (ret_code != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                   "startminiserver failed\n");
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
    while(gMServState != static_cast<MiniServerState>(MSERV_IDLE)) {
        ssdpAddr.sin_family = static_cast<sa_family_t>(AF_INET);
        ssdpAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        ssdpAddr.sin_port = htons(miniSocket->stopPort);
        sendto(sock, buf, bufLen, 0, reinterpret_cast<struct sockaddr *>(&ssdpAddr), socklen);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (gMServState == static_cast<MiniServerState>(MSERV_IDLE)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    UpnpCloseSocket(sock);

    return 0;
}
#endif /* EXCLUDE_MINISERVER */
