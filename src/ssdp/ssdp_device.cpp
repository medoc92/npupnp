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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sstream>
#include <iostream>

#define MSGTYPE_SHUTDOWN    0
#define MSGTYPE_ADVERTISEMENT   1
#define MSGTYPE_REPLY       2

static void* thread_advertiseandreply(void *data)
{
    auto arg = (SsdpSearchReply *) data;

    AdvertiseAndReply(0, arg->handle,
                      arg->event.RequestType,
                      (struct sockaddr *)&arg->dest_addr,
                      arg->event.DeviceType,
                      arg->event.UDN, arg->event.ServiceType, arg->MaxAge);
	return nullptr;
}

#define MAXVAL(a, b) ( (a) > (b) ? (a) : (b) )

#ifdef INCLUDE_DEVICE_APIS
void ssdp_handle_device_request(SSDPPacketParser& parser,
								struct sockaddr_storage *dest_addr)
{
#define MX_FUDGE_FACTOR 10
    int handle, start;
    struct Handle_Info *dev_info = nullptr;
    int mx;
    SsdpEvent event;
    SsdpSearchReply *threadArg = nullptr;
    int replyTime;
    int maxAge;

    /* check man hdr. */
    if (!parser.man || strcmp(parser.man, "\"ssdp:discover\"") != 0)
        /* bad or missing hdr. */
        return;
    /* MX header. Must be >= 1*/
    if (!parser.mx || (mx = atoi(parser.mx)) <= 0)
        return;
    /* ST header. */
    if (!parser.st || ssdp_request_type(parser.st, &event) == -1)
        return;

    start = 0;
    for (;;) {
        HandleLock();
        /* device info. */
        switch (GetDeviceHandleInfo(start, (int)dest_addr->ss_family,
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
        threadArg = (SsdpSearchReply *)malloc(sizeof(SsdpSearchReply));
        if (threadArg == nullptr)
            return;
        threadArg->handle = handle;
        memcpy(&threadArg->dest_addr, dest_addr, sizeof(threadArg->dest_addr));
        threadArg->event = event;
        threadArg->MaxAge = maxAge;

        /* Subtract a percentage from the mx to allow for network and processing
         * delays (i.e. if search is for 30 seconds, respond
         * within 0 - 27 seconds). */
        if (mx >= 2)
            mx -= MAXVAL(1, mx / MX_FUDGE_FACTOR);
        if (mx < 1)
            mx = 1;
        replyTime = rand() % mx;
        gTimerThread->schedule(
			TimerThread::SHORT_TERM, TimerThread::REL_SEC, replyTime, nullptr,
			thread_advertiseandreply, threadArg, (ThreadPool::free_routine)free);
        start = handle;
    }
}
#endif

/*!
 * \brief Works as a request handler which passes the HTTP request string
 * to multicast channel.
 *
 * \return UPNP_E_SUCCESS if successful else appropriate error.
 */
static int NewRequestHandler(
    /*! [in] Ip address, to send the reply. */
    struct sockaddr *DestAddr,
    /*! [in] Number of packet to be sent. */
    int NumPacket,
    /*! [in] . */
    std::string *RqPacket)
{
    char errorBuffer[ERROR_BUFFER_LEN];
    SOCKET ReplySock;
    socklen_t socklen = sizeof(struct sockaddr_storage);
    int Index;
    unsigned long replyAddr = inet_addr(gIF_IPV4);
    /* a/c to UPNP Spec */
    int ttl = 4;
#ifdef UPNP_ENABLE_IPV6
    int hops = 1;
#endif
    char buf_ntop[INET6_ADDRSTRLEN];
    int ret = UPNP_E_SUCCESS;

    ReplySock = socket((int)DestAddr->sa_family, SOCK_DGRAM, 0);
    if (ReplySock == INVALID_SOCKET) {
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                   "NewRequestHandler: socket(): %s\n", errorBuffer);
        return UPNP_E_OUTOF_SOCKET;
    }

    switch (DestAddr->sa_family) {
    case AF_INET:
        inet_ntop(AF_INET, &((struct sockaddr_in *)DestAddr)->sin_addr,
                  buf_ntop, sizeof(buf_ntop));
        setsockopt(ReplySock, IPPROTO_IP, IP_MULTICAST_IF,
                   (char *)&replyAddr, sizeof(replyAddr));
        setsockopt(ReplySock, IPPROTO_IP, IP_MULTICAST_TTL,
                   (char *)&ttl, sizeof(int));
        socklen = sizeof(struct sockaddr_in);
        break;
#ifdef UPNP_ENABLE_IPV6
    case AF_INET6:
        inet_ntop(AF_INET6,
                  &((struct sockaddr_in6 *)DestAddr)->sin6_addr,
                  buf_ntop, sizeof(buf_ntop));
        setsockopt(ReplySock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                   (char *)&gIF_INDEX, sizeof(gIF_INDEX));
        setsockopt(ReplySock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                   (char *)&hops, sizeof(hops));
        break;
#endif
    default:
        UpnpPrintf(UPNP_CRITICAL, SSDP, __FILE__, __LINE__,
                   "Invalid destination address specified.");
        ret = UPNP_E_NETWORK_ERROR;
        goto end_NewRequestHandler;
    }

    for (Index = 0; Index < NumPacket; Index++) {
        ssize_t rc;
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                   ">>> SSDP SEND to %s >>>\n%s\n",
                   buf_ntop, RqPacket[Index].c_str());
        rc = sendto(ReplySock, RqPacket[Index].c_str(),
                    RqPacket[Index].size(), 0, DestAddr, socklen);
        if (rc == -1) {
            posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                       "NewRequestHandler: socket(): %s\n", errorBuffer);
            ret = UPNP_E_SOCKET_WRITE;
            goto end_NewRequestHandler;
        }
    }

end_NewRequestHandler:
    UpnpCloseSocket(ReplySock);

    return ret;
}

/* return 1 if an inet6 @ has been found. */
static int extractIPv6address(const char *url, char *address, int maxlen)
{
	address[0] = 0;
    char *op = strchr((char*)url, '[');
	if (op == nullptr) 
		return 0;
	char *cl = strchr(op, ']');
	if (cl == nullptr || cl <= op + 1 || cl - op >= maxlen) {
		return 0;
    }
	memcpy(address, op + 1, cl - op - 1);
	address[cl-op] = 0;
	return 1;
}

/*!
 * \brief
 *
 * \return 1 if the Url contains an ULA or GUA IPv6 address, 0 otherwise.
 */
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

/*!
 * \brief Creates a HTTP request packet. Depending on the input parameter,
 * it either creates a service advertisement request or service shutdown
 * request etc.
 */
static void CreateServicePacket(
    /*! [in] type of the message (Search Reply, Advertisement
     * or Shutdown). */
    int msg_type,
    /*! [in] ssdp type. */
    const char *nt,
    /*! [in] unique service name ( go in the HTTP Header). */
    const char *usn,
    /*! [in] Location URL. */
    const char *location,
    /*! [in] Service duration in sec. */
    int duration,
    /*! [out] Output buffer filled with HTTP statement. */
    std::string &packet,
    /*! [in] Address family of the HTTP request. */
    int AddressFamily,
    /*! [in] PowerState as defined by UPnP Low Power. */
    int PowerState,
    /*! [in] SleepPeriod as defined by UPnP Low Power. */
    int SleepPeriod,
    /*! [in] RegistrationState as defined by UPnP Low Power. */
    int RegistrationState)
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

    } else if (msg_type == MSGTYPE_ADVERTISEMENT||msg_type == MSGTYPE_SHUTDOWN) {

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
    auto DestAddr4 = (struct sockaddr_in *)&__ss;
    auto DestAddr6 = (struct sockaddr_in6 *)&__ss;
    memset(&__ss, 0, sizeof(__ss));
    switch (AddressFamily) {
    case AF_INET:
        DestAddr4->sin_family = (sa_family_t)AF_INET;
        inet_pton(AF_INET, SSDP_IP, &DestAddr4->sin_addr);
        DestAddr4->sin_port = htons(SSDP_PORT);
        break;
    case AF_INET6:
        DestAddr6->sin6_family = (sa_family_t)AF_INET6;
        inet_pton(AF_INET6,
                  (isUrlV6UlaGua(Location)) ? SSDP_IPV6_SITELOCAL :
                  SSDP_IPV6_LINKLOCAL, &DestAddr6->sin6_addr);
        DestAddr6->sin6_port = htons(SSDP_PORT);
        DestAddr6->sin6_scope_id = gIF_INDEX;
        break;
    default:
		return false;
    }
	return true;
}

int DeviceAdvertisementOrShutdown(
	int msgtype, const char *DevType, int RootDev, const char *Udn, const char *Location,
	int Duration, int AddressFamily, int PowerState,
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
        if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
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
    if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
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
        ret_code = NewRequestHandler((struct sockaddr *)&__ss, 3, &msgs[0]);
    } else {        /* sub-device */
        /* send 2 msg types */
        ret_code = NewRequestHandler((struct sockaddr *)&__ss, 2, &msgs[1]);
    }

error_handler:
    return ret_code;

}

int DeviceAdvertisement(const char *DevType, int RootDev, const char *Udn, const char *Location,
                        int Duration, int AddressFamily, int PowerState,
                        int SleepPeriod, int RegistrationState)
{
	return DeviceAdvertisementOrShutdown(
		MSGTYPE_ADVERTISEMENT, DevType, RootDev, Udn, Location,	Duration,
		AddressFamily, PowerState, SleepPeriod, RegistrationState);
}

int DeviceShutdown(const char *DevType, int RootDev, const char *Udn,
                   const char *Location, int Duration, int AddressFamily,
                   int PowerState, int SleepPeriod, int RegistrationState)
{
	return DeviceAdvertisementOrShutdown(
		MSGTYPE_SHUTDOWN, DevType, RootDev, Udn, Location, Duration,
		AddressFamily, PowerState, SleepPeriod, RegistrationState);
}

int SendReply(struct sockaddr *DestAddr, const char *DevType, int RootDev,
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
        if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(MSGTYPE_REPLY, "upnp:rootdevice",
                            Mil_Usn, Location, Duration, msgs[0],
                            (int)DestAddr->sa_family, PowerState,
                            SleepPeriod, RegistrationState);
    } else {
        /* two msgs for embedded devices */
        num_msgs = 1;

        /*NK: FIX for extra response when someone searches by udn */
        if (!ByType) {
            CreateServicePacket(MSGTYPE_REPLY, Udn, Udn, Location,
                                Duration, msgs[0],
                                (int)DestAddr->sa_family, PowerState,
                                SleepPeriod, RegistrationState);
        } else {
            rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
            if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
                goto error_handler;
            CreateServicePacket(MSGTYPE_REPLY, DevType, Mil_Usn,
                                Location, Duration, msgs[0],
                                (int)DestAddr->sa_family, PowerState,
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
    ret_code = NewRequestHandler(DestAddr, num_msgs, msgs);

error_handler:
    return ret_code;
}

int DeviceReply(struct sockaddr *DestAddr, const char *DevType, int RootDev,
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
        if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
            goto error_handler;
        CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
                            Location, Duration, szReq[0],
                            (int)DestAddr->sa_family, PowerState,
                            SleepPeriod, RegistrationState);
    }
    rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", Udn);
    if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Nt))
        goto error_handler;
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s", Udn);
    if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
                        Location, Duration, szReq[1], (int)DestAddr->sa_family,
                        PowerState, SleepPeriod, RegistrationState);
    rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", DevType);
    if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Nt))
        goto error_handler;
    rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
    if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
                        Location, Duration, szReq[2], (int)DestAddr->sa_family,
                        PowerState, SleepPeriod, RegistrationState);
    /* check error */
    if ((RootDev && szReq[0].empty()) || szReq[1].empty() || szReq[2].empty()) {
        goto error_handler;
    }
    /* send replies */
    if (RootDev) {
        RetVal = NewRequestHandler(DestAddr, 3, szReq);
    } else {
        RetVal = NewRequestHandler(DestAddr, 2, &szReq[1]);
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
    if (rc < 0 || (unsigned int) rc >= sizeof(Mil_Usn))
        goto error_handler;
    CreateServicePacket(tp, ServType, Mil_Usn,
                        Location, Duration, szReq[0], (int)DestAddr->sa_family,
                        PowerState, SleepPeriod, RegistrationState);
    if (szReq[0].empty())
        goto error_handler;
    RetVal = NewRequestHandler(DestAddr, 1, szReq);

error_handler:
    return RetVal;
}

int ServiceReply(struct sockaddr *DestAddr, const char *ServType,
				 const char *Udn, const char *Location, int Duration,
				 int PowerState, int SleepPeriod, int RegistrationState)
{
	return ServiceSend(
		MSGTYPE_REPLY, DestAddr, ServType, Udn, Location,
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
    return ServiceSend(
		tp, (struct sockaddr *)&__ss, ServType, Udn, Location,
		Duration, PowerState, SleepPeriod, RegistrationState);
}

int ServiceAdvertisement(const char *Udn, const char *ServType,
						 const char *Location, int Duration, int AddressFamily,
                         int PowerState, int SleepPeriod, int RegistrationState)
{
    return ServiceAdvShut(
		MSGTYPE_ADVERTISEMENT, Udn, ServType, Location, Duration, AddressFamily,
		PowerState, SleepPeriod, RegistrationState);
}

int ServiceShutdown(const char *Udn, const char *ServType, const char *Location,
					int Duration, int AddressFamily, int PowerState,
                    int SleepPeriod, int RegistrationState)
{
    return ServiceAdvShut(
		MSGTYPE_SHUTDOWN, Udn, ServType, Location, Duration, AddressFamily,
		PowerState, SleepPeriod, RegistrationState);
}

#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_DEVICE_APIS */
