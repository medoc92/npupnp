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

#include <stdlib.h>
#include <sstream>
#include <iostream>
#include <curl/curl.h>

#include "miniserver.h"
#include "httputils.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "soaplib.h"
#include "uri.h"
#include "upnp.h"
#include "smallut.h"
#include "expatmm.hxx"

static int dom_cmp_name(const std::string& domname, const std::string& ref)
{
	std::string::size_type colon = domname.find(':');
	return colon == std::string::npos ?
		domname.compare(ref) : domname.compare(colon+1, std::string::npos, ref);
}

class UPnPResponseParser : public inputRefXMLParser {
public:
    UPnPResponseParser(
		// XML to be parsed
		const std::string& input,
		// If action is "Play" XML response node name is PlayResponse
		const std::string& rspname,
		// Output data from response
		std::vector<std::pair<std::string, std::string>>& respdata,
		int *errp, std::string& errd)
        : inputRefXMLParser(input), responseName(rspname), data(respdata),
		  errcodep(errp), errdesc(errd)	{
	}

protected:
    virtual void EndElement(const XML_Char *name) {
		const std::string& parentname = (m_path.size() == 1) ?
            "root" : m_path[m_path.size()-2].name;
        trimstring(m_chardata, " \t\n\r");

		if (parentname == "UpNPError") {
			if (!strcmp(name, "errorCode")) {
				if (*errcodep) {
					*errcodep = atoi(m_chardata.c_str());
				}
			} else if (!strcmp(name, "errorDescription")) {
				errdesc = m_chardata;
			}
		} else if (!dom_cmp_name(parentname, responseName)) {
			data.push_back({name, m_chardata});
		}
        m_chardata.clear();
    }

    virtual void CharacterData(const XML_Char *s, int len) {
        if (s == 0 || *s == 0)
            return;
        m_chardata.append(s, len);
    }

private:
	const std::string& responseName;
	std::string m_chardata;
	std::vector<std::pair<std::string, std::string>>& data;
	int *errcodep;
	std::string& errdesc;
};


#define SOAP_ACTION_RESP	1
#define SOAP_ACTION_RESP_ERROR  2

// Returns: < 0: local or network error
// SOAP_ACTION_RESP : got normal soap response
// SOAP_ACTION_RESP_ERROR: action failed, got explanation in error response
static int
get_response_value(
	const std::string& payload, long http_status,  const std::string& cttype,
	const std::string& rspname, int *upnp_error_code,
	std::vector<std::pair<std::string, std::string>>& rspdata,
	int *errcodep, std::string& errdesc)
{
	/* only 200 and 500 status codes are relevant */
	if ((http_status != HTTP_OK &&
	     http_status != HTTP_INTERNAL_SERVER_ERROR) ||
	    cttype.find("text/xml") != 0) {
		return UPNP_E_BAD_RESPONSE;
	}

    UPnPResponseParser mparser(payload, rspname, rspdata, errcodep, errdesc);
    if (!mparser.Parse()) {
        return UPNP_E_BAD_RESPONSE;;
	}
	return UPNP_E_SUCCESS;
}

int SoapSendAction(
	const std::string& xml_header_str, const std::string& actionURL,
	const std::string& serviceType,	const std::string& actionName,
	const std::vector<std::pair<std::string, std::string>> actionArgs,
	std::vector<std::pair<std::string, std::string>>& respdata,
	int *errcodep, std::string& errdesc)
{
    const static std::string xml_start{
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"};
    const static std::string xml_header_start{"<s:Header>\r\n"};
    const static std::string xml_header_end{"</s:Header>\r\n"};
    const static std::string xml_body_start{"<s:Body>"};
    const static std::string xml_end{"</s:Body>\r\n</s:Envelope>\r\n"};

    int err_code = UPNP_E_OUTOF_MEMORY;

    UpnpPrintf( UPNP_INFO, SOAP, __FILE__, __LINE__,
        "Inside SoapSendActionEx():\n" );

    /* Action: name and namespace (servicetype) */
	std::ostringstream act;
	act << "<u:" << actionName << " xmlns:u=\"" << serviceType << "\">\n";
	/* Action arguments */
	for (const auto& arg : actionArgs) {
		act << "<" << arg.first << ">" << xmlQuote(arg.second) << "</" <<
			arg.first << ">\n";
	}
	act << "</u:" << actionName << ">\n";
	
    /* parse url */
    uri_type url;
    if (http_FixStrUrl(actionURL.c_str(), actionURL.size(), &url) != 0) {
        return UPNP_E_INVALID_URL;
    }

    UpnpPrintf( UPNP_INFO, SOAP, __FILE__, __LINE__,
        "path=%.*s, hostport=%.*s\n",
				(int)url.pathquery.size(),
				url.pathquery.c_str(),
				(int)url.hostport.text.size(),
				url.hostport.text.c_str());

	std::string payload{xml_start};
	if (!xml_header_str.empty()) {
		payload += xml_header_start + xml_header_str + xml_header_end;
	}
	payload += xml_body_start + act.str() + xml_end;
	
	std::string soapaction = std::string("SOAPACTION: \"") + serviceType + "#" +
		actionName + "\"";

	//std::cerr << "SoapSendAction: SOAPACTION [" << soapaction << "]\n";
	//std::cerr << "SoapSendAction: PAYLOAD [" << payload << "]\n";

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

	std::string responsename(actionName);
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
		responsename, &upnp_error_code, respdata, errcodep, errdesc);

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

