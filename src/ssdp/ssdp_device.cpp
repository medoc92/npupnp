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

#include "httputils.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "ThreadPool.h"
#include "upnpapi.h"
#include "UpnpInet.h"
#include "upnpdebug.h"
#include "TimerThread.h"
#include "smallut.h"
#include "inet_pton.h"
#include "netif.h"
#include "upnpapi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <algorithm>
#include <chrono>

#define MSGTYPE_SHUTDOWN    0
#define MSGTYPE_ADVERTISEMENT   1
#define MSGTYPE_REPLY       2

struct SsdpSearchReply {
	int MaxAge;
	UpnpDevice_Handle handle;
	struct sockaddr_storage dest_addr;
	SsdpEvent event;
};

static void* thread_advertiseandreply(void *data)
{
    auto arg = static_cast<SsdpSearchReply *>(data);

    AdvertiseAndReply(0, arg->handle,
                      arg->event.RequestType,
                      reinterpret_cast<struct sockaddr *>(&arg->dest_addr),
                      arg->event.DeviceType,
                      arg->event.UDN, arg->event.ServiceType, arg->MaxAge);
	return nullptr;
}

void ssdp_handle_device_request(SSDPPacketParser& parser,
								struct sockaddr_storage *dest_addr)
{
    int handle, start;
    struct Handle_Info *dev_info = nullptr;
    int mx;
    SsdpEvent event;
    SsdpSearchReply *threadArg = nullptr;
    int maxAge;

    /* check man hdr. */
    if (!parser.man || strcmp(parser.man, "\"ssdp:discover\"") != 0) {
        /* bad or missing hdr. */
		std::cerr << "ssdp_handle_device_req: no/bad MAN header\n";
        return;
	}
    /* MX header. Must be >= 1*/
    if (!parser.mx || (mx = atoi(parser.mx)) <= 0) {
		std::cerr << "ssdp_handle_device_req: no/bad MX header\n";
        return;
	}
    /* ST header. */
    if (!parser.st || ssdp_request_type(parser.st, &event) == -1) {
		std::cerr << "ssdp_handle_device_req: no/bad ST header\n";
        return;
	}

	// Loop to dispatch the packet to each of our configured devices
	// by starting the handle search at the last processed
	// position. each device response is scheduled by the ThreadPool
	// with a random delay based on the MX header of the search packet.
    start = 0;
    for (;;) {
        HandleLock();
        /* device info. */
        switch (GetDeviceHandleInfo(start,static_cast<int>(dest_addr->ss_family),
                                    &handle, &dev_info)) {
        case HND_DEVICE:
            break;
        default:
            HandleUnlock();
            /* no info found. */
            return;
        }
        maxAge = dev_info->MaxAge;
        HandleUnlock();

        UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
                   "MAX-AGE     =  %d\n", maxAge);
        UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
                   "MX     =  %d\n", event.Mx);
        UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
                   "DeviceType   =  %s\n", event.DeviceType);
        UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
                   "DeviceUuid   =  %s\n", event.UDN);
        UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
                   "ServiceType =  %s\n", event.ServiceType);
        threadArg =
			static_cast<SsdpSearchReply *>(malloc(sizeof(SsdpSearchReply)));
        if (threadArg == nullptr)
            return;
        threadArg->handle = handle;
        memcpy(&threadArg->dest_addr, dest_addr, sizeof(threadArg->dest_addr));
        threadArg->event = event;
        threadArg->MaxAge = maxAge;

		mx = std::max(1, mx);
        /* Subtract a bit from the mx to allow for network/processing delays */
        int delayms = rand() %  (mx * 1000 - 100);
		std::cerr << "ssdp_handle_device_req: scheduling resp in " <<
			delayms << " ms\n";
        gTimerThread->schedule(
			TimerThread::SHORT_TERM, std::chrono::milliseconds(delayms),
			nullptr, thread_advertiseandreply, threadArg,
			static_cast<ThreadPool::free_routine>(free));
        start = handle;
    }
}

/* Send the device packet to the network */
static int sendPackets(struct sockaddr *DestAddr, int NumPacket,
					   std::string *RqPacket)
{
    char errorBuffer[ERROR_BUFFER_LEN];
    /* a/c to UPNP Spec */
    int ttl = 4;
#ifdef UPNP_ENABLE_IPV6
    int hops = 1;
#endif
    int ret = UPNP_E_SUCCESS;

    SOCKET sock = socket(static_cast<int>(DestAddr->sa_family), SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                   "sendPackets: socket(): %s\n", errorBuffer);
        return UPNP_E_OUTOF_SOCKET;
    }

	std::string srcAddrStr;
	socklen_t socklen;
	NetIF::IPAddr destip(DestAddr); // for printing
    switch (DestAddr->sa_family) {
    case AF_INET:
	{
		uint32_t srcAddr = inet_addr(apiFirstIPV4Str().c_str());		
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<char *>(&srcAddr), sizeof(srcAddr));
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   reinterpret_cast<char *>(&ttl), sizeof(int));
        socklen = sizeof(struct sockaddr_in);
		srcAddrStr = apiFirstIPV4Str();
	}
	break;
#ifdef UPNP_ENABLE_IPV6
    case AF_INET6:
	{
		int index = apiFirstIPV6Index();
        setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                   reinterpret_cast<char *>(&index), sizeof(int));
        setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                   reinterpret_cast<char *>(&hops), sizeof(hops));
        socklen = sizeof(struct sockaddr_in6);
		srcAddrStr = apiFirstIPV6Str();
	}
	break;
#endif
    default:
		std::cerr << "SSDP send: bad dest address \n";
        UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
                   "Invalid destination address specified.");
        ret = UPNP_E_NETWORK_ERROR;
        goto end_sendPackets;
    }

    for (int Index = 0; Index < NumPacket; Index++) {
		// Edit the packet LOCATION to match the appropriate interface.
		// Temporary, move up to interface loop later
		std::string packet = RqPacket[Index];

		std::string::size_type locpos = packet.find(g_HostForTemplate);
		if (locpos != std::string::npos) {
			packet = packet.replace(locpos, g_HostForTemplate.size(),srcAddrStr);
		}
        ssize_t rc;
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                   ">>> SSDP SEND to %s >>>\n%s\n",
                   destip.straddr().c_str(), RqPacket[Index].c_str());
		std::cerr << "SSDP: sending to " << destip.straddr() << " data: \n" <<
			RqPacket[Index] << "\n";
        rc = sendto(sock, packet.c_str(), packet.size(), 0, DestAddr, socklen);
        if (rc == -1) {
			std::cerr << "SSDP: sendto " << destip.straddr() << " failed\n";
            posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                       "sendPackets: socket(): %s\n", errorBuffer);
            ret = UPNP_E_SOCKET_WRITE;
            goto end_sendPackets;
        }
    }

end_sendPackets:
    UpnpCloseSocket(sock);
    return ret;
}

/* return 1 if an inet6 @ has been found. */
static int extractIPv6address(const char *url, char *address, int maxlen)
{
	address[0] = 0;
    char *op = std::strchr(const_cast<char*>(url), '[');
	if (op == nullptr) 
		return 0;
	char *cl = std::strchr(op, ']');
	if (cl == nullptr || cl <= op + 1 || cl - op >= maxlen) {
		return 0;
    }
	std::memcpy(address, op + 1, cl - op - 1);
	address[cl-op] = 0;
	return 1;
}

/* Return 1 if the Url contains an ULA or GUA IPv6 address, 0 otherwise. */
static int isUrlV6UlaGua(const char *descdocUrl)
{
    char address[INET6_ADDRSTRLEN+10];
    struct in6_addr v6_addr;

    if (extractIPv6address(descdocUrl, address, INET6_ADDRSTRLEN+10)) {
        inet_pton(AF_INET6, address, &v6_addr);
        return !IN6_IS_ADDR_LINKLOCAL(&v6_addr);
    }

    return 0;
}

/* Creates a device notify or search reply packet. */
static void CreateServicePacket(
    int msg_type, const char *nt, const char *usn, const char *location,
	int duration, std::string &packet, int AddressFamily, int PowerState,
	int SleepPeriod, int RegistrationState)
{
	std::ostringstream str;
    if (msg_type == MSGTYPE_REPLY) {
		str <<
			"HTTP/1.1 " << HTTP_OK << " OK\r\n" <<
			"CACHE-CONTROL: max-age=" << duration << "\r\n" <<
			"DATE: " << make_date_string(0) << "\r\n" <<
			"EXT:\r\n" <<
			"LOCATION: " << location << "\r\n" <<
			"SERVER: " << get_sdk_info() << "\r\n" <<
#ifdef UPNP_HAVE_OPTSSDP
			"OPT: " << "\"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n" <<
			"01-NLS: " << gUpnpSdkNLSuuid << "\r\n" <<
			"X-User-Agent: " << X_USER_AGENT << "\r\n" <<
#endif
			"ST: " << nt << "\r\n" <<
			"USN: " << usn << "\r\n";

    } else if (msg_type == MSGTYPE_ADVERTISEMENT || msg_type == MSGTYPE_SHUTDOWN) {

		const char *nts = (msg_type == MSGTYPE_ADVERTISEMENT) ?
			"ssdp:alive" : "ssdp:byebye";

        /* NOTE: The CACHE-CONTROL and LOCATION headers are not present in
         * a shutdown msg, but are present here for MS WinMe interop. */
        const char *host;
        switch (AddressFamily) {
        case AF_INET: host = SSDP_IP; break;
        default: host = isUrlV6UlaGua(location) ? "[" SSDP_IPV6_SITELOCAL "]" :
			"[" SSDP_IPV6_LINKLOCAL "]";
        }

		str <<
			"NOTIFY * HTTP/1.1\r\n" <<
			"HOST: " << host << ":" << SSDP_PORT << "\r\n" <<
			"CACHE-CONTROL: max-age=" << duration << "\r\n" <<
			"LOCATION: " << location << "\r\n" <<
			"SERVER: " << get_sdk_info() << "\r\n" <<
#ifdef UPNP_HAVE_OPTSSDP
			"OPT: " << "\"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n" <<
			"01-NLS: " << gUpnpSdkNLSuuid << "\r\n" <<
			"X-User-Agent: " << X_USER_AGENT << "\r\n" <<
#endif
			"NT: " << nt << "\r\n" <<
			"NTS: " << nts << "\r\n" <<
			"USN: " << usn << "\r\n";

    } else {
        /* unknown msg */
        assert(0);
	}

	if (PowerState > 0) {
		str <<
			"Powerstate: "        << PowerState        << "\r\n" <<
			"SleepPeriod: "       << SleepPeriod       << "\r\n" <<
			"RegistrationState: " << RegistrationState << "\r\n";
	}

	str << "\r\n";

    packet = str.str();
	// std::cerr << "SERVICEPACKET:\n" << packet << std::endl;
}

static bool setDestAddr(struct sockaddr_storage& __ss, const char *Location,
						int AddressFamily)
{
    auto DestAddr4 = reinterpret_cast<struct sockaddr_in *>(&__ss);
    auto DestAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&__ss);
    memset(&__ss, 0, sizeof(__ss));
    switch (AddressFamily) {
    case AF_INET:
        DestAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
        inet_pton(AF_INET, SSDP_IP, &DestAddr4->sin_addr);
        DestAddr4->sin_port = htons(SSDP_PORT);
        break;
    case AF_INET6:
        DestAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
        inet_pton(AF_INET6,
                  (isUrlV6UlaGua(Location)) ? SSDP_IPV6_SITELOCAL :
                  SSDP_IPV6_LINKLOCAL, &DestAddr6->sin6_addr);
        DestAddr6->sin6_port = htons(SSDP_PORT);
        DestAddr6->sin6_scope_id = apiFirstIPV6Index();
        break;
    default:
		return false;
    }
	return true;
}

static int DeviceAdvertisementOrShutdown(
	int msgtype, const char *DevType, int RootDev, const char *Udn,
	const char *Location, int Duration, int AddressFamily, int PowerState,
	int SleepPeriod, int RegistrationState)
{
    struct sockaddr_storage __ss;
    char Mil_Usn[LINE_SIZE];
	std::string msgs[3];
    int ret_code = UPNP_E_OUTOF_MEMORY;
    int rc = 0;

	if (!setDestAddr(__ss, Location, AddressFamily)) {
        UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
                   "Invalid device address family.\n");
		return UPNP_E_BAD_REQUEST;
	}

    /* If deviceis a root device, we need to send 3 messages */
    if (RootDev) {
        rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
        if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(msgtype, "upnp:rootdevice",
                            Mil_Usn, Location, Duration, msgs[0],
                            AddressFamily, PowerState, SleepPeriod,
                            RegistrationState);
    }
    /* both root and sub-devices need to send these two messages */
    CreateServicePacket(msgtype, Udn, Udn,
                        Location, Duration, msgs[1], AddressFamily,
                        PowerState, SleepPeriod, RegistrationState);
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(msgtype, DevType, Mil_Usn,
                        Location, Duration, msgs[2], AddressFamily,
                        PowerState, SleepPeriod, RegistrationState);
    /* check error */
    if ((RootDev && msgs[0].empty()) || msgs[1].empty() || msgs[2].empty()) {
        goto error_handler;
    }
    /* send packets */
    if (RootDev) {
        /* send 3 msg types */
        ret_code = sendPackets(reinterpret_cast<struct sockaddr *>(&__ss), 3, &msgs[0]);
    } else {        /* sub-device */
        /* send 2 msg types */
        ret_code = sendPackets(reinterpret_cast<struct sockaddr *>(&__ss), 2, &msgs[1]);
    }

error_handler:
    return ret_code;

}

static int DeviceAdvertisement(
	const char *DevType, int RootDev, const char *Udn, const char *Location,
	int Duration, int AddressFamily, int PowerState, int SleepPeriod, int
	RegistrationState)
{
	return DeviceAdvertisementOrShutdown(
		MSGTYPE_ADVERTISEMENT, DevType, RootDev, Udn, Location,	Duration,
		AddressFamily, PowerState, SleepPeriod, RegistrationState);
}

static int DeviceShutdown(const char *DevType, int RootDev, const char *Udn,
                   const char *Location, int Duration, int AddressFamily,
                   int PowerState, int SleepPeriod, int RegistrationState)
{
	return DeviceAdvertisementOrShutdown(
		MSGTYPE_SHUTDOWN, DevType, RootDev, Udn, Location, Duration,
		AddressFamily, PowerState, SleepPeriod, RegistrationState);
}

static int SendReply(
	struct sockaddr *DestAddr, const char *DevType, int RootDev,
	const char *Udn, const char *Location, int Duration, int ByType,
	int PowerState, int SleepPeriod, int RegistrationState)
{
    int ret_code = UPNP_E_OUTOF_MEMORY;
	std::string msgs[2];
    int num_msgs;
    char Mil_Usn[LINE_SIZE];
    int i;
    int rc = 0;

    if (RootDev) {
        /* one msg for root device */
        num_msgs = 1;

        rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
        if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(MSGTYPE_REPLY, "upnp:rootdevice",
                            Mil_Usn, Location, Duration, msgs[0],
                            static_cast<int>(DestAddr->sa_family), PowerState,
                            SleepPeriod, RegistrationState);
    } else {
        /* two msgs for embedded devices */
        num_msgs = 1;

        /*NK: FIX for extra response when someone searches by udn */
        if (!ByType) {
            CreateServicePacket(MSGTYPE_REPLY, Udn, Udn, Location,
                                Duration, msgs[0],
                                static_cast<int>(DestAddr->sa_family),PowerState,
                                SleepPeriod, RegistrationState);
        } else {
            rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
            if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
                goto error_handler;
            CreateServicePacket(MSGTYPE_REPLY, DevType, Mil_Usn,
                                Location, Duration, msgs[0],
                                static_cast<int>(DestAddr->sa_family),PowerState,
                                SleepPeriod, RegistrationState);
        }
    }
    /* check error */
    for (i = 0; i < num_msgs; i++) {
        if (msgs[i].empty()) {
            goto error_handler;
        }
    }
    /* send msgs */
    ret_code = sendPackets(DestAddr, num_msgs, msgs);

error_handler:
    return ret_code;
}

static int DeviceReply(
	struct sockaddr *DestAddr, const char *DevType, int RootDev,
	const char *Udn, const char *Location, int Duration, int PowerState,
	int SleepPeriod, int RegistrationState)
{
	std::string szReq[3];
	char Mil_Nt[LINE_SIZE], Mil_Usn[LINE_SIZE];
    int RetVal = UPNP_E_OUTOF_MEMORY;
    int rc = 0;

    /* create 2 or 3 msgs */
    if (RootDev) {
        /* 3 replies for root device */
        upnp_strlcpy(Mil_Nt, "upnp:rootdevice", sizeof(Mil_Nt));
        rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
        if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
                            Location, Duration, szReq[0],
                            static_cast<int>(DestAddr->sa_family), PowerState,
                            SleepPeriod, RegistrationState);
    }
    rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", Udn);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Nt))
        goto error_handler;
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s", Udn);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
                        Location, Duration, szReq[1],
						static_cast<int>(DestAddr->sa_family),
                        PowerState, SleepPeriod, RegistrationState);
    rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", DevType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Nt))
        goto error_handler;
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
                        Location, Duration, szReq[2],
						static_cast<int>(DestAddr->sa_family),
                        PowerState, SleepPeriod, RegistrationState);
    /* check error */
    if ((RootDev && szReq[0].empty()) || szReq[1].empty() || szReq[2].empty()) {
        goto error_handler;
    }
    /* send replies */
    if (RootDev) {
        RetVal = sendPackets(DestAddr, 3, szReq);
    } else {
        RetVal = sendPackets(DestAddr, 2, &szReq[1]);
    }

error_handler:
    return RetVal;
}

static int ServiceSend(
	int tp, struct sockaddr *DestAddr, const char *ServType, const char *Udn,
	const char *Location, int Duration, int PowerState, int SleepPeriod,
	int RegistrationState)
{
    char Mil_Usn[LINE_SIZE];
	std::string szReq[1];
    int RetVal = UPNP_E_OUTOF_MEMORY;
    int rc = 0;

    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, ServType);
    if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(tp, ServType, Mil_Usn,
                        Location, Duration, szReq[0],
						static_cast<int>(DestAddr->sa_family),
                        PowerState, SleepPeriod, RegistrationState);
    if (szReq[0].empty())
        goto error_handler;
    RetVal = sendPackets(DestAddr, 1, szReq);

error_handler:
    return RetVal;
}

static int ServiceReply(struct sockaddr *DestAddr, const char *ServType,
						const char *Udn, const char *Location, int Duration,
						int PowerState, int SleepPeriod, int RegistrationState)
{
	return ServiceSend(MSGTYPE_REPLY, DestAddr, ServType, Udn, Location,
					   Duration, PowerState, SleepPeriod, RegistrationState);
}

static int ServiceAdvShut(int tp, const char *Udn, const char *ServType,
						  const char *Location, int Duration, int AddressFamily,
						  int PowerState, int SleepPeriod, int RegistrationState)
{
    struct sockaddr_storage __ss;
	if (!setDestAddr(__ss, Location, AddressFamily)) {
        UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
				   "Invalid device address family.\n");
		return UPNP_E_BAD_REQUEST;
	}
    return ServiceSend(tp, reinterpret_cast<struct sockaddr *>(&__ss), ServType,
					   Udn, Location,
					   Duration, PowerState, SleepPeriod, RegistrationState);
}

static int ServiceAdvertisement(
	const char *Udn, const char *ServType, const char *Location, int Duration,
	int AddressFamily, int PowerState, int SleepPeriod, int RegistrationState)
{
    return ServiceAdvShut(
		MSGTYPE_ADVERTISEMENT, Udn, ServType, Location, Duration, AddressFamily,
		PowerState, SleepPeriod, RegistrationState);
}

static int ServiceShutdown(
	const char *Udn, const char *ServType, const char *Location, int Duration,
	int AddressFamily, int PowerState, int SleepPeriod, int RegistrationState)
{
    return ServiceAdvShut(
		MSGTYPE_SHUTDOWN, Udn, ServType, Location, Duration, AddressFamily,
		PowerState, SleepPeriod, RegistrationState);
}

int AdvertiseAndReply(int AdFlag, UpnpDevice_Handle Hnd,
					  enum SsdpSearchType SearchType,
					  struct sockaddr *DestAddr, char *DeviceType,
					  char *DeviceUDN, char *ServiceType, int Exp)
{
	int retVal = UPNP_E_SUCCESS;
	int defaultExp = DEFAULT_MAXAGE;
	int NumCopy = 0;
	std::vector<const UPnPDeviceDesc*> alldevices;

	UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
			   "Inside AdvertiseAndReply: AdFlag = %d\n", AdFlag);

	/* Use a read lock */
	HandleReadLock();
	struct Handle_Info *SInfo = nullptr;
	if (GetHandleInfo(Hnd, &SInfo) != HND_DEVICE) {
		retVal = UPNP_E_INVALID_HANDLE;
		goto end_function;
	}
	defaultExp = SInfo->MaxAge;

	// Store the root and embedded devices in a single vector for convenience
	alldevices.push_back(&SInfo->devdesc);
	for (const auto& dev : SInfo->devdesc.embedded) {
		alldevices.push_back(&dev);
	}

	/* send advertisements/replies */
	while (NumCopy == 0 || (AdFlag && NumCopy < NUM_SSDP_COPY)) {
		if (NumCopy != 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(SSDP_PAUSE));
		NumCopy++;

		for (auto& devp : alldevices) {
			bool isroot = &devp == &(*alldevices.begin());
			const char *devType = devp->deviceType.c_str();
			const char *UDNstr = devp->UDN.c_str();
			if (AdFlag) {
				/* send the device advertisement */
				if (AdFlag == 1) {
					DeviceAdvertisement(
						devType, isroot, UDNstr, SInfo->DescURL, Exp,
						SInfo->DeviceAf, SInfo->PowerState,	SInfo->SleepPeriod,
						SInfo->RegistrationState);
				} else {
					/* AdFlag == -1 */
					DeviceShutdown(
						devType, isroot, UDNstr, SInfo->DescURL, Exp,
						SInfo->DeviceAf, SInfo->PowerState, SInfo->SleepPeriod,
						SInfo->RegistrationState);
				}
			} else {
				switch (SearchType) {
				case SSDP_ALL:
					DeviceReply(
						DestAddr, devType, isroot, UDNstr, SInfo->DescURL,
						defaultExp, SInfo->PowerState, SInfo->SleepPeriod,
						SInfo->RegistrationState);
					break;
				case SSDP_ROOTDEVICE:
					if (isroot) {
						SendReply(
							DestAddr, devType, 1, UDNstr, SInfo->DescURL,
							defaultExp, 0, SInfo->PowerState, SInfo->SleepPeriod,
							SInfo->RegistrationState);
					}
					break;
				case SSDP_DEVICEUDN: {
					if (DeviceUDN && strlen(DeviceUDN)) {
						if (strcasecmp(DeviceUDN, UDNstr) != 0) {
							UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
									   "DeviceUDN=%s/search UDN=%s NOMATCH\n",
									   UDNstr, DeviceUDN);
							break;
						}

						UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
								   "DeviceUDN=%s/search UDN=%s MATCH\n",
								   UDNstr, DeviceUDN);
						SendReply(
							DestAddr, devType, 0, UDNstr, SInfo->DescURL,
							defaultExp, 0, SInfo->PowerState,
							SInfo->SleepPeriod, SInfo->RegistrationState);
						break;
					}
				}
				case SSDP_DEVICETYPE: {
					if (!strncasecmp(DeviceType,devType,strlen(DeviceType)-2)) {
						if (std::atoi(std::strrchr(DeviceType, ':') + 1)
						    < std::atoi(&devType[std::strlen(devType) - static_cast<size_t>(1)])) {
							/* the requested version is lower than the
							   device version must reply with the
							   lower version number and the lower
							   description URL */
							UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
									   "DeviceType=%s/srchdevType=%s MATCH\n",
									   devType, DeviceType);
							SendReply(DestAddr, DeviceType, 0, UDNstr,
									  SInfo->LowerDescURL, defaultExp, 1,
									  SInfo->PowerState, SInfo->SleepPeriod,
									  SInfo->RegistrationState);
						} else if (std::atoi(std::strrchr(DeviceType, ':') + 1)
								   == std::atoi(&devType[std::strlen(devType) - 1])) {
							UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
									   "DeviceType=%s/srchDevType=%s MATCH\n",
									   devType, DeviceType);
							SendReply(DestAddr, DeviceType, 0,
									  UDNstr, SInfo->DescURL, defaultExp, 1,
									  SInfo->PowerState, SInfo->SleepPeriod,
									  SInfo->RegistrationState);
						} else {
							UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
									   "DeviceType=%s/srchDevType=%s NOMATCH\n",
									   devType, DeviceType);
						}
					} else {
						UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
								   "DeviceType=%s /srchdevType=%s NOMATCH\n",
								   devType, DeviceType);
					}
					break;
				}
				default:
					break;
				}
			}

			/* send service advertisements for services corresponding
			 * to the same device */
			UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
					   "Sending service advertisements\n");
			/* Correct service traversal such that each device's serviceList
			 * is directly traversed as a child of its parent device. This
			 * ensures that the service's alive message uses the UDN of
			 * the parent device. */
			for (const auto& service : devp->services) {
				const char *servType = service.serviceType.c_str();
				UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
						   "ServiceType = %s\n", servType);
				if (AdFlag) {
					if (AdFlag == 1) {
						ServiceAdvertisement(
							UDNstr, servType, SInfo->DescURL, Exp,
							SInfo->DeviceAf, SInfo->PowerState,
							SInfo->SleepPeriod, SInfo->RegistrationState);
					} else {
						/* AdFlag == -1 */
						ServiceShutdown(
							UDNstr,	servType, SInfo->DescURL,
							Exp, SInfo->DeviceAf, SInfo->PowerState,
							SInfo->SleepPeriod,	SInfo->RegistrationState);
					}
				} else {
					switch (SearchType) {
					case SSDP_ALL:
						ServiceReply(DestAddr, servType, UDNstr,
									 SInfo->DescURL, defaultExp,
									 SInfo->PowerState, SInfo->SleepPeriod,
									 SInfo->RegistrationState);
						break;
					case SSDP_SERVICE:
						if (ServiceType) {
							if (!strncasecmp(ServiceType, servType,
											 strlen(ServiceType) - 2)) {
								if (std::atoi(std::strrchr(ServiceType, ':') + 1) <
								    std::atoi(&servType[std::strlen(servType) - 1])) {
									/* the requested version is lower
									   than the service version must
									   reply with the lower version
									   number and the lower
									   description URL */
									UpnpPrintf(
										UPNP_DEBUG, SSDP, __FILE__, __LINE__,
										"ServiceTp=%s/searchServTp=%s MATCH\n",
										ServiceType, servType);
									SendReply(DestAddr, ServiceType, 0, UDNstr,
											  SInfo->LowerDescURL, defaultExp, 1,
											  SInfo->PowerState,
											  SInfo->SleepPeriod,
											  SInfo->RegistrationState);
								} else if (
									std::atoi(std::strrchr (ServiceType, ':') + 1)
									== std::atoi(&servType[std::strlen(servType) - 1])) {
									UpnpPrintf(
										UPNP_DEBUG, SSDP, __FILE__, __LINE__,
										"ServiceTp=%s/searchServTp=%s MATCH\n",
										ServiceType, servType);
									SendReply(DestAddr, ServiceType, 0, UDNstr,
											  SInfo->DescURL, defaultExp, 1,
											  SInfo->PowerState,
											  SInfo->SleepPeriod,
											  SInfo->RegistrationState);
								} else {
									UpnpPrintf(
										UPNP_DEBUG, SSDP, __FILE__, __LINE__,
										"ServiceTp=%s/srchServTp=%s NO MATCH\n",
										ServiceType, servType);
								}
							} else {
								UpnpPrintf(
									UPNP_DEBUG, SSDP, __FILE__, __LINE__,
									"ServiceTp=%s/srchServTp=%s NO MATCH\n",
									ServiceType, servType);
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

end_function:
	UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__, "AdvertiseAndReply exit\n");
	HandleUnlock();

	return retVal;
}

#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_DEVICE_APIS */
