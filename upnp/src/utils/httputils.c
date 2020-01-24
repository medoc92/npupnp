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

/*
 * \brief Contains functions for scanner and parser for http messages.
 */

#include "config.h"

#include "httputils.h"


#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <string>
#include <sstream>
#include <iostream>
#include <regex>
#include <map>

#include <curl/curl.h>

#ifdef WIN32
#include <malloc.h>
#define fseeko fseek
#define snprintf _snprintf
#else
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#endif

#include "UpnpInet.h"
#include "UpnpStdInt.h"
#include "httputils.h"
#include "smallut.h"
#include "sock.h"
#include "statcodes.h"
#include "upnp.h"
#include "upnpapi.h"
#include "upnpdebug.h"
#include "uri.h"
#include "webserver.h"

static const std::string bogus_soap_post{"SMPOST"};
static const std::map<std::string, int> Http_Method_Table {
	{"GET", HTTPMETHOD_GET},
	{"HEAD", HTTPMETHOD_HEAD},
	{"M-POST", HTTPMETHOD_MPOST},
	{"M-SEARCH", HTTPMETHOD_MSEARCH},
	{"NOTIFY", HTTPMETHOD_NOTIFY},
	{"POST", HTTPMETHOD_POST},
	{"SUBSCRIBE", HTTPMETHOD_SUBSCRIBE},
	{"UNSUBSCRIBE", HTTPMETHOD_UNSUBSCRIBE},
	{bogus_soap_post, SOAPMETHOD_POST},
};

static const std::map<std::string, int> Http_Header_Names {
	{"accept", HDR_ACCEPT},
	{"accept-charset", HDR_ACCEPT_CHARSET},
	{"accept-encoding", HDR_ACCEPT_ENCODING},
	{"accept-language", HDR_ACCEPT_LANGUAGE},
	{"accept-ranges", HDR_ACCEPT_RANGE},
	{"cache-control", HDR_CACHE_CONTROL},
	{"callback", HDR_CALLBACK},
	{"content-encoding", HDR_CONTENT_ENCODING},
	{"content-language", HDR_CONTENT_LANGUAGE},
	{"content-length", HDR_CONTENT_LENGTH},
	{"content-location", HDR_CONTENT_LOCATION},
	{"content-range", HDR_CONTENT_RANGE},
	{"content-type", HDR_CONTENT_TYPE},
	{"date", HDR_DATE},
	{"ext", HDR_EXT},
	{"host", HDR_HOST},
	{"if-range", HDR_IF_RANGE},
	{"location", HDR_LOCATION},
	{"man", HDR_MAN},
	{"mx", HDR_MX},
	{"nt", HDR_NT},
	{"nts", HDR_NTS},
	{"range", HDR_RANGE},
	{"seq", HDR_SEQ},
	{"server", HDR_SERVER},
	{"sid", HDR_SID},
	{"soapaction", HDR_SOAPACTION},
	{"st", HDR_ST},
	{"te", HDR_TE},
	{"timeout", HDR_TIMEOUT},
	{"transfer-encoding", HDR_TRANSFER_ENCODING},
	{"user-agent", HDR_USER_AGENT},
	{"usn", HDR_USN},
};

http_method_t httpmethod_str2enum(const char *methname)
{
	const auto it = Http_Method_Table.find(methname);
	if (it == Http_Method_Table.end()) {
		return HTTPMETHOD_UNKNOWN;
	} else {
		return (http_method_t)it->second;
	}
}

int httpheader_str2int(const std::string& headername)
{
	auto it = Http_Header_Names.find(headername);
	if (it == Http_Header_Names.end())
		return -1;
	return it->second;
}

/* 
 * Please, do not change these to const int while MSVC cannot understand
 * const int in array dimensions.
 */
#define CHUNK_HEADER_SIZE (size_t)10
#define CHUNK_TAIL_SIZE (size_t)10

#ifdef WIN32
struct tm *http_gmtime_r(const time_t *clock, struct tm *result)
{
	if (clock == NULL || *clock < 0 || result == NULL)
		return NULL;

	/* gmtime in VC runtime is thread safe. */
	*result = *gmtime(clock);
	return result;
}
#endif

int http_FixUrl(uri_type *url, uri_type *fixed_url)
{
	*fixed_url = *url;
	if (stringlowercmp("http", fixed_url->scheme) != 0) {
		return UPNP_E_INVALID_URL;
	}
	if (fixed_url->hostport.text.empty()) {
		return UPNP_E_INVALID_URL;
	}
	/* set pathquery to "/" if it is empty */
	if (fixed_url->pathquery.empty()) {
		fixed_url->pathquery = "/";
	}

	return UPNP_E_SUCCESS;
}

int http_FixStrUrl(
	const char *urlstr,
	size_t urlstrlen,
	uri_type *fixed_url)
{
	uri_type url;

	if (parse_uri(urlstr, urlstrlen, &url) != UPNP_E_SUCCESS) {
		return UPNP_E_INVALID_URL;
	}

	return http_FixUrl(&url, fixed_url);
}

/************************************************************************
 * Function: http_Download
 *
 * Parameters:
 *	IN const char* url_str;	String as a URL
 *	IN int timeout_secs;	time out value
 *	OUT char** document;	buffer to store the document extracted
 *				from the donloaded message.
 *	OUT int* doc_length;	length of the extracted document
 *	OUT char* content_type;	Type of content
 *
 * Description:
 *	Download the document message and extract the document 
 *	from the message.
 *
 * Return: int
 *	UPNP_E_SUCCESS
 *	UPNP_E_INVALID_URL
 ************************************************************************/
int http_Download(const char *url_str,
               int timeout_secs,
               char **document,
               size_t *doc_length,
               char *content_type )
{
	uri_type url;
	UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
		   "DOWNLOAD URL : %s\n", url_str);
	int ret_code = http_FixStrUrl((char *)url_str, strlen(url_str), &url);
	if (ret_code != UPNP_E_SUCCESS)
		return ret_code;

	std::map<std::string, std::string> http_headers;
	std::string data;

	CURL *easy = curl_easy_init();
	char curlerrormessage[CURL_ERROR_SIZE];
	curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curlerrormessage);
	curl_easy_setopt(easy, CURLOPT_URL, uri_asurlstr(url).c_str());
	curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeout_secs);
	curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback_curl);
	curl_easy_setopt(easy, CURLOPT_HEADERDATA, &http_headers);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback_str_curl);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &data);

	struct curl_slist *list = NULL;
	list = curl_slist_append(
		list, (std::string("USER-AGENT: ") + get_sdk_info()).c_str());
	list = curl_slist_append(list, "Connection: close");
	curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);

	CURLcode code = curl_easy_perform(easy);

	if (code != CURLE_OK) {
		curl_easy_cleanup(easy);
		curl_slist_free_all(list);
		/* We may want to detail things here, depending on the curl error */
		std::cerr << "http_Download: curl failed with: " << curlerrormessage <<
			std::endl;
		UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
				   "http_Download: curl failed with: %s\n", curlerrormessage);
		return UPNP_E_SOCKET_CONNECT;
	}
	long http_status;
	curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_status);
	UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__, "Response. Status %ld\n",
			   http_status);

	curl_easy_cleanup(easy);
	curl_slist_free_all(list);
	
	/* optional content-type */
	if (content_type) {
		auto it = http_headers.find("content-type");
		if (it == http_headers.end()) {
			*content_type = '\0';	/* no content-type */
		} else {
			linecopylen(content_type, it->second.c_str(), it->second.size());
		}
	}

	auto it = http_headers.find("content-length");
	if (it != http_headers.end()) {
		uint64_t sizefromheaders = atoll(it->second.c_str());
		if (sizefromheaders != data.size()) {
			UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
					   "Response content-length %" PRIu64
					   " differs from data size %"
					   PRIu64 "\n", sizefromheaders, (uint64_t)data.size());
		}
	}
	
	/* extract doc from msg */
	if (data.empty()) {
		*document = NULL;
	} else {
		*document = (char *)malloc(data.size() + 1);
		if (*document == NULL) {
			return UPNP_E_OUTOF_MEMORY;
		}
		memcpy(*document, data.c_str(), data.size());
		(*document)[data.size()] = 0;
	}
	if (http_status == HTTP_OK) {
		return 0;
	} else {
		return http_status;
	}
}

/************************************************************************
 * Function: http_SendStatusResponse
 *
 * Parameters:
 *	IN SOCKINFO *info;		Socket information object
 *	IN int http_status_code;	error code returned while making 
 *					or sending the response message
 *	IN int request_major_version;	request major version
 *	IN int request_minor_version;	request minor version
 *
 * Description:
 *	Generate a response message for the status query and send the
 *	status response.
 *
 * Return: int
 *	0 -- success
 *	UPNP_E_OUTOF_MEMORY
 *	UPNP_E_SOCKET_WRITE
 *	UPNP_E_TIMEDOUT
 ************************************************************************/
int http_SendStatusResponse(MHDTransaction *mhdt, int status_code)
{
	std::ostringstream body;
	body <<	"<html><body><h1>" << status_code << " " << 
		http_get_code_text(status_code) << "</h1></body></html>";
	mhdt->response = MHD_create_response_from_buffer(
		body.str().size(), (char*)body.str().c_str(), MHD_RESPMEM_MUST_COPY);
	mhdt->httpstatus = status_code;
	return UPNP_E_SUCCESS;
}


static std::regex textxml_re(
	"text[ \t]*/[ \t]*xml([ \t]*;.*)?",
	std::regex_constants::extended | std::regex_constants::icase);

bool has_xml_content_type(MHDTransaction *mhdt)
{
	auto it = mhdt->headers.find("content-type");
	if (it == mhdt->headers.end()) {
		//std::cerr << "has_xml_content: no content type header\n";
		return false;
	}
	bool ret = regex_match(it->second, textxml_re);
	if (!ret) {
		//std::cerr << "has_xml_content: no match for ["<<it->second << "]\n";
	}
	return ret;
}

bool timeout_header_value(std::map<std::string, std::string>& headers,
						  int *time_out)
{
	auto ittimo = headers.find("timeout");
	if (ittimo == headers.end()) {
		std::cerr << "timeout_header_value: no timeout header\n";
		return false;
	}
	stringtolower(ittimo->second);
	if (ittimo->second == "second-infinite") {
		*time_out = -1;
		return true;
	}
	char cbuf[2];
	if (sscanf(ittimo->second.c_str(),"second-%d%1c",time_out,cbuf) != 1) {
		std::cerr << "timeout_header_value: bad header value [" <<
			ittimo->second << "]\n";
		return false;
	}
	return true;
}

std::string make_date_string(time_t thetime)
{
	const char *weekday_str = "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat";
	const char *month_str = "Jan\0Feb\0Mar\0Apr\0May\0Jun\0"
	    "Jul\0Aug\0Sep\0Oct\0Nov\0Dec";

	time_t curr_time = thetime ? thetime : time(NULL);
	struct tm date_storage;
	struct tm *date = http_gmtime_r(&curr_time, &date_storage);
	if (date == NULL)
		return std::string();
	char tempbuf[200];
	snprintf(tempbuf, sizeof(tempbuf),
			 "%s, %02d %s %d %02d:%02d:%02d GMT",
			 &weekday_str[date->tm_wday * 4],
			 date->tm_mday, &month_str[date->tm_mon * 4],
			 date->tm_year + 1900, date->tm_hour,
			 date->tm_min, date->tm_sec);
	return tempbuf;
}

/************************************************************************
 * Function: get_sdk_info
 *
 * Parameters:
 *	OUT char *info;	buffer to store the operating system information
 *	IN size_t infoSize; size of buffer
 *
 * Description:
 *	Returns the server information for the operating system
 *
 * Return:
 *	UPNP_INLINE void
 ************************************************************************/
#include <sstream>
std::string get_sdk_info()
{
	std::ostringstream ostr;
#ifdef UPNP_ENABLE_UNSPECIFIED_SERVER
	ostr << "Unspecified, UPnP/1.0, Unspecified"
#else /* UPNP_ENABLE_UNSPECIFIED_SERVER */
#ifdef WIN32
	OSVERSIONINFO versioninfo;
	versioninfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	if (GetVersionEx(&versioninfo) != 0)
		ostr << versioninfo.dwMajorVersion << "." << versioninfo.dwMinorVersion<<
			"." << versioninfo.dwBuildNumber << " " << versioninfo.dwPlatformId
			<< "/" << versioninfo.szCSDVersion <<
			", UPnP/1.0, Portable SDK for UPnP devices/" PACKAGE_VERSION;
#else
	struct utsname sys_info;

	if (uname(&sys_info) != -1)
		ostr << sys_info.sysname << "/" << sys_info.release << 
			", UPnP/1.0, Portable SDK for UPnP devices/" PACKAGE_VERSION;
#endif
#endif /* UPNP_ENABLE_UNSPECIFIED_SERVER */
	return ostr.str();
}


size_t header_callback_curl(char *buffer, size_t size, size_t nitems, void *s)
{
	size_t bufsize = size * nitems;
	auto headers = (std::map<std::string, std::string>*)s;
	const char *colon = strchr(buffer, ':');
	if (nullptr != colon) {
		size_t colpos = colon - buffer;
		std::string nm = std::string(buffer, colpos);
		std::string value = std::string(colon + 1, bufsize - colpos -1);
		if (!nm.empty()) {
			trimstring(nm, " \t");
			stringtolower(nm);
			trimstring(value, " \t\r\n");
			UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
					   "CURL header: [%s] -> [%s]\n", nm.c_str(), value.c_str());
			(*headers)[nm] = value;
		}
	}
	return bufsize;
}

size_t write_callback_null_curl(char *buffer, size_t size, size_t nitems, void *)
{
#if 0
	fprintf(stderr, "DATA: [");
	fwrite(buffer, size, nitems, stderr);
	fprintf(stderr, "]\n");
	fflush(stderr);
#endif
	return size*nitems;
}

size_t write_callback_str_curl(char *buf, size_t sz, size_t nits, void *s)
{
	((std::string*)s)->append(buf, sz * nits);
	return sz * nits;
}
