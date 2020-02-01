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

#include "config.h"
#ifdef INCLUDE_CLIENT_APIS
#if EXCLUDE_SOAP == 0

#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#include <curl/curl.h>
#include <upnp/ixml.h>

#include "miniserver.h"
#include "httputils.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "soaplib.h"
#include "uri.h"
#include "upnp.h"
#include "smallut.h"

#define SOAP_ACTION_RESP	1
#define SOAP_VAR_RESP		2
/*#define SOAP_ERROR_RESP       3*/
#define SOAP_ACTION_RESP_ERROR  3
#define SOAP_VAR_RESP_ERROR	4

/*!
 * \brief Compares 'name' and node's localname (ignoring possible prefix).
 *
 * \return 0 if both are equal; 1 if not equal, and UPNP_E_OUTOF_MEMORY.
 */
static int dom_cmp_name(const char *name, IXML_Node *node)
{
	const char *nodename = ixmlNode_getNodeName(node);
	if (nullptr == nodename)
		return UPNP_E_OUTOF_MEMORY;
	const char *localname = strchr(nodename, ':');
	return localname ? strcmp(localname+1, name) : strcmp(nodename, name);
}

/*!
 * \brief Goes thru each child of 'start_node' looking for a node having
 * the name 'node_name'.
 *
 * \return UPNP_E_SUCCESS if successful else returns appropriate error.
 */
static int dom_find_node_child_by_name(
	/* [in] name of the node. */
	const char *node_name,
	/* [in] complete xml node. */
	IXML_Node *start_node,
	/* [out] matched node. */
	IXML_Node **matching_node)
{
	IXML_Node *node;

	/* invalid args */
	if (!node_name || !start_node)
		return UPNP_E_NOT_FOUND;
	node = ixmlNode_getFirstChild(start_node);
	while (node != NULL) {
		/* match name */
		if (dom_cmp_name(node_name, node) == 0) {
			*matching_node = node;
			return UPNP_E_SUCCESS;
		}
		/* free and next node */
		node = ixmlNode_getNextSibling(node);
	}
	return UPNP_E_NOT_FOUND;
}

/*!
 * \brief Poor man's xpath: follow path indicated by the names array and return 
 *  the node specifed by the last name.
 *
 * \return UPNP_E_SUCCESS if successful, else returns appropriate error.
 */
static int dom_find_deep_node(
	/* [in] array of names. */
	const char *names[],
	/* [in] size of array. */
	int num_names,
	/* [in] Node from where it should should be searched. */
	IXML_Node *start_node,
	/* [out] Node that matches the last name of the array. */
	IXML_Node **matching_node)
{
	// The previous version could succeed even if the first node name
	// did not match.
	if (dom_cmp_name(names[0], start_node)) {
		return UPNP_E_NOT_FOUND;
	}
	for (int i = 1; i < num_names; i++) {
		IXML_Node *match_node;
		if (dom_find_node_child_by_name(
				names[i], start_node, &match_node) != UPNP_E_SUCCESS)
			return UPNP_E_NOT_FOUND;
		start_node = match_node;
	}
	*matching_node = start_node;
	return UPNP_E_SUCCESS;
}

/****************************************************************************
*	Function :	get_node_value
*
*	Parameters :
*			IN IXML_Node *node : input node	
*
*	Description :	This function returns the value of the text node
*
*	Return : DOMString
*		string containing the node value
*
*	Note :The given node must have a text node as its first child
****************************************************************************/
static const DOMString get_node_value(IN IXML_Node * node)
{
    IXML_Node *text_node = ixmlNode_getFirstChild(node);
    if (text_node == NULL) {
        return NULL;
    }
	return ixmlNode_getNodeValue(text_node);
}

/****************************************************************************
*	Function :	get_response_value
*
*	Parameters :
*			IN payload :	HTTP response message
*			IN int code :	return code in the HTTP response
*			IN char*name :	name of the action
*			OUT int *upnp_error_code :	UPnP error code
*			OUT IXML_Node ** action_value :	SOAP response node 
*
*	Description :	This function handles the response coming back from the 
*		device. This function parses the response and gives back the SOAP 
*		response node.
*
*	Return : int
*		return the type of the SOAP message if successful else returns 
*	appropriate error.
*
*	Note :
****************************************************************************/
static int
get_response_value( IN const std::string& payload,
					IN long http_status,
					IN const std::string& cttype,
                    IN const std::string& name,
                    OUT int *upnp_error_code,
                    OUT IXML_Node **action_value)
{
	IXML_Node *node = NULL;
	IXML_Node *root_node = NULL;
	IXML_Node *error_node = NULL;
	IXML_Document *doc = NULL;
	char *node_str = NULL;
	const char *temp_str = NULL;
	DOMString error_node_str = NULL;
	int err_code = UPNP_E_BAD_RESPONSE; /* default error */
	int done = FALSE;
	const char *names[5];

	UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
			   "get_response_value: status %ld ct [%s] name [%s] payload [%s]\n",
			   http_status, cttype.c_str(), name.c_str(), payload.c_str());

	/* only 200 and 500 status codes are relevant */
	if ((http_status != HTTP_OK &&
	     http_status != HTTP_INTERNAL_SERVER_ERROR) ||
	    cttype.find("text/xml") != 0)
		goto error_handler;
	if (ixmlParseBufferEx(payload.c_str(), &doc) != IXML_SUCCESS)
		goto error_handler;
	root_node = ixmlNode_getFirstChild((IXML_Node *) doc);
	if (root_node == NULL)
		goto error_handler;
	/* try reading soap action response */
	assert(action_value != NULL);

	*action_value = NULL;
	names[0] = "Envelope";
	names[1] = "Body";
	names[2] = name.c_str();
	if (dom_find_deep_node(names, 3, root_node, &node) ==
		UPNP_E_SUCCESS) {
		node_str = ixmlPrintNode(node);
		if (node_str == NULL) {
			err_code = UPNP_E_OUTOF_MEMORY;
			goto error_handler;
		}
		if (ixmlParseBufferEx(node_str,
							  (IXML_Document **) action_value)
			!= IXML_SUCCESS) {
			err_code = UPNP_E_BAD_RESPONSE;
			goto error_handler;
		}
		err_code = SOAP_ACTION_RESP;
		done = TRUE;
	}
	if (!done) {
		/* not action or var resp; read error code and description */
		names[0] = "Envelope";
		names[1] = "Body";
		names[2] = "Fault";
		names[3] = "detail";
		names[4] = "UPnPError";
		if (dom_find_deep_node(names, 5, root_node, &error_node) !=
		    UPNP_E_SUCCESS)
			goto error_handler;
		if (dom_find_node_child_by_name("errorCode", error_node, &node) !=
		    UPNP_E_SUCCESS)
			goto error_handler;
		temp_str = get_node_value(node);
		if (!temp_str)
			goto error_handler;
		*upnp_error_code = atoi(temp_str);
		if (*upnp_error_code > 400) {
			err_code = *upnp_error_code;
			goto error_handler;	/* bad SOAP error code */
		}
		error_node_str = ixmlPrintNode(error_node);
		if (error_node_str == NULL) {
			err_code = UPNP_E_OUTOF_MEMORY;
			goto error_handler;
		}
		if (ixmlParseBufferEx(error_node_str,
							  (IXML_Document **) action_value)
			!= IXML_SUCCESS) {
			err_code = UPNP_E_BAD_RESPONSE;

			goto error_handler;
		}
		err_code = SOAP_ACTION_RESP_ERROR;
	}

 error_handler:
	ixmlDocument_free(doc);
	ixmlFreeDOMString(node_str);
	ixmlFreeDOMString(error_node_str);
	return err_code;
}

/****************************************************************************
*	Function :	get_action_name
*
*	Parameters :
*			IN char* nodename XML possibly qualified node name
*			OUT actname : name of the action	
*
*	Description :	This functions retrieves the action name in the buffer 
*                   which is a qualified action name from the xml soap body
*
*	Return : int
*		returns 0 on success; -1 on error
*
*	Note :
****************************************************************************/
static int
get_action_name(IN const std::string& nodename, OUT std::string& actname)
{
	std::string::size_type colon = nodename.find(':');
	if (colon == std::string::npos) {
		actname = nodename;
	} else {
		actname = nodename.substr(colon+1);
	}
	return 0;
}

/****************************************************************************
*	Function :	SoapSendAction
*
*	Parameters :
*		IN char* action_url :	device contrl URL 
*		IN char *service_type :	device service type
*		IN IXML_Document *action_node : SOAP action node	
*		OUT IXML_Document **response_node :	SOAP response node
*
*	Description :	This function is called by UPnP API to send the SOAP 
*		action request and waits till it gets the response from the device
*		pass the response to the API layer
*
*	Return :	int
*		returns UPNP_E_SUCCESS if successful else returns appropriate error
*	Note :
****************************************************************************/
int
SoapSendAction( IN char *action_url,
                IN char *service_type,
                IN IXML_Document * action_node,
                OUT IXML_Document ** response_node )
{
	return SoapSendActionEx(action_url, service_type, NULL, action_node,
							response_node);
}


/****************************************************************************
*	Function :	SoapSendActionEx
*
*	Parameters :
*		IN char* action_url :	device contrl URL 
*		IN char *service_type :	device service type
		IN IXML_Document *Header: Soap header
*		IN IXML_Document *action_node : SOAP action node ( SOAP body)	
*		OUT IXML_Document **response_node :	SOAP response node
*
*	Description :	This function is called by UPnP API to send the SOAP 
*		action request and waits till it gets the response from the device
*		pass the response to the API layer. This action is similar to the 
*		the SoapSendAction with only difference that it allows users to 
*		pass the SOAP header along the SOAP body ( soap action request)
*
*	Return :	int
*		returns UPNP_E_SUCCESS if successful else returns appropriate error
*	Note :
****************************************************************************/
int SoapSendActionEx(
	IN char *action_url,
	IN char *service_type,
	IN IXML_Document *header,
	IN IXML_Document *action_node,
	OUT IXML_Document **response_node )
{
    const static std::string xml_start{
        "<s:Envelope "
		   "xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		   "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"};
    const static std::string xml_header_start{"<s:Header>\r\n"};
    const static std::string xml_header_end{"</s:Header>\r\n"};
    const static std::string xml_body_start{"<s:Body>"};
    const static std::string xml_end{"</s:Body>\r\n</s:Envelope>\r\n"};

    int err_code = UPNP_E_OUTOF_MEMORY;

    *response_node = NULL;

    UpnpPrintf( UPNP_INFO, SOAP, __FILE__, __LINE__,
        "Inside SoapSendActionEx():\n" );

    /* header string */
	std::string xml_header_str;
	if (header) {
		char *cp = ixmlPrintNode((IXML_Node *)header);
		if (cp == NULL) {
			return UPNP_E_OUTOF_MEMORY;
		}
		xml_header_str.assign(cp);
		ixmlFreeDOMString(cp);
	}

    /* action */
	char *cp = ixmlPrintNode((IXML_Node *)action_node);
	if (cp == NULL) {
		return UPNP_E_OUTOF_MEMORY;
	}
	std::string action_str(cp);
	ixmlFreeDOMString(cp);
	IXML_Node *root_node = ixmlNode_getFirstChild((IXML_Node *)action_node);
	const char *nodename = ixmlNode_getNodeName(root_node);
	std::string responsename;
	if(get_action_name(nodename, responsename) != 0 ) {
		return UPNP_E_INVALID_ACTION;
	}
	
    /* parse url */
    uri_type url;
    if (http_FixStrUrl(action_url, strlen(action_url), &url) != 0) {
        return UPNP_E_INVALID_URL;
    }

    UpnpPrintf( UPNP_INFO, SOAP, __FILE__, __LINE__,
        "path=%.*s, hostport=%.*s\n",
				(int)url.pathquery.size(),
				url.pathquery.c_str(),
				(int)url.hostport.text.size(),
				url.hostport.text.c_str());

	std::string payload{xml_start};
	if (header) {
		payload += xml_header_start + xml_header_str + xml_header_end;
	}
	payload += xml_body_start + action_str + xml_end;
	std::string soapaction = std::string("SOAPACTION: \"") + service_type + "#" +
		responsename + "\"";

	std::map<std::string, std::string> http_headers;
	std::string responsestr;
	long http_status;
    int ret_code;
	{
		CURL *easy = curl_easy_init();
		char curlerrormessage[CURL_ERROR_SIZE];

		curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curlerrormessage);
		curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback_str_curl);
		curl_easy_setopt(easy, CURLOPT_WRITEDATA, &responsestr);
		curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback_curl);
		curl_easy_setopt(easy, CURLOPT_HEADERDATA, &http_headers);
		curl_easy_setopt(easy, CURLOPT_TIMEOUT, long(UPNP_TIMEOUT));
		curl_easy_setopt(easy, CURLOPT_POST, long(1));
		curl_easy_setopt(easy, CURLOPT_POSTFIELDS, payload.c_str()); 
		struct curl_slist *list = NULL;
		list = curl_slist_append(list,
								 "Content-Type: text/xml; charset=\"utf-8\"");
		list = curl_slist_append(list, soapaction.c_str());
		list = curl_slist_append(list, "Accept:");
		list = curl_slist_append(list, "Expect:");
		curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);
		curl_easy_setopt(easy, CURLOPT_URL, uri_asurlstr(url).c_str());

		CURLcode code = curl_easy_perform(easy);
		if (code == CURLE_OK) {
			curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_status);
			ret_code = UPNP_E_SUCCESS;
		} else {
			UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
					   "CURL ERROR MESSAGE %s\n", curlerrormessage);
			ret_code = UPNP_E_BAD_RESPONSE;
		}
		/* Clean-up. */
		curl_slist_free_all(list);
		curl_easy_cleanup(easy);
	}

    if (ret_code != UPNP_E_SUCCESS) {
		return ret_code;
    }

	responsename += "Response";

	const auto it = http_headers.find("content-type");
	if (it == http_headers.end()) {
		UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
				   "No Content-Type header in SOAP response\n");
		return  UPNP_E_BAD_RESPONSE;
	}
	std::string content_type = it->second;

    /* get action node from the response */
    int upnp_error_code = 0;
    ret_code = get_response_value(
		responsestr, http_status, content_type,
		responsename, &upnp_error_code, (IXML_Node **)response_node);

    if( ret_code == SOAP_ACTION_RESP ) {
        err_code = UPNP_E_SUCCESS;
    } else if (ret_code == SOAP_ACTION_RESP_ERROR ) {
        err_code = upnp_error_code;
    } else {
        err_code = ret_code;
    }
    return err_code;
}

#endif /* EXCLUDE_SOAP */
#endif /* INCLUDE_CLIENT_APIS */

