/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 * Copyright (C) 2011-2012 France Telecom All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither name of Intel Corporation nor the names of its contributors
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


#ifndef UPNPAPI_H
#define UPNPAPI_H

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ThreadPool.h"
#include "TimerThread.h"
#include "VirtualDir.h"        /* for struct VirtualDirCallbacks */
#include "gena_ctrlpt.h"
#include "httputils.h"
#include "netif.h"
#include "service_table.h"
#include "ssdplib.h"
#include "upnp.h"
#include "upnpdescription.h"


#define DEFAULT_MX 5

#define DEFAULT_SOAP_CONTENT_LENGTH 16000
#define MAX_SOAP_CONTENT_LENGTH (size_t)32000

extern size_t g_maxContentLength;
extern int g_UpnpSdkEQMaxLen;
extern int g_UpnpSdkEQMaxAge;

typedef enum {HND_INVALID=-1, HND_CLIENT, HND_DEVICE} Upnp_Handle_Type;

struct SsdpSearchArg;

/* Data to be stored in handle table for */
struct Handle_Info
{
    Handle_Info() = default;
    ~Handle_Info() = default;
    Upnp_Handle_Type HType{HND_INVALID};
    Upnp_FunPtr  Callback{nullptr};
    void* Cookie{nullptr};

    /* Device Only */
#ifdef INCLUDE_DEVICE_APIS
    /*! URL for the use of SSDP. */
    char  DescURL[LINE_SIZE];
    /*! URL for the use of SSDP when answering to legacy CPs (CP searching
     * for a v1 when the device is v2). */
    char  LowerDescURL[LINE_SIZE];
    /* Product/version value to send out in SERVER fields */
    std::string productversion;
    /* Advertisement timeout */
    int MaxAge{0};
    /* Power State as defined by UPnP Low Power. */
    int PowerState{0};
    /* Sleep Period as defined by UPnP Low Power. */
    int SleepPeriod{0};
    /* Registration State as defined by UPnP Low Power. */
    int RegistrationState{0};
    /*! Parsed Device Description document. */
    UPnPDeviceDesc devdesc;
    /*! Service information and subscriptions lists */
    std::list<service_info> serviceTable;
    /*! . */
    int MaxSubscriptions{0};
    /*! . */
    int MaxSubscriptionTimeOut{0};
#endif

    /* Client only */
#ifdef INCLUDE_CLIENT_APIS
    /*! Client subscription list. */
    std::list<ClientSubscription> ClientSubList;
    /*! Active SSDP searches. */
    std::list<SsdpSearchArg> SsdpSearchList;
    int SubsOpsTimeoutMS{HTTP_DEFAULT_TIMEOUT * 1000};
#endif

    // Forbid copy construction and assignment
    Handle_Info(const Handle_Info& rhs) = delete;
    Handle_Info& operator=(const Handle_Info& rhs) = delete;
};


/*!
 * \brief Get handle information.
 *
 * \return HND_DEVICE, UPNP_E_INVALID_HANDLE
 */
Upnp_Handle_Type GetHandleInfo(
    /*! handle pointer (key for the client handle structure). */
    int Hnd,
    /*! handle structure passed by this function. */
    struct Handle_Info **HndInfo);


// Global lock for the handle table
extern std::mutex globalHndLock;

#define HANDLELOCK() std::scoped_lock<std::mutex> hdllock(globalHndLock)

/*!
 * \brief Get client handle info.
 *
 * \note The logic around the use of this function should be revised.
 *
 * \return HND_CLIENT, HND_INVALID
 */
Upnp_Handle_Type GetClientHandleInfo(
    /*! [in] client handle pointer (key for the client handle structure). */
    int *client_handle_out,
    /*! [out] Client handle structure passed by this function. */
    struct Handle_Info **HndInfo);

/*!
 * \brief Retrieves the device handle and information of the first
 *  device of the address family specified. The search begins at the 'start'
 *  index, which should be 0 for the first call, then the last successful value
 *  returned. This allows listing all entries for the address family.
 *
 * \return HND_DEVICE or HND_INVALID
 */
Upnp_Handle_Type GetDeviceHandleInfo(
    /*! [in] place to start the search (i.e. last value returned). */
    UpnpDevice_Handle start,
    /*! [out] Device handle pointer. */
    int *device_handle_out,
    /*! [out] Device handle structure passed by this function. */
    struct Handle_Info **HndInfo);

/*!
 * \brief Retrieves the device handle and information of the first device of
 *     the address family specified, with a service having a controlURL or
 *     eventSubURL matching the path.
 *
 * \return HND_DEVICE or HND_INVALID
 */
Upnp_Handle_Type GetDeviceHandleInfoForPath(
    /*! The Uri path. */
    const std::string& path,
    /*! [out] Device handle pointer. */
    int *devhdl,
    /*! [out] Device handle structure passed by this function. */
    struct Handle_Info **HndInfo,
    /*! [out] Service info for found path. */
    service_info **serv_info
    );

extern unsigned short LOCAL_PORT_V4;
extern unsigned short LOCAL_PORT_V6;
/* The network interfaces we were told to use */
extern std::vector<NetIF::Interface> g_netifs;
/* Small optimisation: if the interfaces parameter to UpnpInit2() was
   "*", no need for the web server to filter accepted connections */
extern bool g_use_all_interfaces;

extern unsigned int g_optionFlags;
extern int g_bootidUpnpOrg;
extern int g_configidUpnpOrg;

extern WebCallback_HostValidate g_hostvalidatecallback;
extern void *g_hostvalidatecookie;

// Test if we should do ipv6-related stuff. The flag is set by the
// init caller, and can be reset during init if we find no
// ipv6-enabled interface
inline bool using_ipv6() {
#if !defined(UPNP_ENABLE_IPV6) || defined(__APPLE__)
    return false;
#else
    return 0 != (g_optionFlags & UPNP_FLAG_IPV6);
#endif
}

// Get first usable addresses, for when we don't care
extern std::string apiFirstIPV4Str();
extern std::string apiFirstIPV6Str();
extern int apiFirstIPV6Index();

extern const std::string g_HostForTemplate;

/*! NLS uuid. */
extern Upnp_SID gUpnpSdkNLSuuid;

extern TimerThread *gTimerThread;
extern ThreadPool gRecvThreadPool;
extern ThreadPool gSendThreadPool;
extern ThreadPool gMiniServerThreadPool;

extern struct VirtualDirCallbacks virtualDirCallback;

int PrintHandleInfo(UpnpClient_Handle Hnd);

#endif /* UPNPAPI_H */

