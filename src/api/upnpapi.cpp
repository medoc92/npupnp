/*******************************************************************************
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
 ******************************************************************************/

#include "config.h"

#include <string>
#include <algorithm>
#include <sstream>
#include <vector>
#include <utility>
#include <mutex>

#include "upnpapi.h"
#include "httputils.h"
#include "ssdplib.h"
#include "soaplib.h"
#include "ThreadPool.h"
#include "upnp_timeout.h"
#include "smallut.h"

/* Needed for GENA */
#include "gena.h"
#include "gena_sids.h"
#include "miniserver.h"
#include "service_table.h"

#ifdef INTERNAL_WEB_SERVER
#include "VirtualDir.h"
#include "webserver.h"
#endif /* INTERNAL_WEB_SERVER */

#include <sys/stat.h>

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
/* Do not include these files */
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_GETIFADDRS
#include <ifaddrs.h>
#endif
#endif

#ifndef IN6_IS_ADDR_GLOBAL
#define IN6_IS_ADDR_GLOBAL(a)										\
	((((__const uint32_t *) (a))[0] & htonl((uint32_t)0x70000000))	\
	 == htonl ((uint32_t)0x20000000))
#endif /* IS ADDR GLOBAL */

#ifndef IN6_IS_ADDR_ULA
#define IN6_IS_ADDR_ULA(a)											\
	((((__const uint32_t *) (a))[0] & htonl((uint32_t)0xfe000000))	\
	 == htonl ((uint32_t)0xfc000000))
#endif /* IS ADDR ULA */

/*! This structure is for virtual directory callbacks */
struct VirtualDirCallbacks virtualDirCallback;

#ifdef INCLUDE_CLIENT_APIS
/* Mutex to synchronize the subscription handling at the client side. */
std::mutex GlobalClientSubscribeMutex;
#endif /* INCLUDE_CLIENT_APIS */

// Mutex to synchronize handles (root device or control point
// handle). This used to be an rwlock but this was probably not worth
// the trouble given the small expected contention level */
std::mutex GlobalHndRWLock;

/*! Initialization mutex. */
std::mutex gSDKInitMutex;

/*! Send thread pool. */
ThreadPool gSendThreadPool;
/*! Global timer thread. */
TimerThread *gTimerThread;
/*! Receive thread pool. */
ThreadPool gRecvThreadPool;
/*! Mini server thread pool. */
ThreadPool gMiniServerThreadPool;
static std::vector<std::pair<ThreadPool *, const char *> > o_threadpools{
	{&gSendThreadPool, "Send thread pool"},
	{&gRecvThreadPool, "Receive thread pool"},
	{&gMiniServerThreadPool, "Mini server thread pool"}
};
																 
/*! Flag to indicate the state of web server */
WebServerState bWebServerState = WEB_SERVER_DISABLED;

/*! Static buffer to contain interface name. (extern'ed in upnp.h) */
char gIF_NAME[LINE_SIZE] = { '\0' };

/*! Static buffer to contain interface IPv4 address. (extern'ed in upnp.h) */
char gIF_IPV4[INET_ADDRSTRLEN] = { '\0' };

/*! Static buffer to contain interface IPv6 address. (extern'ed in upnp.h) */
char gIF_IPV6[INET6_ADDRSTRLEN] = { '\0' };

/*! Static buffer to contain interface ULA or GUA IPv6 address. (extern'ed in upnp.h) */
char gIF_IPV6_ULA_GUA[INET6_ADDRSTRLEN] = { '\0' };

/*! Contains interface index. (extern'ed in upnp.h) */
unsigned gIF_INDEX = (unsigned)-1;

/*! local IPv4 port for the mini-server */
unsigned short LOCAL_PORT_V4;

/*! local IPv6 port for the mini-server */
unsigned short LOCAL_PORT_V6;

/*! UPnP device and control point handle table	*/
#define NUM_HANDLE 200
static Handle_Info *HandleTable[NUM_HANDLE];

/*! Maximum content-length (in bytes) that the SDK will process on an incoming
 * packet. Content-Length exceeding this size will be not processed and
 * error 413 (HTTP Error Code) will be returned to the remote end point. */
size_t g_maxContentLength = DEFAULT_SOAP_CONTENT_LENGTH;

/*! Global variable to determines the maximum number of
 *	events which can be queued for a given subscription before events begin
 *	to be discarded. This limits the amount of memory used for a
 *	non-responding subscribed entity. */
int g_UpnpSdkEQMaxLen = MAX_SUBSCRIPTION_QUEUED_EVENTS;

/*! Global variable to determine the maximum number of 
 *	seconds which an event can spend on a subscription queue (waiting for the 
 *	event at the head of the queue to be communicated). This parameter will 
 *	have no effect in most situations with the default (low) value of 
 *	MAX_SUBSCRIPTION_QUEUED_EVENTS. However, if MAX_SUBSCRIPTION_QUEUED_EVENTS 
 *	is set to a high value, the AGE parameter will allow pruning the queue in 
 *	good conformance with the UPnP Device Architecture standard, at the 
 *	price of higher potential memory use. */
int g_UpnpSdkEQMaxAge = MAX_SUBSCRIPTION_EVENT_AGE;

/*! Global variable to denote the state of Upnp SDK == 0 if uninitialized,
 * == 1 if initialized. */
int UpnpSdkInit = 0;

/*! Global variable to denote the state of Upnp SDK client registration.
 * == 0 if unregistered, == 1 if registered. */
int UpnpSdkClientRegistered = 0;

/*! Global variable to denote the state of Upnp SDK IPv4 device registration.
 * == 0 if unregistered, == 1 if registered. */
int UpnpSdkDeviceRegisteredV4 = 0;

/*! Global variable to denote the state of Upnp SDK IPv6 device registration.
 * == 0 if unregistered, == 1 if registered. */
int UpnpSdkDeviceregisteredV6 = 0;

#ifdef UPNP_HAVE_OPTSSDP
/*! Global variable used in discovery notifications. */
Upnp_SID gUpnpSdkNLSuuid;
#endif /* UPNP_HAVE_OPTSSDP */

/* Find an appropriate interface (possibly specified in input) and set
 * the global IP addresses and interface names. The interface must fulfill 
 * these requirements:
 *  UP / Not LOOPBACK / Support MULTICAST / valid IPv4 or IPv6 address.
 *
 * We'll retrieve the following information from the interface:
 *  gIF_NAME -> Interface name (by input or found).
 *  gIF_IPV4 -> IPv4 address (if any).
 *  gIF_IPV6 -> IPv6 address (if any).
 *  gIF_IPV6_ULA_GUA -> ULA or GUA IPv6 address (if any)
 *  gIF_INDEX -> Interface index number. For v6 sin6_scope_id
*/
int UpnpGetIfInfo(const char *IfName)
{
	bool ifname_set{false};
	bool valid_addr_found{false};
	struct in_addr v4_addr;
	struct in6_addr v6_addr;
	memset(&v4_addr, 0, sizeof(v4_addr));
	memset(&v6_addr, 0, sizeof(v6_addr));
	/* Copy interface name, if it was provided. */
	if (IfName && *IfName) {
		if (strlen(IfName) > sizeof(gIF_NAME))
			return UPNP_E_INVALID_INTERFACE;
		upnp_strlcpy(gIF_NAME, IfName, sizeof(gIF_NAME));
		ifname_set = true;
	}

#ifdef WIN32
	/* ---------------------------------------------------- */
	/* WIN32 implementation will use the IpHlpAPI library. */
	/* ---------------------------------------------------- */
	PIP_ADAPTER_ADDRESSES adapts = NULL;
	PIP_ADAPTER_ADDRESSES adapts_item;
	PIP_ADAPTER_UNICAST_ADDRESS uni_addr;
	SOCKADDR *ip_addr;
	ULONG adapts_sz = 0;
	ULONG ret;

	/* Get Adapters addresses required size. */
	ret = GetAdaptersAddresses(
		AF_UNSPEC,  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL, adapts, &adapts_sz);
	if (ret != ERROR_BUFFER_OVERFLOW) {
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
				   "GetAdaptersAddresses: fail1\n");
		return UPNP_E_INIT;
	}
	/* Allocate enough memory. */
	adapts = (PIP_ADAPTER_ADDRESSES) malloc(adapts_sz);
	if (adapts == NULL) {
		return UPNP_E_OUTOF_MEMORY;
	}
	/* Do the call that will actually return the info. */
	ret = GetAdaptersAddresses(
		AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL, adapts, &adapts_sz);
	if (ret != ERROR_SUCCESS) {
		free(adapts);
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
				   "GetAdaptersAddresses: fail2\n");
		return UPNP_E_INIT;
	}
	adapts_item = adapts;
	while (adapts_item != NULL) {
		if (adapts_item->Flags & IP_ADAPTER_NO_MULTICAST) {
			adapts_item = adapts_item->Next;
			continue;
		}
		if (!ifname_set) {
			/* We have found a valid interface name. Keep it. */
#if 1 /*def UPNP_USE_MSVCPP*/
			/*
			 * Partial fix for VC - friendly name is wchar string,
			 * but currently gIF_NAME is char string. For now try
			 * to convert it, which will work with many (but not
			 * all) adapters. A full fix would require a lot of
			 * big changes (gIF_NAME to wchar string?).
			 */
			wcstombs(gIF_NAME, adapts_item->FriendlyName,
					 sizeof(gIF_NAME));
#else
			upnp_strlcpy(gIF_NAME, adapts_item->FriendlyName, sizeof(gIF_NAME));
#endif
			ifname_set = true;
		} else {
#if 1 /*def UPNP_USE_MSVCPP*/
			/*
			 * Partial fix for VC - friendly name is wchar string,
			 * but currently gIF_NAME is char string. For now try
			 * to convert it, which will work with many (but not
			 * all) adapters. A full fix would require a lot of
			 * big changes (gIF_NAME to wchar string?).
			 */
			char tmpIfName[LINE_SIZE] = { 0 };
			wcstombs(tmpIfName, adapts_item->FriendlyName, sizeof(tmpIfName));
			if (strncmp(gIF_NAME, tmpIfName, sizeof(gIF_NAME)) != 0) {
				/* This is not the interface we're looking for. */
				continue;
			}
#else
			if (strncmp(gIF_NAME, adapts_item->FriendlyName, sizeof(gIF_NAME))
				!= 0) {
				/* This is not the interface we're looking for. */
				continue;
			}
#endif
		}
		/* Loop thru this adapter's unicast IP addresses. */
		uni_addr = adapts_item->FirstUnicastAddress;
		while (uni_addr) {
			ip_addr = uni_addr->Address.lpSockaddr;
			switch (ip_addr->sa_family) {
			case AF_INET:
				memcpy(&v4_addr, &((struct sockaddr_in *)ip_addr)->sin_addr,
					   sizeof(v4_addr));
				valid_addr_found = true;
				break;
			case AF_INET6:
				/* Only keep IPv6 link-local addresses. */
				if (IN6_IS_ADDR_LINKLOCAL(
						&((struct sockaddr_in6 *)ip_addr)->sin6_addr)) {
					memcpy(&v6_addr,&((struct sockaddr_in6 *)ip_addr)->sin6_addr,
						   sizeof(v6_addr));
					valid_addr_found = true;
				}
				break;
			default:
				if (!valid_addr_found) {
					/* Address is not IPv4 or IPv6 and no valid
					    address has yet been found for this
					    interface. Discard interface name. */
					ifname_set = false;
				}
				break;
			}
			/* Next address. */
			uni_addr = uni_addr->Next;
		}
		if (valid_addr_found) {
			gIF_INDEX = adapts_item->IfIndex;
			break;
		}
		/* Next adapter. */
		adapts_item = adapts_item->Next;
	}
	free(adapts);
	
#elif defined(HAVE_GETIFADDRS)

	struct ifaddrs *ifap, *ifa;

	/* Get system interface addresses. */
	if (getifaddrs(&ifap) != 0) {
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__, "getifaddrs error\n");
		return UPNP_E_INIT;
	}
	/* cycle through available interfaces and their addresses. */
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/* Skip LOOPBACK interfaces, DOWN interfaces and interfaces that  */
		/* don't support MULTICAST. */
		if ((ifa->ifa_flags & IFF_LOOPBACK)
			|| (!(ifa->ifa_flags & IFF_UP))
			|| (!(ifa->ifa_flags & IFF_MULTICAST))) {
			continue;
		}
		if (ifname_set == 0) {
			upnp_strlcpy(gIF_NAME, ifa->ifa_name, sizeof(gIF_NAME));
			ifname_set = 1;
		} else {
			if (strncmp(gIF_NAME, ifa->ifa_name, sizeof(gIF_NAME))!= 0) {
				/* This is not the interface we're looking for. */
				continue;
			}
		}
		/* Keep interface addresses for later. */
		switch (ifa->ifa_addr->sa_family) {
		case AF_INET:
			memcpy(&v4_addr, &((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr,
				   sizeof(v4_addr));
			valid_addr_found = 1;
			break;
		case AF_INET6:
			/* Only keep IPv6 link-local addresses. */
			if (IN6_IS_ADDR_LINKLOCAL(
					&((struct sockaddr_in6 *)(ifa->ifa_addr))->sin6_addr)) {
				memcpy(&v6_addr,
					   &((struct sockaddr_in6 *)(ifa->ifa_addr))->sin6_addr,
					   sizeof(v6_addr));
				valid_addr_found = 1;
			}
			break;
		default:
			if (valid_addr_found == 0) {
				/* Address is not IPv4 or IPv6 and no valid address has	 */
				/* yet been found for this interface. Discard interface name. */
				ifname_set = 0;
			}
			break;
		}
	}
	freeifaddrs(ifap);
	gIF_INDEX = if_nametoindex(gIF_NAME);
#else
#error Neither windows nor getifaddrs. Lift the old linux code from pupnp?
#endif

	/* Failed to find a valid interface, or valid address. */
	if (!ifname_set || !valid_addr_found) {
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
				   "No adapter with usable IP addresses.\n");
		return UPNP_E_INVALID_INTERFACE;
	}
	inet_ntop(AF_INET, &v4_addr, gIF_IPV4, sizeof(gIF_IPV4));
	inet_ntop(AF_INET6, &v6_addr, gIF_IPV6, sizeof(gIF_IPV6));

	UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
			   "Ifname=%s, index=%d, v4=%s, v6=%s, ULA/GUA v6=%s\n",
			   gIF_NAME, gIF_INDEX, gIF_IPV4, gIF_IPV6, gIF_IPV6_ULA_GUA);

	return UPNP_E_SUCCESS;
}

/* This is the old version for supposedly deprecated UpnpInit: v4 only and 
   if the interface is specified, it's done by IP address */
int getlocalhostname(char *out, size_t out_len)
{
	int ret = UPNP_E_SUCCESS;
	char tempstr[INET_ADDRSTRLEN];
	const char *p = NULL;
	
#ifdef WIN32
	struct hostent *h = NULL;
	struct sockaddr_in LocalAddr;

	memset(&LocalAddr, 0, sizeof(LocalAddr));

	gethostname(out, out_len);
	out[out_len-1] = 0;
	h = gethostbyname(out);
	if (h != NULL) {
		memcpy(&LocalAddr.sin_addr, h->h_addr_list[0], 4);
		p = inet_ntop(AF_INET, &LocalAddr.sin_addr, tempstr, sizeof(tempstr));
		if (p) {
			upnp_strlcpy(out, p, out_len);
		} else {
			UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
						"getlocalhostname: inet_ntop error\n");
			ret = UPNP_E_INIT;
		}
	} else {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
					"getlocalhostname: gethostbyname error\n");
		ret = UPNP_E_INIT;
	}

#elif defined(HAVE_GETIFADDRS)
	struct ifaddrs *ifap, *ifa;

	if (getifaddrs(&ifap) != 0) {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "DiscoverInterfaces: getifaddrs error\n");
		return UPNP_E_INIT;
	}

	/* cycle through available interfaces */
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/* Skip loopback, point-to-point and down interfaces, 
		 * except don't skip down interfaces
		 * if we're trying to get a list of configurable interfaces. */
		if ((ifa->ifa_flags & IFF_LOOPBACK) || (!(ifa->ifa_flags & IFF_UP))) {
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* We don't want the loopback interface. */
			if (((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr.s_addr ==
				htonl(INADDR_LOOPBACK)) {
				continue;
			}
			p = inet_ntop(AF_INET,
						  &((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr,
						  tempstr, sizeof(tempstr));
			if (p) {
				upnp_strlcpy(out, p, out_len);
			} else {
				UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
						   "getlocalhostname: inet_ntop error\n");
				ret = UPNP_E_INIT;
			}
			break;
		}
	}
	freeifaddrs(ifap);

	ret = ifa ? UPNP_E_SUCCESS : UPNP_E_INIT;
#else
#error Neither windows nor getifaddrs. Lift the old linux code from pupnp?
#endif
	return ret;
}

/* Initializes the Windows Winsock library. */
#ifdef _WIN32
static int WinsockInit(void)
{
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		return UPNP_E_INIT_FAILED;
	}
	/* Confirm that the WinSock DLL supports 2.2.
	 * Note that if the DLL supports versions greater
	 * than 2.2 in addition to 2.2, it will still return
	 * 2.2 in wVersion since that is the version we requested. */
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		WSACleanup();
		return UPNP_E_INIT_FAILED; 
	}
	return UPNP_E_SUCCESS;
}
#endif /* _WIN32 */

/* Initializes the global threadm pools used by the UPnP SDK. */
static int UpnpInitThreadPools(void)
{
	int ret = UPNP_E_SUCCESS;
	ThreadPoolAttr attr;

	attr.maxThreads = MAX_THREADS;
	attr.minThreads =  MIN_THREADS;
	attr.stackSize = THREAD_STACK_SIZE;
	attr.jobsPerThread = JOBS_PER_THREAD;
	attr.maxIdleTime = THREAD_IDLE_TIME;
	attr.maxJobsTotal = MAX_JOBS_TOTAL;

	for (const auto& entry : o_threadpools) {
		if (entry.first->start(&attr) != UPNP_E_SUCCESS) {
			ret = UPNP_E_INIT_FAILED;
			break;
		}
	}

	if (ret != UPNP_E_SUCCESS) {
		UpnpSdkInit = 0;
		UpnpFinish();
	}
	return ret;
}


/*!
 * \brief Performs the initial steps in initializing the UPnP SDK.
 *
 *	\li Winsock library is initialized for the process (Windows specific).
 *	\li The logging (for debug messages) is initialized.
 *	\li Mutexes, Handle table and thread pools are allocated and initialized.
 *	\li Callback functions for SOAP and GENA are set, if they're enabled.
 *	\li The SDK timer thread is initialized.
 *
 * \return UPNP_E_SUCCESS on success.
 */
static int UpnpInitPreamble(void)
{
	int retVal = UPNP_E_SUCCESS;

#ifdef _WIN32
	if (WinsockInit() != UPNP_E_SUCCESS) {
		return retVal;
	}
#endif
	
	/* needed by SSDP or other parts. */
	srand((unsigned int)time(NULL));

	/* Initialize debug output. */
	retVal = UpnpInitLog();
	if (retVal != UPNP_E_SUCCESS) {
		/* UpnpInitLog does not return a valid UPNP_E_*. */
		return UPNP_E_INIT_FAILED;
	}

#ifdef UPNP_HAVE_OPTSSDP
	/* Create the NLS uuid. */
	// The gena sid generator is quite ok for this.
	snprintf(gUpnpSdkNLSuuid, sizeof(gUpnpSdkNLSuuid),
			 "uuid:%s", gena_sid_uuid().c_str());
#endif /* UPNP_HAVE_OPTSSDP */

	/* Initializes the handle list. */
	HandleLock();
	memset(HandleTable, 0, sizeof(HandleTable));
	HandleUnlock();

	/* Initialize SDK global thread pools. */
	retVal = UpnpInitThreadPools();
	if (retVal != UPNP_E_SUCCESS) {
		return retVal;
	}

#ifdef INCLUDE_DEVICE_APIS
#if EXCLUDE_SOAP == 0
	SetSoapCallback(soap_device_callback);
#endif
#endif /* INCLUDE_DEVICE_APIS */

#ifdef INTERNAL_WEB_SERVER
#if EXCLUDE_GENA == 0
	SetGenaCallback(genaCallback);
#endif
#endif /* INTERNAL_WEB_SERVER */

	/* Initialize the SDK timer thread. */
	gTimerThread = new TimerThread(&gSendThreadPool);
	if (nullptr == gTimerThread) {
		UpnpFinish();
		return UPNP_E_INIT_FAILED;
	}

	return UPNP_E_SUCCESS;
}


/*!
 * \brief Finishes initializing the UPnP SDK.
 *	\li The MiniServer is started, if enabled.
 *	\li The WebServer is started, if enabled.
 * 
 * \return UPNP_E_SUCCESS on success or	 UPNP_E_INIT_FAILED if a mutex could not
 *	be initialized.
 */
static int UpnpInitStartServers(
	/*! [in] Local Port to listen for incoming connections. */
	unsigned short DestPort)
{
#if EXCLUDE_MINISERVER == 0 || EXCLUDE_WEB_SERVER == 0
	int retVal = 0;
#endif

#if EXCLUDE_MINISERVER == 0
	LOCAL_PORT_V4 = DestPort;
	LOCAL_PORT_V6 = DestPort;
	retVal = StartMiniServer(&LOCAL_PORT_V4, &LOCAL_PORT_V6);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
				   "Miniserver start error");
		UpnpFinish();
		return retVal;
	}
#endif

#if EXCLUDE_WEB_SERVER == 0
	retVal = UpnpEnableWebserver(WEB_SERVER_ENABLED);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpFinish();
		return retVal;
	}
#endif

	return UPNP_E_SUCCESS;
}

static int upnpInitCommonV4V6(bool dov6, const char *HostIP,
							  const char *ifNameForV6, unsigned short DestPort)
{
	int retVal = UPNP_E_SUCCESS;

	std::unique_lock<std::mutex> lck(gSDKInitMutex);

	/* Check if we're already initialized. */
	if (UpnpSdkInit == 1) {
		retVal = UPNP_E_INIT;
		goto exit_function;
	}

	/* Perform initialization preamble. */
	retVal = UpnpInitPreamble();
	if (retVal != UPNP_E_SUCCESS) {
		goto exit_function;
	}

	UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
			   "UpnpInit: HostIP=%s, DestPort=%d.\n", 
			   HostIP ? HostIP : "", (int)DestPort);

	if (dov6) {
		/* Retrieve interface information (Addresses, index, etc). */
		retVal = UpnpGetIfInfo(ifNameForV6);
		if (retVal != UPNP_E_SUCCESS) {
			goto exit_function;
		}
	} else {
		/* Verify HostIP, if provided, or find it ourselves. */
		if (HostIP != NULL) {
			upnp_strlcpy(gIF_IPV4, HostIP, sizeof(gIF_IPV4));
		} else {
			if (getlocalhostname(gIF_IPV4, sizeof(gIF_IPV4)) != UPNP_E_SUCCESS){
				retVal = UPNP_E_INIT_FAILED;
				goto exit_function;
			}
		}
	}
	
	UpnpSdkInit = 1;

	/* Finish initializing the SDK. */
	retVal = UpnpInitStartServers(DestPort);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpSdkInit = 0;
		goto exit_function;
	}

	UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
			   "UpnPInit output: Host Ip: %s Host Port: %d\n", gIF_IPV4,
			   (int)LOCAL_PORT_V4);

exit_function:
	return retVal;
}

int UpnpInit(const char *HostIP, unsigned short DestPort)
{
	return upnpInitCommonV4V6(false, HostIP, nullptr, DestPort);
}

#ifdef UPNP_ENABLE_IPV6
int UpnpInit2(const char *IfName, unsigned short DestPort)
{
	return upnpInitCommonV4V6(true, nullptr, IfName, DestPort);
}
#endif

#ifdef DEBUG
/*!
 * \brief Prints thread pool statistics.
 */
void PrintThreadPoolStats(
	/*! [in] The thread pool. */
	ThreadPool *tp,
	/*! [in] The file name that called this function, use the macro
	 * __FILE__. */
	const char *DbgFileName,
	/*! [in] The line number that the function was called, use the macro
	 * __LINE__. */
	int DbgLineNo,
	/*! [in] The message. */
	const char *msg)
{
	ThreadPoolStats stats;
	tp->getStats(&stats);
	UpnpPrintf(UPNP_INFO, API, DbgFileName, DbgLineNo,
			   "%s\n"
			   "High Jobs pending: %d\n"
			   "Med Jobs Pending: %d\n"
			   "Low Jobs Pending: %d\n"
			   "Average wait in High Q in milliseconds: %lf\n"
			   "Average wait in Med Q in milliseconds: %lf\n"
			   "Average wait in Low Q in milliseconds: %lf\n"
			   "Max Threads Used: %d\n"
			   "Worker Threads: %d\n"
			   "Persistent Threads: %d\n"
			   "Idle Threads: %d\n"
			   "Total Threads: %d\n"
			   "Total Work Time: %lf\n"
			   "Total Idle Time: %lf\n",
			   msg,
			   stats.currentJobsHQ,
			   stats.currentJobsMQ,
			   stats.currentJobsLQ,
			   stats.avgWaitHQ,
			   stats.avgWaitMQ,
			   stats.avgWaitLQ,
			   stats.maxThreads,
			   stats.workerThreads,
			   stats.persistentThreads,
			   stats.idleThreads,
			   stats.totalThreads,
			   stats.totalWorkTime,
			   stats.totalIdleTime);
}
#else
static UPNP_INLINE void PrintThreadPoolStats(
	ThreadPool *, const char *, int, const char *)
{
	return;
}
#endif /* DEBUG */

int UpnpFinish(void)
{
#ifdef INCLUDE_DEVICE_APIS
	UpnpDevice_Handle device_handle;
#endif
#ifdef INCLUDE_CLIENT_APIS
	UpnpClient_Handle client_handle;
#endif
	struct Handle_Info *temp;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;

#ifdef INCLUDE_DEVICE_APIS
	while (GetDeviceHandleInfo(
			   0, AF_INET, &device_handle, &temp) ==  HND_DEVICE) {
		UpnpUnRegisterRootDevice(device_handle);
	}
	while (GetDeviceHandleInfo(
			   0, AF_INET6, &device_handle, &temp) == HND_DEVICE) {
		UpnpUnRegisterRootDevice(device_handle);
	}
#endif
#ifdef INCLUDE_CLIENT_APIS
	switch (GetClientHandleInfo(&client_handle, &temp)) {
	case HND_CLIENT:
		UpnpUnRegisterClient(client_handle);
		break;
	default:
		break;
	}
#endif
	gTimerThread->shutdown();
	delete gTimerThread;
#if EXCLUDE_MINISERVER == 0
	StopMiniServer();
#endif
#if EXCLUDE_WEB_SERVER == 0
	web_server_destroy();
#endif
	for (const auto& entry : o_threadpools) {
		entry.first->shutdown();
		PrintThreadPoolStats(entry.first, __FILE__, __LINE__,
							 entry.second);
	}
	/* remove all virtual dirs */
	UpnpRemoveAllVirtualDirs();
	UpnpSdkInit = 0;
	UpnpCloseLog();

	return UPNP_E_SUCCESS;
}

unsigned short UpnpGetServerPort(void)
{
	if (UpnpSdkInit != 1)
		return 0u;

	return LOCAL_PORT_V4;
}

#ifdef UPNP_ENABLE_IPV6
unsigned short UpnpGetServerPort6(void)
{
	if (UpnpSdkInit != 1)
		return 0u;

	return LOCAL_PORT_V6;
}
#endif

const char *UpnpGetServerIpAddress(void)
{
	if (UpnpSdkInit != 1)
		return NULL;

	return gIF_IPV4;
}

const char *UpnpGetServerIp6Address(void)
{
	if (UpnpSdkInit != 1)
		return NULL;

	return gIF_IPV6;
}

const char *UpnpGetServerUlaGuaIp6Address(void)
{
	if (UpnpSdkInit != 1)
		return NULL;

	return gIF_IPV6_ULA_GUA;
}

/*!
 * \brief Get a free handle.
 *
 * \return On success, an integer greater than zero or UPNP_E_OUTOF_HANDLE on
 *	failure.
 */
static int GetFreeHandle()
{
	/* Handle 0 is not used as NULL translates to 0 when passed as a handle */
	for (int i = 1; i < NUM_HANDLE; i++) {
		if (HandleTable[i] == NULL) {
			return i;
		}
	}
	return UPNP_E_OUTOF_HANDLE;
}

/*!
 * \brief Free handle.
 *
 * \return UPNP_E_SUCCESS if successful or UPNP_E_INVALID_HANDLE if not
 */
static int FreeHandle(int handleindex)
{
	if (handleindex >= 1 && handleindex < NUM_HANDLE) {
		if (HandleTable[handleindex] != NULL) {
			delete HandleTable[handleindex];
			HandleTable[handleindex] = NULL;
			return UPNP_E_SUCCESS;
		}
	}
	return UPNP_E_INVALID_HANDLE;
}

static int checkLockHandle(Upnp_Handle_Type tp, int Hnd,
						   struct Handle_Info **HndInfo, bool readlock=false)
{
	if (readlock) {
		HandleReadLock();
	} else {
		HandleLock();
	}
	Upnp_Handle_Type actualtp = GetHandleInfo(Hnd, HndInfo);
	if (actualtp == HND_INVALID || (tp != HND_INVALID && tp != actualtp)) {
		HandleUnlock();
		return HND_INVALID;
	}
	return actualtp;
}

#ifdef INCLUDE_DEVICE_APIS
static int GetDescDocumentAndURL(
	Upnp_DescType descriptionType, char *description, int config_baseURL,
	int AddressFamily, UPnPDeviceDesc& desc, char descURL[LINE_SIZE]);

int UpnpRegisterRootDeviceAllForms(
	Upnp_DescType descriptionType,
	const char *description_const,
	size_t,	  /* buflen, ignored */
	int config_baseURL,
	Upnp_FunPtr Fun,
	const void *Cookie,
	UpnpDevice_Handle *Hnd,
	int AddressFamily,
	const char *LowerDescUrl)
{
	struct Handle_Info *HInfo = NULL;
	int retVal = 0;
#if EXCLUDE_GENA == 0
	int hasServiceTable = 0;
#endif /* EXCLUDE_GENA */
	char *description = (char *)description_const;

	HandleLock();

	UpnpPrintf(UPNP_INFO,API,__FILE__,__LINE__, "UpnpRegisterRootDeviceAllF\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (Hnd == NULL || Fun == NULL || description == NULL || *description == 0 ||
		(AddressFamily != AF_INET && AddressFamily != AF_INET6)) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}

	*Hnd = GetFreeHandle();
	if (*Hnd == UPNP_E_OUTOF_HANDLE) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}

	HInfo = new Handle_Info;
	if (HInfo == NULL) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}
	HandleTable[*Hnd] = HInfo;

	/* prevent accidental removal of a non-existent alias */
	HInfo->aliasInstalled = 0;

	retVal = GetDescDocumentAndURL(
		descriptionType, description, config_baseURL, AF_INET, 
		HInfo->devdesc, HInfo->DescURL);
	if (retVal != UPNP_E_SUCCESS) {
		FreeHandle(*Hnd);
		goto exit_function;
	}

	if (LowerDescUrl == NULL)
		upnp_strlcpy(HInfo->LowerDescURL, HInfo->DescURL,
					 sizeof(HInfo->LowerDescURL));
	else
		upnp_strlcpy(HInfo->LowerDescURL, LowerDescUrl,
					 sizeof(HInfo->LowerDescURL));
	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
			   "Root Device URL for legacy CPs: %s\n", HInfo->LowerDescURL);
	HInfo->aliasInstalled = config_baseURL != 0;
	HInfo->HType = HND_DEVICE;
	HInfo->Callback = Fun;
	HInfo->Cookie = (char *)Cookie;
	HInfo->MaxAge = DEFAULT_MAXAGE;
	HInfo->MaxSubscriptions = UPNP_INFINITE;
	HInfo->MaxSubscriptionTimeOut = UPNP_INFINITE;
	HInfo->DeviceAf = AddressFamily;

	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
			   "UpnpRegisterRootDeviceAllForms: Ok Description at : %s\n",
			   HInfo->DescURL);

#if EXCLUDE_GENA == 0
	/*
	 * GENA SET UP
	 */
	hasServiceTable = getServiceTable(HInfo->devdesc, &HInfo->ServiceTable,
									  HInfo->DescURL);
	if (hasServiceTable) {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "UpnpRegisterRootDeviceAllForms: GENA services:\n");
		printServiceTable(&HInfo->ServiceTable, UPNP_ALL, API);
	} else {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "\nUpnpRegisterRootDeviceAF: no services\n");
	}
#endif /* EXCLUDE_GENA */

	switch (AddressFamily) {
	case AF_INET:
		UpnpSdkDeviceRegisteredV4 = 1;
		break;
	default:
		UpnpSdkDeviceregisteredV6 = 1;
	}

	retVal = UPNP_E_SUCCESS;

exit_function:
	HandleUnlock();
	return retVal;
}

int UpnpRegisterRootDevice(
	const char *DescUrl, Upnp_FunPtr Fun, const void *Cookie,
	UpnpDevice_Handle *Hnd)
{
	return UpnpRegisterRootDeviceAllForms(
		UPNPREG_URL_DESC, DescUrl, 0, 0, Fun, Cookie, Hnd, AF_INET, nullptr);
}

int UpnpRegisterRootDevice3(
	const char *DescUrl, Upnp_FunPtr Fun, const void *Cookie,
	UpnpDevice_Handle *Hnd,	int AddressFamily)
{
	return UpnpRegisterRootDeviceAllForms(
		UPNPREG_URL_DESC, DescUrl, 0, 0, Fun, Cookie, Hnd, AddressFamily, NULL);
}

int UpnpRegisterRootDevice2(
	Upnp_DescType descriptionType, const char *description_const,
	size_t,	int config_baseURL,	Upnp_FunPtr Fun, const void *Cookie,
	UpnpDevice_Handle *Hnd)
{
	return UpnpRegisterRootDeviceAllForms(
		descriptionType, description_const, 0, config_baseURL, Fun,
		Cookie,	Hnd, AF_INET, nullptr);
}

int UpnpRegisterRootDevice4(
	const char *DescUrl, Upnp_FunPtr Fun, const void *Cookie,
	UpnpDevice_Handle *Hnd, int AddressFamily, const char *LowerDescUrl)
{
	return UpnpRegisterRootDeviceAllForms(
		UPNPREG_URL_DESC, DescUrl, 0, 0, Fun, Cookie, Hnd, AddressFamily,
		LowerDescUrl);
}

int UpnpUnRegisterRootDevice(UpnpDevice_Handle Hnd)
{
	return UpnpUnRegisterRootDeviceLowPower(Hnd, -1, -1, -1);
}

int UpnpUnRegisterRootDeviceLowPower(UpnpDevice_Handle Hnd, int PowerState,
									 int SleepPeriod, int RegistrationState)
{
	int retVal = 0;
	struct Handle_Info *HInfo = NULL;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;

#if EXCLUDE_GENA == 0
	if (genaUnregisterDevice(Hnd) != UPNP_E_SUCCESS)
		return UPNP_E_INVALID_HANDLE;
#endif

	if (checkLockHandle(HND_INVALID, Hnd, &HInfo) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	HInfo->PowerState = PowerState;
	if (SleepPeriod < 0)
		SleepPeriod = -1;
	HInfo->SleepPeriod = SleepPeriod;
	HInfo->RegistrationState = RegistrationState;
	HandleUnlock();

#if EXCLUDE_SSDP == 0
	retVal = AdvertiseAndReply(
		-1, Hnd, (enum SsdpSearchType)0, NULL, NULL, NULL, NULL, HInfo->MaxAge);
#endif

	if (checkLockHandle(HND_INVALID, Hnd, &HInfo) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	switch (HInfo->DeviceAf) {
	case AF_INET:
		UpnpSdkDeviceRegisteredV4 = 0;
		break;
	case AF_INET6:
		UpnpSdkDeviceregisteredV6 = 0;
		break;
	default:
		break;
	}
	FreeHandle(Hnd);
	HandleUnlock();

	return retVal;
}
#endif /* INCLUDE_DEVICE_APIS */

#ifdef INCLUDE_CLIENT_APIS
int UpnpRegisterClient(Upnp_FunPtr Fun, const void *Cookie,
					   UpnpClient_Handle *Hnd)
{
	struct Handle_Info *HInfo;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;

	if (Fun == NULL || Hnd == NULL)
		return UPNP_E_INVALID_PARAM;

	HandleLock();
	if (UpnpSdkClientRegistered) {
		HandleUnlock();
		return UPNP_E_ALREADY_REGISTERED;
	}
	if ((*Hnd = GetFreeHandle()) == UPNP_E_OUTOF_HANDLE) {
		HandleUnlock();
		return UPNP_E_OUTOF_MEMORY;
	}
	HInfo = new Handle_Info;
	if (HInfo == NULL) {
		HandleUnlock();
		return UPNP_E_OUTOF_MEMORY;
	}
	HInfo->HType = HND_CLIENT;
	HInfo->Callback = Fun;
	HInfo->Cookie = (char *)Cookie;
#ifdef INCLUDE_DEVICE_APIS
	HInfo->MaxAge = 0;
	HInfo->MaxSubscriptions = UPNP_INFINITE;
	HInfo->MaxSubscriptionTimeOut = UPNP_INFINITE;
#endif
	HandleTable[*Hnd] = HInfo;
	UpnpSdkClientRegistered = 1;
	HandleUnlock();

	return UPNP_E_SUCCESS;
}

int UpnpUnRegisterClient(UpnpClient_Handle Hnd)
{
	struct Handle_Info *HInfo;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;

	HandleLock();
	if (!UpnpSdkClientRegistered) {
		HandleUnlock();
		return UPNP_E_INVALID_HANDLE;
	}
	HandleUnlock();

#if EXCLUDE_GENA == 0
	if (genaUnregisterClient(Hnd) != UPNP_E_SUCCESS)
		return UPNP_E_INVALID_HANDLE;
#endif
	if (checkLockHandle(HND_INVALID, Hnd, &HInfo) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	/* clean up search list */
	for (auto it = HInfo->SsdpSearchList.begin();
		 it != HInfo->SsdpSearchList.end(); it++) {
		if (*it) {
			delete *it;
		}
	}
	HInfo->SsdpSearchList.clear();

	FreeHandle(Hnd);
	UpnpSdkClientRegistered = 0;
	HandleUnlock();

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */

#ifdef INCLUDE_DEVICE_APIS
#ifdef INTERNAL_WEB_SERVER

static std::string basename(const std::string& name)
{
	std::string::size_type slash = name.find_last_of("/");
	if (slash == std::string::npos) {
		return name;
	} else {
		return name.substr(slash+1);
	}
}
/* Read file contents to allocated buffer (caller must free) */
static int readFile(const char *path, char **data, time_t *modtime)
{
	struct stat st;
	char *buffer{nullptr};
	int ret = UPNP_E_SUCCESS;
	size_t num_read;

	*data = nullptr;
	if (stat(path, &st) != 0) {
		return UPNP_E_FILE_NOT_FOUND;
	}
	*modtime = st.st_mtime;
	FILE *fp = fopen(path, "rb");
	if (nullptr == fp)
		return UPNP_E_FILE_NOT_FOUND;
	buffer = (char *)malloc(st.st_size+1);
	if (nullptr == buffer) {
		ret = UPNP_E_OUTOF_MEMORY;
		goto out;
	}
	num_read = fread(buffer, 1, st.st_size, fp);
	if (num_read != (size_t)st.st_size) {
		ret = UPNP_E_FILE_READ_ERROR;
		goto out;
	}
	buffer[st.st_size + 1] = 0;
	*data = buffer;
out:
	if (fp)
		fclose(fp);
	if (ret != UPNP_E_SUCCESS && buffer) {
		free(buffer);
	}
	return ret;
}

static std::string descurl(int AddressFamily, const std::string& nm)
{
	std::ostringstream url;
	url << "http://";
	if (AddressFamily == AF_INET) {
		url << gIF_IPV4 << ":" << LOCAL_PORT_V4;
	} else {
		url << gIF_IPV6 << ":" << LOCAL_PORT_V6;
	}
	url << "/" << nm;
	return url.str();
}
	
static int GetDescDocumentAndURL(
	Upnp_DescType descriptionType,
	char *description,
	int config_baseURL,
	int AddressFamily,
	UPnPDeviceDesc& desc,
	char descURL[LINE_SIZE])
{
	int retVal = 0;
	if (!description || !*description)
		return UPNP_E_INVALID_PARAM;

	/* We do not support an URLBase set inside the description document. 
	   This was advised against in upnp 1.0 and forbidden in 1.1 */
	std::string localurl;
	std::string simplename;
	std::string descdata;
	time_t modtime = time(0);
	switch (descriptionType) {
	case UPNPREG_URL_DESC:
	{
		if (strlen(description) > LINE_SIZE - 1) {
			return UPNP_E_URL_TOO_BIG;
		}
		upnp_strlcpy(descURL, description, LINE_SIZE);
		char *descstr;
		retVal = UpnpDownloadUrlItem(description, &descstr, 0);
		if (retVal != UPNP_E_SUCCESS) {
			UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
					   "UpnpRegisterRootDevice: error downloading doc: %d\n",
					   retVal);
			return retVal;
		}
		desc = UPnPDeviceDesc(description, descstr);
		free(descstr);
	}
	break;
	case UPNPREG_FILENAME_DESC:
	{
		char *descstr{nullptr};
		retVal = readFile(description, &descstr, &modtime);
		if (retVal == UPNP_E_SUCCESS) {
			return retVal;
		}
		descdata = descstr;
		free(descstr);
		simplename = basename(std::string(description));
		localurl = descurl(AddressFamily, simplename);
	}
	break;
	case UPNPREG_BUF_DESC:
	{
		simplename = "description.xml";
		localurl = descurl(AddressFamily, "description.xml");
		descdata = description;
	}
	break;
	}

	if (!localurl.empty()) {
		upnp_strlcpy(descURL, localurl.c_str(), LINE_SIZE);
		desc = UPnPDeviceDesc(localurl, descdata);
		if (desc.ok) {
			web_server_set_localdoc(std::string("/") + simplename,
									descdata, modtime);
		}
	}

	if (!desc.ok) {
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
				   "UpnpRegisterRootDevice: description parse failed\n");
		return UPNP_E_INVALID_DESC;
	}
	return UPNP_E_SUCCESS;
}


#else /* INTERNAL_WEB_SERVER */ /* no web server */
static int GetDescDocumentAndURL(
	Upnp_DescType descriptionType,
	char *description,
	int config_baseURL,
	int AddressFamily,
	UPnPDeviceDesc& desc,
	char descURL[LINE_SIZE])
{
	int retVal = 0;

	if (descriptionType != (enum Upnp_DescType_e)UPNPREG_URL_DESC ||
		config_baseURL) {
		return UPNP_E_NO_WEB_SERVER;
	}

	if (description == NULL) {
		return UPNP_E_INVALID_PARAM;
	}

	if (strlen(description) > LINE_SIZE - (size_t)1) {
		return UPNP_E_URL_TOO_BIG;
	}
	upnp_strlcpy(descURL, description, LINE_SIZE);
	char *descstr;
	retVal = UpnpDownloadUrlItem(description, &descstr, 0);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "UpnpRegisterRootDevice: error downloading doc: %d\n",
				   retVal);
		return retVal;
	}
	desc = UPnPDeviceDesc(description, descstr);
	free(descstr);

	return UPNP_E_SUCCESS;
}
#endif /* INTERNAL_WEB_SERVER */
#endif /* INCLUDE_DEVICE_APIS */


/*******************************************************************************
 *
 *									SSDP interface
 *
 ******************************************************************************/


#ifdef INCLUDE_DEVICE_APIS
#if EXCLUDE_SSDP == 0

int UpnpSendAdvertisement(UpnpDevice_Handle Hnd, int Exp)
{
	return UpnpSendAdvertisementLowPower (Hnd, Exp, -1, -1, -1);
}

void thread_autoadvertise(void *input)
{
	upnp_timeout *event = (upnp_timeout *)input;

	UpnpSendAdvertisement(event->handle, *((int *)event->Event));
}

int UpnpSendAdvertisementLowPower(
	UpnpDevice_Handle Hnd, int Exp,
	int PowerState, int SleepPeriod, int RegistrationState)
{
	struct Handle_Info *SInfo = NULL;
	int retVal = 0,
		*ptrMx;
	upnp_timeout *adEvent;

	if(UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}

	if (checkLockHandle(HND_DEVICE, Hnd, &SInfo) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	if(Exp < 1)
		Exp = DEFAULT_MAXAGE;
	if(Exp <= AUTO_ADVERTISEMENT_TIME * 2)
		Exp = (AUTO_ADVERTISEMENT_TIME + 1) * 2;
	SInfo->MaxAge = Exp;
	SInfo->PowerState = PowerState;
	if (SleepPeriod < 0)
		SleepPeriod = -1;
	SInfo->SleepPeriod = SleepPeriod;
	SInfo->RegistrationState = RegistrationState;
	HandleUnlock();
	retVal = AdvertiseAndReply(1, Hnd, (enum SsdpSearchType)0,
							   NULL, NULL, NULL, NULL, Exp);

	if(retVal != UPNP_E_SUCCESS)
		return retVal;
	ptrMx = (int *)malloc(sizeof(int));
	if(ptrMx == NULL)
		return UPNP_E_OUTOF_MEMORY;

	adEvent = new upnp_timeout;
	if(adEvent == NULL) {
		free(ptrMx);
		return UPNP_E_OUTOF_MEMORY;
	}
	*ptrMx = Exp;
	adEvent->handle = Hnd;
	adEvent->Event = ptrMx;

	if (checkLockHandle(HND_DEVICE, Hnd, &SInfo) == HND_INVALID) {
		free_upnp_timeout(adEvent);
		return UPNP_E_INVALID_HANDLE;
	}

#ifdef SSDP_PACKET_DISTRIBUTE
	retVal = gTimerThread->schedule(
		TimerThread::SHORT_TERM, TimerThread::REL_SEC,
		((Exp / 2) - (AUTO_ADVERTISEMENT_TIME)),
		 &(adEvent->eventId),
		(start_routine)thread_autoadvertise, adEvent,
		(ThreadPool::free_routine)free_upnp_timeout);
#else
	retVal = gTimerThread->schedule(
		TimerThread::SHORT_TERM, TimerThread::REL_SEC,
		Exp - AUTO_ADVERTISEMENT_TIME, &(adEvent->eventId),
		(start_routine)thread_autoadvertise, adEvent,
		(ThreadPool::free_routine)free_upnp_timeout);
#endif
	if (retVal != UPNP_E_SUCCESS) {
		HandleUnlock();
		free_upnp_timeout(adEvent);
		return retVal;
	}

	HandleUnlock();
	return retVal;

}
#endif /* EXCLUDE_SSDP == 0 */
#endif /* INCLUDE_DEVICE_APIS */


#if EXCLUDE_SSDP == 0
#ifdef INCLUDE_CLIENT_APIS


int UpnpSearchAsync(
	UpnpClient_Handle Hnd, int Mx, const char *Target, const void *Cookie)
{
	struct Handle_Info *SInfo = NULL;
	int retVal;

	if (UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	if (Target == NULL) {
		return UPNP_E_INVALID_PARAM;
	}

	if (checkLockHandle(HND_CLIENT, Hnd, &SInfo, true) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	if(Mx < 1)
		Mx = DEFAULT_MX;

	HandleUnlock();
	retVal = SearchByTarget(Mx, (char *)Target, (void *)Cookie);
	if (retVal != 1)
		return retVal;

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */
#endif


/*******************************************************************************
 *
 *									GENA interface
 *
 ******************************************************************************/


#if EXCLUDE_GENA == 0
#ifdef INCLUDE_DEVICE_APIS
int UpnpSetMaxSubscriptions(UpnpDevice_Handle Hnd, int MaxSubscriptions)
{
	struct Handle_Info *SInfo = NULL;

	if(UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	if((MaxSubscriptions != UPNP_INFINITE)	&& (MaxSubscriptions < 0)) {
		return UPNP_E_INVALID_HANDLE;
	}

	if (checkLockHandle(HND_DEVICE, Hnd, &SInfo) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	SInfo->MaxSubscriptions = MaxSubscriptions;
	HandleUnlock();

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_DEVICE_APIS */


#ifdef INCLUDE_DEVICE_APIS
int UpnpSetMaxSubscriptionTimeOut(UpnpDevice_Handle Hnd,
								  int MaxSubscriptionTimeOut)
{
	struct Handle_Info *SInfo = NULL;

	if (UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	if((MaxSubscriptionTimeOut != UPNP_INFINITE)
	   && (MaxSubscriptionTimeOut < 0)) {
		return UPNP_E_INVALID_HANDLE;
	}

	if (checkLockHandle(HND_DEVICE, Hnd, &SInfo) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}

	SInfo->MaxSubscriptionTimeOut = MaxSubscriptionTimeOut;
	HandleUnlock();

	return UPNP_E_SUCCESS;

}
#endif /* INCLUDE_DEVICE_APIS */

#ifdef INCLUDE_CLIENT_APIS
int UpnpSubscribe(
	UpnpClient_Handle Hnd, const char *EvtUrl, int *TimeOut, Upnp_SID SubsId)
{
	int retVal;
	struct Handle_Info *SInfo = NULL;
	std::string SubsIdTmp;
	
	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpSubscribe\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (EvtUrl == NULL || SubsId == NULL || TimeOut == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}

	if (checkLockHandle(HND_CLIENT, Hnd, &SInfo, true) == HND_INVALID) {
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	retVal = genaSubscribe(Hnd, EvtUrl, TimeOut, &SubsIdTmp);
	upnp_strlcpy(SubsId, SubsIdTmp, sizeof(Upnp_SID));

exit_function:
	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
			   "UpnpSubscribe: retVal=%d\n", retVal);
	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpUnSubscribe(UpnpClient_Handle Hnd, const Upnp_SID SubsId)
{
	struct Handle_Info *SInfo = NULL;
	int retVal;
	std::string SubsIdTmp;

	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpUnSubscribe\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (SubsId == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	SubsIdTmp = SubsId;

	if (checkLockHandle(HND_CLIENT, Hnd, &SInfo, true) == HND_INVALID) {
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	retVal = genaUnSubscribe(Hnd, SubsIdTmp);

exit_function:
	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
			   "UpnpUnSubscribe, retVal=%d\n", retVal);

	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpRenewSubscription(UpnpClient_Handle Hnd, int *TimeOut,
						  const Upnp_SID SubsId)
{
	struct Handle_Info *SInfo = NULL;
	int retVal;
	std::string SubsIdTmp;

	UpnpPrintf(UPNP_ALL, API, __FILE__,__LINE__,"UpnpRenewSubscription\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (SubsId == NULL || TimeOut == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	SubsIdTmp = SubsId;

	if (checkLockHandle(HND_CLIENT, Hnd, &SInfo, true) == HND_INVALID) {
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	retVal = genaRenewSubscription(Hnd, SubsIdTmp, TimeOut);

exit_function:
	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
			   "UpnpRenewSubscription, retVal=%d\n", retVal);
	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */

#ifdef INCLUDE_DEVICE_APIS
int UpnpNotify(
	UpnpDevice_Handle Hnd, const char *DevID, const char *ServName,
	const char **VarName, const char **NewVal, int cVariables)
{
	struct Handle_Info *SInfo = NULL;
	int retVal;

	if (UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	if (DevID == NULL || ServName == NULL || VarName == NULL || NewVal == NULL
		|| cVariables < 0) {
		return UPNP_E_INVALID_PARAM;
	}

	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpNotify\n");

	if (checkLockHandle(HND_DEVICE, Hnd, &SInfo, true) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}

	HandleUnlock();
	retVal = genaNotifyAll(Hnd, (char*)DevID, (char*)ServName,
						   (char**)VarName, (char**)NewVal, cVariables);

	UpnpPrintf(UPNP_ALL,API,__FILE__,__LINE__,"UpnpNotify ret %d\n", retVal);
	return retVal;
}

int UpnpAcceptSubscription(
	UpnpDevice_Handle Hnd, const char *DevID, const char *ServName,
	const char **VarName, const char **NewVal, int cVariables,
	const Upnp_SID SubsId)
{
	int ret = 0;
	struct Handle_Info *SInfo = NULL;

	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpAcceptSubscription\n");

	if (UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	if (DevID == NULL || ServName == NULL || SubsId == NULL) {
		return UPNP_E_INVALID_PARAM;
	}

	if (checkLockHandle(HND_DEVICE, Hnd, &SInfo, true) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}

	HandleUnlock();
	ret = genaInitNotify(Hnd, (char*)DevID, (char*)ServName, (char**)VarName,
						 (char**)NewVal, cVariables, SubsId);

	UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
			   "UpnpAcceptSubscription, ret = %d\n", ret);
	return ret;
}

#endif /* INCLUDE_DEVICE_APIS */
#endif /* EXCLUDE_GENA == 0 */


/*******************************************************************************
 *
 *									SOAP interface
 *
 ******************************************************************************/


#if EXCLUDE_SOAP == 0
#ifdef INCLUDE_CLIENT_APIS
int UpnpSendAction(
	UpnpClient_Handle Hnd,
	const std::string& headerString,
	const std::string& actionURL,
	const std::string& serviceType,
	const std::string& actionName,
	const std::vector<std::pair<std::string, std::string>> actionParams,
	std::vector<std::pair<std::string, std::string>>& response,
	int *errcodep,
	std::string&  errdesc)
{
	struct Handle_Info *SInfo = NULL;

	if (UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	if (actionURL.empty() || serviceType.empty() || actionName.empty()) {
		return UPNP_E_INVALID_PARAM;
	}
	if (checkLockHandle(HND_CLIENT, Hnd, &SInfo, true) == HND_INVALID) {
		return UPNP_E_INVALID_HANDLE;
	}
	HandleUnlock();
	return SoapSendAction(headerString, actionURL, serviceType, actionName,
						  actionParams, response, errcodep, errdesc);
}

#endif /* INCLUDE_CLIENT_APIS */
#endif /* EXCLUDE_SOAP */


int UpnpDownloadUrlItem(const char *url, char **outBuf, char *contentType)
{
	int ret_code;
	size_t dummy;

	if (url == NULL || outBuf == NULL)
		return UPNP_E_INVALID_PARAM;
	ret_code = http_Download(url, HTTP_DEFAULT_TIMEOUT, outBuf, &dummy,
							 contentType);
	if (ret_code > 0)
		/* error reply was received */
		ret_code = UPNP_E_INVALID_URL;

	return ret_code;
}

/* Get callback function ptr from a handle. */
Upnp_FunPtr GetCallBackFn(UpnpClient_Handle Hnd)
{
	return ((struct Handle_Info *)HandleTable[Hnd])->Callback;
}

/* Assumes at most one client */
Upnp_Handle_Type GetClientHandleInfo(
	UpnpClient_Handle *client_handle_out,
	struct Handle_Info **HndInfo)
{
	Upnp_Handle_Type ret = HND_CLIENT;
	UpnpClient_Handle client;

	switch (GetHandleInfo(1, HndInfo)) {
	case HND_CLIENT:
		client = 1;
		break;
	default:
		switch (GetHandleInfo(2, HndInfo)) {
		case HND_CLIENT:
			client = 2;
			break;
		default:
			client = -1;
			ret = HND_INVALID;
		}
	}

	*client_handle_out = client;
	return ret;
}


Upnp_Handle_Type GetDeviceHandleInfo(
	UpnpDevice_Handle start, 
	int AddressFamily,
	UpnpDevice_Handle *device_handle_out,
	struct Handle_Info **HndInfo)
{
#ifdef INCLUDE_DEVICE_APIS
	/* Check if we've got a registered device of the address family specified.*/
	if ((AddressFamily == AF_INET  && UpnpSdkDeviceRegisteredV4 == 0) ||
		(AddressFamily == AF_INET6 && UpnpSdkDeviceregisteredV6 == 0)) {
		*device_handle_out = -1;
		return HND_INVALID;
	}
	if (start < 0 || start >= NUM_HANDLE-1) {
		*device_handle_out = -1;
		return HND_INVALID;
	}
	++start;
	/* Find it. */
	for (*device_handle_out=start; *device_handle_out < NUM_HANDLE;
		 (*device_handle_out)++) {
		switch (GetHandleInfo(*device_handle_out, HndInfo)) {
		case HND_DEVICE:
			if ((*HndInfo)->DeviceAf == AddressFamily) {
				return HND_DEVICE;
			}
			break;
		default:
			break;
		}
	}
#endif /* INCLUDE_DEVICE_APIS */

	*device_handle_out = -1;
	return HND_INVALID;
}


/* Check if we've got a registered device of the address family specified. */
Upnp_Handle_Type GetDeviceHandleInfoForPath(
	const std::string& path, int AddressFamily, UpnpDevice_Handle *devhdl,
	struct Handle_Info **HndInfo, service_info **serv_info)
{
	*devhdl = -1;
	*serv_info = nullptr;

#ifdef INCLUDE_DEVICE_APIS
	if ((AddressFamily == AF_INET  && UpnpSdkDeviceRegisteredV4 == 0) ||
		(AddressFamily == AF_INET6 && UpnpSdkDeviceregisteredV6 == 0)) {
		*devhdl = -1;
		return HND_INVALID;
	}

	for (int idx = 1; idx < NUM_HANDLE;	idx++) {
		Handle_Info *hinf;
		if (GetHandleInfo(idx, &hinf) == HND_DEVICE &&
			hinf->DeviceAf == AddressFamily) {
			if ((*serv_info = FindServiceControlURLPath(
					 &hinf->ServiceTable,	path)) ||
				(*serv_info = FindServiceEventURLPath(
					&hinf->ServiceTable,  path))) {
				*HndInfo = hinf;
				*devhdl = idx;
				return HND_DEVICE;
			}
		}
	}
#endif /* INCLUDE_DEVICE_APIS */

	return HND_INVALID;
}


Upnp_Handle_Type GetHandleInfo(UpnpClient_Handle Hnd,
							   struct Handle_Info **HndInfo)
{
	Upnp_Handle_Type ret = HND_INVALID;

	if (Hnd < 1 || Hnd >= NUM_HANDLE) {
		UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
				   "GetHandleInfo: out of range\n");
	} else if (HandleTable[Hnd] == NULL) {
		UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
				   "GetHandleInfo: HTable[%d] is NULL\n",
				   Hnd);
	} else if (HandleTable[Hnd] != NULL) {
		*HndInfo = (struct Handle_Info *)HandleTable[Hnd];
		ret = ((struct Handle_Info *)*HndInfo)->HType;
	}

	return ret;
}

int PrintHandleInfo(UpnpClient_Handle Hnd)
{
	struct Handle_Info * HndInfo;
	if (HandleTable[Hnd] != NULL) {
		HndInfo = (struct Handle_Info*)HandleTable[Hnd];
		UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
				   "Handle_%d Type_%d: \n", Hnd, HndInfo->HType);
#ifdef INCLUDE_DEVICE_APIS
		switch(HndInfo->HType) {
		case HND_CLIENT:
			break;
		default:
			UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
					   "DescURL: %s\n", HndInfo->DescURL);
		}
#endif /* INCLUDE_DEVICE_APIS */
	} else {
		return UPNP_E_INVALID_HANDLE;
	}
	return UPNP_E_SUCCESS;
}


#ifdef INTERNAL_WEB_SERVER
int UpnpSetWebServerRootDir(const char *rootDir)
{
	if(UpnpSdkInit == 0)
		return UPNP_E_FINISH;
	if((rootDir == NULL) || (strlen(rootDir) == 0)) {
		return UPNP_E_INVALID_PARAM;
	}

	return web_server_set_root_dir(rootDir);
}
#endif /* INTERNAL_WEB_SERVER */


int UpnpAddVirtualDir(const char *dirname, const void *cookie,
					  const void **oldcookie)
{
	if(UpnpSdkInit != 1) {
		/* SDK is not initialized */
		return UPNP_E_FINISH;
	}
	return web_server_add_virtual_dir(dirname, cookie, oldcookie);
}


int UpnpRemoveVirtualDir(const char *dirname)
{
	if (UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}
	return web_server_remove_virtual_dir(dirname);
}


void UpnpRemoveAllVirtualDirs(void)
{
	web_server_clear_virtual_dirs();
}


int UpnpEnableWebserver(int enable)
{
	int retVal = UPNP_E_SUCCESS;

	if(UpnpSdkInit != 1) {
		return UPNP_E_FINISH;
	}

	switch (enable) {
#ifdef INTERNAL_WEB_SERVER
	case true:
		if((retVal = web_server_init()) != UPNP_E_SUCCESS) {
			return retVal;
		}
		bWebServerState = WEB_SERVER_ENABLED;
		SetHTTPGetCallback(web_server_callback);
		break;

	case false:
		web_server_destroy();
		bWebServerState = WEB_SERVER_DISABLED;
		SetHTTPGetCallback(NULL);
		break;
#endif /* INTERNAL_WEB_SERVER */
	default:
		retVal = UPNP_E_INVALID_PARAM;
	}

	return retVal;
}


/*!
 * \brief Checks if the webserver is enabled or disabled. 
 *
 * \return 1, if webserver is enabled or 0, if webserver is disabled.
 */
int UpnpIsWebserverEnabled(void)
{
	if (UpnpSdkInit != 1) {
		return 0;
	}

	return bWebServerState == (WebServerState)WEB_SERVER_ENABLED;
}

int UpnpSetVirtualDirCallbacks(struct UpnpVirtualDirCallbacks *callbacks)
{
	int ret = 0;

	if(UpnpSdkInit != 1) {
		/* SDK is not initialized */
		return UPNP_E_FINISH;
	}

	if (callbacks == NULL)
		return UPNP_E_INVALID_PARAM;

	ret = UpnpVirtualDir_set_GetInfoCallback(callbacks->get_info) == UPNP_E_SUCCESS
		&& UpnpVirtualDir_set_OpenCallback(callbacks->open) == UPNP_E_SUCCESS
		&& UpnpVirtualDir_set_ReadCallback(callbacks->read) == UPNP_E_SUCCESS
		&& UpnpVirtualDir_set_WriteCallback(callbacks->write) == UPNP_E_SUCCESS
		&& UpnpVirtualDir_set_SeekCallback(callbacks->seek) == UPNP_E_SUCCESS
		&& UpnpVirtualDir_set_CloseCallback(callbacks->close) == UPNP_E_SUCCESS;

	return ret ? UPNP_E_SUCCESS : UPNP_E_INVALID_PARAM;
}

int UpnpVirtualDir_set_GetInfoCallback(VDCallback_GetInfo callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
		ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.get_info = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_OpenCallback(VDCallback_Open callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
		ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.open = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_ReadCallback(VDCallback_Read callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
		ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.read = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_WriteCallback(VDCallback_Write callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
		ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.write = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_SeekCallback(VDCallback_Seek callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
		ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.seek = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_CloseCallback(VDCallback_Close callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
		ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.close = callback;
	}

	return ret;
}

int UpnpSetContentLength(UpnpClient_Handle Hnd, size_t contentLength)
{
	int errCode = UPNP_E_SUCCESS;
	struct Handle_Info *HInfo = NULL;

	do {
		if (UpnpSdkInit != 1) {
			errCode = UPNP_E_FINISH;
			break;
		}

		HandleLock();

		switch (GetHandleInfo(Hnd, &HInfo)) {
		case HND_DEVICE:
			break;
		default:
			HandleUnlock();
			return UPNP_E_INVALID_HANDLE;
		}
		if (contentLength > MAX_SOAP_CONTENT_LENGTH) {
			errCode = UPNP_E_OUTOF_BOUNDS;
			break;
		}
		g_maxContentLength = contentLength;
	} while (0);

	HandleUnlock();
	return errCode;
}

int UpnpSetMaxContentLength(size_t contentLength)
{
	int errCode = UPNP_E_SUCCESS;

	do {
		if (UpnpSdkInit != 1) {
			errCode = UPNP_E_FINISH;
			break;
		}
		g_maxContentLength = contentLength;
	} while(0);

	return errCode;
}

int UpnpSetEventQueueLimits(int maxLen, int maxAge)
{
	g_UpnpSdkEQMaxLen = maxLen;
	g_UpnpSdkEQMaxAge = maxAge;
	return UPNP_E_SUCCESS;
}
