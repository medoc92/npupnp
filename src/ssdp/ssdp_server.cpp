/**************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 * Copyright (C) 2011-2012 France Telecom All rights reserved.
 * Copyright (c) 2020-2023 J.F. Dockes
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

#include "ThreadPool.h"
#include "genut.h"
#include "upnpapi.h"
#include "uri.h"

#include <algorithm>
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
//
// ST
//     ssdp:all
// ST, NT
//     upnp:rootdevice
//     urn:domain-name:device:<deviceType>:<v>
//     urn:schemas-upnp-org:device:<deviceType>:<v>
//     urn:domain-name:service:<serviceType>:<v>
//     urn:schemas-upnp-org:service:<serviceType>:<v>
//  ST, NT, USN
//     uuid:<device-UUID>
//  USN
//     uuid:<device-UUID>::upnp:rootdevice
//     uuid:<device-UUID>::urn:schemas-upnp-org:device:<deviceType>:<v>
//     uuid:<device-UUID>::urn:domain-name:device:<deviceType>:<v>
//     uuid:<device-UUID>::urn:domain-name:service:<serviceType>:<v>
//     uuid:<device-UUID>::urn:schemas-upnp-org:service:<serviceType>:<v>
// We get the UDN, device or service type as available.
int unique_service_name(const char *cmd, SsdpEntity *Evt)
{
    int CommandFound = 0;

    if (strstr(cmd, "uuid:") == cmd) {
        const char *theend = strstr(cmd, "::");
        if (nullptr != theend) {
            size_t n = theend - cmd;
            Evt->UDN = std::string(cmd, n);
        } else {
            Evt->UDN = std::string(cmd, std::min(LINE_SIZE, strlen(cmd)));
        }
        CommandFound = 1;
    }

    const char *urncp = strstr(cmd, "urn:");
    if (urncp && strstr(cmd, ":service:")) {
        Evt->ServiceType = std::string(urncp, std::min(LINE_SIZE, strlen(urncp)));
        CommandFound = 1;
    }
    if (urncp && strstr(const_cast<char*>(cmd), ":device:")) {
        Evt->DeviceType = std::string(urncp, std::min(LINE_SIZE, strlen(urncp)));
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
    if (strstr(cmd, "urn:")) {
        if (strstr(cmd, ":device:"))
            return SSDP_DEVICETYPE;
        if (strstr(cmd, ":service:"))
            return SSDP_SERVICE;
    }
    return SSDP_SERROR;
}

int ssdp_request_type(const char *cmd, SsdpEntity *Evt)
{
    /* clear event */
    *Evt = SsdpEntity();
    unique_service_name(cmd, Evt);
    if ((Evt->RequestType = ssdp_request_type1(cmd)) == SSDP_SERROR) {
        return -1;
    }
    return 0;
}


/*!
 * \brief Does some quick checking of an ssdp request msg.
 *
 * \return HTTPMETHOD_UNKNOWN if packet is invalid, else method.
 */
static http_method_t valid_ssdp_msg(SSDPPacketParser& parser, const NetIF::IPAddr& claddr)
{
    http_method_t method = HTTPMETHOD_UNKNOWN;
    if (!parser.isresponse) {
        if (!parser.method) {
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "NULL method in SSDP request????\n");
            return HTTPMETHOD_UNKNOWN;
        }
        method = httpmethod_str2enum(parser.method);
        if (method != HTTPMETHOD_NOTIFY && method != HTTPMETHOD_MSEARCH) {
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "Invalid method "
                       "in SSDP message: [%s] \n", parser.method);
            return HTTPMETHOD_UNKNOWN;
        }
        /* check PATH == "*" */
        if (!parser.url || strcmp(parser.url, "*")) {
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "Invalid URI "
                       "in SSDP message NOTIFY or M-SEARCH: [%s] \n",
                       (parser.url ? parser.url : "(null)"));
            return HTTPMETHOD_UNKNOWN;
        }
        /* Check HOST header. Not sure that we really do need this? Can't see what harm would come
         * from a bad HOST (unlike the web server ops where this could indicate mischief) */
        if (!parser.host) {
            UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                       "valid_ssdp_msg: no HOST header in request from %s\n",
                       claddr.straddr().c_str());
            return HTTPMETHOD_UNKNOWN;
        }
        // Check the dest to be either one of the well-known multicast addresses or one of our own
        // interfaces.
        if (!strcmp(parser.host, "239.255.255.250:1900") ||
            !strcasecmp(parser.host, "[FF02::C]:1900") ||
            !strcasecmp(parser.host, "[FF05::C]:1900")) {
            // Multicast request. Needs an MX
            if (method == HTTPMETHOD_MSEARCH && (!parser.mx || atoi(parser.mx) <= 0)) {
                UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                           "valid_ssdp_msg: HOST header indicates multicast but no MX set\n");
                return HTTPMETHOD_UNKNOWN;
            }
        } else {
            // Unicast request maybe
            struct hostport_type hostport;
            if (UPNP_E_INVALID_URL == parse_hostport(parser.host, &hostport, false)) {
                UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                           "valid_ssdp_msg: bad HOST header %s in request from %s\n",
                           parser.host, claddr.straddr().c_str());
                return HTTPMETHOD_UNKNOWN;
            }
            NetIF::IPAddr hostaddr(hostport.strhost);
            if (!hostaddr.ok()) {
                UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                           "valid_ssdp_msg: bad HOST header %s in request from %s\n",
                           parser.host, claddr.straddr().c_str());
                return HTTPMETHOD_UNKNOWN;
            }
            // IPV6: set the scope idx from the client sockaddr. Does nothing for IPV4
            hostaddr.setScopeIdx(claddr);
            NetIF::IPAddr notused;
            if (nullptr == NetIF::Interfaces::interfaceForAddress(hostaddr, g_netifs, notused)) {
                UpnpPrintf(UPNP_INFO, MSERV, __FILE__, __LINE__,
                           "valid_ssdp_msg: no interface for address in HOST header %s "
                           "in request from %s\n", parser.host, claddr.straddr().c_str());
                return HTTPMETHOD_UNKNOWN;
            }
        }
    } else {
        // We return HTTPMETHOD_MSEARCH + isresponse for response packets.
        // Analog to what the original code did.
        method = HTTPMETHOD_MSEARCH;
    }

    return method;
}

#define BUFSIZE   size_t(2500)
struct ssdp_thread_data {
    ssdp_thread_data() {
        m_packet = static_cast<char*>(malloc(BUFSIZE));
        if (nullptr == m_packet) {
            std::cerr << "Out of memory in readFromSSDPSocket\n";
            abort();
        }
    }
    ~ssdp_thread_data() {
        if (m_packet) {
            free(m_packet);
        }
    }
    ssdp_thread_data(const ssdp_thread_data&) = delete;
    ssdp_thread_data& operator=(const ssdp_thread_data&) = delete;
    static size_t size() {return BUFSIZE;}
    char *packet() { return m_packet; }
    // For transferring the data packet ownership to the parser.
    char *giveuppacket() {
        auto p = m_packet;
        m_packet = nullptr;
        return p;
    }
    struct sockaddr_storage dest_addr;
private:
    char *m_packet;
};

class SSDPEventHandlerJobWorker : public JobWorker {
public:
    explicit SSDPEventHandlerJobWorker(std::unique_ptr<ssdp_thread_data> data)
        : m_data(std::move(data)) {}
    void work() override;
    std::unique_ptr<ssdp_thread_data> m_data;
};

/* Thread routine to process one received SSDP message */
void SSDPEventHandlerJobWorker::work()
{
    NetIF::IPAddr claddr(reinterpret_cast<struct sockaddr *>(&m_data->dest_addr));
    // The parser takes ownership of the buffer
    SSDPPacketParser parser(m_data->giveuppacket());
    if (!parser.parse()) {
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,    "SSDP parser error\n");
        return;
    }
 
    http_method_t method = valid_ssdp_msg(parser, claddr);
    if (method == HTTPMETHOD_UNKNOWN) {
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,    "SSDP unknown method\n");
        return;
    }

    /* Dispatch message to device or ctrlpt */
    if (method == HTTPMETHOD_NOTIFY ||
        (parser.isresponse && method == HTTPMETHOD_MSEARCH)) {
#ifdef INCLUDE_CLIENT_APIS
        ssdp_handle_ctrlpt_msg(parser, &m_data->dest_addr, nullptr);
#endif /* INCLUDE_CLIENT_APIS */
    } else {
        ssdp_handle_device_request(parser, &m_data->dest_addr);
    }
}

void readFromSSDPSocket(SOCKET socket)
{
    auto data = std::make_unique<ssdp_thread_data>();
    auto sap = reinterpret_cast<struct sockaddr *>(&data->dest_addr);
    socklen_t socklen = sizeof(data->dest_addr);
    ssize_t cnt = recvfrom(socket, data->packet(), data->size() - 1, 0, sap, &socklen);
    if (cnt > 0) {
        data->packet()[cnt] = '\0';
        NetIF::IPAddr nipa(sap);
        UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__,
                   "\nSSDP message from host %s --------------------\n"
                   "%s\n"
                   "End of received data -----------------------------\n",
                   nipa.straddr().c_str(), data->packet());
        /* add thread pool job to handle request */
        auto worker = std::make_unique<SSDPEventHandlerJobWorker>(std::move(data));
        gRecvThreadPool.addJob(std::move(worker));
    }
}

static int create_ssdp_sock_v4(SOCKET *ssdpSock)
{
    int onOff;
    struct sockaddr_storage ss = {};
    auto ssdpAddr4 = reinterpret_cast<struct sockaddr_in *>(&ss);
    int ret = UPNP_E_SOCKET_ERROR;
    std::string errorcause;

    *ssdpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (*ssdpSock == INVALID_SOCKET) {
        errorcause = "socket()";
        ret = UPNP_E_OUTOF_SOCKET;
        goto error_handler;
    }

    onOff = 1;
    ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<char *>(&onOff), sizeof(onOff));
    if (ret == -1) {
        errorcause = "setsockopt() SO_REUSEADDR";
        goto error_handler;
    }

#if (defined(BSD) && !defined(__GNU__)) || defined(__OSX__) || defined(__APPLE__)
    onOff = 1;
    ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<char *>(&onOff), sizeof(onOff));
    if (ret == -1) {
        errorcause = "setsockopt() SO_REUSEPORT";
        goto error_handler;
    }
#endif /* BSD, __OSX__, __APPLE__ */

    ssdpAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
    ssdpAddr4->sin_addr.s_addr = htonl(INADDR_ANY);
    ssdpAddr4->sin_port = htons(SSDP_PORT);
    ret = bind(*ssdpSock, reinterpret_cast<struct sockaddr *>(ssdpAddr4), sizeof(*ssdpAddr4));
    if (ret == -1) {
        errorcause = "bind(INADDR_ANY)";
        ret = UPNP_E_SOCKET_BIND;
        goto error_handler;
    }

    for (const auto& netif : g_netifs) {
        auto ipaddr = netif.firstipv4addr();
        if (!ipaddr)
            continue;
        struct ip_mreq ssdpMcastAddr = {};
        if (inet_pton(AF_INET, ipaddr->straddr().c_str(), &(ssdpMcastAddr.imr_interface)) != 1) {
            errorcause = "inet_pton() error";
            goto error_handler;
        }
        if (inet_pton(AF_INET, SSDP_IP, &(ssdpMcastAddr.imr_multiaddr)) != 1) {
            errorcause = "inet_pton() error for multicast address";
            goto error_handler;
        }
        ret = setsockopt(*ssdpSock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         reinterpret_cast<char *>(&ssdpMcastAddr), sizeof(struct ip_mreq));
        if (ret == -1) {
            errorcause = "setsockopt() IP_ADD_MEMBERSHIP";
            goto error_handler;
        }
    }

    return UPNP_E_SUCCESS;

error_handler:
    char errorBuffer[ERROR_BUFFER_LEN];
    posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
    UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
               "%s: %s\n", errorcause.c_str(), errorBuffer);
    if (*ssdpSock != INVALID_SOCKET) {
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
static int create_ssdp_sock_reqv4(SOCKET *ssdpReqSock, int port)
{
    char ttl = 2;
    int ret = UPNP_E_SOCKET_ERROR;

    *ssdpReqSock = INVALID_SOCKET;

    std::string sadrv4 = apiFirstIPV4Str();
    if (sadrv4.empty()) {
        UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__, "create_ssdp_sock_reqv4: no IPV4??\n");
        return ret;
    }

    uint32_t hostaddrv4;
    std::string errorcause;

    if (inet_pton(AF_INET, sadrv4.c_str(), &hostaddrv4) != 1) {
        errorcause = "inet_pton() error";
        ret = UPNP_E_INVALID_PARAM;
        goto error_handler;
    }

    *ssdpReqSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (*ssdpReqSock == INVALID_SOCKET) {
        errorcause = "socket()";
        ret = UPNP_E_OUTOF_SOCKET;
        goto error_handler;
    }
    if (setsockopt(*ssdpReqSock, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<char *>(&hostaddrv4), sizeof(hostaddrv4)) < 0) {
        errorcause = "setsockopt(IP_MULTICAST_IF)";
        goto error_handler;
    }
    if (setsockopt(*ssdpReqSock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        errorcause = "setsockopt(IP_MULTICAST_TTL)";
        goto error_handler;
    }

    sock_make_no_blocking(*ssdpReqSock);

    if (port > 0)
    {
        struct sockaddr_storage ss = {};
        auto ssdpAddr4 = reinterpret_cast<struct sockaddr_in *>(&ss);

        ssdpAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
        ssdpAddr4->sin_addr.s_addr = htonl(INADDR_ANY);
        ssdpAddr4->sin_port = htons(port);
        ret = bind(*ssdpReqSock, reinterpret_cast<struct sockaddr *>(ssdpAddr4), sizeof(*ssdpAddr4));

        if (ret == -1) {
            errorcause = "bind(INADDR_ANY)";
            ret = UPNP_E_SOCKET_BIND;
            goto error_handler;
        }
    }

    return UPNP_E_SUCCESS;

error_handler:
    char errorBuffer[ERROR_BUFFER_LEN];
    posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
    UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__, "%s: %s\n", errorcause.c_str(), errorBuffer);
    if (*ssdpReqSock != INVALID_SOCKET) {
        UpnpCloseSocket(*ssdpReqSock);
    }
    return ret;
}
#endif /* INCLUDE_CLIENT_APIS */

#ifdef UPNP_ENABLE_IPV6

static int create_ssdp_sock_v6(bool isulagua, SOCKET *ssdpSock)
{
    int onOff;
    int ret = UPNP_E_SOCKET_ERROR;
    std::string errorcause;

    *ssdpSock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (*ssdpSock == INVALID_SOCKET) {
        errorcause = "socket()";
        ret = UPNP_E_OUTOF_SOCKET;
        goto error_handler;
    }

    onOff = 1;
    ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<char *>(&onOff), sizeof(onOff));
    if (ret == -1) {
        errorcause = "setsockopt() SO_REUSEADDR";
        goto error_handler;
    }

#if (defined(BSD) && !defined(__GNU__)) || defined(__OSX__) || defined(__APPLE__)
    onOff = 1;
    ret = setsockopt(*ssdpSock, SOL_SOCKET, SO_REUSEPORT,
                     reinterpret_cast<char *>(&onOff), sizeof(onOff));
    if (ret == -1) {
        errorcause = "setsockopt() SO_REUSEPORT";
        goto error_handler;
    }
#endif /* BSD, __OSX__, __APPLE__ */

    onOff = 1;
    ret = setsockopt(*ssdpSock, IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<char *>(&onOff), sizeof(onOff));
    if (ret == -1) {
        errorcause = "setsockopt() IPV6_V6ONLY";
        goto error_handler;
    }

    {
        struct sockaddr_storage ss;
        auto ssdpAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&ss);
        ss = {};
        ssdpAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
        ssdpAddr6->sin6_addr = in6addr_any;
        ssdpAddr6->sin6_scope_id = 0;
        ssdpAddr6->sin6_port = htons(SSDP_PORT);
        ret = bind(*ssdpSock, reinterpret_cast<struct sockaddr *>(ssdpAddr6), sizeof(*ssdpAddr6));
        if (ret == -1) {
            errorcause = "bind()";
            goto error_handler;
        }
        struct ipv6_mreq ssdpMcastAddr = {};
        NetIF::IPAddr ipa(isulagua? SSDP_IPV6_SITELOCAL : SSDP_IPV6_LINKLOCAL);
        struct sockaddr_in6 sa6;
        ipa.copyToAddr(reinterpret_cast<struct sockaddr*>(&sa6));
        memcpy(&ssdpMcastAddr.ipv6mr_multiaddr, &sa6.sin6_addr,
                sizeof(ssdpMcastAddr.ipv6mr_multiaddr));
        ret = setsockopt(*ssdpSock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                         reinterpret_cast<char *>(&ssdpMcastAddr), sizeof(ssdpMcastAddr));
        if (ret == -1) {
            errorcause = "setsockopt() IPV6_JOIN_GROUP";
            goto error_handler;
        }
    }

    return UPNP_E_SUCCESS;

error_handler:
    char errorBuffer[ERROR_BUFFER_LEN];
    posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
    UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__, "%s: %s\n", errorcause.c_str(), errorBuffer);
    if (*ssdpSock != INVALID_SOCKET) {
        UpnpCloseSocket(*ssdpSock);
    }
    return ret;
}


#ifdef INCLUDE_CLIENT_APIS
/* Create the SSDP IPv6 socket to be used by the control point. */
static int create_ssdp_sock_reqv6(SOCKET *ssdpReqSock, int port)
{
#ifdef _WIN32
    DWORD hops = 1;
#else
    int hops = 1;
#endif
    int index = apiFirstIPV6Index();
    std::string errorcause;
    int ret = UPNP_E_SOCKET_ERROR;

    *ssdpReqSock = INVALID_SOCKET;

    if ((*ssdpReqSock = socket(AF_INET6, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        errorcause = "socket()";
        ret = UPNP_E_OUTOF_SOCKET;
        goto error_handler;
    }
    if (setsockopt(*ssdpReqSock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                   reinterpret_cast<char *>(&index), sizeof(index)) < 0) {
        errorcause = "setsockopt(IPV6_MULTICAST_IF)";
        goto error_handler;
    }

    if (setsockopt(*ssdpReqSock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                   reinterpret_cast<char *>(&hops), sizeof(hops)) < 0) {
        errorcause = "setsockopt(IPV6_MULTICAST_HOPS)";
        goto error_handler;
    }

    sock_make_no_blocking(*ssdpReqSock);

    if (port > 0)
    {
        int onOff = 1;

        // Set IPV6 socket to only bind to IPV6 (Linux Dual Stack)
        ret = setsockopt(*ssdpReqSock, IPPROTO_IPV6, IPV6_V6ONLY,
                reinterpret_cast<char *>(&onOff), sizeof(onOff));

        if (ret == -1) {
            errorcause = "setsockopt() IPV6_V6ONLY";
            goto error_handler;
        }

        struct sockaddr_storage ss = {};
        auto ssdpAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&ss);

        ssdpAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
        ssdpAddr6->sin6_addr = in6addr_any;
        ssdpAddr6->sin6_scope_id = 0;
        ssdpAddr6->sin6_port = htons(port);
        ret = bind(*ssdpReqSock, reinterpret_cast<struct sockaddr *>(ssdpAddr6), sizeof(*ssdpAddr6));

        if (ret == -1) {
            errorcause = "bind(IN6ADDR_ANY)";
            ret = UPNP_E_SOCKET_BIND;
            goto error_handler;
        }
    }

    return UPNP_E_SUCCESS;

error_handler:
    char errorBuffer[ERROR_BUFFER_LEN];
    posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
    UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__, "%s: %s\n", errorcause.c_str(), errorBuffer);
    if (*ssdpReqSock != INVALID_SOCKET) {
        UpnpCloseSocket(*ssdpReqSock);
    }
    return ret;
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

int get_ssdp_sockets(MiniServerSockArray *out, int port)
{
    int retVal = UPNP_E_SOCKET_ERROR;
    bool hasIPV4 = !apiFirstIPV4Str().empty();

#ifdef INCLUDE_CLIENT_APIS
#ifdef UPNP_ENABLE_IPV6
    bool hasIPV6 = !apiFirstIPV6Str().empty();
    if (using_ipv6()) {
        /* Create the IPv6 socket for SSDP REQUESTS */
        if (hasIPV6) {
            if ((retVal = create_ssdp_sock_reqv6(&out->ssdpReqSock6, port)) != UPNP_E_SUCCESS) {
                goto out;
            }
            /* For use by ssdp control point. */
            gSsdpReqSocket6 = out->ssdpReqSock6;
        }
    }
#endif /* UPNP_ENABLE_IPV6 */
    /* Create the IPv4 socket for SSDP REQUESTS */
    if (hasIPV4) {
        if ((retVal = create_ssdp_sock_reqv4(&out->ssdpReqSock4, port)) != UPNP_E_SUCCESS) {
            goto out;
        }
        /* For use by ssdp control point. */
        gSsdpReqSocket4 = out->ssdpReqSock4;
    }
#endif /* INCLUDE_CLIENT_APIS */

    /* Create the IPv4 socket for SSDP */
    if (hasIPV4) {
        if ((retVal = create_ssdp_sock_v4(&out->ssdpSock4)) != UPNP_E_SUCCESS) {
            goto out;
        }
    }

#ifdef UPNP_ENABLE_IPV6
    if (using_ipv6()) {
        /* Create the IPv6 socket for SSDP */
        if (hasIPV6) {
            if ((retVal = create_ssdp_sock_v6(false, &out->ssdpSock6)) != UPNP_E_SUCCESS) {
                goto out;
            }
        }
        if (strlen(""/*gIF_IPV6_ULA_GUA*/) > static_cast<size_t>(0)) {
            if ((retVal = create_ssdp_sock_v6(true, &out->ssdpSock6UlaGua)) != UPNP_E_SUCCESS) {
                goto out;
            }
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
