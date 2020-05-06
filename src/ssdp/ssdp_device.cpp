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
#include "genut.h"
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

struct SsdpSearchReply {
	int MaxAge;
	UpnpDevice_Handle handle;
	struct sockaddr_storage dest_addr;
	SsdpEntity event;
};

static void *thread_advertiseandreply(void *data)
{
	auto arg = static_cast<SsdpSearchReply *>(data);

	AdvertiseAndReply(MSGTYPE_REPLY, arg->handle,
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
	SsdpEntity event;
	SsdpSearchReply *threadArg = nullptr;
	int maxAge;

	/* check man hdr. */
	if (!parser.man || strcmp(parser.man, "\"ssdp:discover\"") != 0) {
		/* bad or missing hdr. */
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "ssdp_handle_device_req: no/bad MAN header\n");
		return;
	}
	/* MX header. Must be >= 1*/
	if (!parser.mx || (mx = atoi(parser.mx)) <= 0) {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "ssdp_handle_device_req: no/bad MX header\n");
		return;
	}
	/* ST header. */
	if (!parser.st || ssdp_request_type(parser.st, &event) == -1) {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "ssdp_handle_device_req: no/bad ST header\n");
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
		switch (GetDeviceHandleInfo(start, &handle, &dev_info)) {
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
				   "MAX-AGE		=  %d\n", maxAge);
		UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
				   "MX	   =  %d\n", maxAge);
		UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
				   "DeviceType	 =	%s\n", event.DeviceType);
		UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
				   "DeviceUuid	 =	%s\n", event.UDN);
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
		int delayms = rand() %	(mx * 1000 - 100);
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "ssdp_handle_device_req: scheduling resp in %d ms\n",delayms);
		gTimerThread->schedule(
			TimerThread::SHORT_TERM, std::chrono::milliseconds(delayms),
			nullptr, thread_advertiseandreply, threadArg,
			static_cast<ThreadPool::free_routine>(free));
		start = handle;
	}
}

// Create the reply socket and determine the appropriate host address
// for setting the LOCATION header
static int createMulticastSocket4(
	const struct sockaddr_in *srcaddr, std::string& lochost)
{
	char ttl = 2;
	int bcast = 1;
	const NetIF::IPAddr
		ipaddr(reinterpret_cast<const struct sockaddr *>(srcaddr));
	lochost = ipaddr.straddr();
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
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
	if(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) < 0) {
		goto error;
	}
	if (bind(sock, reinterpret_cast<const struct sockaddr *>(srcaddr),
			 sizeof(struct sockaddr_in)) < 0) {
		goto error;
	}
	return sock;
error:
	close(sock);
	return -1;
}

static int createReplySocket4(
	struct sockaddr_in *destaddr, std::string& lochost)
{
	SOCKET sock = socket(static_cast<int>(destaddr->sin_family), SOCK_DGRAM, 0);
	if (sock < 0) {
		return INVALID_SOCKET;
	}
	// Determine the proper interface and compute the location string
	NetIF::IPAddr dipaddr(reinterpret_cast<struct sockaddr*>(destaddr));
	NetIF::IPAddr hostaddr;
	const NetIF::Interface *netif =
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
static int createMulticastSocket6(int index, std::string& lochost)
{
	int hops = 1;
	SOCKET sock = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sock < 0) {
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
			const NetIF::IPAddr *ipaddr =
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
	close(sock);
	return INVALID_SOCKET;
}

static int createReplySocket6(
	struct sockaddr_in6 *destaddr, std::string& lochost)
{
	SOCKET sock = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sock < 0) {
		return INVALID_SOCKET;
	}
	int index = destaddr->sin6_scope_id;
	lochost.clear();
	for (const auto& netif : g_netifs) {
		if (netif.getindex() == index) {
			const NetIF::IPAddr *ipaddr =
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
#endif

// Set the UPnP predefined multicast destination addresses
static bool ssdpMcastAddr(struct sockaddr_storage& ss, int AddressFamily)
{
	memset(&ss, 0, sizeof(ss));
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

static int sendPackets(
	SOCKET sock, struct sockaddr *daddr, int cnt, std::string *pckts)
{
	NetIF::IPAddr destip(daddr);
	int socklen = daddr->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
		sizeof(struct sockaddr_in6);

	for (int i = 0; i < cnt; i++) {
		ssize_t rc;
		UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__,
				   ">>> SSDP SEND to %s >>>\n%s\n",
				   destip.straddr().c_str(), pckts[i].c_str());
		rc = sendto(sock, pckts[i].c_str(), pckts[i].size(), 0, daddr, socklen);
					
		if (rc == -1) {
			char errorBuffer[ERROR_BUFFER_LEN];
			posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
					   "sendPackets: sendto: %s\n", errorBuffer);
			return UPNP_E_SOCKET_WRITE;

		}
	}
	return UPNP_E_SUCCESS;
}

struct SSDPPwrState {
	int PowerState;
	int SleepPeriod;
	int RegistrationState;
};
	
/* Creates a device notify or search reply packet. */
static void CreateServicePacket(
	SSDPDevMessageType msg_type, const char *nt, const char *usn,
	const char *location, int duration, std::string &packet, int AddressFamily,
	SSDPPwrState& pwr)
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
			"SERVER: " << get_sdk_info() << "\r\n" <<
#ifdef UPNP_HAVE_OPTSSDP
			"OPT: " << "\"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n" <<
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
			"SERVER: " << get_sdk_info() << "\r\n" <<
#ifdef UPNP_HAVE_OPTSSDP
			"OPT: " << "\"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n" <<
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

	str << "\r\n";
	packet = str.str();
}

static int DeviceAdvertisementOrShutdown(
	SOCKET sock, struct sockaddr *DestAddr,
	SSDPDevMessageType msgtype, const char *DevType, int RootDev,
	const char *Udn, const char *Location, int Duration, SSDPPwrState& pwr)
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
							DestAddr->sa_family, pwr);
	}
	/* both root and sub-devices need to send these two messages */
	CreateServicePacket(msgtype, Udn, Udn,
						Location, Duration, msgs[1], DestAddr->sa_family,
						pwr);
	rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
	if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
		goto error_handler;
	CreateServicePacket(msgtype, DevType, Mil_Usn,
						Location, Duration, msgs[2], DestAddr->sa_family, pwr);
	/* check error */
	if ((RootDev && msgs[0].empty()) || msgs[1].empty() || msgs[2].empty()) {
		goto error_handler;
	}
	/* send packets */
	if (RootDev) {
		/* send 3 msg types */
		ret_code = sendPackets(sock, DestAddr, 3, &msgs[0]);
	} else {		/* sub-device */
		/* send 2 msg types */
		ret_code = sendPackets(sock, DestAddr, 2, &msgs[1]);
	}

error_handler:
	return ret_code;
}

static int SendReply(
	SOCKET sock, struct sockaddr *DestAddr, const char *DevType, int RootDev,
	const char *Udn, const char *Location, int Duration, int ByType,
	SSDPPwrState& pwr)
{
	int ret_code = UPNP_E_OUTOF_MEMORY;
	std::string msgs[2];
	int num_msgs;
	char Mil_Usn[LINE_SIZE];
	int i;
	int rc = 0;
	int family = static_cast<int>(DestAddr->sa_family);

	if (RootDev) {
		/* one msg for root device */
		num_msgs = 1;

		rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
		if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
			goto error_handler;
		CreateServicePacket(MSGTYPE_REPLY, "upnp:rootdevice", Mil_Usn,
							Location, Duration, msgs[0], family, pwr);
	} else {
		/* two msgs for embedded devices */
		num_msgs = 1;

		/*NK: FIX for extra response when someone searches by udn */
		if (!ByType) {
			CreateServicePacket(MSGTYPE_REPLY, Udn, Udn, Location,
								Duration, msgs[0], family, pwr);
		} else {
			rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
			if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
				goto error_handler;
			CreateServicePacket(MSGTYPE_REPLY, DevType, Mil_Usn,
								Location, Duration, msgs[0], family, pwr);
		}
	}
	/* check error */
	for (i = 0; i < num_msgs; i++) {
		if (msgs[i].empty()) {
			goto error_handler;
		}
	}

	ret_code =	sendPackets(sock, DestAddr, num_msgs, msgs);

error_handler:
	return ret_code;
}

static int DeviceReply(
	SOCKET sock, struct sockaddr *DestAddr, const char *DevType, int RootDev,
	const char *Udn, const char *Location, int Duration, SSDPPwrState& pwr)
{
	std::string szReq[3];
	char Mil_Nt[LINE_SIZE], Mil_Usn[LINE_SIZE];
	int rc = 0;
	int family = static_cast<int>(DestAddr->sa_family);
	
	/* create 2 or 3 msgs */
	if (RootDev) {
		/* 3 replies for root device */
		upnp_strlcpy(Mil_Nt, "upnp:rootdevice", sizeof(Mil_Nt));
		rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::upnp:rootdevice", Udn);
		if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
			return UPNP_E_OUTOF_MEMORY;
		CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
							Location, Duration, szReq[0], family, pwr);
	}
	rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", Udn);
	if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Nt))
		return UPNP_E_OUTOF_MEMORY;
	rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s", Udn);
	if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
		return UPNP_E_OUTOF_MEMORY;
	CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,
						Location, Duration, szReq[1], family, pwr);
	rc = snprintf(Mil_Nt, sizeof(Mil_Nt), "%s", DevType);
	if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Nt))
		return UPNP_E_OUTOF_MEMORY;
	rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, DevType);
	if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
		return UPNP_E_OUTOF_MEMORY;
	CreateServicePacket(MSGTYPE_REPLY, Mil_Nt, Mil_Usn,	Location, Duration,
						szReq[2], family, pwr);
	/* check error */
	if ((RootDev && szReq[0].empty()) || szReq[1].empty() || szReq[2].empty()) {
		return UPNP_E_OUTOF_MEMORY;
	}
	/* send replies */
	if (RootDev) {
		return sendPackets(sock, DestAddr, 3, szReq);
	}
	return sendPackets(sock, DestAddr, 2, &szReq[1]);
}

static int ServiceSend(
	SOCKET sock, SSDPDevMessageType tp, struct sockaddr *DestAddr,
	const char *ServType, const char *Udn, const char *Location, int Duration,
	SSDPPwrState& pwr)
{
	char Mil_Usn[LINE_SIZE];
	std::string szReq[1];
	int rc = 0;

	rc = snprintf(Mil_Usn, sizeof(Mil_Usn), "%s::%s", Udn, ServType);
	if (rc < 0 || static_cast<unsigned int>(rc) >= sizeof(Mil_Usn))
		return UPNP_E_OUTOF_MEMORY;
	CreateServicePacket(tp, ServType, Mil_Usn, Location, Duration, szReq[0],
						DestAddr->sa_family, pwr);
	if (szReq[0].empty()) {
		return UPNP_E_OUTOF_MEMORY;

	}
	
	return sendPackets(sock, DestAddr, 1, szReq);
}

static int AdvertiseAndReplyOneDest(
	SOCKET sock, SSDPDevMessageType tp, UpnpDevice_Handle Hnd,
	enum SsdpSearchType SearchType, struct sockaddr *DestAddr, char *DeviceType,
	char *DeviceUDN, char *ServiceType, int Exp, const char *location)
{
	int retVal = UPNP_E_SUCCESS;
	int defaultExp = DEFAULT_MAXAGE;
	int NumCopy = 0;
	std::vector<const UPnPDeviceDesc*> alldevices;
	bool isNotify = (tp == MSGTYPE_ADVERTISEMENT || tp == MSGTYPE_SHUTDOWN);
	
	/* Use a read lock */
	HandleReadLock();
	struct Handle_Info *SInfo = nullptr;
	if (GetHandleInfo(Hnd, &SInfo) != HND_DEVICE) {
		HandleUnlock();
		return UPNP_E_INVALID_HANDLE;
	}
	defaultExp = SInfo->MaxAge;
	SSDPPwrState pwr {
		SInfo->PowerState, SInfo->SleepPeriod,SInfo->RegistrationState};

	// Store the root and embedded devices in a single vector for convenience
	alldevices.push_back(&SInfo->devdesc);
	for (const auto& dev : SInfo->devdesc.embedded) {
		alldevices.push_back(&dev);
	}
	/* send advertisements/replies */
	while (NumCopy == 0 || (isNotify && NumCopy < NUM_SSDP_COPY)) {
		if (NumCopy != 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(SSDP_PAUSE));
		NumCopy++;

		for (auto& devp : alldevices) {
			bool isroot = &devp == &(*alldevices.begin());
			const char *devType = devp->deviceType.c_str();
			const char *UDNstr = devp->UDN.c_str();
			if (isNotify) {
				DeviceAdvertisementOrShutdown(
					sock, DestAddr, tp, devType,  isroot, UDNstr,
					location, Exp, pwr);
			} else {
				switch (SearchType) {
				case SSDP_ALL:
					DeviceReply(sock, DestAddr, devType, isroot, UDNstr,
								location, defaultExp, pwr);
					break;
				case SSDP_ROOTDEVICE:
					if (isroot) {
						SendReply(sock, DestAddr, devType, 1, UDNstr,
								  location, defaultExp, 0, pwr);
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
						SendReply(sock, DestAddr, devType, 0, UDNstr, location,
								  defaultExp, 0, pwr);
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
							SendReply(sock, DestAddr, DeviceType, 0, UDNstr,
									  SInfo->LowerDescURL, defaultExp, 1, pwr);
						} else if (std::atoi(std::strrchr(DeviceType, ':') + 1)
								   == std::atoi(&devType[std::strlen(devType) - 1])) {
							UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
									   "DeviceType=%s/srchDevType=%s MATCH\n",
									   devType, DeviceType);
							SendReply(sock, DestAddr, DeviceType, 0,
									  UDNstr, location, defaultExp, 1, pwr);
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
				if (isNotify) {
					ServiceSend(sock, tp, DestAddr, servType, UDNstr, location,
								Exp, pwr);
				} else {
					switch (SearchType) {
					case SSDP_ALL:
						ServiceSend(sock, MSGTYPE_REPLY, DestAddr, servType,
									UDNstr, location, defaultExp, pwr);
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
									SendReply(sock, DestAddr, ServiceType, 0,
											  UDNstr, SInfo->LowerDescURL,
											  defaultExp, 1, pwr);
								} else if (
									std::atoi(std::strrchr(ServiceType, ':') + 1)
									== std::atoi(&servType[std::strlen(servType) - 1])) {
									UpnpPrintf(
										UPNP_DEBUG, SSDP, __FILE__, __LINE__,
										"ServiceTp=%s/searchServTp=%s MATCH\n",
										ServiceType, servType);
									SendReply(sock, DestAddr, ServiceType, 0,
											  UDNstr, location, defaultExp, 1,
											  pwr);
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


	UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__, "AdvertiseAndReply1 exit\n");
	HandleUnlock();
	return retVal;
}

int AdvertiseAndReply(SSDPDevMessageType tp, UpnpDevice_Handle Hnd,
					  enum SsdpSearchType SearchType,
					  struct sockaddr *repDestAddr, char *DeviceType,
					  char *DeviceUDN, char *ServiceType, int Exp)
{
	bool isNotify = (tp == MSGTYPE_ADVERTISEMENT || tp == MSGTYPE_SHUTDOWN);
	int ret = UPNP_E_SUCCESS;
	std::string lochost;
	SOCKET sock = -1;

	/* Use a read lock */
	HandleReadLock();
	struct Handle_Info *SInfo = nullptr;
	if (GetHandleInfo(Hnd, &SInfo) != HND_DEVICE) {
		HandleUnlock();
		return UPNP_E_INVALID_HANDLE;
	}
	std::string loctmpl = SInfo->DescURL;
	HandleUnlock();
	
	if (isNotify) {
		// Loop on our interfaces and addresses
		for (const auto& netif : g_netifs) {
			UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__,
					   "ssdp_device: mcast for %s\n", netif.getname().c_str());

			struct sockaddr_storage dss;
			auto destaddr = reinterpret_cast<struct sockaddr*>(&dss);

#ifdef UPNP_ENABLE_IPV6
			if (g_option_flags & UPNP_FLAG_IPV6) {
				ssdpMcastAddr(dss, AF_INET6);
				sock = createMulticastSocket6(netif.getindex(), lochost);
				if (sock < 0) {
					goto exitfunc;
				}

				std::string loc{loctmpl};
				std::string::size_type locpos = loc.find(g_HostForTemplate);
				if (locpos != std::string::npos) {
					loc = loc.replace(locpos, g_HostForTemplate.size(), lochost);
				}
				
				ret = AdvertiseAndReplyOneDest(
					sock, tp, Hnd, SearchType, destaddr, DeviceType,
					DeviceUDN, ServiceType, Exp, loc.c_str());
				
				if (ret != UPNP_E_SUCCESS) {
					UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
							   "SSDP dev: IPV6 SEND failed for %s\n",
							   netif.getname().c_str());
					goto exitfunc;
				}
				close(sock);
				sock = -1;
			}
#endif /* UPNP_ENABLE_IPV6 */

			ssdpMcastAddr(dss, AF_INET);
			auto addresses = netif.getaddresses();
			for (const auto& ipaddr : addresses.first) {
				std::string loc{loctmpl};
				if (ipaddr.family() == NetIF::IPAddr::Family::IPV4) {
					const struct sockaddr_storage& fss{ipaddr.getaddr()};
					sock = createMulticastSocket4(
						reinterpret_cast<const struct sockaddr_in*>(&fss),
						lochost);
					std::string::size_type locpos = loc.find(g_HostForTemplate);
					if (locpos != std::string::npos) {
						loc = loc.replace(
							locpos, g_HostForTemplate.size(), lochost);
					}
				} else {
					continue;
				}
				if (sock < 0) {
					goto exitfunc;
				}
				ret = AdvertiseAndReplyOneDest(
					sock, tp, Hnd, SearchType, destaddr, DeviceType,
					DeviceUDN, ServiceType, Exp, loc.c_str());
				close(sock);
				sock = -1;
			}
		}
	} else {
		sock = repDestAddr->sa_family == AF_INET ?
			createReplySocket4(
				reinterpret_cast<struct sockaddr_in*>(repDestAddr), lochost) :
			createReplySocket6(
				reinterpret_cast<struct sockaddr_in6*>(repDestAddr), lochost);
		if (sock < 0) {
			goto exitfunc;
		}
		std::string loc{loctmpl};
		std::string::size_type locpos = loc.find(g_HostForTemplate);
		if (locpos != std::string::npos) {
			loc = loc.replace(locpos, g_HostForTemplate.size(), lochost);
		}
		ret = AdvertiseAndReplyOneDest(
			sock, tp, Hnd, SearchType, repDestAddr, DeviceType,
			DeviceUDN, ServiceType, Exp, loc.c_str());
	}

exitfunc:
	if (ret != UPNP_E_SUCCESS) {
		char errorBuffer[ERROR_BUFFER_LEN];
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
				   "sendPackets: %s\n", errorBuffer);
		return UPNP_E_NETWORK_ERROR;
	}
	if (sock >= 0) 
		UpnpCloseSocket(sock);
	return ret;
}

#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_DEVICE_APIS */
