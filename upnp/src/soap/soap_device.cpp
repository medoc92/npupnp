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

/*!
 * \file
 */

#include "config.h"

#ifdef INCLUDE_DEVICE_APIS
#if EXCLUDE_SOAP == 0


#include <assert.h>
#include <string.h>
#include <microhttpd.h>
#include <iostream>

#include "soaplib.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "httputils.h"
#include "upnputil.h"
#include "smallut.h"

#define SREQ_HDR_NOT_FOUND	 -1
#define SREQ_BAD_HDR_FORMAT	 -2
#define SREQ_NOT_EXTENDED	 -3

#define SOAP_INVALID_ACTION 401
#define SOAP_INVALID_ARGS	402
#define SOAP_OUT_OF_SYNC	403
#define SOAP_INVALID_VAR	404
#define SOAP_ACTION_FAILED	501
#define SOAP_MEMORY_OUT		603


static const char *ContentTypeXML = "text/xml; charset=\"utf-8\"";
static const char *SOAP_BODY = "Body";
static const char *SOAP_URN = "http:/""/schemas.xmlsoap.org/soap/envelope/";
static const char *QUERY_STATE_VAR_URN = "urn:schemas-upnp-org:control-1-0";

static const char *Soap_Invalid_Action = "Invalid Action";
/*static const char* Soap_Invalid_Args = "Invalid Args"; */
static const char *Soap_Action_Failed = "Action Failed";
static const char *Soap_Invalid_Var = "Invalid Var";
static const char *Soap_Memory_out = "Out of Memory";

typedef struct soap_devserv_t {
	char dev_udn[NAME_SIZE];
	char service_type[NAME_SIZE];
	char service_id[NAME_SIZE];
	std::string action_name;
	Upnp_FunPtr callback;
	void *cookie;
} soap_devserv_t;


/*!
 * \brief Sends SOAP error response.
 */
static void send_error_response(
	MHDTransaction *mhdt,
	/*! [in] Error code. */
	IN int error_code,
	/*! [in] Error message. */
	IN const char *err_msg)
{
/*		"<?xml version=\"1.0\"?>\n" required?? */
	std::string start_body {
		"<s:Envelope "
			"xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
			"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
			"<s:Body>\n"
			"<s:Fault>\n"
			"<faultcode>s:Client</faultcode>\n"
			"<faultstring>UPnPError</faultstring>\n"
			"<detail>\n"
			"<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">\n"
			"<errorCode>"};
	const char *mid_body =
		"</errorCode>\n"
		"<errorDescription>";
	const char *end_body =
		"</errorDescription>\n"
		"</UPnPError>\n"
		"</detail>\n"
		"</s:Fault>\n"
		"</s:Body>\n"
		"</s:Envelope>\n";
	char err_code_str[30];
	memset(err_code_str, 0, sizeof(err_code_str));
	snprintf(err_code_str, sizeof(err_code_str), "%d", error_code);
	start_body += std::string(err_code_str) + mid_body + err_msg + end_body;
	mhdt->response = MHD_create_response_from_buffer(
		start_body.size(), (char*)start_body.c_str(), MHD_RESPMEM_MUST_COPY);
	/* We do as the original code, but should this not be error_code? */
	mhdt->httpstatus = 500;
}

/*!
 * \brief Sends response of get var status.
 */
static UPNP_INLINE void send_var_query_response(
	MHDTransaction *mhdt,
	/*! [in] Value of the state variable. */
	const char *var_value)
{
	const char *start_body =
		"<s:Envelope "
		"xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
		"<s:Body>\n"
		"<u:QueryStateVariableResponse "
		"xmlns:u=\"urn:schemas-upnp-org:control-1-0\">\n" "<return>";
	const char *end_body =
		"</return>\n"
		"</u:QueryStateVariableResponse>\n" "</s:Body>\n" "</s:Envelope>\n";
	std::string response(start_body);
	response += var_value;
	response += end_body;

	mhdt->response = MHD_create_response_from_buffer(
		response.size(), (char*)response.c_str(), MHD_RESPMEM_MUST_COPY);
	mhdt->httpstatus = 200;
}


/*!
 * \brief Sends the SOAP action response.
 */
static UPNP_INLINE void send_action_response(
	MHDTransaction *mhdt,
	/*! [in] The response document. */
	IXML_Document *action_resp)
{
	int err_code;
	static const char *start_body =
		/*"<?xml version=\"1.0\"?>" required?? */
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap."
		"org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap."
		"org/soap/encoding/\"><s:Body>\n";
	static const char *end_body = "</s:Body> </s:Envelope>";
	std::string response(start_body);

	err_code = UPNP_E_OUTOF_MEMORY;	/* one error only */
	/* get xml */
	char *xml_response = ixmlPrintNode((IXML_Node *) action_resp);
	if (!xml_response)
		goto error_handler;
	response += xml_response;
	response += end_body;

	mhdt->response = MHD_create_response_from_buffer(
	response.size(), (char*)response.c_str(), MHD_RESPMEM_MUST_COPY);
	mhdt->httpstatus = 200;
	err_code = 0;
	
error_handler:
	ixmlFreeDOMString(xml_response);
	if (err_code != 0) {
		/* only one type of error to worry about - out of mem */
		send_error_response(mhdt, SOAP_ACTION_FAILED, "Out of memory");
	}
}


/*!
 * \brief Handles the SOAP requests to querry the state variables.
 * This functionality has been deprecated in the UPnP V1.0 architecture.
 */
static UPNP_INLINE void handle_query_variable(
	MHDTransaction *mhdt,
	/*! [in] SOAP device/service information. */
	soap_devserv_t *soap_info,
	/*! [in] Node containing variable name. */
	IXML_Node *req_node)
{
	struct Upnp_State_Var_Request variable;
	const char *err_str;
	int err_code;
	const DOMString var_name;

	memset(&variable, 0, sizeof(variable));
	upnp_strlcpy(variable.DevUDN, soap_info->dev_udn, NAME_SIZE);
	upnp_strlcpy(variable.ServiceID, soap_info->service_id, NAME_SIZE);
	var_name = ixmlNode_getNodeValue(req_node);
	upnp_strlcpy(variable.StateVarName, var_name, NAME_SIZE);
	memcpy(&variable.CtrlPtIPAddr, mhdt->client_address,
		   sizeof(struct sockaddr_storage));
	variable.CurrentVal = NULL;

	/* send event */
	soap_info->callback(UPNP_CONTROL_GET_VAR_REQUEST, &variable,
						soap_info->cookie);
	UpnpPrintf(UPNP_INFO, SOAP, __FILE__, __LINE__,
			   "Return from callback for var request\n");
	/* validate, and handle result */
	if (variable.CurrentVal == NULL) {
		send_error_response(mhdt, SOAP_INVALID_VAR, Soap_Invalid_Var);
		return;
	}
	if (variable.ErrCode != UPNP_E_SUCCESS) {
		if (strlen(variable.ErrStr) == 0) {
			err_code = SOAP_INVALID_VAR;
			err_str = Soap_Invalid_Var;
		} else {
			err_code = variable.ErrCode;
			err_str = variable.ErrStr;
		}
		send_error_response(mhdt, err_code, err_str);
		ixmlFreeDOMString(variable.CurrentVal);
		return;
	}
	/* send response */
	send_var_query_response(mhdt, variable.CurrentVal);
	ixmlFreeDOMString(variable.CurrentVal);
}

/*!
 * \brief Handles the SOAP action request.
 */
static void handle_invoke_action(
	MHDTransaction *mhdt,
	/*! [in] SOAP device/service information. */
	soap_devserv_t *soap_info,
	/*! [in] Node containing the SOAP action request. */
	IXML_Node *req_node)
{
	IXML_Document *req_doc = NULL;
	struct Upnp_Action_Request action;
	int err_code;
	const char *err_str;
	action.ActionResult = NULL;
	DOMString act_node = NULL;

	/* get action node */
	act_node = ixmlPrintNode(req_node);
	if (!act_node) {
		err_code = SOAP_MEMORY_OUT;
		err_str = Soap_Memory_out;
		goto error_handler;
	}
	err_code = ixmlParseBufferEx(act_node, &req_doc);
	if (err_code != IXML_SUCCESS) {
		if (IXML_INSUFFICIENT_MEMORY == err_code) {
			err_code = SOAP_MEMORY_OUT;
			err_str = Soap_Memory_out;
		} else {
			err_code = SOAP_INVALID_ACTION;
			err_str = Soap_Invalid_Action;
		}
		goto error_handler;
	}
	action.ErrCode = UPNP_E_SUCCESS;
	action.ErrStr[0] = 0;
	upnp_strlcpy(action.ActionName, soap_info->action_name, NAME_SIZE);
	upnp_strlcpy(action.DevUDN, soap_info->dev_udn, NAME_SIZE);
	upnp_strlcpy(action.ServiceID, soap_info->service_id, NAME_SIZE);
	action.ActionRequest = req_doc;
	action.ActionResult = NULL;
	if (mhdt->client_address->ss_family == AF_INET) {
		memcpy(&action.CtrlPtIPAddr, mhdt->client_address,
			   sizeof(struct sockaddr_in));
	} else {
		memcpy(&action.CtrlPtIPAddr, mhdt->client_address,
			   sizeof(struct sockaddr_in6));
	}		
	UpnpPrintf(UPNP_INFO, SOAP, __FILE__, __LINE__, "Calling Callback\n");
	soap_info->callback(UPNP_CONTROL_ACTION_REQUEST, &action, soap_info->cookie);
	if (action.ErrCode != UPNP_E_SUCCESS) {
		if (strlen(action.ErrStr) <= 0) {
			err_code = SOAP_ACTION_FAILED;
			err_str = Soap_Action_Failed;
		} else {
			err_code = action.ErrCode;
			err_str = action.ErrStr;
		}
		goto error_handler;
	}
	/* validate, and handle action error */
	if (action.ActionResult == NULL) {
		err_code = SOAP_ACTION_FAILED;
		err_str = Soap_Action_Failed;
		goto error_handler;
	}
	/* send response */
	send_action_response(mhdt, action.ActionResult);
	err_code = 0;

	/* error handling and cleanup */
error_handler:
	ixmlDocument_free(action.ActionResult);
	ixmlDocument_free(req_doc);
	ixmlFreeDOMString(act_node);
	/* restore */
	if (err_code != 0)
		send_error_response(mhdt, err_code, err_str);
}

/*!
 * \brief Retrieve SOAP device/service information associated
 * with request-URI, which includes the callback function to hand-over
 * the request to the device application.
 *
 * \return 0 if OK, -1 on error.
 */
static int get_dev_service(MHDTransaction *mhdt,
						   /*! [in] Address family: AF_INET or AF_INET6. */
						   int AddressFamily,
						   /*! [out] SOAP device/service information. */
						   soap_devserv_t *soap_info)
{
	struct Handle_Info *device_info;
	int device_hnd;
	service_info *serv_info;
	/* error by default */
	int ret_code = -1;

	HandleReadLock();

	if (GetDeviceHandleInfoForPath(mhdt->url.c_str(), AddressFamily, &device_hnd,
								   &device_info, &serv_info) != HND_DEVICE)
		goto error_handler;
	if (!serv_info)
		goto error_handler;

	upnp_strlcpy(soap_info->dev_udn, serv_info->UDN, NAME_SIZE);
	upnp_strlcpy(soap_info->service_type, serv_info->serviceType, NAME_SIZE);
	upnp_strlcpy(soap_info->service_id, serv_info->serviceId, NAME_SIZE);
	soap_info->callback = device_info->Callback;
	soap_info->cookie = device_info->Cookie;
	ret_code = 0;

 error_handler:
	HandleUnlock();
	return ret_code;
}

/*!
 * \brief Get the SOAPACTION header value for M-POST request (deprecated).
 *
 * \return UPNP_E_SUCCESS if OK, error number on failure.
 */
static int get_mpost_acton_hdrval(MHDTransaction *mhdt, std::string& val)
{
	assert(HTTPMETHOD_MPOST == mhdt->method);
	auto it = mhdt->headers.find("man");
	if (it == mhdt->headers.end())
		return SREQ_NOT_EXTENDED;

	stringtolower(it->second);
	char aname[201];
	int ret = sscanf(it->second.c_str(), " \"%*[^\"]\" ; ns = %200s", aname);
	if (ret != 1) {
		return SREQ_NOT_EXTENDED;
	}
	/* create soapaction name header */
	std::string soap_action_name{aname};
	soap_action_name += "-SOAPACTION";
	it = mhdt->headers.find(soap_action_name);
	if (it == mhdt->headers.end())
		return SREQ_HDR_NOT_FOUND;
	val = it->second;
	return UPNP_E_SUCCESS;
}

/*!
 * \brief Check the header validity, and get the action name
 * and the version of the service that the CP wants to use.
 *
 * \return UPNP_E_SUCCESS if OK, error number on failure.
 */
static int check_soapaction_hdr(MHDTransaction *mhdt,
		/*! [in, out] SOAP device/service information. */
		soap_devserv_t *soap_info)
{
	int ret_code;
	std::string header;
	/* find SOAPACTION header */
	if (SOAPMETHOD_POST == mhdt->method) {
		auto it = mhdt->headers.find("soapaction");
		if (it == mhdt->headers.end())
			return SREQ_HDR_NOT_FOUND;
		header = it->second;
	} else {
		/* Note that M-POST is deprecated */
		ret_code = get_mpost_acton_hdrval(mhdt, header);
		if (ret_code != UPNP_E_SUCCESS) {
			return ret_code;
		}
	}

	/* error by default */
	ret_code = SREQ_BAD_HDR_FORMAT;

	/* The header value is something like: "urn:av-open...:Playlist:1#Id"  */
	if (header[0] != '\"') {
		return ret_code;
	}

	std::string::size_type hash_pos = header.find('#');
	if (hash_pos == std::string::npos) {
		return ret_code;
	}
	char anm[201];
	if (sscanf(header.c_str() + hash_pos+1, "%200[^\"]", anm) != 1) {
		return ret_code;
	}
	soap_info->action_name = anm;

	/* check service type */
	std::string serv_type = header.substr(1, hash_pos-1);
	size_t cp1_diff = serv_type.rfind(':');
	if (std::string::npos == cp1_diff) {
		return ret_code;
	}
	const char *col_pos2 = strrchr(soap_info->service_type, ':');
	/* XXX: this should be checked when service list is generated */
	assert(col_pos2 != NULL);
	size_t cp2_diff = col_pos2 - soap_info->service_type;
	if (cp2_diff == cp1_diff &&
		strncmp(soap_info->service_type, serv_type.c_str(), cp1_diff) == 0) {
		/* for action invocation, update the version information */
		upnp_strlcpy(soap_info->service_type, serv_type, NAME_SIZE);
	} else if (strcmp(serv_type.c_str(), QUERY_STATE_VAR_URN) == 0 &&
			   soap_info->action_name.compare("QueryStateVariable") == 0) {
		/* query variable */
		soap_info->action_name.clear();
	} else {
		return ret_code;
	}

	return UPNP_E_SUCCESS;
}


/*!
 * \brief Check validity of the SOAP request per UPnP specification.
 *
 * \return 0 if OK, -1 on failure.
 */
static int check_soap_request(
		/*! [in] SOAP device/service information. */
		soap_devserv_t *soap_info,
		/*! [in] Document containing the SOAP action request. */
		IN IXML_Document *xml_doc,
		/*! [out] Node containing the SOAP action request/variable name. */
		IXML_Node **req_node)
{
	IXML_Node *envp_node = NULL;
	IXML_Node *body_node = NULL;
	IXML_Node *action_node = NULL;
	const DOMString local_name = NULL;
	const DOMString ns_uri = NULL;
	int ret_val = -1;

	/* Got the Envelop node here */
	envp_node = ixmlNode_getFirstChild((IXML_Node *) xml_doc);
	if (NULL == envp_node) {
		goto error_handler;
	}
	ns_uri = ixmlNode_getNamespaceURI(envp_node);
	if (NULL == ns_uri || strcmp(ns_uri, SOAP_URN) != 0) {
		goto error_handler;
	}
	/* Got Body here */
	body_node = ixmlNode_getFirstChild(envp_node);
	if (NULL == body_node) {
		goto error_handler;
	}
	local_name = ixmlNode_getLocalName(body_node);
	if (NULL == local_name || strcmp(local_name, SOAP_BODY) != 0) {
		goto error_handler;
	}
	/* Got action node here */
	action_node = ixmlNode_getFirstChild(body_node);
	if (NULL == action_node) {
		goto error_handler;
	}
	/* check local name and namespace of action node */
	ns_uri = ixmlNode_getNamespaceURI(action_node);
	if (NULL == ns_uri) {
		goto error_handler;
	}
	local_name = ixmlNode_getLocalName(action_node);
	if (NULL == local_name) {
		goto error_handler;
	}
	if (soap_info->action_name.empty()) {
		IXML_Node *varname_node = NULL;
		IXML_Node *nametxt_node = NULL;
		if (strcmp(ns_uri, QUERY_STATE_VAR_URN) != 0 ||
			strcmp(local_name, "QueryStateVariable") != 0) {
			goto error_handler;
		}
		varname_node = ixmlNode_getFirstChild(action_node);
		if(NULL == varname_node) {
			goto error_handler;
		}
		local_name = ixmlNode_getLocalName(varname_node);
		if (strcmp(local_name, "varName") != 0) {
			goto error_handler;
		}
		nametxt_node = ixmlNode_getFirstChild(varname_node);
		if (NULL == nametxt_node ||
			ixmlNode_getNodeType(nametxt_node) != eTEXT_NODE) {
			goto error_handler;
		}
		*req_node = nametxt_node;
	} else {
		/* check service type against SOAPACTION header */
		if (strcmp(soap_info->service_type, ns_uri) != 0 ||
			soap_info->action_name.compare(local_name) != 0) {
			goto error_handler;
		}
		*req_node = action_node;
	}
	/* success */
	ret_val = 0;

error_handler:
	return ret_val;
}


/*!
 * \brief This is a callback called by minisever after receiving the request
 * from the control point. After HTTP processing, it calls handle_soap_request
 * to start SOAP processing.
 */
void soap_device_callback(MHDTransaction *mhdt)
{
	int err_code;
	IXML_Document *xml_doc = NULL;
	soap_devserv_t *soap_info = NULL;
	IXML_Node *req_node = NULL;

	/* get device/service identified by the request-URI */
	soap_info = new soap_devserv_t;
	if (NULL == soap_info) {
		err_code = HTTP_INTERNAL_SERVER_ERROR;
		goto error_handler;
	}

	if (get_dev_service(mhdt, mhdt->client_address->ss_family, soap_info) < 0) {
		err_code = HTTP_NOT_FOUND;
		goto error_handler;
	}

	/* validate: content-type == text/xml */
	if (!has_xml_content_type(mhdt)) {
		err_code = HTTP_UNSUPPORTED_MEDIA_TYPE;
		goto error_handler;
	}
	
	/* check SOAPACTION HTTP header */
	err_code = check_soapaction_hdr(mhdt, soap_info);
	if (err_code != UPNP_E_SUCCESS) {
		switch (err_code) {
		case SREQ_NOT_EXTENDED:
			err_code = HTTP_NOT_EXTENDED;
			break;
		case UPNP_E_OUTOF_MEMORY:
			err_code = HTTP_INTERNAL_SERVER_ERROR;
			break;
		default:
			err_code = HTTP_BAD_REQUEST;
			break;
		}
		goto error_handler;
	}
	/* parse XML */
	err_code = ixmlParseBufferEx(mhdt->postdata.c_str(), &xml_doc);
	if (err_code != IXML_SUCCESS) {
		if (IXML_INSUFFICIENT_MEMORY == err_code)
			err_code = HTTP_INTERNAL_SERVER_ERROR;
		else
			err_code = HTTP_BAD_REQUEST;
		goto error_handler;
	}
	/* check SOAP body */
	if (check_soap_request(soap_info, xml_doc, &req_node) < 0) {
		err_code = HTTP_BAD_REQUEST;
		goto error_handler;
	}
	/* process SOAP request */
	if (soap_info->action_name.empty())
		/* query var */
		handle_query_variable(mhdt, soap_info, req_node);
	else
		/* invoke action */
		handle_invoke_action(mhdt, soap_info, req_node);

	if (mhdt->response)
		MHD_add_response_header(mhdt->response, "Content-Type", ContentTypeXML);

error_handler:
	ixmlDocument_free(xml_doc);
	delete soap_info;
	return;
}

#endif /* EXCLUDE_SOAP */

#endif /* INCLUDE_DEVICE_APIS */

