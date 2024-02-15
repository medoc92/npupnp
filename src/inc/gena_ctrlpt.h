/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
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

#ifndef GENA_CTRLPT_H
#define GENA_CTRLPT_H

#include <mutex>
#include <string>

#include "upnp.h"

struct ClientSubscription {
    int renewEventId;
    std::string SID;
    std::string eventURL;
    ClientSubscription(int id, std::string sid, std::string eURL) : renewEventId(id), SID(std::move(sid)), eventURL(std::move(eURL)) {}
    ~ClientSubscription() = default;
    ClientSubscription(const ClientSubscription& other) = default;
    ClientSubscription& operator=(const ClientSubscription& other) {
        if (this != &other) {
            SID = other.SID;
            eventURL = other.eventURL;
            renewEventId = -1;
        }
        return *this;
    }
};

extern std::mutex GlobalClientSubscribeMutex;

struct MHDTransaction;

/** Processes NOTIFY events that are sent by devices. */
void gena_process_notification_event(MHDTransaction *);

/*!
 * \brief This function subscribes to a PublisherURL (also mentioned as EventURL
 * in some places).
 *
 * It sends SUBSCRIBE http request to service processes request. Finally adds a
 * Subscription to the clients subscription list, if service responds with OK.
 *
 * \return UPNP_E_SUCCESS if service response is OK, otherwise returns the
 *    appropriate error code
 */
#ifdef INCLUDE_CLIENT_APIS
extern int genaSubscribe(
    /*! [in] The client handle. */
    UpnpClient_Handle client_handle,
    /*! [in] Of the form: "http://134.134.156.80:4000/RedBulb/Event */
    const std::string& PublisherURL,
    /*! [in,out] requested Duration:
     * \li if -1, then "infinite".
     * \li in the OUT case: actual Duration granted by Service,
     *     -1 for infinite. */
    int *TimeOut,
    /*! [out] sid of subscription, memory passed in by caller. */
    std::string *out_sid);

/*!
 * \brief Unsubscribes a SID.
 *
 * It first validates the SID and client_handle,copies the subscription, sends
 * UNSUBSCRIBE http request to service processes request and finally removes
 * the subscription.
 *
 * \return UPNP_E_SUCCESS if service response is OK, otherwise returns the
 *     appropriate error code.
 */
extern int genaUnSubscribe(
    /*! [in] UPnP client handle. */
    UpnpClient_Handle client_handle,
    /*! [in] The subscription ID. */
    const std::string& in_sid);


/*!
 * \brief Unsubcribes all the outstanding subscriptions and cleans the
 *     subscription list.
 *
 * This function is called when control point unregisters.
 *
 * \returns UPNP_E_SUCCESS if successful, otherwise returns the appropriate
 *     error code.
 */
extern int genaUnregisterClient(
    /*! [in] Handle containing all the control point related information. */
    UpnpClient_Handle client_handle);

/*!
 * \brief Renews a SID.
 *
 * It first validates the SID and client_handle and copies the subscription.
 * It sends RENEW (modified SUBSCRIBE) http request to service and processes
 * the response.
 *
 * \return UPNP_E_SUCCESS if service response is OK, otherwise the
 *     appropriate error code.
 */
extern int genaRenewSubscription(
    /*! [in] Client handle. */
    UpnpClient_Handle client_handle,
    /*! [in] Subscription ID. */
    const std::string& in_sid,
    /*! [in,out] requested Duration, if -1, then "infinite". In the OUT case:
     * actual Duration granted by Service, -1 for infinite. */
    int *TimeOut);
#endif /* INCLUDE_CLIENT_APIS */

#endif /* GENA_CTRLPT_H */

