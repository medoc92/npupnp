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

#include "config.h"

#ifdef INCLUDE_DEVICE_APIS
#if EXCLUDE_SOAP == 0

#include <cassert>
#include <cstring>
#include <microhttpd.h>

#include <iostream>
#include <sstream>
#include <string>
#include <map>

#ifdef USE_EXPAT
#include "expatmm.hxx"
#define XMLPARSERTP inputRefXMLParser
#else
#include "picoxml.h"
#define XMLPARSERTP PicoXMLParser
#endif

#include "soaplib.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "httputils.h"
#include "genut.h"

#define SREQ_HDR_NOT_FOUND	 -1
#define SREQ_BAD_HDR_FORMAT	 -2
#define SREQ_NOT_EXTENDED	 -3

#define SOAP_INVALID_ACTION 401
#define SOAP_INVALID_ARGS	402
#define SOAP_OUT_OF_SYNC	403
#define SOAP_INVALID_VAR	404
#define SOAP_ACTION_FAILED	501
#define SOAP_MEMORY_OUT		603
static const char *Soap_Invalid_Action = "Invalid Action";
static const char *Soap_Action_Failed = "Action Failed";

static const char *QUERY_STATE_VAR_URN = "urn:schemas-upnp-org:control-1-0";

struct soap_devserv_t {
	char dev_udn[NAME_SIZE];
	char service_type[NAME_SIZE];
	char service_id[NAME_SIZE];
	std::string action_name;
	Upnp_FunPtr callback;
	void *cookie;
};

/*!
 * \brief Sends SOAP error response.
 */
static void send_error_response(
	MHDTransaction *mhdt, int error_code, const char *err_msg)
{
	const static std::string start_body {
		"<?xml version=\"1.0\"?>\n"
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
	const static std::string mid_body {
		"</errorCode>\n"
		"<errorDescription>"
	};
	const static std::string end_body {
		"</errorDescription>\n"
		"</UPnPError>\n"
		"</detail>\n"
		"</s:Fault>\n"
		"</s:Body>\n"
		"</s:Envelope>\n"
	};
	std::ostringstream ostr;
	ostr << start_body << error_code << mid_body << err_msg << end_body;
	const std::string& txt = ostr.str();
	mhdt->response = MHD_create_response_from_buffer(
		txt.size(), const_cast<char*>(txt.c_str()), MHD_RESPMEM_MUST_COPY);
	MHD_add_response_header(mhdt->response, "Content-Type", "text/xml");
	/* We do as the original code, but should this not be error_code? */
	mhdt->httpstatus = 500;
}

/* Sends the SOAP action response. */
static void send_action_response(
	MHDTransaction *mhdt, soap_devserv_t *soap_info,
	const std::vector<std::pair<std::string, std::string> >& data)
{
	static const std::string start_body{
		"<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap."
		"org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap."
		"org/soap/encoding/\"><s:Body>\n"
	};
	static const std::string end_body = "</s:Body></s:Envelope>";

	std::ostringstream response;
	response << start_body;
    response << "<u:" << soap_info->action_name << "Response" <<
		" xmlns:u=\"" << soap_info->service_type << "\">\n";
	for (const auto&  arg : data) {
		response << "<" << arg.first << ">" <<
			xmlQuote(arg.second) <<
			"</" <<	arg.first << ">\n";
	}
    response << "</u:" << soap_info->action_name << "Response" << ">\n";
	response << end_body;
	const std::string& txt(response.str());
	UpnpPrintf(UPNP_INFO, SOAP, __FILE__, __LINE__,
			   "Action Response data: [%s]\n", txt.c_str());
	mhdt->response = MHD_create_response_from_buffer(
		txt.size(), const_cast<char*>(txt.c_str()),	MHD_RESPMEM_MUST_COPY);
	mhdt->httpstatus = 200;
}


/* The original code performed a few consistency checks on the action xml
   - Checked the soap namespace against
     SOAP_URN = "http:/""/schemas.xmlsoap.org/soap/envelope/";
   - Checked the "Body" elt name
   - Checked that the action node namespace uri matched the service
     type from the SOAPACTION header
   - Checked that the action node local name matched the action name
     from the SOAPACTION header.
   - Other checks for a var request. We don't support this any more.
  As we're not in the business of checking conformity, we did not reproduce
  the tests for now.
*/
class UPnPActionRequestParser : public XMLPARSERTP {
public:
	UPnPActionRequestParser(
		// XML to be parsed
		const std::string& input,
		// The action name is the XML element name for the argument
		// elements parent element.
		const std::string& actname,
		// Output: action arguments
		std::vector<std::pair<std::string, std::string>>& args,
		bool isresponse)
		: XMLPARSERTP(input), m_actname(actname), m_args(args),
		  m_isresp(isresponse) { }

	// On output, and only if we are parsing the action (not a
	// response): XML sub-document, stripping the top <Envelope> and
	// <Body> tags, because this is what upnp used to send in the ixml
	// tree. This is just to ease the transition.
	std::string outxml;

protected:
	void StartElement(const XML_Char *name, const XML_Char**) override {
		if (!m_isresp && m_path.size() >= 3) {
			outxml += std::string("<") + name + ">";
		}
	}
    void EndElement(const XML_Char *name) override {
		const std::string& parentname = (m_path.size() == 1) ?
            "root" : m_path[m_path.size()-2].name;
        trimstring(m_chardata, " \t\n\r");
		if (!dom_cmp_name(parentname, m_actname)) {
			m_args.emplace_back(name, m_chardata);
		}
		if (!m_isresp && m_path.size() >= 3) {
			outxml += xmlQuote(m_chardata);
			outxml += std::string("</") + name + ">";
		}
        m_chardata.clear();
    }

    void CharacterData(const XML_Char *s, int len) override {
        if (s == nullptr || *s == 0)
            return;
        m_chardata.append(s, len);
    }

private:
	const std::string& m_actname;
	std::string m_chardata;
	std::vector<std::pair<std::string, std::string>>& m_args;
	bool m_isresp;
};

/*!
 * \brief Handles the SOAP action request.
 */
static void handle_invoke_action(
	MHDTransaction *mhdt, soap_devserv_t *soap_info, const std::string& xml,
	const std::vector<std::pair<std::string, std::string> >& actargs)
{
	struct Upnp_Action_Request action;
	int err_code;
	const char *err_str;

	action.ErrCode = UPNP_E_SUCCESS;
	action.ErrStr[0] = 0;
	upnp_strlcpy(action.ActionName, soap_info->action_name, NAME_SIZE);
	upnp_strlcpy(action.DevUDN, soap_info->dev_udn, NAME_SIZE);
	upnp_strlcpy(action.ServiceID, soap_info->service_id, NAME_SIZE);
	action.xmlAction = xml;
	action.args = actargs;
	mhdt->copyClientAddress(&action.CtrlPtIPAddr);
	mhdt->copyHeader("user-agent", action.Os);
	soap_info->callback(UPNP_CONTROL_ACTION_REQUEST,&action,soap_info->cookie);
	if (action.ErrCode != UPNP_E_SUCCESS) {
		if (strlen(action.ErrStr) == 0) {
			err_code = SOAP_ACTION_FAILED;
			err_str = Soap_Action_Failed;
		} else {
			err_code = action.ErrCode;
			err_str = action.ErrStr;
		}
		goto error_handler;
	}
	if (!action.xmlResponse.empty()) {
		// The client prefers to talk xml than use the args. We need to
		// parse the data.
		std::vector<std::pair<std::string, std::string>> args;
		std::string rspnm = soap_info->action_name + "Response";
		UPnPActionRequestParser parser(action.xmlResponse, rspnm, args, true);
		if (!parser.Parse()) {
			UpnpPrintf(UPNP_INFO, SOAP, __FILE__, __LINE__,
					   "XML response parse failed for [%s]\n",
					   action.xmlResponse.c_str());
			err_code = SOAP_ACTION_FAILED;
			err_str = Soap_Action_Failed;
			goto error_handler;
		}
		send_action_response(mhdt, soap_info, args);
	} else {
		// Got argument vector from client.
		send_action_response(mhdt, soap_info, action.resdata);
	}
	err_code = 0;

error_handler:
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
static int get_dev_service(
	MHDTransaction *mhdt, soap_devserv_t *soap_info)
{
	struct Handle_Info *hdlinfo;
	int device_hnd;
	service_info *serv_info;

	HandleReadLock();

	auto hdltp = GetDeviceHandleInfoForPath(
		mhdt->url, &device_hnd, &hdlinfo, &serv_info);

	if (hdltp != HND_DEVICE || nullptr == serv_info) {
		HandleUnlock();
		UpnpPrintf(UPNP_ERROR, SOAP, __FILE__, __LINE__,
				   "get_dev_service: client not found.\n");
		return -1;
	}

	upnp_strlcpy(soap_info->dev_udn, serv_info->UDN, NAME_SIZE);
	upnp_strlcpy(soap_info->service_type, serv_info->serviceType, NAME_SIZE);
	upnp_strlcpy(soap_info->service_id, serv_info->serviceId, NAME_SIZE);
	soap_info->callback = hdlinfo->Callback;
	soap_info->cookie = hdlinfo->Cookie;
	HandleUnlock();
	return 0;
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
	int ret = sscanf(it->second.c_str(), R"( "%*[^"]" ; ns = %200s)", aname);
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
static int check_soapaction_hdr(
	MHDTransaction *mhdt, soap_devserv_t *soap_info)
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
	const char *col_pos2 = std::strrchr(soap_info->service_type, ':');
	/* XXX: this should be checked when service list is generated */
	assert(col_pos2 != nullptr);
	size_t cp2_diff = col_pos2 - soap_info->service_type;
	if (cp2_diff == cp1_diff &&
		strncmp(soap_info->service_type, serv_type.c_str(), cp1_diff) == 0) {
		/* for action invocation, update the version information */
		upnp_strlcpy(soap_info->service_type, serv_type, NAME_SIZE);
	} else if (strcmp(serv_type.c_str(), QUERY_STATE_VAR_URN) == 0 &&
			   soap_info->action_name == "QueryStateVariable") {
		/* query variable */
		soap_info->action_name.clear();
	} else {
		return ret_code;
	}

	return UPNP_E_SUCCESS;
}




/*!
 * \brief This is a callback called by miniserver after receiving the request
 * from the control point. After HTTP processing, it calls handle_soap_request
 * to start SOAP processing.
 */
void soap_device_callback(MHDTransaction *mhdt)
{
	int err_code;
	const char *err_str = "";
	soap_devserv_t soap_info;
	std::vector<std::pair<std::string, std::string>> args;
	std::string strippedxml;
	
	/* The device/service identified by the request URI */
	if (get_dev_service(mhdt, &soap_info) < 0) {
		err_code = HTTP_NOT_FOUND;
		goto error_handler;
	}

	/* validate: content-type == text/xml */
	if (!has_xml_content_type(mhdt)) {
		err_code = HTTP_UNSUPPORTED_MEDIA_TYPE;
		goto error_handler;
	}
	
	/* check SOAPACTION HTTP header */
	err_code = check_soapaction_hdr(mhdt, &soap_info);
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

	if (soap_info.action_name.empty()) {
		// This is a var state request. We don't support these any more.
		UpnpPrintf(UPNP_ERROR, SOAP, __FILE__, __LINE__,
				   "Got query variable request: not supported\n");
		err_code = HTTP_BAD_REQUEST;
		goto error_handler;
	}

	{
		// soap_info.action_name was computed from the SOAPACTION
		// header The parser will produce both argument vectors and an
		// XML subdocument matching the subtree which libupnp would
		// have sent (transition help).
		UPnPActionRequestParser parser(
			mhdt->postdata, soap_info.action_name, args, false);
		if (!parser.Parse()) {
			UpnpPrintf(UPNP_INFO, SOAP, __FILE__, __LINE__,
					   "XML parse failed for [%s]\n", mhdt->postdata.c_str());
			err_code = SOAP_INVALID_ACTION;
			err_str = Soap_Invalid_Action;
			goto error_handler;
		}
		strippedxml = parser.outxml;
	}

	/* invoke action */
	handle_invoke_action(mhdt, &soap_info, strippedxml, args);

	static const char *ContentTypeXML = "text/xml; charset=\"utf-8\"";
	if (mhdt->response)
		MHD_add_response_header(mhdt->response, "Content-Type", ContentTypeXML);

error_handler:
	if (err_code != 0)
		send_error_response(mhdt, err_code, err_str);
}

#endif /* EXCLUDE_SOAP */

#endif /* INCLUDE_DEVICE_APIS */
