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

#ifdef INCLUDE_DEVICE_APIS
#if EXCLUDE_SSDP == 0

#include "ThreadPool.h"
#include "genut.h"
#include "httputils.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "upnpapi.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

struct SsdpSearchReply {
    SsdpSearchReply(int a, UpnpDevice_Handle h, const sockaddr_storage* da, SsdpEntity e)
        : MaxAge(a), handle(h), event(std::move(e))
    {
        std::memcpy(&dest_addr, da, sizeof(dest_addr));
    }
    int MaxAge;
    UpnpDevice_Handle handle;
    struct sockaddr_storage dest_addr;
    SsdpEntity event;
};

struct SSDPPwrState {
    int PowerState;
    int SleepPeriod;
    int RegistrationState;
};

// A bundle to simplify arg lists
struct SSDPCommonData {
    SOCKET sock;
    struct sockaddr_storage *DestAddr;
    SSDPPwrState pwr;
    std::string prodvers;
};

class SSDPSearchJobWorker : public JobWorker {
public:
    explicit SSDPSearchJobWorker(std::unique_ptr<SsdpSearchReply> reply)
        : m_reply(std::move(reply)) {}
    void work() override {
        AdvertiseAndReply(m_reply->handle, MSGTYPE_REPLY,
                          m_reply->MaxAge,
                          &m_reply->dest_addr,
                          m_reply->event);
    }
    std::unique_ptr<SsdpSearchReply> m_reply;
};

void ssdp_handle_device_request(const SSDPPacketParser& parser, struct sockaddr_storage *dest_addr)
{
    int handle, start;
    struct Handle_Info *dev_info = nullptr;
    SsdpEntity event;
    
    /* check man hdr. */
    if (!parser.man || strcmp(parser.man, R"("ssdp:discover")") != 0) {
        /* bad or missing hdr. */
        UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "ssdp_handle_device_req: no/bad MAN header\n");
        return;
    }
    /* MX header. May be absent for a unicast request. Consistency has been checked in ssdp_server */
    int mx = 0;
    if (parser.mx) {
        mx = atoi(parser.mx);
    }
    /* ST header. */
    if (!parser.st || ssdp_request_type(parser.st, &event) == -1) {
        UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "ssdp_handle_device_req: no/bad ST header\n");
        return;
    }

    // Loop to dispatch the packet to each of our configured devices by starting the handle search
    // at the last processed position. each device response is scheduled by the ThreadPool with a
    // random delay based on the MX header of the search packet.
    start = 0;
    for (;;) {
        int maxAge;
        {
            HANDLELOCK();
            /* device info. */
            switch (GetDeviceHandleInfo(start, &handle, &dev_info)) {
            case HND_DEVICE:
                break;
            default:
                /* no info found. */
                return;
            }
            maxAge = dev_info->MaxAge;
        }

        UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__, "MAX-AGE        =  %d\n", maxAge);
        UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__, "MX       =  %d\n", mx);
        UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__,
                   "DeviceType     =    %s\n", event.DeviceType.c_str());
        UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__,
                   "DeviceUuid     =    %s\n", event.UDN.c_str());
        UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__,
                   "ServiceType =  %s\n", event.ServiceType.c_str());

        auto threadArg = std::make_unique<SsdpSearchReply>(maxAge, handle, dest_addr, event);
        auto worker = std::make_unique<SSDPSearchJobWorker>(std::move(threadArg));
        if (mx) {
            mx = std::max(1, mx);
            /* Subtract a bit from the mx to allow for network/processing delays */
            int delayms = rand() % (mx * 1000 - 100);
            UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
                       "ssdp_handle_device_req: scheduling resp in %d ms\n", delayms);
            gTimerThread->schedule(TimerThread::SHORT_TERM, std::chrono::milliseconds(delayms),
                                   nullptr, std::move(worker));
        } else {
            gSendThreadPool.addJob(std::move(worker));
        }
        start = handle;
    }
}

// Create the reply socket and determine the appropriate host address
// for setting the LOCATION header
static SOCKET createMulticastSocket4(
    const struct sockaddr_in *srcaddr, std::string& lochost)
{
    char ttl = 2;
#ifdef _WIN32
    BOOL bcast = TRUE;
#else
    int bcast = 1;
#endif
    const NetIF::IPAddr
        ipaddr(reinterpret_cast<const struct sockaddr *>(srcaddr));
    lochost = ipaddr.straddr();
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    uint32_t srcAddr = srcaddr->sin_addr.s_addr;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<char *>(&srcAddr), sizeof(srcAddr)) < 0) {
        goto error;
    }
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        goto error;
    }
    if(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char *>(&bcast), sizeof(bcast)) < 0) {
        goto error;
    }
    if (bind(sock, reinterpret_cast<const struct sockaddr *>(srcaddr),
             sizeof(struct sockaddr_in)) < 0) {
        goto error;
    }
    return sock;
error:
    UpnpCloseSocket(sock);
    return INVALID_SOCKET;
}

static SOCKET createReplySocket4(
    struct sockaddr_in *destaddr, std::string& lochost)
{
    SOCKET sock = socket(static_cast<int>(destaddr->sin_family), SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    // Determine the proper interface and compute the location string
    NetIF::IPAddr dipaddr(reinterpret_cast<struct sockaddr*>(destaddr));
    NetIF::IPAddr hostaddr;
    const auto netif =
        NetIF::Interfaces::interfaceForAddress(dipaddr, g_netifs, hostaddr);
    if (netif && hostaddr.ok()) {
        lochost = hostaddr.straddr();
    } else {
        lochost = apiFirstIPV4Str();
    }
    return sock;
}


#ifdef UPNP_ENABLE_IPV6
static std::string strInBrackets(const std::string& straddr)
{
    return std::string("[") + straddr + "]";
}
static SOCKET createMulticastSocket6(int index, std::string& lochost)
{
    int hops = 1;
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                   reinterpret_cast<char *>(&index), sizeof(index)) < 0) {
        goto error;
    }
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                   reinterpret_cast<char *>(&hops), sizeof(hops)) < 0) {
        goto error;
    }
    lochost.clear();
    for (const auto& netif : g_netifs) {
        if (netif.getindex() == index) {
            const auto ipaddr =
                netif.firstipv6addr(NetIF::IPAddr::Scope::LINK);
            if (ipaddr) {
                lochost = strInBrackets(ipaddr->straddr());
                break;
            }
        }
    }
    if (lochost.empty()) {
        lochost = strInBrackets(apiFirstIPV6Str());
    }
    return sock;
error:
    UpnpCloseSocket(sock);
    return INVALID_SOCKET;
}

static SOCKET createReplySocket6(
    const struct sockaddr_in6 *destaddr, std::string& lochost)
{
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    int index = destaddr->sin6_scope_id;
    lochost.clear();
    for (const auto& netif : g_netifs) {
        if (netif.getindex() == index) {
            const auto ipaddr =
                netif.firstipv6addr(NetIF::IPAddr::Scope::LINK);
            if (ipaddr) {
                lochost = strInBrackets(ipaddr->straddr());
                break;
            }
        }
    }
    if (lochost.empty()) {
        lochost = strInBrackets(apiFirstIPV6Str());
    }
    return sock;
}
#else // ! ENABLE_IPV6 ->
static SOCKET createReplySocket6(
    struct sockaddr_in6 *destaddr, std::string& lochost)
{
    return INVALID_SOCKET;
}
#endif // UPNP_ENABLE_IPV6

// Set the UPnP predefined multicast destination addresses
static bool ssdpMcastAddr(struct sockaddr_storage& ss, int AddressFamily)
{
    ss = {};
    switch (AddressFamily) {
    case AF_INET:
    {
        auto DestAddr4 = reinterpret_cast<struct sockaddr_in *>(&ss);
        DestAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
        inet_pton(AF_INET, SSDP_IP, &DestAddr4->sin_addr);
        DestAddr4->sin_port = htons(SSDP_PORT);
    }
    break;
    case AF_INET6:
    {
        auto DestAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&ss);
        DestAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
        inet_pton(AF_INET6, SSDP_IPV6_LINKLOCAL, &DestAddr6->sin6_addr);
        DestAddr6->sin6_port = htons(SSDP_PORT);
    }
    break;
    default:
        return false;
    }
    return true;
}

static int sendPackets(SOCKET sock, struct sockaddr_storage *daddr, int cnt, std::string *pckts)
{
    NetIF::IPAddr destip(reinterpret_cast<struct sockaddr*>(daddr));
    int socklen = daddr->ss_family == AF_INET ? sizeof(struct sockaddr_in) :
        sizeof(struct sockaddr_in6);

    for (int i = 0; i < cnt; i++) {
        ssize_t rc;
        UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__, ">>> SSDP SEND to %s >>>\n%s\n",
                   destip.straddr().c_str(), pckts[i].c_str());
        rc = sendto(sock, pckts[i].c_str(), pckts[i].size(), 0,
                    reinterpret_cast<struct sockaddr*>(daddr), socklen);

        if (rc == -1) {
            std::string errorDesc;
            NetIF::getLastError(errorDesc);
            UpnpPrintf(UPNP_INFO,SSDP,__FILE__,__LINE__,
                       "sendPackets: sendto: %s\n", errorDesc.c_str());
            return UPNP_E_SOCKET_WRITE;
        }
    }
    return UPNP_E_SUCCESS;
}


/* Creates a device notify or search reply packet. */
static void CreateServicePacket(
    SSDPDevMessageType msg_type, const char *nt, const char *usn,
    const std::string& location, int duration, std::string &packet,
    int AddressFamily, const SSDPPwrState& pwr, const std::string& prodvers)
{
    std::ostringstream str;
    switch (msg_type) {
    case MSGTYPE_REPLY:
        str <<
            "HTTP/1.1 " << HTTP_OK << " OK\r\n" <<
            "CACHE-CONTROL: max-age=" << duration << "\r\n" <<
            "DATE: " << make_date_string(0) << "\r\n" <<
            "EXT:\r\n" <<
            "LOCATION: " << location << "\r\n" <<
            "SERVER: " << get_sdk_device_info(prodvers) << "\r\n" <<
#ifdef UPNP_HAVE_OPTSSDP
            "OPT: " << R"("http://schemas.upnp.org/upnp/1/0/"; ns=01)" << "\r\n" <<
            "01-NLS: " << gUpnpSdkNLSuuid << "\r\n" <<
            "X-User-Agent: " << X_USER_AGENT << "\r\n" <<
#endif
            "ST: " << nt << "\r\n" <<
            "USN: " << usn << "\r\n";
        break;
    case MSGTYPE_ADVERTISEMENT:
    case MSGTYPE_SHUTDOWN:
    {
        const char *nts = (msg_type == MSGTYPE_ADVERTISEMENT) ?
            "ssdp:alive" : "ssdp:byebye";

        /* NOTE: The CACHE-CONTROL and LOCATION headers are not present in
         * a shutdown msg, but are present here for MS WinMe interop. */
        const char *host;
        switch (AddressFamily) {
        case AF_INET: host = SSDP_IP; break;
        default: host = "[" SSDP_IPV6_LINKLOCAL "]";
        }

        str <<
            "NOTIFY * HTTP/1.1\r\n" <<
            "HOST: " << host << ":" << SSDP_PORT << "\r\n" <<
            "CACHE-CONTROL: max-age=" << duration << "\r\n" <<
            "LOCATION: " << location << "\r\n" <<
            "SERVER: " << get_sdk_device_info(prodvers) << "\r\n" <<
#ifdef UPNP_HAVE_OPTSSDP
            "OPT: " << R"("http://schemas.upnp.org/upnp/1/0/"; ns=01)" << "\r\n" <<
            "01-NLS: " << gUpnpSdkNLSuuid << "\r\n" <<
            "X-User-Agent: " << X_USER_AGENT << "\r\n" <<
#endif
            "NT: " << nt << "\r\n" <<
            "NTS: " << nts << "\r\n" <<
            "USN: " << usn << "\r\n";
    }
    break;
    default:
        std::cerr << "Unknown message type in CreateServicePacket\n";
        abort();
    }

    if (pwr.PowerState > 0) {
        str <<
            "Powerstate: " << pwr.PowerState << "\r\n" <<
            "SleepPeriod: " << pwr.SleepPeriod << "\r\n" <<
            "RegistrationState: " << pwr.RegistrationState << "\r\n";
    }
    str << "BOOTID.UPNP.ORG: " << g_bootidUpnpOrg << "\r\n" <<
        "CONFIGID.UPNP.ORG: " << g_configidUpnpOrg << "\r\n";

    str << "\r\n";
    packet = str.str();
}

static int DeviceAdvertisementOrShutdown(
    const SSDPCommonData& sscd, SSDPDevMessageType msgtype, const char *DevType,
    int RootDev, const char *Udn, const std::string& Location, int Duration)
{
    char Mil_Usn[LINE_SIZE];
    std::string msgs[3];
    int ret_code = UPNP_E_OUTOF_MEMORY;
    int rc = 0;

    /* If device is a root device, we need to send 3 messages */
    if (RootDev) {
        rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
        if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(msgtype, "upnp:rootdevice",
                            Mil_Usn, Location, Duration, msgs[0],
                            sscd.DestAddr->ss_family, sscd.pwr, sscd.prodvers);
    }
    /* both root and sub-devices need to send these two messages */
    CreateServicePacket(msgtype, Udn, Udn,
                        Location, Duration, msgs[1], sscd.DestAddr->ss_family,
                        sscd.pwr, sscd.prodvers);
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(msgtype, DevType, Mil_Usn, Location, Duration, msgs[2],
                        sscd.DestAddr->ss_family, sscd.pwr, sscd.prodvers);
    /* check error */
    if ((RootDev && msgs[0].empty()) || msgs[1].empty() || msgs[2].empty()) {
        goto error_handler;
    }
    /* send packets */
    if (RootDev) {
        /* send 3 msg types */
        ret_code = sendPackets(sscd.sock, sscd.DestAddr, 3, &msgs[0]);
    } else {        /* sub-device */
        /* send 2 msg types */
        ret_code = sendPackets(sscd.sock, sscd.DestAddr, 2, &msgs[1]);
    }

error_handler:
    return ret_code;
}

static int SendReply(
    const SSDPCommonData& sscd, const char *DevType, int RootDev,
    const char *Udn, const std::string& Location, int Duration, int ByType)
{
    int ret_code = UPNP_E_OUTOF_MEMORY;
    std::string msgs[2];
    int num_msgs;
    char Mil_Usn[LINE_SIZE];
    int i;
    int rc = 0;
    auto family = static_cast<int>(sscd.DestAddr->ss_family);

    if (RootDev) {
        /* one msg for root device */
        num_msgs = 1;

        rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
        if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(MSGTYPE_REPLY, "upnp:rootdevice", Mil_Usn, Location,
                            Duration, msgs[0], family, sscd.pwr, sscd.prodvers);
    } else {
        /* two msgs for embedded devices */
        num_msgs = 1;

        /*NK: FIX for extra response when someone searches by udn */
        if (!ByType) {
            CreateServicePacket(MSGTYPE_REPLY, Udn, Udn, Location, Duration,
                                msgs[0], family, sscd.pwr, sscd.prodvers);
        } else {
            rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
            if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
                goto error_handler;
            CreateServicePacket(MSGTYPE_REPLY, DevType, Mil_Usn, Location,
                                Duration, msgs[0],family,sscd.pwr,sscd.prodvers);
        }
    }
    /* check error */
    for (i = 0; i < num_msgs; i++) {
        if (msgs[i].empty()) {
            goto error_handler;
        }
    }

    ret_code = sendPackets(sscd.sock, sscd.DestAddr, num_msgs, msgs);

error_handler:
    return ret_code;
}

static int DeviceReply(
    const SSDPCommonData& sscd, const char *DevType, int RootDev,
    const char *Udn, const std::string& Location, int Duration)
{
    std::string szReq[3];
    char Mil_Nt[LINE_SIZE], Mil_Usn[LINE_SIZE];
    int rc = 0;
    auto family = static_cast<int>(sscd.DestAddr->ss_family);

    /* create 2 or 3 msgs */
    if (RootDev) {
        /* 3 replies for root device */
        upnp_strlcpy(Mil_Nt, "upnp:rootdevice", sizeof(Mil_Nt));
        rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
        if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
            return UPNP_E_OUTOF_MEMORY;
        CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn, Location,
                            Duration, szReq[0], family, sscd.pwr, sscd.prodvers);
    }
    rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", Udn);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Nt))
        return UPNP_E_OUTOF_MEMORY;
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s", Udn);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        return UPNP_E_OUTOF_MEMORY;
    CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn, Location, Duration,
                        szReq[1], family, sscd.pwr, sscd.prodvers);
    rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", DevType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Nt))
        return UPNP_E_OUTOF_MEMORY;
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        return UPNP_E_OUTOF_MEMORY;
    CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn, Location, Duration,
                        szReq[2], family, sscd.pwr, sscd.prodvers);
    /* check error */
    if ((RootDev && szReq[0].empty()) || szReq[1].empty() || szReq[2].empty()) {
        return UPNP_E_OUTOF_MEMORY;
    }
    /* send replies */
    if (RootDev) {
        return sendPackets(sscd.sock, sscd.DestAddr, 3, szReq);
    }
    return sendPackets(sscd.sock, sscd.DestAddr, 2, &szReq[1]);
}

static int ServiceSend(
    const SSDPCommonData& sscd, SSDPDevMessageType tp, const char *ServType,
    const char *Udn, const std::string& Location, int Duration)
{
    char Mil_Usn[LINE_SIZE];
    std::string szReq[1];
    int rc = 0;

    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, ServType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        return UPNP_E_OUTOF_MEMORY;
    CreateServicePacket(tp, ServType, Mil_Usn, Location, Duration, szReq[0],
                        sscd.DestAddr->ss_family, sscd.pwr, sscd.prodvers);
    if (szReq[0].empty()) {
        return UPNP_E_OUTOF_MEMORY;

    }

    return sendPackets(sscd.sock, sscd.DestAddr, 1, szReq);
}

static void replaceLochost(std::string& location, const std::string& lochost)
{
    std::string::size_type pos = location.find(g_HostForTemplate);
    if (pos != std::string::npos) {
        location.replace(pos, g_HostForTemplate.size(), lochost);
    }
}

static int servOrDevVers(const char *in)
{
    const char *cp = std::strrchr(in, ':');
    if (nullptr == cp)
        return 0;
    cp++;
    if (*cp != 0) {
        return std::atoi(cp);
    }
    return 0;
}

static bool sameServOrDevNoVers(const char *his, const char *mine)
{
    const char *cp = std::strrchr(mine, ':');
    if (nullptr == cp) {
        // ??
        return !strcasecmp(his, mine);
    }
    return !strncasecmp(his, mine, cp - mine);
}

// Send SSDP messages for one root device, one destination address,
// which is the reply host or one of our source addresses. There may
// be subdevices
static int AdvertiseAndReplyOneDest(
    UpnpDevice_Handle Hnd, SSDPDevMessageType tp, int Exp,
    struct sockaddr_storage *DestAddr, const SsdpEntity& sdata, SOCKET sock,
    const std::string& lochost)
{
    int retVal = UPNP_E_SUCCESS;
    int NumCopy = 0;
    std::vector<UPnPDeviceDesc> alldevices;
    bool isNotify = (tp == MSGTYPE_ADVERTISEMENT || tp == MSGTYPE_SHUTDOWN);
    struct Handle_Info *SInfoPtr;
    struct Handle_Info SInfo;
    std::string location, lowerloc;
    
    {
        // We make copies of everything with the handle table lock held, so as not to hold it while
        // sending the advertisements. Not sure that this is actually useful.
        HANDLELOCK();
        if (GetHandleInfo(Hnd, &SInfoPtr) != HND_DEVICE) {
            return UPNP_E_INVALID_HANDLE;
        }
        SInfo.MaxAge = SInfoPtr->MaxAge;
        SInfo.PowerState = SInfoPtr->PowerState;
        SInfo.SleepPeriod = SInfoPtr->SleepPeriod;
        SInfo.RegistrationState = SInfoPtr->RegistrationState;
        SInfo.productversion = SInfoPtr->productversion;
        memcpy(SInfo.DescURL, SInfoPtr->DescURL, LINE_SIZE);
        memcpy(SInfo.LowerDescURL, SInfoPtr->LowerDescURL, LINE_SIZE);
        // Store pointers to the root and embedded devices in a single vector
        // for later convenience of mostly identical processing.
        alldevices.push_back(SInfoPtr->devdesc);
        for (const auto& dev : SInfoPtr->devdesc.embedded) {
            alldevices.push_back(dev);
        }
        location = SInfoPtr->DescURL;
        replaceLochost(location, lochost);
        lowerloc = SInfoPtr->LowerDescURL;
        replaceLochost(lowerloc, lochost);
    }

    int defaultExp = SInfo.MaxAge;
    SSDPCommonData sscd{sock, DestAddr,
                        SSDPPwrState{SInfo.PowerState, SInfo.SleepPeriod,
                                     SInfo.RegistrationState}, SInfo.productversion};


    /* send advertisements/replies */
    while (NumCopy == 0 || (isNotify && NumCopy < NUM_SSDP_COPY)) {
        if (NumCopy != 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(SSDP_PAUSE));
        NumCopy++;

        for (auto& devp : alldevices) {
            bool isroot = &devp == &(*alldevices.begin());
            const char *devType = devp.deviceType.c_str();
            const char *UDNstr = devp.UDN.c_str();
            if (isNotify) {
                DeviceAdvertisementOrShutdown(sscd, tp, devType,  isroot, UDNstr, location, Exp);
            } else {
                switch (sdata.RequestType) {
                case SSDP_ALL:
                    DeviceReply(sscd, devType, isroot, UDNstr, location, defaultExp);
                    break;

                case SSDP_ROOTDEVICE:
                    if (isroot) {
                        SendReply(sscd, devType, 1, UDNstr, location, defaultExp, 0);
                    }
                    break;

                case SSDP_DEVICEUDN:
                    if (!strcasecmp(sdata.UDN.c_str(), UDNstr)) {
                        UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                   "DeviceUDN=%s/search UDN=%s MATCH\n", UDNstr, sdata.UDN.c_str());
                        SendReply(sscd, devType, 0, UDNstr, location, defaultExp, 0);
                    } else {
                        UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                   "DeviceUDN=%s/search UDN=%s NOMATCH\n",
                                   UDNstr, sdata.UDN.c_str());
                    }
                    break;

                case SSDP_DEVICETYPE: {
                    const char *dt = sdata.DeviceType.c_str();
                    if (sameServOrDevNoVers(dt, devType)) {
                        int myvers = servOrDevVers(devType);
                        int hisvers = servOrDevVers(dt);
                        if (hisvers < myvers) {
                            /* the requested version is lower than the
                               device version must reply with the
                               lower version number and the lower
                               description URL */
                            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                       "DeviceType=%s/srchdevType=%s MATCH\n", devType, dt);
                            SendReply(sscd, dt, 0, UDNstr, lowerloc, defaultExp, 1);
                        } else if (hisvers == myvers) {
                            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                       "DeviceType=%s/srchDevType=%s MATCH\n", devType, dt);
                            SendReply(sscd, dt, 0, UDNstr, location, defaultExp, 1);
                        } else {
                            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                       "DeviceType=%s/srchDevType=%s NOMATCH\n", devType, dt);
                        }
                    } else {
                        UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                   "DeviceType=%s /srchdevType=%s NOMATCH\n", devType, dt);
                    }
                }
                    break;

                default:
                    break;
                }
            }

            /* send service advertisements for services corresponding
             * to the same device */
            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__, "Sending service advertisements\n");
            /* Correct service traversal such that each device's serviceList
             * is directly traversed as a child of its parent device. This
             * ensures that the service's alive message uses the UDN of
             * the parent device. */
            for (const auto& service : devp.services) {
                const char *servType = service.serviceType.c_str();
                UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__, "ServiceType = %s\n", servType);
                if (isNotify) {
                    ServiceSend(sscd, tp, servType, UDNstr, location, Exp);
                } else {
                    switch (sdata.RequestType) {
                    case SSDP_ALL:
                        ServiceSend(sscd, MSGTYPE_REPLY, servType, UDNstr, location, defaultExp);
                        break;

                    case SSDP_SERVICE: {
                        const char *sst = sdata.ServiceType.c_str();
                        if (sameServOrDevNoVers(sst, servType)) {
                            int myvers = servOrDevVers(servType);
                            int hisvers = servOrDevVers(sst);
                            if (hisvers < myvers) {
                                /* the requested version is lower
                                   than the service version must
                                   reply with the lower version
                                   number and the lower
                                   description URL */
                                UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                           "ServiceTp=%s/searchServTp=%s MATCH\n", sst, servType);
                                SendReply(sscd, sst, 0, UDNstr, lowerloc, defaultExp, 1);
                            } else if (hisvers == myvers) {
                                UpnpPrintf(
                                    UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                    "ServiceTp=%s/searchServTp=%s MATCH\n",
                                    sst, servType);
                                SendReply(sscd, sst, 0, UDNstr, location, defaultExp, 1);
                            } else {
                                UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                           "ServiceTp=%s/srchServTp=%s NO MATCH\n", sst, servType);
                            }
                        } else {
                            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                                       "ServiceTp=%s/srchServTp=%s NO MATCH\n", sst, servType);
                        }
                    }
                        break;

                    default:
                        break;
                    }
                }
            }
        }
    }

    UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__, "AdvertiseAndReply1 exit\n");
    return retVal;
}


// Process the advertisements or replies for one root device (which
// may have subdevices).
//
// This top routine calls AdvertiseAndReplyOneDest() to do the real
// work for each of the appropriate network addresses/interfaces.

int AdvertiseAndReply(UpnpDevice_Handle Hnd, SSDPDevMessageType tp, int Exp,
                      struct sockaddr_storage *repDestAddr, const SsdpEntity& sdata)
{
    bool isNotify = (tp == MSGTYPE_ADVERTISEMENT || tp == MSGTYPE_SHUTDOWN);
    int ret = UPNP_E_SUCCESS;
    std::string lochost;
    SOCKET sock = INVALID_SOCKET;

    if (isNotify) {
        // Loop on our interfaces and addresses
        for (const auto& netif : g_netifs) {
            UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__,
                       "ssdp_device: mcast for %s\n", netif.getname().c_str());

            struct sockaddr_storage dss;
            auto destaddr = &dss;

#ifdef UPNP_ENABLE_IPV6
            if (using_ipv6()) {
                ssdpMcastAddr(dss, AF_INET6);
                sock = createMulticastSocket6(netif.getindex(), lochost);
                if (sock == INVALID_SOCKET) {
                    goto exitfunc;
                }

                ret = AdvertiseAndReplyOneDest(
                    Hnd, tp, Exp, destaddr, sdata, sock, lochost);

                if (ret != UPNP_E_SUCCESS) {
                    UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                               "SSDP dev: IPV6 SEND failed for %s\n", netif.getname().c_str());
                    goto exitfunc;
                }
                UpnpCloseSocket(sock);
                sock = INVALID_SOCKET;
            }
#endif /* UPNP_ENABLE_IPV6 */

            ssdpMcastAddr(dss, AF_INET);
            for (const auto& ipaddr : netif.getaddresses().first) {
                if (ipaddr.family() != NetIF::IPAddr::Family::IPV4)
                    continue;
                const struct sockaddr_storage& fss{ipaddr.getaddr()};
                sock = createMulticastSocket4(
                    reinterpret_cast<const struct sockaddr_in*>(&fss),
                    lochost);
                if (sock == INVALID_SOCKET) {
                    goto exitfunc;
                }
                ret = AdvertiseAndReplyOneDest(
                    Hnd, tp, Exp, destaddr, sdata, sock, lochost);

                UpnpCloseSocket(sock);
                sock = INVALID_SOCKET;
            }
        }
    } else {
        sock = repDestAddr->ss_family == AF_INET ?
            createReplySocket4(
                reinterpret_cast<struct sockaddr_in*>(repDestAddr), lochost) :
            createReplySocket6(
                reinterpret_cast<struct sockaddr_in6*>(repDestAddr), lochost);
        if (sock == INVALID_SOCKET) {
            goto exitfunc;
        }
        ret = AdvertiseAndReplyOneDest(
            Hnd, tp, Exp, repDestAddr, sdata, sock, lochost);
    }

exitfunc:
    if (ret != UPNP_E_SUCCESS) {
        std::string errorDesc;
        NetIF::getLastError(errorDesc);
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "sendPackets: %s\n", errorDesc.c_str());
        return UPNP_E_NETWORK_ERROR;
    }
    if (sock != INVALID_SOCKET)
        UpnpCloseSocket(sock);
    return ret;
}

#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_DEVICE_APIS */
