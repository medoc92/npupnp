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


/*!
 * \file
 */


#include "config.h"

#include <string>

#include <string.h>
#include <time.h>


#include "client_table.h"
#include "httputils.h"
#include "miniserver.h"
#include "service_table.h"
#include "sock.h"
#include "ThreadPool.h"
#include "upnp.h"

#include "uri.h"


/*!
 * \brief XML version comment. Not used because it is not interopeable with
 * other UPnP vendors.
 */
#define XML_VERSION "<?xml version='1.0' encoding='ISO-8859-1' ?>\n"
#define XML_PROPERTYSET_HEADER \
	"<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">\n"


#define UNABLE_MEMORY "HTTP/1.1 500 Internal Server Error\r\n\r\n"
#define UNABLE_SERVICE_UNKNOWN "HTTP/1.1 404 Not Found\r\n\r\n"
#define UNABLE_SERVICE_NOT_ACCEPT "HTTP/1.1 503 Service Not Available\r\n\r\n"


#define NOT_IMPLEMENTED "HTTP/1.1 501 Not Implemented\r\n\r\n"
#define BAD_REQUEST "HTTP/1.1 400 Bad Request\r\n\r\n"
#define INVALID_NT BAD_CALLBACK
#define BAD_CALLBACK "HTTP/1.1 412 Precondition Failed\r\n\r\n" 
#define HTTP_OK_CRLF "HTTP/1.1 200 OK\r\n\r\n"
#define HTTP_OK_STR "HTTP/1.1 200 OK\r\n"
#define INVALID_SID BAD_CALLBACK
#define MISSING_SID BAD_CALLBACK
#define MAX_CONTENT_LENGTH 20
#define MAX_SECONDS 10
#define MAX_EVENTS 20
#define MAX_PORT_SIZE 10


#define GENA_E_BAD_RESPONSE UPNP_E_BAD_RESPONSE
#define GENA_E_BAD_SERVICE UPNP_E_INVALID_SERVICE
#define GENA_E_SUBSCRIPTION_UNACCEPTED UPNP_E_SUBSCRIBE_UNACCEPTED
#define GENA_E_BAD_SID UPNP_E_INVALID_SID
#define GENA_E_UNSUBSCRIBE_UNACCEPTED UPNP_E_UNSUBSCRIBE_UNACCEPTED
#define GENA_E_NOTIFY_UNACCEPTED UPNP_E_NOTIFY_UNACCEPTED
#define GENA_E_NOTIFY_UNACCEPTED_REMOVE_SUB -9
#define GENA_E_BAD_HANDLE UPNP_E_INVALID_HANDLE


#define XML_ERROR -5
#define XML_SUCCESS UPNP_E_SUCCESS
#define GENA_SUCCESS UPNP_E_SUCCESS


#define CALLBACK_SUCCESS 0
#define DEFAULT_TIMEOUT 1801


extern ithread_mutex_t GlobalClientSubscribeMutex;


/*!
 * \brief Locks the subscription.
 */
#define SubscribeLock() \
	UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, \
		"Trying Subscribe Lock\n");  \
	ithread_mutex_lock(&GlobalClientSubscribeMutex); \
	UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, \
		"Subscribe Lock\n");


/*!
 * \brief Unlocks the subscription.
 */
#define SubscribeUnlock() \
	UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, \
		"Trying Subscribe UnLock\n"); \
	ithread_mutex_unlock(&GlobalClientSubscribeMutex); \
	UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, \
		"Subscribe UnLock\n");


/*!
 * \brief This is the callback function called by the miniserver to handle
 *	incoming GENA requests.
 */
extern void genaCallback(MHDTransaction *);
 
/*!
 * \brief This function subscribes to a PublisherURL (also mentioned as EventURL
 * in some places).
 *
 * It sends SUBSCRIBE http request to service processes request. Finally adds a
 * Subscription to the clients subscription list, if service responds with OK.
 *
 * \return UPNP_E_SUCCESS if service response is OK, otherwise returns the 
 *	appropriate error code
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
	 * 	-1 for infinite. */
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
 * 	appropriate error code.
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
 * 	subscription list.
 *
 * This function is called when control point unregisters.
 *
 * \returns UPNP_E_SUCCESS if successful, otherwise returns the appropriate
 * 	error code.
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
 * 	appropriate error code.
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
 *	is it takes event variable array instead of xml document.
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
#endif /* INCLUDE_DEVICE_APIS */


/*!
 * \brief Sends a notification to all the subscribed control points.
 *
 * \return int
 *
 * \note This function is similar to the genaNotifyAll. the only difference
 *	is it takes the document instead of event variable array.
 */
#ifdef INCLUDE_DEVICE_APIS
extern int genaNotifyAllExt(
	/*! [in] Device handle. */
	UpnpDevice_Handle device_handle, 
	/*! [in] Device udn. */
	char *UDN,
	/*! [in] Service ID. */
	char *servId,
	/*! [in] XML document Event varible property set. */
	IXML_Document *PropSet);
#endif /* INCLUDE_DEVICE_APIS */


/*!
 * \brief Sends the intial state table dump to newly subscribed control point.
 *
 * \return GENA_E_SUCCESS if successful, otherwise the appropriate error code.
 * 
 * \note  No other event will be sent to this control point before the 
 *	intial state table dump.
 */
#ifdef INCLUDE_DEVICE_APIS
extern int genaInitNotify(
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

