/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 * Copyright (c) 2012 France Telecom All rights reserved.
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

#ifndef SERVICE_TABLE_H
#define SERVICE_TABLE_H

#include <ctime>
#include <list>
#include <string>
#include <vector>
#include <memory>

#include "config.h"
#include "upnp.h"
#include "upnpdebug.h"
#include "upnpdescription.h"

#define SID_SIZE  size_t(41)

#ifdef INCLUDE_DEVICE_APIS

struct Notification;
struct subscription {
    Upnp_SID sid; /* char[44] in upnp.h */
    int ToSendEventKey{0};
    time_t expireTime{0};
    int active{0};
    std::vector<std::string> DeliveryURLs;
    /* List of queued events for this subscription. Only one event job
       at a time goes into the thread pool. The first element in the
       list is a copy of the active job. Others are activated on job
       completion. */
    std::list<std::shared_ptr<Notification>> outgoing;
};

struct service_info {
    std::string serviceType;
    std::string serviceId;
    std::string SCPDURL;
    std::string controlURL;
    std::string eventURL;
    std::string UDN;
    int        active{0};
    int        TotalSubscriptions{0};
    std::list<subscription>    subscriptionList;

    service_info() = default;
    ~service_info() = default;
    service_info& operator=(const service_info&) = delete;
    service_info(const service_info& rhs) = delete;
};

using service_table =  std::list<service_info>;

/*!
 * \brief Makes a copy of the subscription.
 *
 * \return UPNP_E_SUCCESS on success.
 */
int copy_subscription(
    /*! [in] Source subscription. */
    const subscription *in,
    /*! [in] Destination subscription. */
    subscription *out);

/*
 * \brief Remove the subscription represented by the const Upnp_SID sid parameter
 * from the service table and update the service table.
 */
void RemoveSubscriptionSID(
    /*! [in] Subscription ID. */
    const Upnp_SID& sid,
    /*! [in] Service object providing the list of subscriptions. */
    service_info *service);

/*!
 * \brief Return the subscription from the service table that matches
 * const Upnp_SID sid value.
 *
 * \return Pointer to the matching subscription node.
 */
subscription *GetSubscriptionSID(
    /*! [in] Subscription ID. */
    const Upnp_SID& sid,
    /*! [in] Service object providing the list of subscriptions. */
    service_info *service);

/*!
 * \brief Gets pointer to the first subscription node in the service table.
 *
 * \return Pointer to the first subscription node.
 */
std::list<subscription>::iterator GetFirstSubscription(
    /*! [in] Service object providing the list of subscriptions. */
    service_info *service);

/*!
 * \brief Get current and valid subscription from the service table.
 *
 * \return Pointer to the next subscription node.
 */

std::list<subscription>::iterator GetNextSubscription(
    service_info *service, std::list<subscription>::iterator current,
    bool getfirst = false);

/*!
 * \brief Traverses through the service table and returns a pointer to the
 * service node that matches a known service id and a known UDN.
 *
 * \return Pointer to the matching service_info node.
 */
service_info *FindServiceId(
    /*! [in] Service table. */
    service_table& table,
    /*! [in] String representing the service id to be found among those
     * in the table. */
    const std::string& serviceId,
    /*! [in] String representing the UDN to be found among those in the
     * table. */
    const std::string& UDN);

/*!
 * \brief Traverses the service table and finds the node whose event URL Path
 * matches a know value.
 *
 * \return Pointer to the service list node from the service table whose event
 * URL matches a known event URL.
 */
service_info *FindServiceEventURLPath(
    /*! [in] Service table. */
    service_table& table,
    /*! [in] Event URL path used to find a service from the table. */
    const std::string& eventURLPath);

/*!
 * \brief Traverses the service table and finds the node whose control URL Path
 * matches a know value.
 *
 * \return Pointer to the service list node from the service table whose control
 * URL Path matches a known value.
 */
service_info * FindServiceControlURLPath(
    /*! [in] Service table. */
    service_table& table,
    /*! [in] Control URL path used to find a service from the table. */
    const std::string& controlURLPath);

/*!
 * \brief For debugging purposes prints information from the service passed
 * into the function.
 */
#ifdef DEBUG
void printService(const service_info *service, Upnp_LogLevel level, Dbg_Module module);
void printServiceTable(const service_table& table, Upnp_LogLevel level, Dbg_Module module);
#else
static inline void printService(const service_info*, Upnp_LogLevel, Dbg_Module) {}
static inline void printServiceTable(const service_table&, Upnp_LogLevel, Dbg_Module) {}
#endif

/*!
 * \brief Free's dynamic memory in table (does not free table, only memory
 * within the structure).
 */
void clearServiceTable(service_table& table);

/*!
 * \brief Retrieve service from the table.
 *
 * \return An integer
 */
int initServiceTable(
    const UPnPDeviceDesc& devdesc,
    /*! [in] Output parameter which will contain the service list and URL. */
    service_table& out);

#endif /* INCLUDE_DEVICE_APIS */

#endif /* SERVICE_TABLE_H */

