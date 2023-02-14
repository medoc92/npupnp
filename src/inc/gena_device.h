/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
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

#ifndef GENA_DEVICE_H
#define GENA_DEVICE_H

#ifdef INCLUDE_DEVICE_APIS
/*!
 * \brief Cleans the service table of the device.
 *
 * \return UPNP_E_SUCCESS if successful, otherwise returns GENA_E_BAD_HANDLE
 */
extern int genaUnregisterDevice(
     /*! [in] Handle of the root device */
    UpnpDevice_Handle device_handle);

/*!
 * \brief Sends a notification to all the subscribed control points.
 *
 * \return int
 *
 * \note This function is similar to the genaNotifyAllExt. The only difference
 *    is it takes event variable array instead of xml document.
 */
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
    const Upnp_SID& sid);

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
    const Upnp_SID& sid);

#endif /* INCLUDE_DEVICE_APIS */


struct MHDTransaction;

/*!
 * \brief Handles a subscription request from a ctrl point.
 */
void gena_process_subscription_request(MHDTransaction *);

/*!
 * \brief Handles a subscription renewal request from a ctrl point.
 */
void gena_process_subscription_renewal_request(MHDTransaction *);

/*!
 * \brief Handles a subscription cancellation request from a ctrl point.
 */
void gena_process_unsubscribe_request(MHDTransaction *);

#endif /* GENA_DEVICE_H */

