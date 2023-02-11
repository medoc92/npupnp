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
#include <stdarg.h>
#include <thread>
#include <algorithm>
#include <condition_variable>

#include <microhttpd.h>

#if MHD_VERSION < 0x00095300
#define MHD_USE_INTERNAL_POLLING_THREAD MHD_USE_SELECT_INTERNALLY
#endif

#if MHD_VERSION <= 0x00097000
#define MHD_Result int
#endif

#define APPLICATION_LISTENING_PORT 49152

using MiniServerState = enum {
    MSERV_IDLE,
    MSERV_RUNNING
};

/*!
 * module vars
 */
static std::mutex gMServStateMutex;
static std::condition_variable gMServStateCV;
static MiniServerSockArray *miniSocket;
static MiniServerState gMServState = MSERV_IDLE;
static struct MHD_Daemon *mhd;

#ifdef INTERNAL_WEB_SERVER
static MiniServerCallback gGetCallback = nullptr;
static MiniServerCallback gSoapCallback = nullptr;
static MiniServerCallback gGenaCallback = nullptr;

void SetHTTPGetCallback(MiniServerCallback callback)
{
    std::lock_guard<std::mutex> lck(gMServStateMutex);
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

static MHD_Result headers_cb(void *cls, enum MHD_ValueKind, const char *k, const char *value)
{
    auto mhtt = static_cast<MHDTransaction *>(cls);
    std::string key(k);
    stringtolower(key);
    // It is always possible to combine multiple identically named headers
    // name into one comma separated list. See HTTP 1.1 section 4.2
    auto it = mhtt->headers.find(key);
    if (it != mhtt->headers.end()) {
        it->second = it->second + "," + value;
    } else {
        mhtt->headers[key] = value;
    }
    UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__,
               "miniserver:req_header: [%s: %s]\n",    key.c_str(), value);
    return MHD_YES;
}

static MHD_Result show_resp_headers_cb(
    void *, enum MHD_ValueKind, const char *k, const char *value)
{
    UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__,
               "miniserver:resp_header: [%s] -> [%s]\n", k, value);
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

// Use int not enum as data.second spares a map code instanciation (at least with some compilers)
static const std::map<std::string, int> strmethtometh {
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
    if (NetIF::Interfaces::interfaceForAddress(incoming, g_netifs, ifaddr) == nullptr) {
        UpnpPrintf(UPNP_ERROR, MSERV, __FILE__, __LINE__,
                   "Refusing connection from %s\n", incoming.straddr().c_str());
        return MHD_NO;
    }
    return MHD_YES;
}

// Validate the HOST header. This is is to mitigate a possible dns rebinding
// exploit implemented in a local browser. Of course this supposes that the
// browser will actually set a meaningful HOST, which they appear to do.
enum VHH_Status{VHH_YES, VHH_NO, VHH_REDIRECT};
static VHH_Status validate_host_header(MHDTransaction *mhdt, NetIF::IPAddr& claddr)
{
    // Find HOST header
    auto hostit = mhdt->headers.find("host");
    if (hostit == mhdt->headers.end()) {
        // UPNP specifies that HOST is required in HTTP requests
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                   "answer_to_connection: no HOST header in request from %s\n",
                   claddr.straddr().c_str());
        return VHH_NO;
    }
    // Parse the value
    struct hostport_type hostport;
    if (UPNP_E_INVALID_URL == parse_hostport(hostit->second.c_str(), &hostport, false)) {
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                   "answer_to_connection: bad HOST header %s in request from %s\n",
                   hostit->second.c_str(), claddr.straddr().c_str());
        return VHH_NO;
    }

    // Host name: if the appropriate callback was set, validate with the client
    // code, only for a Web request (UPnP calls like SOAP want numeric).
    if (hostport.hostisname) {
        switch (mhdt->method) {
        case HTTPMETHOD_GET:
        case HTTPMETHOD_HEAD:
        case HTTPMETHOD_POST:
        case HTTPMETHOD_SIMPLEGET:
            break;
        default:
            UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                       "answer_to_connection: bad HOST header %s (host name) in non-web "
                       "request from %s\n", hostit->second.c_str(), claddr.straddr().c_str());
            return VHH_NO;
        }
        if (nullptr != g_hostvalidatecallback &&
            g_hostvalidatecallback(
                hostport.strhost.c_str(), g_hostvalidatecookie) == UPNP_E_SUCCESS) {
            return VHH_YES;
        }
        return (g_optionFlags & UPNP_FLAG_REJECT_HOSTNAMES) ? VHH_NO : VHH_REDIRECT;
    }

    // At this point, we know that we had a numeric IP address, and we
    // don't actually need to check the addresses against our
    // interfaces, the dns-rebind issue is solved. However, just because we can:
    NetIF::IPAddr hostaddr(hostport.strhost);
    if (!hostaddr.ok()) {
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                   "answer_to_connection: bad HOST header %s in request from %s\n",
                   hostit->second.c_str(), claddr.straddr().c_str());
        return VHH_NO;
    }
    // IPV6: set the scope idx from the client sockaddr. Does nothing for IPV4
    hostaddr.setScopeIdx(claddr);
    NetIF::IPAddr notused;
    if (nullptr == NetIF::Interfaces::interfaceForAddress(hostaddr, g_netifs, notused)) {
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                   "answer_to_connection: no interface for address in HOST header %s "
                   "in request from %s\n", hostit->second.c_str(), claddr.straddr().c_str());
        return VHH_NO;
    }

#if 0
    UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
               "answer_to_connection: host header %s (host %s port %s) ok for claddr %s\n",
               hostit->second.c_str(), hostport.strhost.c_str(), hostport.strport.c_str(),
               claddr.straddr().c_str());
#endif
    return VHH_YES;
}

static std::string rebuild_url_from_mhdt(
    MHDTransaction *mhdt, const std::string& path, NetIF::IPAddr& claddr)
{
    std::string aurl("http://");
    auto hostport = UpnpGetUrlHostPortForClient(mhdt->client_address);
    if (hostport.empty()) {
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                   "answer_to_connection: got empty hostport for connection from %s\n",
                   claddr.straddr().c_str());
        return {};
    }
    aurl += hostport;
    aurl += path;
    if (!mhdt->queryvalues.empty()) {
        aurl += "?";
        for (const auto& entry: mhdt->queryvalues) {
            aurl += query_encode(entry.first) + "=" + query_encode(entry.second) + "&";
        }
        aurl.pop_back();
    }
    return aurl;
}

static MHD_Result answer_to_connection(
    void *, struct MHD_Connection *conn, 
    const char *url, const char *method, const char *version, 
    const char *upload_data, size_t *upload_data_size,
    void **con_cls)
{
    if (nullptr == *con_cls) {
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                   "answer_to_connection1: url [%s] method [%s]"
                   " version [%s]\n", url, method, version);
        // First call, allocate and set context, get the headers, etc.
        auto mhdt = new MHDTransaction;
        *con_cls = mhdt;
        MHD_get_connection_values(conn, MHD_HEADER_KIND, headers_cb, mhdt);
        mhdt->client_address =
            reinterpret_cast<struct sockaddr_storage*>(
                MHD_get_connection_info (conn,MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr);

        MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND, queryvalues_cb, mhdt);
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
                mhdt->method = http_method_t(it->second);
            }
        }

        // We normally verify the contents of the HOST header, but we used not
        // to. This option preserves the old behaviour.
        if (g_optionFlags & UPNP_FLAG_NO_HOST_VALIDATE) {
            return MHD_YES;
        }
        
        NetIF::IPAddr claddr(reinterpret_cast<sockaddr*>(mhdt->client_address));
        switch (validate_host_header(mhdt, claddr)) {
        case VHH_YES: return MHD_YES;
        case VHH_NO: return MHD_NO;
        case VHH_REDIRECT: break;
        }

        // Redirect
        std::string aurl = rebuild_url_from_mhdt(mhdt, url, claddr);
        if (aurl.empty()) {
            return MHD_NO;
        }
        UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__, "Redirecting to [%s]\n", aurl.c_str());
        struct MHD_Response *response = MHD_create_response_from_buffer(0,0,MHD_RESPMEM_PERSISTENT);
        if (nullptr == response ) {
            UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__,
                       "answer_to_connection: can't create redirect\n");
            return MHD_NO;
        }
        MHD_add_response_header (response, "Location", aurl.c_str());
        MHD_Result ret = MHD_queue_response(conn, 302, response);
        MHD_destroy_response(response);
        return ret;
    }

    auto mhdt = static_cast<MHDTransaction *>(*con_cls);
    if (*upload_data_size) {
        mhdt->postdata.append(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }
    UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__,
               "answer_to_connection: end of upload, postdata:\n[%s]\n", mhdt->postdata.c_str());
    
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
    {
        std::lock_guard<std::mutex> lck(gMServStateMutex);
        callback = gGetCallback;
        break;
    }
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

    //MHD_add_response_header(mhdt->response, "Connection", "close");

    MHD_get_response_headers (mhdt->response, show_resp_headers_cb, nullptr);
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
        ss = {};
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

class MiniServerJobWorker : public JobWorker {
public:
    MiniServerJobWorker() = default;
    virtual ~MiniServerJobWorker() = default;
    virtual void work();
};
/*!
 * \brief Run the miniserver.
 *
 * The MiniServer accepts a new request and schedules a thread to handle the
 * new request. Checks for socket state and invokes appropriate read and
 * shutdown actions for the Miniserver and SSDP sockets.
 */
void MiniServerJobWorker::work()
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

    {
        std::unique_lock<std::mutex> lck(gMServStateMutex);
        gMServState = MSERV_RUNNING;
        gMServStateCV.notify_all();
    }
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

    std::unique_lock<std::mutex> lck(gMServStateMutex);
    delete miniSocket;
    miniSocket = nullptr;
    gMServState = MSERV_IDLE;
    gMServStateCV.notify_all();
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
    code = getsockname(
        sockfd, reinterpret_cast<struct sockaddr *>(&sockinfo), &len);
    if (code == -1) {
        return -1;
    }
    if (sockinfo.ss_family == AF_INET) {
        *port = ntohs(reinterpret_cast<struct sockaddr_in*>(&sockinfo)->sin_port);
    } else if(sockinfo.ss_family == AF_INET6) {
        *port = ntohs(reinterpret_cast<struct sockaddr_in6*>(&sockinfo)->sin6_port);
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
    stop_sockaddr = {};
    stop_sockaddr.sin_family = static_cast<sa_family_t>(AF_INET);
    stop_sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = bind(out->miniServerStopSock,
               reinterpret_cast<struct sockaddr *>(&stop_sockaddr),
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

    int port = reqport <= 0 ? APPLICATION_LISTENING_PORT : reqport;
    int ret = UPNP_E_SOCKET_BIND;
    struct sockaddr_storage saddr = {};
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

static void mhdlogger(void *, const char *fmt, va_list ap)
{
    char buf[1024];
    vsnprintf(buf, 1023, fmt, ap);
    buf[1023] = 0;
    UpnpPrintf(UPNP_DEBUG, MSERV, __FILE__, __LINE__, "microhttpd: %s\n", buf);
}

/* @param[input,output] listen_port4/6 listening ports for incoming HTTP. */
int StartMiniServer(uint16_t *listen_port4, uint16_t *listen_port6)
{
    int port=0;
    int ret_code = UPNP_E_OUTOF_MEMORY;
    unsigned int mhdflags = 0;

    {
        std::unique_lock<std::mutex> lck(gMServStateMutex);
        if (gMServState != MSERV_IDLE) {
            /* miniserver running. */
            UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                       "miniserver: ALREADY RUNNING !\n");
            return UPNP_E_INTERNAL_ERROR;
        }
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

    {
        std::unique_lock<std::mutex> lck(gMServStateMutex);
        auto worker = std::make_unique<MiniServerJobWorker>();
        ret_code = gMiniServerThreadPool.addPersistent(std::move(worker));
        if (ret_code != 0) {
            ret_code = UPNP_E_OUTOF_MEMORY;
            goto out;
        }
        /* Wait for miniserver to start. */
        gMServStateCV.wait_for(lck, std::chrono::seconds(60));
        if (gMServState != MSERV_RUNNING) {
            /* Took it too long to start that thread. */
            UpnpPrintf(UPNP_CRITICAL, MSERV, __FILE__, __LINE__,
                       "miniserver: thread_miniserver not starting !\n");
            ret_code = UPNP_E_INTERNAL_ERROR;
            goto out;
        }
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
        MHD_OPTION_EXTERNAL_LOGGER, mhdlogger, nullptr, 
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
    struct sockaddr_in stopaddr;
    char buf[256] = "ShutDown";
    size_t bufLen = strlen(buf);

    std::unique_lock<std::mutex> lck(gMServStateMutex);
    if (gMServState != MSERV_RUNNING) {
        return 0;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                   "StopMiniserver: socket(): %s\n", errorBuffer);
        return 0;
    }
    stopaddr.sin_family = static_cast<sa_family_t>(AF_INET);
    stopaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    stopaddr.sin_port = htons(miniSocket->stopPort);

    while (gMServState != MSERV_IDLE) {
        sendto(sock, buf, bufLen, 0,
               reinterpret_cast<struct sockaddr *>(&stopaddr), socklen);
        gMServStateCV.wait_for(lck, std::chrono::seconds(1));
    }
    UpnpCloseSocket(sock);

    return 0;
}
#endif /* EXCLUDE_MINISERVER */
