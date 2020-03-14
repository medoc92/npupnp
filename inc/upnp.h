#ifndef UPNP_H
#define UPNP_H

/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * Copyright (C) 2011-2012 France Telecom All rights reserved.
 * Copyright (C) 2020 J.F. Dockes <jf@dockes.org>
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

#include <string>
#include <vector>
#include <unordered_map>
#include <map>

#include "upnpconfig.h"
#include "UpnpInet.h"
#include "UpnpGlobal.h"

#define LINE_SIZE  (size_t)180
#define NAME_SIZE  (size_t)256
#define UPNP_INFINITE		-1

/*!
 * \name Error codes 
 *
 * The functions in the SDK API can return a variety of error 
 * codes to describe problems encountered during execution.  This section 
 * lists the error codes and provides a brief description of what each error 
 * code means.  Refer to the documentation for each function for a 
 * description of what an error code means in that context.
 *
 * @{
 */

/*!
 * \brief The operation completed successfully.
 *
 */
#define UPNP_E_SUCCESS			0

/*!
 * \brief The handle passed to a function is not a recognized as a valid handle.
 */
#define UPNP_E_INVALID_HANDLE		-100

/*!
 * \brief One or more of the parameters passed to the function is not valid.
 *
 * Refer to the documentation for each function for more information on the
 * valid ranges of the parameters.
 */
#define UPNP_E_INVALID_PARAM		-101

/*!
 * \brief The SDK does not have any more space for additional handles.
 *
 * The SDK allocates space for only a few handles in order to conserve memory.
 */
#define UPNP_E_OUTOF_HANDLE		-102

#define UPNP_E_OUTOF_CONTEXT		-103

/*!
 * \brief Not enough resources are currently available to complete the operation.
 *
 * Most operations require some free memory in order to complete their work.
 */
#define UPNP_E_OUTOF_MEMORY		-104

/*!
 * \brief The SDK has already been initialized.
 *
 * The SDK needs to be initialied only once per process. Any additional
 * initialization attempts simply return this error with no other ill effects.
 */
#define UPNP_E_INIT			-105

#define UPNP_E_BUFFER_TOO_SMALL		-106

/*!
 * \brief The description document passed to \b UpnpRegisterRootDevice,
 * \b UpnpRegisterRootDevice2 \b UpnpRegisterRootDevice3 or
 * \b UpnpRegisterRootDevice4 is invalid.
 */
#define UPNP_E_INVALID_DESC		-107

/*!
 * \brief An URL passed into the function is invalid.
 *
 * The actual cause is function specific, but in general, the URL itself
 * might be malformed (e.g. have invalid characters in it) or the host might
 * be unreachable.
 */
#define UPNP_E_INVALID_URL		-108

#define UPNP_E_INVALID_SID		-109

#define UPNP_E_INVALID_DEVICE		-110

/*!
 * \brief The device ID/service ID pair does not refer to a valid service.
 *
 * Returned only by \b UpnpNotify, \b UpnpNotifyExt, \b UpnpAcceptSubscription,
 * and \b UpnpAcceptSubscriptionExt.
 */
#define UPNP_E_INVALID_SERVICE		-111

/*!
 * \brief The response received from the remote side of a connection is not correct
 * for the protocol.
 *
 * This applies to the GENA, SOAP, and HTTP protocols.
 */
#define UPNP_E_BAD_RESPONSE		-113

#define UPNP_E_BAD_REQUEST		-114

/*!
 * \brief The SOAP action message is invalid.
 *
 * This can be because the DOM document passed to the function was malformed or
 * the action message is not correct for the given action.
 */
#define UPNP_E_INVALID_ACTION		-115

/*!
 * \brief \b UpnpInit has not been called, or \b UpnpFinish has already been called.
 *
 * None of the API functions operate until \b UpnpInit successfully completes.
 */
#define UPNP_E_FINISH			-116

/*!
 * \brief \b UpnpInit cannot complete.  
 *
 * The typical reason is failure to allocate sufficient resources.
 */
#define UPNP_E_INIT_FAILED		-117

/*!
 * \brief The URL passed into a function is too long.
 *
 * The SDK limits URLs to 180 characters in length.  
 */
#define UPNP_E_URL_TOO_BIG		-118

/*!
 * \brief The HTTP message contains invalid message headers.
 *
 * The error always refers to the HTTP message header received from the remote
 * host.  The main areas where this occurs are in SOAP control messages (e.g.
 * \b UpnpSendAction), GENA subscription message (e.g. \b UpnpSubscribe),
 * GENA event notifications (e.g. \b UpnpNotify), and HTTP transfers (e.g.
 * \b UpnpDownloadXmlDoc).
 */
#define UPNP_E_BAD_HTTPMSG		-119

/*!
 * \brief A client or a device is already registered.
 *
 * The SDK currently has a limit of one registered client and one registered
 * device per process.
 */
#define UPNP_E_ALREADY_REGISTERED	-120

/*!
 * \brief The interface provided to \b UpnpInit2 is unknown or does not have a valid
 * IPv4 or IPv6 address configured.
 */
#define UPNP_E_INVALID_INTERFACE	-121

/*!
 * \brief A network error occurred.
 *
 * It is the generic error code for network problems that are not covered under
 * one of the more specific error codes.  The typical meaning is the SDK failed
 * to read the local IP address or had problems configuring one of the sockets.
 */
#define UPNP_E_NETWORK_ERROR		-200

/*!
 * \brief An error happened while writing to a socket.
 *
 * This occurs in any function that makes network connections, such as discovery
 * (e.g. \b UpnpSearchAsync or \b UpnpSendAdvertisement), control (e.g. 
 * \b UpnpSendAction), eventing (e.g. \b UpnpNotify), and HTTP functions (e.g.
 * \b UpnpDownloadXmlDoc).
 */
#define UPNP_E_SOCKET_WRITE		-201

/*!
 * \brief An error happened while reading from a socket.
 *
 * This occurs in any function that makes network connections, such as discovery
 * (e.g. \b UpnpSearchAsync or \b UpnpSendAdvertisement), control (e.g.
 * \b UpnpSendAction), eventing (e.g. \b UpnpNotify), and HTTP functions (e.g.
 * \b UpnpDownloadXmlDoc).
 */
#define UPNP_E_SOCKET_READ		-202

/*!
 * \brief The SDK had a problem binding a socket to a network interface.
 *
 * This occurs in any function that makes network connections, such as discovery
 * (e.g. \b UpnpSearchAsync or \b UpnpSendAdvertisement), control (e.g.
 * \b UpnpSendAction), eventing (e.g. \b UpnpNotify), and HTTP functions (e.g. 
 * \b UpnpDownloadXmlDoc).
 */
#define UPNP_E_SOCKET_BIND		-203

/*!
 * \brief The SDK had a problem connecting to a remote host.
 *
 * This occurs in any function that makes network connections, such as discovery
 * (e.g. \b UpnpSearchAsync or \b UpnpSendAdvertisement), control (e.g.
 * \b UpnpSendAction), eventing (e.g. \b UpnpNotify), and HTTP functions (e.g. 
 * \b UpnpDownloadXmlDoc).
 */
#define UPNP_E_SOCKET_CONNECT		-204

/*!
 * \brief The SDK cannot create any more sockets.
 *
 * This occurs in any function that makes network connections, such as discovery
 * (e.g. \b UpnpSearchAsync or \b UpnpSendAdvertisement), control (e.g.
 * \b UpnpSendAction), eventing (e.g. \b UpnpNotify), and HTTP functions (e.g. 
 * \b UpnpDownloadXmlDoc).
 */
#define UPNP_E_OUTOF_SOCKET		-205

/*!
 * \brief The SDK had a problem setting the socket to listen for incoming
 * connections.
 *
 * This error only happens during initialization (i.e. \b UpnpInit).
 */
#define UPNP_E_LISTEN			-206

/*!
 * \brief Too much time elapsed before the required number of bytes were sent
 * or received over a socket.
 *
 * This error can be returned by any function that performs network operations.
 */
#define UPNP_E_TIMEDOUT			-207

/*!
 * \brief Generic socket error code for conditions not covered by other error
 * codes.
 *
 * This error can be returned by any function that performs network operations.
 */
#define UPNP_E_SOCKET_ERROR		-208

#define UPNP_E_FILE_WRITE_ERROR		-209

/*! \brief The operation was canceled.
 *
 * This error can be returned by any function that allows for external cancelation.
 */
#define UPNP_E_CANCELED			-210

#define UPNP_E_EVENT_PROTOCOL		-300

/*!
 * \brief A subscription request was rejected from the remote side.  
 */
#define UPNP_E_SUBSCRIBE_UNACCEPTED	-301

/*!
 * \brief An unsubscribe request was rejected from the remote side.
 */
#define UPNP_E_UNSUBSCRIBE_UNACCEPTED	-302

/*!
 * \brief The remote host did not accept the notify sent from the local device.
 */
#define UPNP_E_NOTIFY_UNACCEPTED	-303

/*!
 * \brief One or more of the parameters passed to a function is invalid.
 *
 * Refer to the individual function descriptions for the acceptable ranges for
 * parameters.
 */
#define UPNP_E_INVALID_ARGUMENT		-501

/*!
 * \brief The filename passed to one of the device registration functions was
 * not found or was not accessible.
 */
#define UPNP_E_FILE_NOT_FOUND		-502

/*!
 * \brief An error happened while reading a file.
 */
#define UPNP_E_FILE_READ_ERROR		-503

/*!
 * \brief The file name of the description document passed to
 * \b UpnpRegisterRootDevice2 does not end in ".xml".
 */
#define UPNP_E_EXT_NOT_XML		-504

#define UPNP_E_NO_WEB_SERVER		-505
#define UPNP_E_OUTOF_BOUNDS		-506

/*!
 * \brief The response to a SOAP request did not contain the required XML
 * constructs.  
 */
#define UPNP_E_NOT_FOUND		-507

/*!
 * \brief Generic error code for internal conditions not covered by other
 * error codes.
 */
#define UPNP_E_INTERNAL_ERROR		-911

/* SOAP-related error codes */
#define UPNP_SOAP_E_INVALID_ACTION	401
#define UPNP_SOAP_E_INVALID_ARGS	402
#define UPNP_SOAP_E_OUT_OF_SYNC		403
#define UPNP_SOAP_E_INVALID_VAR		404
#define UPNP_SOAP_E_ACTION_FAILED	501

/* @} ErrorCodes */


/*!
 * \name Constants and Types
 *
 * @{
 */

enum UpnpOpenFileMode
{
	UPNP_READ,
	UPNP_WRITE
};

/*!
 * \brief Returned when a control point application registers with
 * \b UpnpRegisterClient.
 *
 * Client handles can only be used with functions that operate with a client
 * handle.
 */
typedef int  UpnpClient_Handle;

/*!
 * \brief Returned when a device application registers with
 * \b UpnpRegisterRootDevice, \b UpnpRegisterRootDevice2,
 * \b UpnpRegisterRootDevice3 or \b UpnpRegisterRootDevice4.
 *
 * Device handles can only be used with functions that operate with a device
 * handle.
 */
typedef int  UpnpDevice_Handle;

/*!
 * \brief The reason code for an event callback.
 *
 * The \b Event parameter will be different depending on the reason for the
 * callback. The descriptions for each event type describe the contents of the
 * \b Event parameter.
 */
enum Upnp_EventType_e {
	/*
	 * Control callbacks
	 */

	/*! Received by a device when a control point issues a control
	 * request.  The \b Event parameter contains a pointer to a \b
	 * UpnpActionRequest structure containing the action.  The application
	 * stores the results of the action in this structure. */
	UPNP_CONTROL_ACTION_REQUEST,

	/*! A \b UpnpSendActionAsync call completed. Not used at the moment. */
	UPNP_CONTROL_ACTION_COMPLETE,

	/*! Not used UPNP State var queries are deprecated. */
	UPNP_CONTROL_GET_VAR_REQUEST,

	/*! Not used */
	UPNP_CONTROL_GET_VAR_COMPLETE,

	/*
	 * Discovery callbacks
	 */

	/*! Received by a control point when a new device or service is available.  
	 * The \b Event parameter contains a pointer to a \b
	 * UpnpDiscovery structure with the information about the device
	 * or service.  */
	UPNP_DISCOVERY_ADVERTISEMENT_ALIVE,

	/*! Received by a control point when a device or service shuts down. The \b
	 * Event parameter contains a pointer to a \b UpnpDiscovery
	 * structure containing the information about the device or
	 * service.  */
	UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE,

	/*! Received by a control point when a matching device or service responds.
	 * The \b Event parameter contains a pointer to a \b
	 * UpnpDiscovery structure containing the information about
	 * the reply to the search request.  */
	UPNP_DISCOVERY_SEARCH_RESULT,

	/*! Received by a control point when the search timeout expires.  The
	 * SDK generates no more callbacks for this search after this 
	 * event.  The \b Event parameter is \c NULL.  */
	UPNP_DISCOVERY_SEARCH_TIMEOUT,

	/*
	 * Eventing callbacks
	 */

	/*! Received by a device when a subscription arrives.
	 * The \b Event parameter contains a pointer to a \b
	 * UpnpSubscriptionRequest structure.  At this point, the
	 * subscription has already been accepted.  \b UpnpAcceptSubscription
	 * needs to be called to confirm the subscription and transmit the
	 * initial state table.  This can be done during this callback.  The SDK
	 * generates no events for a subscription unless the device 
	 * application calls \b UpnpAcceptSubscription.
	 */
	UPNP_EVENT_SUBSCRIPTION_REQUEST,

	/*! Received by a control point when an event arrives. The \b
	 * Event parameter contains a \b UpnpEvent structure
	 * with the information about the event.  */
	UPNP_EVENT_RECEIVED,

	/*! A \b UpnpRenewSubscriptionAsync call completed. Not used */
	UPNP_EVENT_RENEWAL_COMPLETE,

	/*! A \b UpnpSubscribeAsync call completed. Not used */
	UPNP_EVENT_SUBSCRIBE_COMPLETE,

	/*! A \b UpnpUnSubscribeAsync call completed. Not used */
	UPNP_EVENT_UNSUBSCRIBE_COMPLETE,

	/*! The auto-renewal of a client subscription failed.   
	 * The \b Event parameter is a \b UpnpEventSubscribe structure 
	 * with the error code set appropriately. The subscription is no longer 
	 * valid. */
	UPNP_EVENT_AUTORENEWAL_FAILED,

	/*! A client subscription has expired. This will only occur 
	 * if auto-renewal of subscriptions is disabled.
	 * The \b Event parameter is a \b UpnpEventSubscribe
	 * structure. The subscription is no longer valid. */
	UPNP_EVENT_SUBSCRIPTION_EXPIRED
};

typedef enum Upnp_EventType_e Upnp_EventType;

/*!
 * \brief Holds the subscription identifier for a subscription between a
 * client and a device.
 *
 * The SID is a string representation of a globally unique id (GUID) and should
 * not be modified.
 */
typedef char Upnp_SID[44];

/*!
 * \brief Specifies the type of description in \b UpnpRegisterRootDevice2.
 *
 * These values control how \b UpnpRegisterRootDevice2 interprets the
 * \b description parameter.
 */
enum Upnp_DescType_e { 
	/*! The description is the URL to the description document. */
	UPNPREG_URL_DESC, 
	
	/*! The description is a file name on the local file system 
	    containing the description of the device. */
	UPNPREG_FILENAME_DESC,
    
	/*! The description is a pointer to a character array containing 
	    the XML description document. */
	UPNPREG_BUF_DESC 
};

typedef enum Upnp_DescType_e Upnp_DescType;

/** Used in the device callback API as parameter for 
	UPNP_CONTROL_ACTION_REQUEST */
struct Upnp_Action_Request {
	/** The result of the operation. */
	int ErrCode;

	/** The socket number of the connection to the requestor. */
	int Socket;

	/** The error string in case of error. */
	char ErrStr[LINE_SIZE];

	/** The Action Name. */
	char ActionName[NAME_SIZE];

	/** The unique device ID. */
	char DevUDN[NAME_SIZE];

	/** The service ID. */
	char ServiceID[NAME_SIZE];

	/** [input] The action arguments */
	std::vector<std::pair<std::string, std::string> > args;

	/** [output] The action results. */
	std::vector<std::pair<std::string, std::string> > resdata;

	/** IP address of the control point requesting this action. */
	struct sockaddr_storage CtrlPtIPAddr;

	/** The XML request document in case the callback has something
	   else to get from there. This is always set in addition to the
	   args vector */
	std::string xmlAction;

	/** Alternative data return: return an XML document instead of
		using the resdata vector. If this is not empty on callback
		return, it is used instead of resdata. This is to ease the
		transition from the ixml-based interface */
	std::string xmlResponse;
};

/* compat code for libupnp-1.8 */
typedef struct Upnp_Action_Request UpnpActionRequest;
#define UpnpActionRequest_get_ErrCode(x) ((x)->ErrCode)
#define UpnpActionRequest_set_ErrCode(x, v) ((x)->ErrCode = (v))
#define UpnpActionRequest_get_Socket(x) ((x)->Socket)
#define UpnpActionRequest_get_ErrStr_cstr(x) ((x)->ErrStr)
#define UpnpActionRequest_set_ErrStr(x, v) (strncpy((x)->ErrStr, v, LINE_SIZE))
#define UpnpActionRequest_strcpy_ErrStr(x, v) (strncpy((x)->ErrStr, v, LINE_SIZE))
#define UpnpActionRequest_get_ActionName_cstr(x) ((x)->ActionName)
#define UpnpActionRequest_get_DevUDN_cstr(x) ((x)->DevUDN)
#define UpnpActionRequest_get_ServiceID_cstr(x) ((x)->ServiceID)
#define UpnpActionRequest_get_xmlAction(x) ((x)->xmlAction)
#define UpnpActionRequest_get_xmlResponse(x) ((x)->xmlResponse)
#define UpnpActionRequest_set_xmlResponse(x, v) ((x)->xmlResponse = (v))


/** Returned along with a {\bf UPNP_EVENT_RECEIVED} callback.  */

struct Upnp_Event
{
	/** The subscription ID for this subscription. */
	Upnp_SID Sid;

	/** The event sequence number. */
	int EventKey;

	/** The changes generating the event. map would have been a better choice, 
	 * but too late to change... */
	std::unordered_map<std::string, std::string> ChangedVariables;
};

/* compat code for libupnp-1.8 */
typedef struct Upnp_Event UpnpEvent;
#define UpnpEvent_get_SID_cstr(x) ((x)->Sid)
#define UpnpEvent_get_EventKey(x) ((x)->EventKey)
#define UpnpEvent_get_ChangedVariables(x) ((x)->ChangedVariables)

/** Returned in a {\bf UPNP_DISCOVERY_RESULT} callback. */
struct Upnp_Discovery
{
	/** The result code of the {\bf UpnpSearchAsync} call. */
	int  ErrCode;                  
				     
	/** The expiration time of the advertisement. */
	int  Expires;                  
				     
	/** The unique device identifier. */
	char DeviceId[LINE_SIZE];      

	/** The device type. */
	char DeviceType[LINE_SIZE];    

	/** The service type. */
	char ServiceType[LINE_SIZE];

	/** The service version. */
	char ServiceVer[LINE_SIZE];    

	/** The URL to the UPnP description document for the device. */
	char Location[LINE_SIZE];      

	/** The operating system the device is running. */
	char Os[LINE_SIZE];            
				     
	/** Date when the response was generated. */
	char Date[LINE_SIZE];            
				     
	/** Confirmation that the MAN header was understood by the device. */
	char Ext[LINE_SIZE];           
				     
	/** The host address of the device responding to the search. */
	struct sockaddr_storage DestAddr;
};

/* compat code for libupnp-1.8 */
typedef struct Upnp_Discovery UpnpDiscovery;
#define UpnpDiscovery_get_ErrCode(x) ((x)->ErrCode)
#define UpnpDiscovery_get_Expires(x) ((x)->Expires)
#define UpnpDiscovery_get_DeviceID_cstr(x) ((x)->DeviceId)
#define UpnpDiscovery_get_DeviceType_cstr(x) ((x)->DeviceType)
#define UpnpDiscovery_get_ServiceType_cstr(x) ((x)->ServiceType)
#define UpnpDiscovery_get_ServiceVer_cstr(x) ((x)->ServiceVer)
#define UpnpDiscovery_get_Location_cstr(x) ((x)->Location)
#define UpnpDiscovery_get_Os_cstr(x) ((x)->Os)
#define UpnpDiscovery_get_Date_cstr(x) ((x)->Date)
#define UpnpDiscovery_get_Ext_cstr(x) ((x)->Ext)

/** Returned along with a {\bf UPNP_EVENT_SUBSCRIBE_COMPLETE} or {\bf
 * UPNP_EVENT_UNSUBSCRIBE_COMPLETE} callback.  */

struct Upnp_Event_Subscribe {

  /** The SID for this subscription.  For subscriptions, this only
   *  contains a valid SID if the {\bf Upnp_EventSubscribe.result} field
   *  contains a {\tt UPNP_E_SUCCESS} result code.  For unsubscriptions,
   *  this contains the SID from which the subscription is being
   *  unsubscribed.  */

  Upnp_SID Sid;            

  /** The result of the operation. */
  int ErrCode;              

  /** The event URL being subscribed to or removed from. */
  char PublisherUrl[NAME_SIZE]; 

  /** The actual subscription time (for subscriptions only). */
  int TimeOut;              
                              
};

/* compat code for libupnp-1.8 */
typedef struct Upnp_Event_Subscribe UpnpEventSubscribe;
#define UpnpEventSubscribe_get_SID_cstr(x) ((x)->Sid)
#define UpnpEventSubscribe_get_ErrCode(x) ((x)->ErrCode)
#define UpnpEventSubscribe_get_PublisherUrl_cstr(x) ((x)->PublisherUrl)
#define UpnpEventSubscribe_get_TimeOut(x) ((x)->TimeOut)
  
/** Returned along with a {\bf UPNP_EVENT_SUBSCRIPTION_REQUEST}
 *  callback.  */

struct Upnp_Subscription_Request
{
  /** The identifier for the service being subscribed to. */
  const char *ServiceId; 

  /** Universal device name. */
  const char *UDN;       

  /** The assigned subscription ID for this subscription. */
  Upnp_SID Sid;
};

/* compat code for libupnp-1.8 */
typedef struct Upnp_Subscription_Request UpnpSubscriptionRequest;
#define UpnpSubscriptionRequest_get_ServiceId_cstr(x) ((x)->ServiceId)
#define UpnpSubscriptionRequest_get_UDN_cstr(x) ((x)->UDN)
#define UpnpSubscriptionRequest_get_SID_cstr(x) ((x)->Sid)

struct File_Info
{
	/** The length of the file. A length less than 0 indicates the size 
	*  is unknown, and data will be sent until 0 bytes are returned from
	*  a read call. */
	int64_t file_length{0};

	/** The time at which the contents of the file was modified;
	*  The time system is always local (not GMT). */
	time_t last_modified{0};

	/** If the file is a directory, {\bf is_directory} contains
	* a non-zero value. For a regular file, it should be 0. */
	int is_directory{0};

	/** If the file or directory is readable, this contains 
	* a non-zero value. If unreadable, it should be set to 0. */
	int is_readable{0};

	/** The content type of the file. */
	std::string content_type;

	/** Headers received with the HTTP request. Set by the library
		before calling VDCallback_GetInfo */
	std::map<std::string, std::string> request_headers;

	/** Additional headers which should be set in the response. Set by
		the client inside the VDCallback_GetInfo function. These
		should not be standard HTTP headers (e.g. content-length/type)
		but only specific ones like the DLNA ones. */
	std::map<std::string, std::string> response_headers;
};

/* compat code for libupnp-1.8 */
typedef struct File_Info UpnpFileInfo;
#define UpnpFileInfo_get_FileLength(x) ((x)->file_length)
#define UpnpFileInfo_set_FileLength(x, v) ((x)->file_length = (v))
#define UpnpFileInfo_get_LastModified(x) ((x)->last_modified)
#define UpnpFileInfo_set_LastModified(x, v) ((x)->last_modified = (v))
#define UpnpFileInfo_get_IsDirectory(x) ((x)->is_directory)
#define UpnpFileInfo_set_IsDirectory(x, v) ((x)->is_directory = (v))
#define UpnpFileInfo_get_IsReadable(x) ((x)->is_readable)
#define UpnpFileInfo_set_IsReadable(x, v) ((x)->is_readable = (v))
#define UpnpFileInfo_get_ContentType(x) ((x)->content_type)
#define UpnpFileInfo_set_ContentType(x, v) ((x)->content_type = (v))
#define UpnpFileInfo_set_ExtraHeaders(x, v) ((x)->extra_headers = (v))

/*!
 *  All callback functions share the same prototype, documented below.
 *  Note that any memory passed to the callback function
 *  is valid only during the callback and should be copied if it
 *  needs to persist.  This callback function needs to be thread
 *  safe.  The context of the callback is always on a valid thread 
 *  context and standard synchronization methods can be used.  Note, 
 *  however, because of this the callback cannot call SDK functions
 *  unless explicitly noted.
 *
 *  \verbatim
      int CallbackFxn(Upnp_EventType EventType, void *Event, void *Cookie);
    \endverbatim 
 *
 *  where \b EventType is the event that triggered the callback, 
 *  \b Event is a structure that denotes event-specific information for that
 *  event, and \b Cookie is the user data passed when the callback was
 *  registered.
 *
 *  See \b Upnp_EventType for more information on the callback values and
 *  the associated \b Event parameter.  
 *
 *  The return value of the callback is currently ignored. It may be used
 *  in the future to communicate results back to the SDK.
 */
typedef int (*Upnp_FunPtr)(
	/*! [in] .*/
	Upnp_EventType EventType,
	/*! [in] .*/
	const void *Event,
	/*! [in] .*/
	void *Cookie);

/* @} Constants and Types */

/*!
 * \name Initialization and Registration
 *
 * @{
 */

/*!
 * \brief Initializes the Linux SDK for UPnP Devices (IPv4 only).
 *
 * \deprecated Kept for backwards compatibility. Use UpnpInit2 for new
 * implementations or where IPv6 is required.
 *
 * This function must be called before any other API function can be called.
 * It should be called only once. Subsequent calls to this API return a
 * \c UPNP_E_INIT error code.
 *
 * Optionally, the application can specify a host IPv4 address (in the
 * case of a multi-homed configuration) and a port number to use for
 * all UPnP operations.  Since a port number can be used only by one
 * process, multiple processes using the SDK must specify
 * different port numbers.
 *
 * If unspecified, the SDK will use the first IPv4-capable adapter's IP address
 * and an arbitrary port.
 *
 * This call is synchronous.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist 
 *             to initialize the SDK.
 *     \li \c UPNP_E_INIT: The SDK is already initialized. 
 *     \li \c UPNP_E_INIT_FAILED: The SDK initialization 
 *             failed for an unknown reason.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_LISTEN: An error occurred listening to a socket.
 *     \li \c UPNP_E_OUTOF_SOCKET: An error ocurred creating a socket.
 *     \li \c UPNP_E_INTERNAL_ERROR: An internal error ocurred.
 */
EXPORT_SPEC int UpnpInit(
	/*! The host local IPv4 address to use, in string format, for example
	 * "192.168.0.1", or \c NULL to use the first IPv4 adapter's IP address. */
	const char *HostIP,
	/*! Local Port to listen for incoming connections
	 * \c NULL will pick an arbitrary free port. */
	unsigned short DestPort);

/*!
 * \brief Initializes the Linux SDK for UPnP Devices (IPv4 or IPv6).
 *
 * This function must be called before any other API function can be called.
 * It should be called only once. Subsequent calls to this API return a
 * \c UPNP_E_INIT error code.
 *
 * Optionally, the application can specify an interface name (in the
 * case of a multi-homed configuration) and a port number to use for
 * all UPnP operations.  Since a port number can be used only by one
 * process, multiple processes using the SDK must specify
 * different port numbers.
 *
 * If unspecified, the SDK will use the first suitable interface and an 
 * arbitrary port.
 *
 * This call is synchronous.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist 
 *             to initialize the SDK.
 *     \li \c UPNP_E_INIT: The SDK is already initialized. 
 *     \li \c UPNP_E_INIT_FAILED: The SDK initialization 
 *             failed for an unknown reason.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_LISTEN: An error occurred listening to a socket.
 *     \li \c UPNP_E_OUTOF_SOCKET: An error ocurred creating a socket.
 *     \li \c UPNP_E_INTERNAL_ERROR: An internal error ocurred.
 *     \li \c UPNP_E_INVALID_INTERFACE: IfName is invalid or does not
 *             have a valid IPv4 or IPv6 addresss configured.
 */
#ifdef UPNP_ENABLE_IPV6
EXPORT_SPEC int UpnpInit2( 
	/*! The interface name to use by the UPnP SDK operations.
	 * Examples: "eth0", "xl0", "Local Area Connection", \c NULL to
	 * use the first suitable interface. */
	const char *IfName,
	/*!  Local Port to listen for incoming connections.
	 * \c NULL will pick an arbitrary free port. */
	unsigned short DestPort);
#endif

/*!
 * \brief Terminates the Linux SDK for UPnP Devices.
 *
 * \li Checks for pending jobs and threads
 * \li Unregisters either the client or device 
 * \li Shuts down the Timer Thread
 * \li Stops the Mini Server
 * \li Uninitializes the Thread Pool
 * \li For Win32 cleans up Winsock Interface 
 * \li Cleans up mutex objects
 *
 * This function must be the last API function called. It should be called only
 * once. Subsequent calls to this API return a \c UPNP_E_FINISH error code.
 *
 *  \return An integer representing one of the following:
 *      \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *      \li \c UPNP_E_FINISH: The SDK is already terminated or 
 *                                 it is not initialized. 
 */
EXPORT_SPEC int UpnpFinish(void);

/*!
 * \brief Returns the internal server IPv4 UPnP listening port.
 *
 * If '0' is used as the port number in \b UpnpInit, then this function can be
 * used to retrieve the actual port allocated to the SDK.
 *
 * \return
 * 	\li On success: The port on which an internal server is listening for IPv4 UPnP
 *		related requests.
 *	\li On error: 0 is returned if \b UpnpInit has not succeeded.
 */
EXPORT_SPEC unsigned short UpnpGetServerPort(void);

/*!
 * \brief Returns the internal server IPv6 UPnP listening port.
 *
 * If '0' is used as the port number in \b UpnpInit, then this function can be
 * used to retrieve the actual port allocated to the SDK.
 *
 * \return
 * 	\li On success: The port on which an internal server is listening for IPv6 UPnP
 *		related requests.
 *	\li On error: 0 is returned if \b UpnpInit has not succeeded.
 */
#ifdef UPNP_ENABLE_IPV6
EXPORT_SPEC unsigned short UpnpGetServerPort6(void);
#endif

/*!
 * \brief Returns the local IPv4 listening ip address.
 *
 * If \c NULL is used as the IPv4 address in \b UpnpInit, then this function can
 * be used to retrieve the actual interface address on which device is running.
 *
 * \return
 * 	\li On success: The IPv4 address on which an internal server is
 * 		listening for UPnP related requests.
 * 	\li On error: \c NULL is returned if \b UpnpInit has not succeeded.
 */
EXPORT_SPEC const char *UpnpGetServerIpAddress(void);

/*!
 * \brief Returns the local IPv6 listening ip address.
 *
 * If \c NULL is used as the IPv6 address in \b UpnpInit, then this function can
 * be used to retrieve the actual interface address on which device is running.
 *
 * \return
 * 	\li On success: The IPv6 address on which an internal server is
 * 		listening for UPnP related requests.
 * 	\li On error: \c NULL is returned if \b UpnpInit has not succeeded.
 */
#ifdef UPNP_ENABLE_IPV6
EXPORT_SPEC const char *UpnpGetServerIp6Address(void);
EXPORT_SPEC const char *UpnpGetServerUlaGuaIp6Address(void);
#endif

/*!
 * \brief Registers a device application with the UPnP Library.
 *
 * A device application cannot make any other API calls until it registers
 * using this function.
 *
 * Device applications can also register as control points (see 
 * \b UpnpRegisterClient to get a control point handle to perform control
 * point functionality).
 *
 * This is a synchronous call and does not generate any callbacks. Callbacks
 * can occur as soon as this function returns.
 *
 *  \return An integer representing one of the following:
 *      \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *      \li \c UPNP_E_FINISH: The SDK is already terminated or is not
 *		initialized.
 *      \li \c UPNP_E_INVALID_DESC: The description document was not 
 *		a valid device description.
 *      \li \c UPNP_E_INVALID_URL: The URL for the description document 
 *              is not valid.
 *      \li \c UPNP_E_INVALID_PARAM: Either \b Callback or \b Hnd 
 *              is not a valid pointer or \b DescURL is \c NULL.
 *      \li \c UPNP_E_NETWORK_ERROR: A network error occurred.
 *      \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *              to a socket.
 *      \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *              from a socket.
 *      \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *      \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting the 
 *              socket.
 *      \li \c UPNP_E_OUTOF_SOCKET: Too many sockets are currently 
 *              allocated.
 *      \li \c UPNP_E_OUTOF_MEMORY: There are insufficient resources to 
 *              register this root device.
 */
EXPORT_SPEC int UpnpRegisterRootDevice(
	/*! [in] Pointer to a string containing the description URL for
	 *  this root device instance. 
	 */
	const char *DescUrl,
	/*! [in] Callback function.*/
	Upnp_FunPtr Callback,
	/*! [in] Pointer to be passed as parameter to the callback invocations. */
	const void *Cookie,
	/*! [out] Pointer to a variable to store the new device handle. */
	UpnpDevice_Handle *Hnd);

/*!
 * \brief Registers a device application with the UPnP Library. Similar to
 * \b UpnpRegisterRootDevice, except that it also allows the description
 * document to be specified as a file or a memory buffer.
 *
 * The description can also be configured to have the correct IP and port
 * address.
 *
 * NOTE: For the configuration to be functional, the internal web server
 * MUST be present. In addition, the web server MUST be activated
 * (using \b UpnpSetWebServerRootDir) before calling this function.
 * The only condition where the web server can be absent is if the 
 * description document is specified as a URL and no configuration is 
 * required (i.e. <tt>config_baseURL = 0</tt>.)
 *
 * This is a synchronous call and does not generate any callbacks. Callbacks
 * can occur as soon as this function returns.
 *
 * Examples of using different types of description documents:
 * \verbatim
   1) Description specified as a URL:
         descriptionType == UPNPREG_URL_DESC
         description is the URL
         bufferLen = 0 (ignored)
   2) Description specified as a file:
         descriptionType == UPNPREG_FILENAME_DESC
         description is a filename
         bufferLen = 0 (ignored)
   3) Description specified as a memory buffer:
         descriptionType == UPNPREG_BUF_DESC
         description is pointer to a memory buffer
         bufferLen == length of memory buffer
   \endverbatim
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_FINISH: The SDK is already terminated or 
 *                                is not initialized.
 *     \li \c UPNP_E_INVALID_DESC: The description document is not 
 *             a valid device description.
 *     \li \c UPNP_E_INVALID_PARAM: Either \b Callback or \b Hnd 
 *             is not a valid pointer or \b DescURL is \c NULL.
 *     \li \c UPNP_E_NETWORK_ERROR: A network error occurred.
 *     \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *             to a socket.
 *     \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *             from a socket.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting the 
 *             socket.
 *     \li \c UPNP_E_OUTOF_SOCKET: Too many sockets are currently 
 *             allocated.
 *     \li \c UPNP_E_OUTOF_MEMORY: There are insufficient resources to 
 *             register this root device.
 *     \li \c UPNP_E_URL_TOO_BIG: Length of the URL is bigger than the 
 *             internal buffer.
 *     \li \c UPNP_E_FILE_NOT_FOUND: The description file could not 
 *             be found.
 *     \li \c UPNP_E_FILE_READ_ERROR: An error occurred reading the 
 *             description file.
 *     \li \c UPNP_E_INVALID_URL: The URL to the description document 
 *             is invalid.
 *     \li \c UPNP_E_EXT_NOT_XML: The URL to the description document 
 *             or file should have a <tt>.xml</tt> extension.
 *     \li \c UPNP_E_NO_WEB_SERVER: The internal web server has been 
 *             compiled out; the SDK cannot configure itself from the 
 *             description document.
 */
EXPORT_SPEC int UpnpRegisterRootDevice2(
	/*! [in] The type of the description document. */
	Upnp_DescType descriptionType,
	/*! [in] Treated as a URL, file name or memory buffer depending on
	 * description type. */
	const char* description,
	/*! [in] The length of memory buffer if passing a description in a buffer,
	 * otherwise it is ignored. */
	size_t bufferLen,
	/*! [in] If nonzero, \c URLBase of description document is configured and
	 * the description is served using the internal web server. */
	int config_baseURL,
	/*! [in] Callback function */
	Upnp_FunPtr Fun,
	/*! [in] Pointer to be passed as parameter to the callback invocations. */
	const void* Cookie,
	/*! [out] Pointer to a variable to store the new device handle. */
	UpnpDevice_Handle* Hnd);

/*!
 * \brief Registers a device application for a specific address family with
 * the UPnP library.
 *
 * A device application cannot make any other API calls until it registers
 * using this function. Device applications can also register as control
 * points (see \b UpnpRegisterClient to get a control point handle to perform
 * control point functionality).
 *
 * This is synchronous and does not generate any callbacks. Callbacks can occur
 * as soon as this function returns.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_FINISH: The SDK is already terminated or 
 *                                is not initialized. 
 *     \li \c UPNP_E_INVALID_DESC: The description document was not 
 *             a valid device description.
 *     \li \c UPNP_E_INVALID_URL: The URL for the description document 
 *             is not valid.
 *     \li \c UPNP_E_INVALID_PARAM: Either \b Callback or \b Hnd 
 *             is not a valid pointer or \b DescURL is \c NULL.
 *     \li \c UPNP_E_NETWORK_ERROR: A network error occurred.
 *     \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *             to a socket.
 *     \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *             from a socket.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting the 
 *             socket.
 *     \li \c UPNP_E_OUTOF_SOCKET: Too many sockets are currently 
 *             allocated.
 *     \li \c UPNP_E_OUTOF_MEMORY: There are insufficient resources to 
 *             register this root device.
 */
EXPORT_SPEC int UpnpRegisterRootDevice3(
	/*! [in] Pointer to a string containing the description URL for this root
	 * device instance. */
	const char *DescUrl,
	/*! [in] Callback function.*/
	Upnp_FunPtr Callback,
	/*! [in] Pointer to be passed as parameter to the callback invocations. */
	const void *Cookie,
	/*! [out] Pointer to a variable to store the new device handle. */
	UpnpDevice_Handle *Hnd,
	/*! [in] Address family of this device. Can be AF_INET for an IPv4 device, or
	 * AF_INET6 for an IPv6 device. Defaults to AF_INET. */
	int  AddressFamily);

/*!
 * \brief Registers a device application for a specific address family with
 * the UPnP library. This function can also be used to specify a dedicated
 * description URL to be returned for legacy CPs.
 *
 * A device application cannot make any other API calls until it registers
 * using this function. Device applications can also register as control
 * points (see \b UpnpRegisterClient to get a control point handle to perform
 * control point functionality).
 *
 * This is synchronous and does not generate any callbacks. Callbacks can occur
 * as soon as this function returns.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_FINISH: The SDK is already terminated or
 *                                is not initialized.
 *     \li \c UPNP_E_INVALID_DESC: The description document was not
 *             a valid device description.
 *     \li \c UPNP_E_INVALID_URL: The URL for the description document
 *             is not valid.
 *     \li \c UPNP_E_INVALID_PARAM: Either \b Callback or \b Hnd
 *             is not a valid pointer or \b DescURL is \c NULL.
 *     \li \c UPNP_E_NETWORK_ERROR: A network error occurred.
 *     \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing
 *             to a socket.
 *     \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading
 *             from a socket.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting the
 *             socket.
 *     \li \c UPNP_E_OUTOF_SOCKET: Too many sockets are currently
 *             allocated.
 *     \li \c UPNP_E_OUTOF_MEMORY: There are insufficient resources to
 *             register this root device.
 */
EXPORT_SPEC int UpnpRegisterRootDevice4(
	/*! [in] Pointer to a string containing the description URL for this root
	 * device instance. */
	const char *DescUrl,
	/*! [in] Callback function.*/
	Upnp_FunPtr Callback,
	/*! [in] Pointer to be passed as parameter to the callback invocations. */
	const void *Cookie,
	/*! [out] Pointer to a variable to store the new device handle. */
	UpnpDevice_Handle *Hnd,
	/*! [in] Address family of this device. Can be AF_INET for an IPv4 device, or
	 * AF_INET6 for an IPv6 device. Defaults to AF_INET. */
	int  AddressFamily,
	/*! [in] Pointer to a string containing the description URL to be returned 
	 * for legacy CPs for this root device instance. */
	const char *LowerDescUrl);

/*!
 * \brief Unregisters a root device registered with \b UpnpRegisterRootDevice,
 * \b UpnpRegisterRootDevice2, \b UpnpRegisterRootDevice3 or
 * \b UpnpRegisterRootDevice4.
 *
 * After this call, the \b UpnpDevice_Handle is no longer valid. For all
 * advertisements that have not yet expired, the SDK sends a device unavailable
 * message automatically.
 *
 * This is a synchronous call and generates no callbacks. Once this call
 * returns, the SDK will no longer generate callbacks to the application.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid device handle.
 */
EXPORT_SPEC int UpnpUnRegisterRootDevice(
	/*! [in] The handle of the root device instance to unregister. */
	UpnpDevice_Handle Hnd);

/*!
 * \brief Unregisters a root device registered with \b UpnpRegisterRootDevice,
 * \b UpnpRegisterRootDevice2, \b UpnpRegisterRootDevice3 or
 * \b UpnpRegisterRootDevice4.
 *
 * After this call, the \b UpnpDevice_Handle is no longer valid. For all
 * advertisements that have not yet expired, the SDK sends a device unavailable
 * message automatically.
 *
 * This is a synchronous call and generates no callbacks. Once this call
 * returns, the SDK will no longer generate callbacks to the application.
 *
 * This function allow a device to specify the SSDP extensions defined by UPnP
 * Low Power.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid device handle.
 */
EXPORT_SPEC int UpnpUnRegisterRootDeviceLowPower(
        /*! [in] The handle of the root device instance to unregister. */
        UpnpDevice_Handle Hnd,
        /*! PowerState as defined by UPnP Low Power. */
        int PowerState,
        /*! SleepPeriod as defined by UPnP Low Power. */
        int SleepPeriod,
        /*! RegistrationState as defined by UPnP Low Power. */
        int RegistrationState);

/*!
 * \brief Registers a control point application with the UPnP Library.
 *
 * A control point application cannot make any other API calls until it
 * registers using this function.
 *
 * \b UpnpRegisterClient is a synchronous call and generates no callbacks.
 * Callbacks can occur as soon as this function returns.
 *
 * \return An integer representing one of the following:
 *      \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *      \li \c UPNP_E_FINISH: The SDK is already terminated or 
 *                            is not initialized. 
 *      \li \c UPNP_E_INVALID_PARAM: Either \b Callback or \b Hnd 
 *              is not a valid pointer.
 *      \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *              register this control point.
 */
EXPORT_SPEC int UpnpRegisterClient(
	/*! [in] Callback function. */
	Upnp_FunPtr Callback,
	/*! [in] Pointer to be passed as parameter to the callback invocations. */
	const void *Cookie,
	/*! [out] Pointer to a variable to store the new control point handle. */
	UpnpClient_Handle *Hnd);

/*!
 * \brief Unregisters a control point application, unsubscribing all active
 * subscriptions.
 *
 * This function unregisters a client registered with UpnpRegisterclient or
 * UpnpRegisterclient2. After this call, the \b UpnpClient_Handle is no longer
 * valid. The UPnP Library generates no more callbacks after this function
 * returns.
 *
 * \b UpnpUnRegisterClient is a synchronous call and generates no
 * callbacks.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid control point handle.
 */
EXPORT_SPEC int UpnpUnRegisterClient(
	/*! [in] The handle of the control point instance to unregister. */
	UpnpClient_Handle Hnd);

/*!
 * \brief Sets the maximum content-length that the SDK will process on an
 * incoming SOAP requests or responses.
 *
 * This API allows devices that have memory constraints to exhibit consistent
 * behaviour if the size of the incoming SOAP message exceeds the memory that
 * device can allocate.
 *
 * If set to 0 then checking will be disabled.
 *
 * The default maximum content-length is \c DEFAULT_SOAP_CONTENT_LENGTH 
 * = 16K bytes.
 *  
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 */
#define UpnpSetContentLength UpnpSetMaxContentLength
EXPORT_SPEC int UpnpSetMaxContentLength(
	/*! [in] The maximum permissible content length for incoming SOAP actions,
	 * in bytes. */
	size_t contentLength);

/* @} Initialization and Registration */

/******************************************************************************
 ******************************************************************************
 *                                                                            *
 *                        D I S C O V E R Y                                   *
 *                                                                            *
 ******************************************************************************
 ******************************************************************************/

/*!
 * \name Discovery
 *
 * @{
 */

/*!
 * \brief Searches for devices matching the given search target.
 *
 * The function returns immediately and the SDK calls the default callback
 * function, registered during the \b UpnpRegisterClient call, for each
 * matching root device, device, or service. The application specifies the
 * search type by the \b Target parameter.  
 *
 * This function searches for the devices for the provided maximum time.
 * It is an asynchronous function. It schedules a search job and returns. 
 * The client is notified about the search results after search timer.
 *
 * Note that there is no way for the SDK to distinguish which client
 * instance issued a particular search.  Therefore, the client can get
 * search callbacks that do not match the original criteria of the search.
 * Also, the application will receive multiple callbacks for each search.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid control 
 *             point handle.
 *     \li \c UPNP_E_INVALID_PARAM: \b Target is \c NULL.
 */
EXPORT_SPEC int UpnpSearchAsync(
	/*! The handle of the client performing the search. */
	UpnpClient_Handle Hnd,
	/*! The time, in seconds, to wait for responses. If the time is greater
	 * than \c MAX_SEARCH_TIME then the time is set to \c MAX_SEARCH_TIME.
	 * If the time is less than \c MIN_SEARCH_TIME then the time is set to
	 * \c MIN_SEARCH_TIME. */ 
	int Mx,
	/*! The search target as defined in the UPnP Device Architecture v1.0
	 * specification. */
	const char *TTarget_constarget_const,
	/*! The user data to pass when the callback function is invoked. */
	const void *Cookie_const); 

/*!
 * \brief Sends out the discovery announcements for all devices and services
 * for a device.
 *
 * Each announcement is made with the same expiration time.
 *
 * This is a synchronous call.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid 
 *             device handle.
 *     \li \c UPNP_E_OUTOF_MEMORY: There are insufficient resources to 
 *             send future advertisements.
 */
EXPORT_SPEC int UpnpSendAdvertisement(
	/*! The device handle for which to send out the announcements. */
	UpnpDevice_Handle Hnd,
	/*! The expiration age, in seconds, of the announcements. If the
	 * expiration age is less than 1 then the expiration age is set to
	 * \c DEFAULT_MAXAGE. If the expiration age is less than or equal to
	 * \c AUTO_ADVERTISEMENT_TIME * 2 then the expiration age is set to
	 * ( \c AUTO_ADVERTISEMENT_TIME + 1 ) * 2. */
	int Exp);

/*!
 * \brief Sends out the discovery announcements for all devices and services
 * for a device.
 *
 * Each announcement is made with the same expiration time.
 *
 * This is a synchronous call.
 *
 * This function allow a device to specify the SSDP extensions defined by UPnP
 * Low Power.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid
 *             device handle.
 *     \li \c UPNP_E_OUTOF_MEMORY: There are insufficient resources to
 *             send future advertisements.
 */
EXPORT_SPEC int UpnpSendAdvertisementLowPower(
        /*! The device handle for which to send out the announcements. */
        UpnpDevice_Handle Hnd,
        /*! The expiration age, in seconds, of the announcements. If the
         * expiration age is less than 1 then the expiration age is set to
         * \c DEFAULT_MAXAGE. If the expiration age is less than or equal to
         * \c AUTO_ADVERTISEMENT_TIME * 2 then the expiration age is set to
         * ( \c AUTO_ADVERTISEMENT_TIME + 1 ) * 2. */
        int Exp,
        /*! PowerState as defined by UPnP Low Power. */
        int PowerState,
        /*! SleepPeriod as defined by UPnP Low Power. */
        int SleepPeriod,
        /*! RegistrationState as defined by UPnP Low Power. */
        int RegistrationState);

/* @} Discovery */





/******************************************************************************
 *                                                                            *
 *                            C O N T R O L                                   *
 *                                                                            *
 ******************************************************************************/

/*!
 * \brief Sends a message to change a state variable in a service.
 *
 * This is a synchronous call that does not return until the action is complete.
 * 
 * Note that a positive return value indicates a SOAP-protocol error code.
 * In this case,  the error description can be retrieved from \b RespNode.
 * A negative return value indicates an SDK error.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid control 
 *             point handle.
 *     \li \c UPNP_E_INVALID_URL: \b ActionUrl is not a valid URL.
 *     \li \c UPNP_E_INVALID_ACTION: This action is not valid.
 *     \li \c UPNP_E_INVALID_DEVICE: \b DevUDN is not a 
 *             valid device.
 *     \li \c UPNP_E_INVALID_PARAM: \b ServiceType, \b Action, 
 *             \b ActionUrl, or 
 *             \b RespNode is not a valid pointer.
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *             complete this operation.
 *
 *  @param Hnd client handle
 * 	@param headerString SOAP header. This may be empty if no header
 *     header is not required. <soapns:Header>[xxx]</soapns:Header>
 *  @param actionURL the service action url from the device description document.
 *  @param serviceType the service type from the device description document
 *  @param actionName the action to perform (from the service description)
 *  @param actionArgs the action name/value argument pairs, in order.
 *  @param[output] responseData the return values
 *  @param[output] errorCodep pointer to an integer to store the UPNP error code
 *   if we got an error response document
 *  @param[output] errorDescr A place to store an error description (if we got 
 *     an error response document).
 */
EXPORT_SPEC int UpnpSendAction(
	UpnpClient_Handle Hnd,
	const std::string& headerString,
	const std::string& actionURL,
	const std::string& serviceType,
	const std::string& actionName,
	const std::vector<std::pair<std::string, std::string>> actionArgs,
	std::vector<std::pair<std::string, std::string>>& responsedata,
	int *errorCodep,
	std::string&  errorDescr
	);


/******************************************************************************
 ******************************************************************************
 *                                                                            *
 *                        E V E N T I N G                                     *
 *                                                                            *
 ******************************************************************************
 ******************************************************************************/

/*!
 * \name Eventing
 *
 * @{
 */

/*!
 * \brief Accepts a subscription request and sends out the current state of the
 * eventable variables for a service.
 *
 * The device application should call this function when it receives a
 * \c UPNP_EVENT_SUBSCRIPTION_REQUEST callback.
 *
 * This function is synchronous and generates no callbacks.
 *
 * This function can be called during the execution of a callback function.
 *
 * \return An integer representing one of the following:
 *      \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *      \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid device 
 *              handle.
 *      \li \c UPNP_E_INVALID_SERVICE: The \b DevId/\b ServId 
 *              pair refers to an invalid service. 
 *      \li \c UPNP_E_INVALID_SID: The specified subscription ID is not 
 *              valid.
 *      \li \c UPNP_E_INVALID_PARAM: Either \b VarName, 
 *              \b NewVal, \b DevID, or \b ServID is not a valid 
 *              pointer or \b cVariables is less than zero.
 *      \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *              complete this operation.
 */
EXPORT_SPEC int UpnpAcceptSubscription(
	/*! [in] The handle of the device. */
	UpnpDevice_Handle Hnd,
	/*! [in] The device ID of the service device .*/
	const char *DevID,
	/*! [in] The unique service identifier of the service generating the event.*/
	const char *ServID,
	/*! [in] Pointer to an array of event variables. */
	const char **VarName,
	/*! [in] Pointer to an array of values for the event variables. */
	const char **NewVal,
	/*! [in] The number of event variables in \b VarName. */
	int cVariables,
	/*! [in] The subscription ID of the newly registered control point. */
	const Upnp_SID SubsId);

EXPORT_SPEC int UpnpAcceptSubscriptionXML(
	/*! [in] The handle of the device. */
	UpnpDevice_Handle Hnd,
	/*! [in] The device ID of the service device .*/
	const char *DevID,
	/*! [in] The unique service identifier of the service generating the event.*/
	const char *ServID,
	/*! [in] Initial property set (all state variables) as XML string. */
	const std::string& propertyset,
	/*! [in] The subscription ID of the newly registered control point. */
	const Upnp_SID SubsId);

/*!
 * \brief Sends out an event change notification to all control points
 * subscribed to a particular service.
 *
 * This function is synchronous and generates no callbacks.
 *
 * This function may be called during a callback function to send out a
 * notification.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid device 
 *             handle.
 *     \li \c UPNP_E_INVALID_SERVICE: The \b DevId/\b ServId 
 *             pair refers to an invalid service.
 *     \li \c UPNP_E_INVALID_PARAM: Either \b VarName, \b NewVal, 
 *              \b DevID, or \b ServID is not a valid pointer or 
 *              \b cVariables is less than zero.
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *             complete this operation.
 */
EXPORT_SPEC int UpnpNotify(
	/*! [in] The handle to the device sending the event. */
	UpnpDevice_Handle,
	/*! [in] The device ID of the subdevice of the service generating the event. */
	const char *DevID,
	/*! [in] The unique identifier of the service generating the event. */
	const char *ServID,
	/*! [in] Pointer to an array of variables that have changed. */
	const char **VarName,
	/*! [in] Pointer to an array of new values for those variables. */
	const char **NewVal,
	/*! [in] The count of variables included in this notification. */
	int cVariables);

EXPORT_SPEC int UpnpNotifyXML(
	/*! [in] The handle to the device sending the event. */
	UpnpDevice_Handle,
	/*! [in] The device ID of the device generating the event. */
	const char *DevID,
	/*! [in] The unique identifier of the service generating the event. */
	const char *ServID,
	/*! [in] Property set (changed variables) as XML string */
	const std::string& propertyset);

/*!
 * \brief Renews a subscription that is about to expire.
 *
 * This function is synchronous.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid control 
 *             point handle.
 *     \li \c UPNP_E_INVALID_PARAM: \b Timeout is not a valid pointer.
 *     \li \c UPNP_E_INVALID_SID: The SID being passed to this function 
 *             is not a valid subscription ID.
 *     \li \c UPNP_E_NETWORK_ERROR: A network error occured. 
 *     \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *             to a socket.
 *     \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *             from a socket.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.  
 *     \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting to 
 *             \b PublisherUrl.
 *     \li \c UPNP_E_OUTOF_SOCKET: An error occurred creating a socket.
 *     \li \c UPNP_E_BAD_RESPONSE: An error occurred in response from 
 *             the publisher.
 *     \li \c UPNP_E_SUBSCRIBE_UNACCEPTED: The publisher refused 
 *             the subscription renew.
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *             complete this operation.
 */
EXPORT_SPEC int UpnpRenewSubscription(
	/*! [in] The handle of the control point that is renewing the subscription. */
	UpnpClient_Handle Hnd,
	/*! [in,out] Pointer to a variable containing the requested subscription time.
	 * Upon return, it contains the actual renewal time. */
	int *TimeOut,
	/*! [in] The ID for the subscription to renew. */
	const Upnp_SID SubsId);

/*!
 * \brief Sets the maximum number of subscriptions accepted per service.
 *
 * The default value accepts as many as system resources allow. If the number
 * of current subscriptions for a service is greater than the requested value,
 * the SDK accepts no new subscriptions or renewals, however, the SDK does not
 * remove any current subscriptions.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid device 
 *             handle.
 */
EXPORT_SPEC int UpnpSetMaxSubscriptions(  
	/*! The handle of the device for which the maximum number of
	 * subscriptions is being set. */
	UpnpDevice_Handle Hnd,
	/*! The maximum number of subscriptions to be allowed per service. */
	int MaxSubscriptions);

/*!
 * \brief Sets the maximum time-out accepted for a subscription request or
 * renewal.
 *
 * The default value accepts the time-out set by the control point.
 * If a control point requests a subscription time-out less than or equal to
 * the maximum, the SDK grants the value requested by the control point. If the
 * time-out is greater, the SDK returns the maximum value.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid device 
 *             handle.
 */
EXPORT_SPEC int UpnpSetMaxSubscriptionTimeOut(  
	/*! The handle of the device for which the maximum subscription
	 * time-out is being set. */
	UpnpDevice_Handle Hnd,
	/*! The maximum subscription time-out to be accepted. */
	int MaxSubscriptionTimeOut);

/*!
 * \brief Registers a control point to receive event notifications from another
 * device.
 *
 * This operation is synchronous.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid control 
 *             point handle.
 *     \li \c UPNP_E_INVALID_URL: \b PublisherUrl is not a valid URL.
 *     \li \c UPNP_E_INVALID_PARAM: \b Timeout is not a valid pointer 
 *             or \b SubsId or \b PublisherUrl is \c NULL.
 *     \li \c UPNP_E_NETWORK_ERROR: A network error occured. 
 *     \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *             to a socket.
 *     \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *             from a socket.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting to 
 *             \b PublisherUrl.
 *     \li \c UPNP_E_OUTOF_SOCKET: An error occurred creating a socket.
 *     \li \c UPNP_E_BAD_RESPONSE: An error occurred in response from 
 *             the publisher.
 *     \li \c UPNP_E_SUBSCRIBE_UNACCEPTED: The publisher refused 
 *             the subscription request.
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *             complete this operation.
 */
EXPORT_SPEC int UpnpSubscribe(
	/*! [in] The handle of the control point. */
	UpnpClient_Handle Hnd,
	/*! [in] The URL of the service to subscribe to. */
	const char *PublisherUrl,
	/*! [in,out]Pointer to a variable containing the requested subscription time.
	 * Upon return, it contains the actual subscription time returned from
	 * the service. */
	int *TimeOut,
	/*! [out] Pointer to a variable to receive the subscription ID (SID). */
	Upnp_SID SubsId);

/*!
 * \brief Removes the subscription of a control point from a service previously
 * subscribed to using \b UpnpSubscribe or \b UpnpSubscribeAsync.
 *
 * This is a synchronous call.
 *
 * \return An integer representing one of the following:
 *     \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *     \li \c UPNP_E_INVALID_HANDLE: The handle is not a valid control 
 *             point handle.
 *     \li \c UPNP_E_INVALID_SID: The \b SubsId is not a valid 
 *             subscription ID.
 *     \li \c UPNP_E_NETWORK_ERROR: A network error occured. 
 *     \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *             to a socket.
 *     \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *             from a socket.
 *     \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *     \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting to 
 *             \b PublisherUrl.
 *     \li \c UPNP_E_OUTOF_SOCKET: An error ocurred creating a socket.
 *     \li \c UPNP_E_BAD_RESPONSE: An error occurred in response from 
 *             the publisher.
 *     \li \c UPNP_E_UNSUBSCRIBE_UNACCEPTED: The publisher refused 
 *             the unsubscribe request (the client is still unsubscribed and 
 *             no longer receives events).
 *     \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *             complete this operation.
 */
EXPORT_SPEC int UpnpUnSubscribe(
	/*! [in] The handle of the subscribed control point. */
	UpnpClient_Handle Hnd,
	/*! [in] The ID returned when the control point subscribed to the service. */
	const Upnp_SID SubsId);

/*! @} Eventing */


/******************************************************************************
 ******************************************************************************
 *                                                                            *
 *                        C L I E N T - A P I                                 *
 *                                                                            *
 ******************************************************************************
 ******************************************************************************/

/*!
 * \name Control Point HTTP API
 *
 * @{
 */

/*!
 * \brief Downloads a file specified in a URL.
 *
 * The SDK allocates the memory for \b outBuf and the application is
 * responsible for freeing this memory. Note that the item is passed as a
 * single buffer. Large items should not be transferred using this function.
 *
 *  \return An integer representing one of the following:
 *      \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *      \li \c UPNP_E_INVALID_PARAM: Either \b url, \b outBuf 
 *              or \b contentType is not a valid pointer.
 *      \li \c UPNP_E_INVALID_URL: The \b url is not a valid 
 *              URL.
 *      \li \c UPNP_E_OUTOF_MEMORY: Insufficient resources exist to 
 *              download this file.
 *      \li \c UPNP_E_NETWORK_ERROR: A network error occurred.
 *      \li \c UPNP_E_SOCKET_WRITE: An error or timeout occurred writing 
 *              to a socket.
 *      \li \c UPNP_E_SOCKET_READ: An error or timeout occurred reading 
 *              from a socket.
 *      \li \c UPNP_E_SOCKET_BIND: An error occurred binding a socket.
 *      \li \c UPNP_E_SOCKET_CONNECT: An error occurred connecting a 
 *              socket.
 *      \li \c UPNP_E_OUTOF_SOCKET: Too many sockets are currently 
 *              allocated.
 */
EXPORT_SPEC int UpnpDownloadUrlItem(
	/*! [in] URL of an item to download. */
	const char *url,
	/*! [out] Buffer to store the downloaded item. */
	char **outBuf,
	/*! [out] HTTP header value content type if present. It should be at least
	 * \c LINE_SIZE bytes in size. */
	char *contentType);

/*! @} Control Point HTTP API */

/******************************************************************************
 ******************************************************************************
 *                                                                            *
 *                    W E B  S E R V E R  A P I                               *
 *                                                                            *
 ******************************************************************************
 ******************************************************************************/

/*!
 * \name Web Server API
 * 
 * @{
 */

/*!
 * \brief Sets the document root directory for the internal web server.
 *
 * This directory is considered the root directory (i.e. "/") of the web server.
 *
 * This function also activates or deactivates the web server. To disable the
 * web server, pass \c NULL for \b rootDir; to activate, pass a valid directory
 * string.
 * 
 * \note This function is not available when the web server is not compiled
 * 	into the UPnP Library.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b rootDir is an invalid directory.
 */
EXPORT_SPEC int UpnpSetWebServerRootDir( 
	/*! [in] Path of the root directory of the web server. */
	const char *rootDir);

/*!
 * \brief The type of handle returned by the web server for open requests.
 */
typedef void *UpnpWebFileHandle;

/*!
 * \brief Get-info callback function prototype.
 */
typedef int (*VDCallback_GetInfo)(
		/*! [in] The name of the file to query. */
		const char *filename,
		/*! [out] Pointer to a structure to store the information on the file. */
		struct File_Info *info,
		const void *cookie,
		const void **request_cookiep
	);

/*!
 * \brief Sets the get_info callback function to be used to access a virtual
 * directory.
 * 
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b callback is not a valid pointer.
 */
EXPORT_SPEC int UpnpVirtualDir_set_GetInfoCallback(VDCallback_GetInfo callback);

/*!
 * \brief Open callback function prototype.
 */
typedef UpnpWebFileHandle (*VDCallback_Open)(
	/*! [in] The name of the file to open. */ 
	const char *filename,
	/*! [in] The mode in which to open the file.
	 * Valid values are \c UPNP_READ or \c UPNP_WRITE. */
	enum UpnpOpenFileMode Mode,
	const void *cookie,
	const void *request_cookie
	);

/*!
 * \brief Sets the open callback function to be used to access a virtual
 * directory.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b callback is not a valid pointer.
 */
EXPORT_SPEC int UpnpVirtualDir_set_OpenCallback(VDCallback_Open callback);

/*!
 * \brief Read callback function prototype.
 */
typedef int (*VDCallback_Read)(
	/*! [in] The handle of the file to read. */
	UpnpWebFileHandle fileHnd,
	/*! [out] The buffer in which to place the data. */
	char *buf,
	/*! [in] The size of the buffer (i.e. the number of bytes to read). */
	size_t buflen,
	const void *cookie,
	const void *request_cookie
	);

/*! 
 * \brief Sets the read callback function to be used to access a virtual
 * directory.
 *
 *  \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b callback is not a valid pointer.
 */
EXPORT_SPEC int UpnpVirtualDir_set_ReadCallback(VDCallback_Read callback);

/*!
 * \brief Write callback function prototype.
 */
typedef	int (*VDCallback_Write)(
	/*! [in] The handle of the file to write. */
	UpnpWebFileHandle fileHnd,
	/*! [in] The buffer with the bytes to write. */
	char *buf,
	/*! [in] The number of bytes to write. */
	size_t buflen,
	const void *cookie,
	const void *request_cookie
	);

/*!
 * \brief Sets the write callback function to be used to access a virtual
 * directory.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b callback is not a valid pointer.
 */
EXPORT_SPEC int UpnpVirtualDir_set_WriteCallback(VDCallback_Write callback);

/*!
 * \brief Seek callback function prototype.
 */
typedef int (*VDCallback_Seek) (
	/*! [in] The handle of the file to move the file pointer. */
	UpnpWebFileHandle fileHnd,
	/*! [in] The number of bytes to move in the file.  Positive values
	 * move foward and negative values move backward.  Note that
	 * this must be positive if the \b origin is \c SEEK_SET. */
	int64_t offset,
	/*! [in] The position to move relative to.  It can be \c SEEK_CUR
	 * to move relative to the current position, \c SEEK_END to
	 * move relative to the end of the file, or \c SEEK_SET to
	 * specify an absolute offset. */
	int origin,
	const void *cookie,
	const void *request_cookie
	);

/*!
 * \brief Sets the seek callback function to be used to access a virtual
 * directory.
 *
 *  \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b callback is not a valid pointer.
 */
EXPORT_SPEC int UpnpVirtualDir_set_SeekCallback(VDCallback_Seek callback);

/*!
 * \brief Close callback function prototype.
 */
typedef int (*VDCallback_Close)(
		/*! [in] The handle of the file to close. */
	UpnpWebFileHandle fileHnd,
	const void *cookie,
	const void *request_cookie
	);

/*!
 * \brief Sets the close callback function to be used to access a virtual
 * directory.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b callback is not a valid pointer.
 */
EXPORT_SPEC int UpnpVirtualDir_set_CloseCallback(VDCallback_Close callback);

/*!
 * \brief The {\bf UpnpVirtualDirCallbacks} structure contains the pointers to
 *  file-related callback functions a device application can register to
 *  virtualize URLs.
 */
struct UpnpVirtualDirCallbacks
{
	VDCallback_GetInfo get_info;
	VDCallback_Open open;
	VDCallback_Read read;
	VDCallback_Write write;
	VDCallback_Seek seek;
	VDCallback_Close close;
};

/*!
 * \brief Enables or disables the webserver.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b enable is not valid.
 */
EXPORT_SPEC int UpnpEnableWebserver(
	/*! [in] \c TRUE to enable, \c FALSE to disable. */
	int enable);

/*!
 * \brief Returns \c TRUE if the webserver is enabled, or \c FALSE if it is not.
 *
 *  \return An integer representing one of the following:
 *       \li \c TRUE: The webserver is enabled.
 *       \li \c FALSE: The webserver is not enabled
 */
EXPORT_SPEC int UpnpIsWebserverEnabled(void);

/*!
 *  \brief Sets the callback functions to be used to access a virtual directory.
 *
 *  \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_PARAM: one of the callbacks is not valid.
 */
EXPORT_SPEC int UpnpSetVirtualDirCallbacks(
	/*! [in] A structure that contains the callback functions. */
	struct UpnpVirtualDirCallbacks *callbacks );

/*!
 * \brief Adds a virtual directory mapping.
 *
 * All webserver requests containing the given directory are read using
 * functions contained in a \b VirtualDirCallbacks structure registered
 * via \b UpnpSetVirtualDirCallbacks.
 *
 * \note This function is not available when the web server is not
 * 	compiled into the UPnP Library.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b dirName is not valid.
 */
EXPORT_SPEC int UpnpAddVirtualDir(
	/*! [in] The name of the new directory mapping to add. */
	const char *dirName, const void *cookie, const void **oldcookie);

/*!
 * \brief Removes a virtual directory mapping made with \b UpnpAddVirtualDir.
 *
 * \return An integer representing one of the following:
 *       \li \c UPNP_E_SUCCESS: The operation completed successfully.
 *       \li \c UPNP_E_INVALID_ARGUMENT: \b dirName is not valid.
 */
EXPORT_SPEC int UpnpRemoveVirtualDir(
	/*! [in] The name of the virtual directory mapping to remove. */
	const char *dirName);

/*!
 * \brief Removes all virtual directory mappings.
 */
EXPORT_SPEC void UpnpRemoveAllVirtualDirs(void);

/* @} Web Server API */

#endif /* UPNP_H */
