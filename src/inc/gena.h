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
#ifndef GENA_H
#define GENA_H

#include "config.h"

#include <string>
#include <mutex>

#include <string.h>
#include <time.h>

#include "httputils.h"
#include "miniserver.h"
#include "service_table.h"
#include "upnp.h"
#include "uri.h"


#define GENA_E_BAD_RESPONSE UPNP_E_BAD_RESPONSE
#define GENA_E_BAD_SERVICE UPNP_E_INVALID_SERVICE
#define GENA_E_SUBSCRIPTION_UNACCEPTED UPNP_E_SUBSCRIBE_UNACCEPTED
#define GENA_E_BAD_SID UPNP_E_INVALID_SID
#define GENA_E_UNSUBSCRIBE_UNACCEPTED UPNP_E_UNSUBSCRIBE_UNACCEPTED
#define GENA_E_NOTIFY_UNACCEPTED UPNP_E_NOTIFY_UNACCEPTED
#define GENA_E_NOTIFY_UNACCEPTED_REMOVE_SUB -9
#define GENA_E_BAD_HANDLE UPNP_E_INVALID_HANDLE

#define GENA_SUCCESS UPNP_E_SUCCESS

#define GENA_DEFAULT_TIMEOUT 1801

/** miniserver incoming GENA request callback function. */
extern void genaCallback(MHDTransaction *);
 
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
#endif /* INCLUDE_CLIENT_APIS */


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
#ifdef INCLUDE_CLIENT_APIS
extern int genaUnSubscribe(
    /*! [in] UPnP client handle. */
    UpnpClient_Handle client_handle,
    /*! [in] The subscription ID. */
    const std::string& in_sid);
#endif /* INCLUDE_CLIENT_APIS */


/*!
 * \brief Unsubcribes all the outstanding subscriptions and cleans the
 *     subscription list.
 *
 * This function is called when control point unregisters.
 *
 * \returns UPNP_E_SUCCESS if successful, otherwise returns the appropriate
 *     error code.
 */
#ifdef INCLUDE_CLIENT_APIS
extern int genaUnregisterClient(
    /*! [in] Handle containing all the control point related information. */
    UpnpClient_Handle client_handle);
#endif /* INCLUDE_CLIENT_APIS */


/*
 * DEVICE
 */


/*!
 * \brief Cleans the service table of the device.
 *
 * \return UPNP_E_SUCCESS if successful, otherwise returns GENA_E_BAD_HANDLE
 */
#ifdef INCLUDE_DEVICE_APIS
extern int genaUnregisterDevice(
     /*! [in] Handle of the root device */
    UpnpDevice_Handle device_handle);
#endif /* INCLUDE_CLIENT_APIS */


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
#ifdef INCLUDE_CLIENT_APIS
extern int genaRenewSubscription(
    /*! [in] Client handle. */
    UpnpClient_Handle client_handle,
    /*! [in] Subscription ID. */
    const std::string& in_sid,
    /*! [in,out] requested Duration, if -1, then "infinite". In the OUT case:
     * actual Duration granted by Service, -1 for infinite. */
    int *TimeOut);
#endif /* INCLUDE_CLIENT_APIS */


/*!
 * \brief Sends a notification to all the subscribed control points.
 *
 * \return int
 *
 * \note This function is similar to the genaNotifyAllExt. The only difference
 *    is it takes event variable array instead of xml document.
 */
#ifdef INCLUDE_DEVICE_APIS
extern int genaNotifyAll(
    /*! [in] Device handle. */
    UpnpDevice_Handle device_handle,
    /*! [in] Device udn. */
    char *UDN,
    /*! [in] Service ID. */
    char *servId,
    /*! [in] Array of varible names. */
    char **VarNames,
    /*! [in] Array of variable values. */
    char **VarValues,
    /*! [in] Number of variables. */
    int var_count);
extern int genaNotifyAllXML(
    /*! [in] Device handle. */
    UpnpDevice_Handle device_handle,
    /*! [in] Device udn. */
    char *UDN,
    /*! [in] Service ID. */
    char *servId,
    const std::string& propertySet);


/*!
 * \brief Sends the intial state table dump to newly subscribed control point.
 *
 * \return GENA_E_SUCCESS if successful, otherwise the appropriate error code.
 * 
 * \note  No other event will be sent to this control point before the 
 *    intial state table dump.
 */
extern int genaInitNotifyXML(
    /*! [in] Device handle. */
    UpnpDevice_Handle device_handle,
    /*! [in] Device udn. */
    char *UDN,
    /*! [in] Service ID. */
    char *servId,
    const std::string& propertyset,
    /*! [in] Subscription ID. */
    const Upnp_SID sid);

extern int genaInitNotifyVars(
    /*! [in] Device handle. */
    UpnpDevice_Handle device_handle,
    /*! [in] Device udn. */
    char *UDN,
    /*! [in] Service ID. */
    char *servId,
    /*! [in] Array of variable names. */
    char **VarNames,
    /*! [in] Array of variable values. */
    char **VarValues,
    /*! [in] Array size. */
    int var_count,
    /*! [in] Subscription ID. */
    const Upnp_SID sid);

#endif /* INCLUDE_DEVICE_APIS */


#endif /* GENA_H */
