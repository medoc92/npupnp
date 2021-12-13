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
#include <thread>
#include <array>

#include <curl/curl.h>

#include "upnpapi.h"
#include "httputils.h"
#include "ssdplib.h"
#include "soaplib.h"
#include "ThreadPool.h"
#include "upnp_timeout.h"
#include "genut.h"
#include "netif.h"

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

#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#endif

/*! This structure is for virtual directory callbacks */
struct VirtualDirCallbacks virtualDirCallback;

// Mutex to synchronize handles (root device or control point
// handle). This used to be an rwlock but this was probably not worth
// the trouble given the small expected contention level
std::mutex GlobalHndRWLock;

/*! Initialization mutex. */
static std::mutex gSDKInitMutex;

/*! Send thread pool. */
ThreadPool gSendThreadPool;
/*! Global timer thread. */
TimerThread *gTimerThread;
/*! Receive thread pool. */
ThreadPool gRecvThreadPool;
/*! Mini server thread pool. */
ThreadPool gMiniServerThreadPool;
using tpooldesc = std::pair<ThreadPool*, const char*>;
static const std::array<std::pair<ThreadPool*, const char*>, 3> o_threadpools{
    tpooldesc{&gSendThreadPool, "Send thread pool"},
    tpooldesc{&gRecvThreadPool, "Receive thread pool"},
    tpooldesc{&gMiniServerThreadPool, "Mini server thread pool"},
};

/*! Flag to indicate the state of web server */
WebServerState bWebServerState = WEB_SERVER_DISABLED;

WebCallback_HostValidate g_hostvalidatecallback;
void *g_hostvalidatecookie;

/* Interfaces we are using */
std::vector<NetIF::Interface> g_netifs;
/* Small optimisation: if the interfaces parameter to UpnpInit2() was
   "*", no need for the web server to filter accepted connections */
bool g_use_all_interfaces;

/* General option flags */
unsigned int g_optionFlags;

/* Local global options, usually set from the options list of initWithOptions */
static int o_networkWaitSeconds = 60;

/* Marker to be replaced by an appropriate address in LOCATION URLs */
const std::string g_HostForTemplate{"@HOST_ADDR_FOR@"};

/*! local IPv4 port for the mini-server */
unsigned short LOCAL_PORT_V4;
/*! local IPv6 port for the mini-server */
unsigned short LOCAL_PORT_V6;

/*! UPnP device and control point handle table    */
#define NUM_HANDLE 200
static std::array<Handle_Info*, NUM_HANDLE> HandleTable;

/*! Maximum content-length (in bytes) that the SDK will process on an incoming
 * packet. Content-Length exceeding this size will be not processed and
 * error 413 (HTTP Error Code) will be returned to the remote end point. */
size_t g_maxContentLength = DEFAULT_SOAP_CONTENT_LENGTH;

/*! Global variable to determines the maximum number of
 *    events which can be queued for a given subscription before events begin
 *    to be discarded. This limits the amount of memory used for a
 *    non-responding subscribed entity. */
int g_UpnpSdkEQMaxLen = MAX_SUBSCRIPTION_QUEUED_EVENTS;

/*! Global variable to determine the maximum number of 
 *    seconds which an event can spend on a subscription queue (waiting for the 
 *    event at the head of the queue to be communicated). This parameter will 
 *    have no effect in most situations with the default (low) value of 
 *    MAX_SUBSCRIPTION_QUEUED_EVENTS. However, if MAX_SUBSCRIPTION_QUEUED_EVENTS 
 *    is set to a high value, the AGE parameter will allow pruning the queue in 
 *    good conformance with the UPnP Device Architecture standard, at the 
 *    price of higher potential memory use. */
int g_UpnpSdkEQMaxAge = MAX_SUBSCRIPTION_EVENT_AGE;

/*! Global variable to denote the state of Upnp SDK == 0 if uninitialized,
 * == 1 if initialized. */
static int UpnpSdkInit = 0;

/*! Global variable to denote the state of Upnp SDK client registration.
 * == 0 if unregistered, == 1 if registered. We only accept one client (CP) */
static int UpnpSdkClientRegistered = 0;

#ifdef UPNP_HAVE_OPTSSDP
/*! Global variable used in discovery notifications. */
Upnp_SID gUpnpSdkNLSuuid;
#endif /* UPNP_HAVE_OPTSSDP */

// Functions to retrieve an address from our interface(s) when we don't care
// which.
std::string apiFirstIPV4Str()
{
    if (g_netifs.empty())
        return {};
    const auto a = g_netifs.begin()->firstipv4addr();
    if (nullptr == a)
        return {};
    return a->straddr();
}
std::string apiFirstIPV6Str()
{
    if (g_netifs.empty())
        return {};
    const auto a = g_netifs.begin()->firstipv6addr();
    if (nullptr == a)
        return {};
    return a->straddr();
}
int apiFirstIPV6Index()
{
    for (const auto& netif : g_netifs) {
        if (netif.hasflag(NetIF::Interface::Flags::HASIPV6)) {
            return netif.getindex();
        }
    }
    return 0;
}

/* Find either the specified interfaces or the first appropriate interface
 * The interface must fulfill these requirements:
 *  UP / Not LOOPBACK / Support MULTICAST / valid IPv4 or IPv6 address.
 *
 * We'll retrieve the following information from the interface:
 */
static int getIfInfo(const char *IfNames)
{
    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
               "getIfInfo: IfNames: [%s]\n", (IfNames?IfNames:"null"));
    
    g_use_all_interfaces = (IfNames && std::string("*") == IfNames);
    bool ifnamespecified = IfNames && *IfNames;
    NetIF::Interfaces *ifs = NetIF::Interfaces::theInterfaces();
    NetIF::Interface *netifp{nullptr};
    std::vector<NetIF::Interface> selected;
    std::string v4addr;
    std::string v6addr;
    std::string actifnames;

    std::vector<std::string> vifnames;
    /* Special case for compatibility: if the IfNames string
       matches an interface name, this is a call from a
       non-multi-interface-aware app (on Windows where adapter
       names can contain space chars), and we don't split the
       string, but use it whole as the first vector element */
    if (!g_use_all_interfaces && ifnamespecified) {
        if (ifs->findByName(IfNames) != nullptr) {
            vifnames.emplace_back(IfNames);
        } else {
            stringToStrings(IfNames, vifnames);
        }
    }
    if (!vifnames.empty()) {
        for (const auto& name : vifnames) {
            netifp = ifs->findByName(name);
            if (nullptr == netifp) {
                UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                           "Adapter %s not found\n", name.c_str());
                return UPNP_E_INVALID_INTERFACE;
            }
            selected.emplace_back(*netifp);
        }
    } else {
        // No interface specified. Use first appropriate one, or all.
        std::vector<NetIF::Interface::Flags>
            needed{NetIF::Interface::Flags::HASIPV4, 
                   NetIF::Interface::Flags::UP,
                   NetIF::Interface::Flags::MULTICAST};
        if (0 != (g_optionFlags & UPNP_FLAG_IPV6_REQUIRED)) {
            UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__, "Requiring IPV6\n");
            needed.push_back(NetIF::Interface::Flags::HASIPV6);
        }
        NetIF::Interfaces::Filter filt;
        filt.needs=needed;
        filt.rejects={NetIF::Interface::Flags::LOOPBACK};

        selected = ifs->select(filt);
        if (!selected.empty() && !g_use_all_interfaces) {
            selected.resize(1);
        }
    }

    if (selected.empty()) {
        UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                   "No adapter with usable IP addresses.\n");
        return UPNP_E_INVALID_INTERFACE;
    }

    for (const auto& netif: selected) {
        if (netif.hasflag(NetIF::Interface::Flags::HASIPV4)) {
            const auto addr = netif.firstipv4addr();
            if (nullptr != addr) {
                v4addr += addr->straddr() + " ";
            }
        }
        if (using_ipv6() && netif.hasflag(NetIF::Interface::Flags::HASIPV6)) {
            const auto addr =
                netif.firstipv6addr(NetIF::IPAddr::Scope::LINK);
            if (nullptr != addr) {
                v6addr += addr->straddr() + " ";
            } 
        }
        actifnames += netif.getname() + " ";
    }

    /* If IPV6_REQUIRED was not set, maybe we have no IPV6 address. In this 
     * case, turn off all ipv6 operation */
    if (v6addr.empty()) {
        UpnpPrintf(UPNP_DEBUG, API, __FILE__, __LINE__, "No IPV6 address on "
                   "selected interface(s): turning off IPV6 operation\n");
        g_optionFlags &= ~UPNP_FLAG_IPV6;
    }
    
    if (v4addr.empty() && v6addr.empty()) {
        UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                   "No usable IP addresses were found.\n");
        return UPNP_E_INVALID_INTERFACE;
    }

    g_netifs = selected;

    if (!using_ipv6()) {
        // Trim the ipv6 addresses
        for (auto& netif : g_netifs) {
            auto addrmasks = netif.getaddresses();
            std::vector<NetIF::IPAddr> kept;
            std::copy_if(addrmasks.first.begin(), addrmasks.first.end(),
                         std::back_inserter(kept),
                         [](const NetIF::IPAddr &addr){
                             return addr.family() ==
                                 NetIF::IPAddr::Family::IPV4;});
            netif.trimto(kept);
        }
    }
    UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
               "Chosen interfaces= %s, v4= %s, v6= %s\n",
               actifnames.c_str(), v4addr.c_str(), v6addr.c_str());

    return UPNP_E_SUCCESS;
}

/* This is the old version for supposedly deprecated UpnpInit: v4 only
   and if the interface is specified, it's done by IP address. We are
   called if the address was not specified, and select the first ipv4
   we find */
static int getmyipv4(const char *inipv4 = nullptr)
{
    bool ipspecified = (nullptr != inipv4 && 0 != inipv4[0]);
    NetIF::Interfaces *ifs = NetIF::Interfaces::theInterfaces();
    NetIF::Interface *netifp{nullptr};
    NetIF::Interfaces::Filter filt;
    filt.needs = {NetIF::Interface::Flags::HASIPV4, 
                       NetIF::Interface::Flags::UP,
                  NetIF::Interface::Flags::MULTICAST};
    filt.rejects = {NetIF::Interface::Flags::LOOPBACK};

    std::vector<NetIF::Interface> selected = ifs->select(filt);
    if (selected.empty()) {
        UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                   "No adapter with usable IPV4 address.\n");
        return UPNP_E_INVALID_INTERFACE;
    }

    if (ipspecified) {
        for (auto& iface : selected) {
            const auto addrs = iface.getaddresses().first;
            for (const auto& addr : addrs) {
                if (addr.straddr() == inipv4) {
                    netifp = &iface;
                    goto ipv4found;
                }
            }
        }
    ipv4found:
        if (nullptr == netifp) {
            UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                       "No adapter found with specified IPV4 address.\n");
            return UPNP_E_INVALID_INTERFACE;
        }
    }

    if (nullptr == netifp) {
        if (selected.empty()) {
            UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                       "No appropriate network adapter found.\n");
            return UPNP_E_INVALID_INTERFACE;
        }
        netifp = &selected[0];
    }

    // If an IP was specified, trim the addresses from the found adapter
    if (ipspecified) {
        netifp->trimto({NetIF::IPAddr(inipv4)});
    }

    // Double-check that we do have an IPV4 addr
    const auto addr = netifp->firstipv4addr();
    if (nullptr == addr) {
        // can't happen, really
        return UPNP_E_INVALID_INTERFACE;
    }

    g_netifs.push_back(*netifp);
    UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__, "Ifname=%s, v4=%s\n",
               netifp->getname().c_str(), addr->straddr().c_str());
    return UPNP_E_SUCCESS;
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
static int UpnpInitThreadPools()
{
    int ret = UPNP_E_SUCCESS;
    ThreadPoolAttr attr;

    attr.maxThreads = MAX_THREADS;
    attr.minThreads =  MIN_THREADS;
    attr.stackSize = THREAD_STACK_SIZE;
    attr.jobsPerThread = JOBS_PER_THREAD;
    attr.maxIdleTime = THREAD_IDLE_TIME;
    attr.maxJobsTotal = MAX_JOBS_TOTAL;

    auto i = std::any_of(o_threadpools.begin(), o_threadpools.end(), [&](const std::pair<ThreadPool*, const char*> &entry)
        { return entry.first->start(&attr) != UPNP_E_SUCCESS; });

    if (i) {
        ret = UPNP_E_INIT_FAILED;
        UpnpSdkInit = 0;
        UpnpFinish();
    }
    return ret;
}


/*!
 * \brief Performs the initial steps in initializing the UPnP SDK.
 *
 *    \li Winsock library is initialized for the process (Windows specific).
 *    \li The logging (for debug messages) is initialized.
 *    \li Mutexes, Handle table and thread pools are allocated and initialized.
 *    \li Callback functions for SOAP and GENA are set, if they're enabled.
 *    \li The SDK timer thread is initialized.
 *
 * \return UPNP_E_SUCCESS on success.
 */
static int UpnpInitPreamble()
{
    int retVal = UPNP_E_SUCCESS;

#ifdef _WIN32
    if (WinsockInit() != UPNP_E_SUCCESS) {
        std::cerr << "WinsockInit failed\n";
        return retVal;
    }
#endif
    NetIF::Interfaces::setlogfp(UpnpGetDebugFile(UPNP_DEBUG, API));
    curl_global_init(CURL_GLOBAL_ALL);
    
    /* needed by SSDP or other parts. */
    srand(static_cast<unsigned int>(time(nullptr)));

    /* Initialize debug output. */
    retVal = UpnpInitLog();
    if (retVal != UPNP_E_SUCCESS) {
        std::cerr << "UpnpInitLog failed\n";
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
    HandleTable = {};
    HandleUnlock();

    /* Initialize SDK global thread pools. */
    retVal = UpnpInitThreadPools();
    if (retVal != UPNP_E_SUCCESS) {
        std::cerr << "UpnpInitThreadPools failed\n";
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
        std::cerr << "Timer Thread init failed\n";
        UpnpFinish();
        return UPNP_E_INIT_FAILED;
    }

    return UPNP_E_SUCCESS;
}


/*!
 * \brief Finishes initializing the UPnP SDK.
 *    \li The MiniServer is started, if enabled.
 *    \li The WebServer is started, if enabled.
 * 
 * \return UPNP_E_SUCCESS on success or     UPNP_E_INIT_FAILED if a mutex could not
 *    be initialized.
 */
static int UpnpInitStartServers(unsigned short DestPort)
{
#if EXCLUDE_MINISERVER == 0 || EXCLUDE_WEB_SERVER == 0
    int retVal = 0;
#endif

#if EXCLUDE_MINISERVER == 0
    LOCAL_PORT_V4 = LOCAL_PORT_V6 = DestPort;
    retVal = StartMiniServer(&LOCAL_PORT_V4, &LOCAL_PORT_V6);
    if (retVal != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                   "Miniserver start error\n");
        UpnpFinish();
        return retVal;
    }
#endif

#if EXCLUDE_WEB_SERVER == 0
    retVal = UpnpEnableWebserver(WEB_SERVER_ENABLED);
    if (retVal != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                   "Webserver start error\n");
        UpnpFinish();
        return retVal;
    }
#endif

    return UPNP_E_SUCCESS;
}

/* Only use HostIP if ifName is nullptr (meaning a call from UpnpInit()) */
static int waitForNetwork(
    const char *HostIP, const char *ifName)
{
    int ret = UPNP_E_INVALID_INTERFACE;

    int loop_sleep = 2;
    int loops = o_networkWaitSeconds / loop_sleep;
    loops = std::max(1, loops);
    for (int i = 0; i < loops; i++) {
        if (HostIP && *HostIP) {
            /* Verify HostIP, if provided, or find it ourselves. */
            ret = getmyipv4(HostIP);
        } else {
            /* Retrieve interface information (Addresses, index, etc). */
            ret = getIfInfo(ifName);
        }
        if (ret == UPNP_E_SUCCESS) {
            break;
        }

        if (!NetIF::Interfaces::theInterfaces()->refresh()) {
            UpnpPrintf(UPNP_ERROR, API, __FILE__, __LINE__,
                       "UpnpInit: could not read network interface state "
                       "from system\n");
        }
        std::this_thread::sleep_for(std::chrono::seconds(loop_sleep));
    }
    return ret;
}

/* If ifName is nullptr this is a call from UpnpInit(). UpnpInit2() calls us with
 * a non-null, possibly empty string */
static int upnpInitCommon(const char *hostIP, const char *ifName,
                          unsigned short DestPort)
{
    int retVal = UPNP_E_SUCCESS;

    std::unique_lock<std::mutex> lck(gSDKInitMutex);

    /* Check if we're already initialized. */
    if (UpnpSdkInit == 1) {
        retVal = UPNP_E_INIT;
        goto exit_function;
    }

    /* WinsockInit, initlog etc. */
    retVal = UpnpInitPreamble();
    if (retVal != UPNP_E_SUCCESS) {
        goto exit_function;
    }
    UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
               "UpnpInit: hostIP=%s, ifName=%s, DestPort=%d.\n", 
               hostIP ? hostIP : "",ifName?ifName:"",static_cast<int>(DestPort));

    retVal = waitForNetwork(hostIP, ifName);

    {
        std::ostringstream ifdump;
        NetIF::Interfaces *ifs = NetIF::Interfaces::theInterfaces();
        ifs->print(ifdump);
        UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
                   "All network interfaces:\n%s\n", ifdump.str().c_str());
    }
    if (retVal != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_ERROR, API, __FILE__, __LINE__,
                   "UpnpInit: no usable IP address found after waiting %d S\n",
                   o_networkWaitSeconds);
        goto exit_function;
    }
    
    /* Finish initializing the SDK. Webserver start
       UpnpEnableWebServer() is part of the API for some reasons and
       tests UpnpSdkInit==1, so this must be set before the call */
    UpnpSdkInit = 1;
    retVal = UpnpInitStartServers(DestPort);
    if (retVal != UPNP_E_SUCCESS) {
        UpnpSdkInit = 0;
        goto exit_function;
    }
    UpnpPrintf(UPNP_INFO, API, __FILE__, __LINE__,
               "Upnpinit: retVal %d Host Ip: %s Host Port: %d\n",
               retVal, g_netifs.begin()->firstipv4addr()->straddr().c_str(),
               static_cast<int>(LOCAL_PORT_V4));

exit_function:
    return retVal;
}

EXPORT_SPEC int UpnpInit(const char *hostIP, unsigned short DestPort)
{
    return upnpInitCommon(hostIP, nullptr, DestPort);
}

EXPORT_SPEC int UpnpInit2(const char *IfName, unsigned short DestPort)
{
    return UpnpInitWithOptions(IfName, DestPort,
                               UPNP_FLAG_IPV6, UPNP_OPTION_END);
}

EXPORT_SPEC int UpnpInit2(const std::vector<std::string>& ifnames, unsigned short port)
{
    // A bit wasteful, but really simpler. Just build an interfaces
    // list string and continue with this.
    std::string names = stringsToString(ifnames);
    return UpnpInit2(names.c_str(), port);
}

EXPORT_SPEC int UpnpInitWithOptions(
    const char *ifnames, unsigned short port, unsigned int flags,  ...)
{
    va_list ap;
    int ret = UPNP_E_SUCCESS;

    g_optionFlags = flags;

    va_start(ap, flags);
    for (;;) {
        int option = static_cast<Upnp_InitOption>(va_arg(ap, int));
        if (option == UPNP_OPTION_END) {
            break;
        }
        switch (option) {
        case UPNP_OPTION_NETWORK_WAIT:
            o_networkWaitSeconds = va_arg(ap, int);
            break;
        default:
            UpnpPrintf(UPNP_CRITICAL, API, __FILE__, __LINE__,
                       "UpnPInitWithOptions: bad option %d in list\n", option);
            ret = UPNP_E_INVALID_PARAM;
            goto breakloop;
        }
    }

breakloop:
    va_end(ap);
    if (ret == UPNP_E_SUCCESS) {
        ret = upnpInitCommon(nullptr, ifnames, port);
    }
    return ret;
}


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
    UpnpPrintf(UPNP_DEBUG, API, DbgFileName, DbgLineNo,
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
}
#endif /* DEBUG */

EXPORT_SPEC int UpnpFinish()
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
    while (GetDeviceHandleInfo(0, &device_handle, &temp) ==  HND_DEVICE) {
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

EXPORT_SPEC int UpnpSetWebRequestHostValidateCallback(
    WebCallback_HostValidate callback, void *cookie)
{
    g_hostvalidatecallback = callback;
    g_hostvalidatecookie = cookie;
    return UPNP_E_SUCCESS;
}

EXPORT_SPEC std::string UpnpGetUrlHostPortForClient(const struct sockaddr_storage* clsock)
{
    NetIF::IPAddr claddr(reinterpret_cast<const struct sockaddr*>(clsock));
    NetIF::IPAddr hostaddr;
    const NetIF::Interface *itf =
        NetIF::Interfaces::interfaceForAddress(claddr, g_netifs, hostaddr);
    if (nullptr == itf) {
        return {};
    }
    int port = 0;
    std::string prefix;
    switch (hostaddr.family()) {
    case NetIF::IPAddr::Family::IPV4:
        port = UpnpGetServerPort();
        break;
    case NetIF::IPAddr::Family::IPV6:
        prefix = "[";
        port = UpnpGetServerPort6();
            break;
    default:
        return {};
    }

    return prefix + hostaddr.straddr() + (prefix.empty() ? "" : "]") + ":" + lltodecstr(port);
}

EXPORT_SPEC unsigned short UpnpGetServerPort()
{
    if (UpnpSdkInit != 1)
        return 0U;

    return LOCAL_PORT_V4;
}

EXPORT_SPEC unsigned short UpnpGetServerPort6()
{
#ifdef UPNP_ENABLE_IPV6
    if (UpnpSdkInit != 1)
        return 0U;

    return LOCAL_PORT_V6;
#else
    return 0;
#endif
}
EXPORT_SPEC unsigned short UpnpGetServerUlaGuaPort6()
{
#ifdef UPNP_ENABLE_IPV6
        return 0;
#else
        return 0;
#endif
}

EXPORT_SPEC const char *UpnpGetServerIpAddress()
{
    if (UpnpSdkInit != 1)
        return "";
    static std::string addr;
    if (addr.empty()) {
        addr = apiFirstIPV4Str();
    }
    return addr.c_str();
}

EXPORT_SPEC const char *UpnpGetServerIp6Address()
{
#ifdef UPNP_ENABLE_IPV6
    if (UpnpSdkInit != 1 || !using_ipv6()) {
        return "";
    }
    static std::string addr;
    if (addr.empty()) {
        addr = apiFirstIPV6Str();
    }
    return addr.c_str();
#else
    return nullptr;
#endif
}

EXPORT_SPEC const char *UpnpGetServerUlaGuaIp6Address()
{
    return "";
}

/*!
 * \brief Get a free handle.
 *
 * \return On success, an integer greater than zero or UPNP_E_OUTOF_HANDLE on
 *    failure.
 */
static int GetFreeHandle()
{
    /* Handle 0 is not used as NULL translates to 0 when passed as a handle */
    auto it = std::find(std::next(HandleTable.begin()), HandleTable.end(), nullptr);
    if (it == HandleTable.end())
        return UPNP_E_OUTOF_HANDLE;
    return std::distance(HandleTable.begin(), it);
}

/*!
 * \brief Free handle.
 *
 * \return UPNP_E_SUCCESS if successful or UPNP_E_INVALID_HANDLE if not
 */
static int FreeHandle(int handleindex)
{
    if (handleindex >= 1 && handleindex < NUM_HANDLE) {
        if (HandleTable[handleindex] != nullptr) {
            delete HandleTable[handleindex];
            HandleTable[handleindex] = nullptr;
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
    Upnp_DescType descriptionType, char *description,
    int AddressFamily, UPnPDeviceDesc& desc, char descURL[LINE_SIZE]);

static int registerRootDeviceAllForms(
    Upnp_DescType descriptionType,
    const char *description_const,
    Upnp_FunPtr Fun,
    const void *Cookie,
    UpnpDevice_Handle *Hnd,
    const char *LowerDescUrl)
{
    struct Handle_Info *HInfo = nullptr;
    int retVal = 0;
#if EXCLUDE_GENA == 0
    int hasServiceTable = 0;
#endif /* EXCLUDE_GENA */
    char *description = const_cast<char *>(description_const);

    HandleLock();

    UpnpPrintf(UPNP_INFO,API,__FILE__,__LINE__, "registerRootDeviceAllF\n");

    if (UpnpSdkInit != 1) {
        retVal = UPNP_E_FINISH;
        goto exit_function;
    }

    if (Hnd == nullptr || Fun == nullptr ||
        description == nullptr ||*description == 0) {
        retVal = UPNP_E_INVALID_PARAM;
        goto exit_function;
    }

    *Hnd = GetFreeHandle();
    if (*Hnd == UPNP_E_OUTOF_HANDLE) {
        retVal = UPNP_E_OUTOF_MEMORY;
        goto exit_function;
    }

    HInfo = new Handle_Info;
    if (HInfo == nullptr) {
        retVal = UPNP_E_OUTOF_MEMORY;
        goto exit_function;
    }
    HandleTable[*Hnd] = HInfo;

    retVal = GetDescDocumentAndURL(
        descriptionType, description, AF_INET, HInfo->devdesc, HInfo->DescURL);
    if (retVal != UPNP_E_SUCCESS) {
        FreeHandle(*Hnd);
        goto exit_function;
    }

    if (LowerDescUrl == nullptr)
        upnp_strlcpy(HInfo->LowerDescURL, HInfo->DescURL,
                     sizeof(HInfo->LowerDescURL));
    else
        upnp_strlcpy(HInfo->LowerDescURL, LowerDescUrl,
                     sizeof(HInfo->LowerDescURL));
    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
               "Root Device URL for legacy CPs: %s\n", HInfo->LowerDescURL);
    HInfo->HType = HND_DEVICE;
    HInfo->Callback = Fun;
    HInfo->Cookie = reinterpret_cast<char*>(const_cast<void*>(Cookie));
    HInfo->MaxAge = DEFAULT_MAXAGE;
    HInfo->MaxSubscriptions = UPNP_INFINITE;
    HInfo->MaxSubscriptionTimeOut = UPNP_INFINITE;

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
               "registerRootDeviceAllForms: Ok Description at : %s\n",
               HInfo->DescURL);

#if EXCLUDE_GENA == 0
    /*
     * GENA SET UP
     */
    hasServiceTable = initServiceTable(HInfo->devdesc, &HInfo->ServiceTable);
    if (hasServiceTable) {
        UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
                   "registerRootDeviceAllForms: GENA services:\n");
        printServiceTable(&HInfo->ServiceTable, UPNP_ALL, API);
    } else {
        UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
                   "\nUpnpRegisterRootDeviceAF: no services\n");
    }
#endif /* EXCLUDE_GENA */

    retVal = UPNP_E_SUCCESS;

exit_function:
    HandleUnlock();
    return retVal;
}

EXPORT_SPEC int UpnpRegisterRootDevice(
    const char *DescUrl, Upnp_FunPtr Fun, const void *Cookie,
    UpnpDevice_Handle *Hnd)
{
    return registerRootDeviceAllForms(
        UPNPREG_URL_DESC, DescUrl, Fun, Cookie, Hnd, nullptr);
}

EXPORT_SPEC int UpnpRegisterRootDevice2(
    Upnp_DescType descriptionType, const char *description_const,
    size_t, int /*ignored*/, Upnp_FunPtr Fun, const void *Cookie,
    UpnpDevice_Handle *Hnd)
{
    return registerRootDeviceAllForms(
        descriptionType, description_const, Fun, Cookie, Hnd, nullptr);
}

EXPORT_SPEC int UpnpRegisterRootDevice4(
    const char *DescUrl, Upnp_FunPtr Fun, const void *Cookie,
    UpnpDevice_Handle *Hnd, int /*AddressFamily*/, const char *LowerDescUrl)
{
    return registerRootDeviceAllForms(
        UPNPREG_URL_DESC, DescUrl, Fun, Cookie, Hnd, LowerDescUrl);
}

EXPORT_SPEC int UpnpDeviceSetProduct(
    UpnpDevice_Handle Hnd, const char *product, const char *version)
{
    struct Handle_Info *HInfo = nullptr;
    if (UpnpSdkInit != 1) {
        return UPNP_E_INVALID_HANDLE;
    }
    if (nullptr==product || 0== *product || nullptr==version || 0== *version) {
        return UPNP_E_INVALID_PARAM;
    }
    if (checkLockHandle(HND_INVALID, Hnd, &HInfo) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
    }
    HInfo->productversion = std::string(product) + "/" + std::string(version);
    HandleUnlock();
    return UPNP_E_SUCCESS;
}

EXPORT_SPEC int UpnpUnRegisterRootDevice(UpnpDevice_Handle Hnd)
{
    return UpnpUnRegisterRootDeviceLowPower(Hnd, -1, -1, -1);
}

EXPORT_SPEC int UpnpUnRegisterRootDeviceLowPower(UpnpDevice_Handle Hnd, int PowerState,
                                     int SleepPeriod, int RegistrationState)
{
    UpnpPrintf(UPNP_DEBUG,API, __FILE__, __LINE__, "UpnpUnRegisterRootDevice\n");

    int retVal = 0;
    struct Handle_Info *HInfo = nullptr;

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
    SsdpEntity sd;
    retVal = AdvertiseAndReply(Hnd, MSGTYPE_SHUTDOWN,HInfo->MaxAge, nullptr, sd);
#endif

    if (checkLockHandle(HND_INVALID, Hnd, &HInfo) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
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

    if (Fun == nullptr || Hnd == nullptr)
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
    if (HInfo == nullptr) {
        HandleUnlock();
        return UPNP_E_OUTOF_MEMORY;
    }
    HInfo->HType = HND_CLIENT;
    HInfo->Callback = Fun;
    HInfo->Cookie = reinterpret_cast<char*>(const_cast<void*>(Cookie));
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

void UpnpClientSetProduct(
    UpnpClient_Handle, const char *product, const char *version)
{
    if (nullptr == product || 0 == *product ||
        nullptr == version || 0 == *version) {
        return;
    }
    // We just have one client connection, so let httputils store the thing.
    get_sdk_client_info(std::string(product) + "/" + version);
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
    std::string::size_type slash = name.find_last_of('/');
    if (slash == std::string::npos) {
        return name;
    }

    return name.substr(slash+1);


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
    buffer = static_cast<char *>(malloc(st.st_size+1));
    if (nullptr == buffer) {
        ret = UPNP_E_OUTOF_MEMORY;
        goto out;
    }
    num_read = fread(buffer, 1, st.st_size, fp);
    if (num_read != static_cast<size_t>(st.st_size)) {
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

static std::string descurl(int family, const std::string& nm)
{
    std::ostringstream url;
    url << "http://" << g_HostForTemplate << ":" <<
        (family == AF_INET ? LOCAL_PORT_V4 : LOCAL_PORT_V6) << "/" << nm;
    return url.str();
}
    
/* We do not support an URLBase set inside the description document.
   This was advised against in upnp 1.0 and forbidden in 1.1. */
static int GetDescDocumentAndURL(
    Upnp_DescType descriptionType,
    char *description,
    int AddressFamily,
    UPnPDeviceDesc& desc,
    char descURL[LINE_SIZE])
{
    int retVal = 0;
    if (!description || !*description)
        return UPNP_E_INVALID_PARAM;

    std::string localurl;
    std::string simplename;
    std::string descdata;
    time_t modtime = time(nullptr);
    switch (descriptionType) {
    case UPNPREG_URL_DESC:
    {
        std::string globurl{description};
        if (globurl.size() > LINE_SIZE - 1) {
            return UPNP_E_URL_TOO_BIG;
        }
        upnp_strlcpy(descURL, globurl.c_str(), LINE_SIZE);
        char *descstr;
        retVal = UpnpDownloadUrlItem(globurl.c_str(), &descstr, nullptr);
        if (retVal != UPNP_E_SUCCESS) {
            UpnpPrintf(UPNP_ERROR, API, __FILE__, __LINE__,
                       "UpnpRegisterRootDevice: error downloading doc: %d\n",
                       retVal);
            return retVal;
        }
        descdata = descstr;
        free(descstr);
        // We replace the host part with the address replacement template
        uri_type parsed_url;
        if (parse_uri(globurl, &parsed_url) != UPNP_E_SUCCESS) {
            UpnpPrintf(UPNP_ERROR, API, __FILE__, __LINE__,
                       "UpnpRegisterRootDevice: can't parse description URL\n");
            return UPNP_E_INVALID_URL;
        }
        std::string hp{parsed_url.hostport.text};
        std::string::size_type pos = hp.rfind(':');
        if (pos != std::string::npos) {
            hp = hp.erase(pos);
        }
        localurl = globurl;
        pos = localurl.find(hp);
        if (pos != std::string::npos) {
            std::string::size_type inc{0};
            if (pos > 0 && localurl[pos-1] == '[') {
                pos--;
                inc = 2;
            }
            localurl = localurl.replace(pos, hp.size()+inc, g_HostForTemplate);
        }
    }
    break;
    case UPNPREG_FILENAME_DESC:
    {
        char *descstr{nullptr};
        retVal = readFile(description, &descstr, &modtime);
        if (retVal != UPNP_E_SUCCESS) {
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
    default:
        UpnpPrintf(UPNP_ERROR, API, __FILE__, __LINE__,
                   "UpnpRegisterRootDevice: bad desc type %d\n",
                   int(descriptionType));
        return UPNP_E_INVALID_DESC;
    }

    if (!localurl.empty()) {
        UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
                   "UpnpRegisterRootDevice: local url: %s\n", localurl.c_str());
        upnp_strlcpy(descURL, localurl.c_str(), LINE_SIZE);
        desc = UPnPDeviceDesc(localurl, descdata);
        if (desc.ok && !simplename.empty()) {
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
    int AddressFamily,
    UPnPDeviceDesc& desc,
    char descURL[LINE_SIZE])
{
    int retVal = 0;

    if (descriptionType != (enum Upnp_DescType_e)UPNPREG_URL_DESC) {
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
 *                                    SSDP interface
 *
 ******************************************************************************/


#ifdef INCLUDE_DEVICE_APIS
#if EXCLUDE_SSDP == 0

int UpnpSendAdvertisement(UpnpDevice_Handle Hnd, int Exp)
{
    return UpnpSendAdvertisementLowPower(Hnd, Exp, -1, -1, -1);
}

void* thread_autoadvertise(void *input)
{
    auto event = static_cast<upnp_timeout *>(input);

    UpnpSendAdvertisement(event->handle, *(static_cast<int *>(event->Event)));

    return nullptr;
}

int UpnpSendAdvertisementLowPower(
    UpnpDevice_Handle Hnd, int Exp,
    int PowerState, int SleepPeriod, int RegistrationState)
{
    struct Handle_Info *SInfo = nullptr;
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
    SsdpEntity sd;
    retVal = AdvertiseAndReply(Hnd, MSGTYPE_ADVERTISEMENT, Exp, nullptr, sd);

    if(retVal != UPNP_E_SUCCESS)
        return retVal;
    ptrMx = static_cast<int *>(malloc(sizeof(int)));
    if(ptrMx == nullptr)
        return UPNP_E_OUTOF_MEMORY;

    adEvent = new upnp_timeout;
    if(adEvent == nullptr) {
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
    time_t thetime = ((Exp / 2) - (AUTO_ADVERTISEMENT_TIME));
#else
    time_t thetime = Exp - AUTO_ADVERTISEMENT_TIME;
#endif

    retVal = gTimerThread->schedule(
        TimerThread::SHORT_TERM, TimerThread::REL_SEC, thetime, &adEvent->eventId,
        thread_autoadvertise, adEvent,reinterpret_cast<ThreadPool::free_routine>(free_upnp_timeout));
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
    struct Handle_Info *SInfo = nullptr;
    int retVal;

    if (UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }
    if (Target == nullptr) {
        return UPNP_E_INVALID_PARAM;
    }

    if (checkLockHandle(HND_CLIENT, Hnd, &SInfo, true) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
    }
    if(Mx < 1)
        Mx = DEFAULT_MX;

    HandleUnlock();
    retVal = SearchByTarget(Mx, const_cast<char *>(Target), const_cast<void *>(Cookie));
    if (retVal != 1)
        return retVal;

    return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */
#endif


/*******************************************************************************
 *
 *                                    GENA interface
 *
 ******************************************************************************/


#if EXCLUDE_GENA == 0
#ifdef INCLUDE_DEVICE_APIS
int UpnpSetMaxSubscriptions(UpnpDevice_Handle Hnd, int MaxSubscriptions)
{
    struct Handle_Info *SInfo = nullptr;

    if(UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }
    if((MaxSubscriptions != UPNP_INFINITE)    && (MaxSubscriptions < 0)) {
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
    struct Handle_Info *SInfo = nullptr;

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
    struct Handle_Info *SInfo = nullptr;
    std::string SubsIdTmp;
    
    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpSubscribe\n");

    if (UpnpSdkInit != 1) {
        retVal = UPNP_E_FINISH;
        goto exit_function;
    }

    if (EvtUrl == nullptr || SubsId == nullptr || TimeOut == nullptr) {
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
    struct Handle_Info *SInfo = nullptr;
    int retVal;
    std::string SubsIdTmp;

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpUnSubscribe\n");

    if (UpnpSdkInit != 1) {
        retVal = UPNP_E_FINISH;
        goto exit_function;
    }

    if (SubsId == nullptr) {
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
    struct Handle_Info *SInfo = nullptr;
    int retVal;
    std::string SubsIdTmp;

    UpnpPrintf(UPNP_ALL, API, __FILE__,__LINE__,"UpnpRenewSubscription\n");

    if (UpnpSdkInit != 1) {
        retVal = UPNP_E_FINISH;
        goto exit_function;
    }

    if (SubsId == nullptr || TimeOut == nullptr) {
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
    struct Handle_Info *SInfo = nullptr;
    int retVal;

    if (UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }
    if (DevID == nullptr || ServName == nullptr || VarName == nullptr || NewVal == nullptr
        || cVariables < 0) {
        return UPNP_E_INVALID_PARAM;
    }

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpNotify\n");

    if (checkLockHandle(HND_DEVICE, Hnd, &SInfo, true) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
    }

    HandleUnlock();
    retVal = genaNotifyAll(Hnd, const_cast<char*>(DevID), const_cast<char*>(ServName),
                           const_cast<char**>(VarName), const_cast<char**>(NewVal), cVariables);

    UpnpPrintf(UPNP_ALL,API,__FILE__,__LINE__,"UpnpNotify ret %d\n", retVal);
    return retVal;
}

int UpnpNotifyXML(UpnpDevice_Handle Hnd, const char *DevID,
                  const char *ServName, const std::string& propset)
{
    struct Handle_Info *SInfo = nullptr;
    int retVal;

    if (UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }
    if (DevID == nullptr || ServName == nullptr) {
        return UPNP_E_INVALID_PARAM;
    }

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpNotifyXML\n");

    if (checkLockHandle(HND_DEVICE, Hnd, &SInfo, true) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
    }

    HandleUnlock();
    retVal = genaNotifyAllXML(Hnd, const_cast<char*>(DevID), const_cast<char*>(ServName), propset);

    UpnpPrintf(UPNP_ALL,API,__FILE__,__LINE__, "UpnpNotifyXML ret %d\n", retVal);
    return retVal;
}

int UpnpAcceptSubscription(
    UpnpDevice_Handle Hnd, const char *DevID, const char *ServName,
    const char **VarName, const char **NewVal, int cVariables,
    const Upnp_SID SubsId)
{
    int ret = 0;
    struct Handle_Info *SInfo = nullptr;

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpAcceptSubscription\n");

    if (UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }
    if (DevID == nullptr || ServName == nullptr || SubsId == nullptr) {
        return UPNP_E_INVALID_PARAM;
    }

    if (checkLockHandle(HND_DEVICE, Hnd, &SInfo, true) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
    }

    HandleUnlock();
    ret = genaInitNotifyVars(Hnd, const_cast<char*>(DevID), const_cast<char*>(ServName), const_cast<char**>(VarName),
                             const_cast<char**>(NewVal), cVariables, SubsId);

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
               "UpnpAcceptSubscription, ret = %d\n", ret);
    return ret;
}

int UpnpAcceptSubscriptionXML(
    UpnpDevice_Handle Hnd, const char *DevID, const char *ServName,
    const std::string& propertyset,
    const Upnp_SID SubsId)
{
    int ret = 0;
    struct Handle_Info *SInfo = nullptr;

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__, "UpnpAcceptSubscriptionXML\n");

    if (UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }
    if (DevID == nullptr || ServName == nullptr || SubsId == nullptr) {
        return UPNP_E_INVALID_PARAM;
    }

    if (checkLockHandle(HND_DEVICE, Hnd, &SInfo, true) == HND_INVALID) {
        return UPNP_E_INVALID_HANDLE;
    }

    HandleUnlock();
    ret = genaInitNotifyXML(
        Hnd, const_cast<char*>(DevID), const_cast<char*>(ServName), propertyset, SubsId);

    UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
               "UpnpAcceptSubscriptionXML, ret = %d\n", ret);
    return ret;
}

#endif /* INCLUDE_DEVICE_APIS */
#endif /* EXCLUDE_GENA == 0 */


/*******************************************************************************
 *
 *                                    SOAP interface
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
    const std::vector<std::pair<std::string, std::string>>& actionParams,
    std::vector<std::pair<std::string, std::string>>& response,
    int *errcodep,
    std::string&  errdesc)
{
    struct Handle_Info *SInfo = nullptr;

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

    if (url == nullptr || outBuf == nullptr)
        return UPNP_E_INVALID_PARAM;
    ret_code = http_Download(url, HTTP_DEFAULT_TIMEOUT, outBuf, &dummy, contentType);
    if (ret_code > 0)
        /* error reply was received */
        ret_code = UPNP_E_INVALID_URL;

    return ret_code;
}

int UpnpDownloadUrlItem(const std::string& url,
                        std::string& data, std::string& ct)
{
    char ctbuf[LINE_SIZE];
    char *datap{nullptr};
    ctbuf[0] = 0;
    int ret_code = UpnpDownloadUrlItem(url.c_str(), &datap, ctbuf);
    if (ret_code == 0) {
        if (datap) {
            data.assign(datap, strlen(datap));
            free(datap);
        }
        ct = ctbuf;
    }
    return ret_code;
}

/* Get callback function ptr from a handle. */
Upnp_FunPtr GetCallBackFn(UpnpClient_Handle Hnd)
{
    return (static_cast<struct Handle_Info *>(HandleTable[Hnd]))->Callback;
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
    UpnpDevice_Handle *device_handle_out,
    struct Handle_Info **HndInfo)
{
#ifdef INCLUDE_DEVICE_APIS
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
            return HND_DEVICE;
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
    const std::string& path, UpnpDevice_Handle *devhdl,
    struct Handle_Info **HndInfo, service_info **serv_info)
{
    *devhdl = -1;
    *serv_info = nullptr;

#ifdef INCLUDE_DEVICE_APIS

    for (int idx = 1; idx < NUM_HANDLE;    idx++) {
        Handle_Info *hinf;
        if (GetHandleInfo(idx, &hinf) == HND_DEVICE) {
            if ((*serv_info = FindServiceControlURLPath(
                     &hinf->ServiceTable,    path)) ||
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
        UpnpPrintf(UPNP_ERROR, API, __FILE__, __LINE__,
                   "GetHandleInfo: out of range\n");
    } else if (HandleTable[Hnd] == nullptr) {
        // Don't print anything, we sometimes walk the table
        //UpnpPrintf(UPNP_ALL, API, __FILE__, __LINE__,
        //           "GetHandleInfo: HTable[%d] is NULL\n",
        //           Hnd);
    } else if (HandleTable[Hnd] != nullptr) {
        *HndInfo = static_cast<struct Handle_Info *>(HandleTable[Hnd]);
        ret = (*HndInfo)->HType;
    }

    return ret;
}

int PrintHandleInfo(UpnpClient_Handle Hnd)
{
    struct Handle_Info * HndInfo;
    if (HandleTable[Hnd] != nullptr) {
        HndInfo = static_cast<struct Handle_Info*>(HandleTable[Hnd]);
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
    if((rootDir == nullptr) || (strlen(rootDir) == 0)) {
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


void UpnpRemoveAllVirtualDirs()
{
    web_server_clear_virtual_dirs();
}


int UpnpEnableWebserver(int enable)
{
    int retVal = UPNP_E_SUCCESS;

    if (UpnpSdkInit != 1) {
        return UPNP_E_FINISH;
    }

    switch (enable) {
#ifdef INTERNAL_WEB_SERVER
    case true:
        if((retVal = web_server_init()) != UPNP_E_SUCCESS) {
            return retVal;
        }
        break;

    case false:
        web_server_destroy();
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
int UpnpIsWebserverEnabled()
{
    if (UpnpSdkInit != 1) {
        return 0;
    }

    return bWebServerState == static_cast<WebServerState>(WEB_SERVER_ENABLED);
}

int UpnpSetVirtualDirCallbacks(struct UpnpVirtualDirCallbacks *callbacks)
{
    int ret = 0;

    if(UpnpSdkInit != 1) {
        /* SDK is not initialized */
        return UPNP_E_FINISH;
    }

    if (callbacks == nullptr)
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

int UpnpSetMaxContentLength(size_t contentLength)
{
    if (UpnpSdkInit != 1) {
        return  UPNP_E_FINISH;
    }
    g_maxContentLength = contentLength;
    return UPNP_E_SUCCESS;
}

int UpnpSetEventQueueLimits(int maxLen, int maxAge)
{
    g_UpnpSdkEQMaxLen = maxLen;
    g_UpnpSdkEQMaxAge = maxAge;
    return UPNP_E_SUCCESS;
}
