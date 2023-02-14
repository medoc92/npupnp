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

/* Misc HTTP-related utilities */

#include "config.h"

#include "httputils.h"

#include <cctype>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include <string>
#include <sstream>
#include <iostream>
#include <map>

#include <curl/curl.h>

#include "UpnpInet.h"
#include "genut.h"
#include "statcodes.h"
#include "upnp.h"
#include "upnpdebug.h"
#include "uri.h"
#include "upnpapi.h"

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

void MHDTransaction::copyClientAddress(struct sockaddr_storage *dest) const
{
    if (nullptr == dest)
        return;
    if (nullptr == client_address) {
        *dest = {};
        return;
    }
    if (client_address->ss_family == AF_INET) {
        memcpy(dest, client_address, sizeof(struct sockaddr_in));
    } else {
        memcpy(dest, client_address, sizeof(struct sockaddr_in6));
    }        
}

bool MHDTransaction::copyHeader(const std::string& name,
                                std::string& value)
{
    auto it = headers.find(stringtolower(name));
    if (it == headers.end()) {
        return false;
    }
    value = it->second;
    return true;
}

http_method_t httpmethod_str2enum(const char *methname)
{
    const auto it = Http_Method_Table.find(methname);
    if (it == Http_Method_Table.end()) {
        return HTTPMETHOD_UNKNOWN;
    }

    return static_cast<http_method_t>(it->second);
}

int httpheader_str2int(const std::string& headername)
{
    auto it = Http_Header_Names.find(headername);
    if (it == Http_Header_Names.end())
        return -1;
    return it->second;
}

int http_FixStrUrl(const std::string& surl, uri_type *fixed_url)
{
    uri_type url;

    if (parse_uri(surl, &url) != UPNP_E_SUCCESS) {
        return UPNP_E_INVALID_URL;
    }

    *fixed_url = url;
    if (stringlowercmp("http", fixed_url->scheme) ||
        fixed_url->hostport.text.empty()) {
        return UPNP_E_INVALID_URL;
    }

    /* set pathquery to "/" if it is empty */
    if (fixed_url->path.empty()) {
        fixed_url->path = "/";
    }

    return UPNP_E_SUCCESS;
}

/************************************************************************
 * Function: http_Download
 *
 * Parameters:
 *    IN const char* url_str;    String as a URL
 *    IN int timeout_secs;    time out value
 *    OUT char** document;    buffer to store the document extracted
 *                from the donloaded message.
 *    OUT int* doc_length;    length of the extracted document
 *    OUT char* content_type;    Type of content
 *
 * Description:
 *    Download the document message and extract the document 
 *    from the message.
 *
 * Return: int
 *    UPNP_E_SUCCESS
 *    UPNP_E_INVALID_URL
 ************************************************************************/
int http_Download(const char *_surl, int timeout_secs,
                  char **document, size_t *, char *content_type)
{
    uri_type url;
    UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__, "http_Download: %s\n",_surl);
    int ret_code = http_FixStrUrl(_surl, &url);
    if (ret_code != UPNP_E_SUCCESS)
        return ret_code;

    std::map<std::string, std::string> http_headers;
    std::string data;

    CURL *easy = curl_easy_init();
    char curlerrormessage[CURL_ERROR_SIZE];
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curlerrormessage);
    std::string surl = uri_asurlstr(url);
    curl_easy_setopt(easy, CURLOPT_URL, surl.c_str());
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeout_secs);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback_curl);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &http_headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback_str_curl);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &data);

    struct curl_slist *list = nullptr;
    list = curl_slist_append(list, (std::string("USER-AGENT: ") + get_sdk_client_info()).c_str());
    list = curl_slist_append(list, "Connection: close");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);

    CURLcode code = curl_easy_perform(easy);

    if (code != CURLE_OK) {
        curl_easy_cleanup(easy);
        curl_slist_free_all(list);
        /* We may want to detail things here, depending on the curl error */
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
            *content_type = '\0';    /* no content-type */
        } else {
            upnp_strlcpy(content_type, it->second, LINE_SIZE);
        }
    }

    auto it = http_headers.find("content-length");
    if (it != http_headers.end()) {
        uint64_t sizefromheaders = atoll(it->second.c_str());
        if (sizefromheaders != data.size()) {
            UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
                       "Response content-length %" PRIu64
                       " differs from data size %"
                       PRIu64 "\n", sizefromheaders, static_cast<uint64_t>(data.size()));
        }
    }

    *document = nullptr;
    if (http_status == HTTP_OK) {
        /* extract doc from msg */
        if (!data.empty()) {
            *document = strdup(data.c_str());
            if (nullptr == *document) {
                return UPNP_E_OUTOF_MEMORY;
            }
        }
        return 0;
    }

    return http_status;
}

/************************************************************************
 * Function: http_SendStatusResponse
 *
 * Parameters:
 *    IN int http_status_code;    error code returned while making 
 *                    or sending the response message
 *    IN int request_major_version;    request major version
 *    IN int request_minor_version;    request minor version
 *
 * Description:
 *    Generate a response message for the status query and send the
 *    status response.
 *
 * Return: int
 *    0 -- success
 *    UPNP_E_OUTOF_MEMORY
 *    UPNP_E_SOCKET_WRITE
 *    UPNP_E_TIMEDOUT
 ************************************************************************/
int http_SendStatusResponse(MHDTransaction *mhdt, int status_code)
{
    std::ostringstream body;
    body <<    "<html><body><h1>" << status_code << " " << 
        http_get_code_text(status_code) << "</h1></body></html>";
    mhdt->response = MHD_create_response_from_buffer(
        body.str().size(), const_cast<char*>(body.str().c_str()), MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(mhdt->response, "Content-Type", "text/html");
    mhdt->httpstatus = status_code;
    return UPNP_E_SUCCESS;
}

bool has_xml_content_type(MHDTransaction *mhdt)
{
    static const char *xmlmtype = "text/xml";
    static const size_t mtlen = strlen(xmlmtype);

    auto it = mhdt->headers.find("content-type");
    if (it == mhdt->headers.end()) {
        UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
                   "has_xml_content: no content type header\n");
        return false;
    }
    if (strncasecmp(xmlmtype, it->second.c_str(), mtlen)) {
        UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__, "has_xml_content: "
                   "text/xml not found in [%s]\n", it->second.c_str());
        return false;
    }
    return true;
}

bool timeout_header_value(std::map<std::string, std::string>& headers,
                          int *time_out)
{
    auto ittimo = headers.find("timeout");
    if (ittimo == headers.end()) {
        UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
                   "timeout_header_value: no timeout header\n");
        return false;
    }
    stringtolower(ittimo->second);
    if (ittimo->second == "second-infinite") {
        *time_out = -1;
        return true;
    }
    char cbuf[2];
    if (sscanf(ittimo->second.c_str(),"second-%d%1c",time_out,cbuf) != 1) {
        UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__, "timeout_header_value: "
                   "bad header value [%s]\n", ittimo->second.c_str());
        return false;
    }
    return true;
}

#ifdef _WIN32
struct tm *http_gmtime_r(const time_t *clock, struct tm *result)
{
    if (clock == NULL || *clock < 0 || result == NULL)
        return NULL;

    /* gmtime in VC runtime is thread safe. */
    *result = *gmtime(clock);
    return result;
}

#else /* !_WIN32 ->*/

#include <sys/utsname.h>
#define http_gmtime_r gmtime_r

#endif

static const std::string& get_sdk_common_info()
{
    static std::string sdk_common_info;
    if (sdk_common_info.empty()) {
        std::ostringstream ostr;
#ifdef UPNP_ENABLE_UNSPECIFIED_SERVER
        ostr << "Unspecified UPnP/1.0 Unspecified";
#else /* UPNP_ENABLE_UNSPECIFIED_SERVER */
#ifdef _WIN32
            OSVERSIONINFO versioninfo;
        versioninfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

        if (GetVersionEx(&versioninfo) != 0)
            ostr << versioninfo.dwMajorVersion << "." <<
                versioninfo.dwMinorVersion << "." <<
                versioninfo.dwBuildNumber << " " << versioninfo.dwPlatformId
                 << "/" << versioninfo.szCSDVersion;
#else
        struct utsname sys_info;

        if (uname(&sys_info) != -1)
            ostr << sys_info.sysname << "/" << sys_info.release;
#endif
#endif /* UPNP_ENABLE_UNSPECIFIED_SERVER */

        ostr << " UPnP/1.0 ";
        sdk_common_info = ostr.str();
    }
    return sdk_common_info;
}

std::string get_sdk_device_info(const std::string& customvalue)
{
    return get_sdk_common_info() +
        (!customvalue.empty() ? customvalue :
         std::string("Portable SDK for UPnP devices/" NPUPNP_VERSION_STRING));
}

const std::string& get_sdk_client_info(const std::string& newvalue)
{
    static std::string sdk_client_info;
    if (sdk_client_info.empty() || !newvalue.empty()) {
        // If this was never set, or the client wants to set its name, compute
        sdk_client_info = get_sdk_common_info() +
            (!newvalue.empty() ? newvalue : 
             std::string("Portable SDK for UPnP devices/" NPUPNP_VERSION_STRING));
    }
    
    return sdk_client_info;
}

std::string make_date_string(time_t thetime)
{
    const char *weekday_str = "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat";
    const char *month_str = "Jan\0Feb\0Mar\0Apr\0May\0Jun\0"
        "Jul\0Aug\0Sep\0Oct\0Nov\0Dec";

    time_t curr_time = thetime ? thetime : time(nullptr);
    struct tm date_storage;
    struct tm *date = http_gmtime_r(&curr_time, &date_storage);
    if (date == nullptr)
        return {};
    char tempbuf[200];
    snprintf(tempbuf, sizeof(tempbuf),
             "%s, %02d %s %d %02d:%02d:%02d GMT",
             &weekday_str[date->tm_wday * 4],
             date->tm_mday, &month_str[date->tm_mon * 4],
             date->tm_year + 1900, date->tm_hour,
             date->tm_min, date->tm_sec);
    return tempbuf;
}

std::string query_encode(const std::string& qs)
{
    std::string out;
    out.reserve(qs.size());
    const char *h = "0123456789ABCDEF";
    const char *cp = qs.c_str();
    while (*cp) {
        if ((*cp >= 'A' && *cp <= 'Z') ||
            (*cp >= 'a' && *cp <= 'z') || (*cp >= '0' && *cp <= '9') ||
            *cp == '*' || *cp == '-' || *cp== '.' || *cp == '_') {
            out += *cp;
        } else {
            out += '%';
            out += h[((uint32_t(*cp)) >> 4) & 0xf];
            out += h[uint32_t(*cp) & 0xf];
        }
        cp++;
    }
    return out;
}

size_t header_callback_curl(char *buffer, size_t size, size_t nitems, std::map<std::string, std::string> *headers)
{
    size_t bufsize = size * nitems;
    const char *colon = std::strchr(buffer, ':');
    if (nullptr != colon) {
        size_t colpos = colon - buffer;
        std::string nm = std::string(buffer, colpos);
        std::string value = std::string(colon + 1, bufsize - colpos -1);
        if (!nm.empty()) {
            trimstring(nm, " \t");
            stringtolower(nm);
            trimstring(value, " \t\r\n");
            UpnpPrintf(UPNP_ALL, HTTP, __FILE__, __LINE__,
                       "CURL header: [%s] -> [%s]\n", nm.c_str(), value.c_str());
            (*headers)[nm] = value;
        }
    }
    return bufsize;
}

size_t write_callback_null_curl(char *buffer, size_t size, size_t nitems, std::string *)
{
    (void)buffer;
#if 0
    fprintf(stderr, "DATA: [");
    fwrite(buffer, size, nitems, stderr);
    fprintf(stderr, "]\n");
    fflush(stderr);
#endif
    return size*nitems;
}

size_t write_callback_str_curl(char *buf, size_t sz, size_t nits, std::string *s)
{
    s->append(buf, sz * nits);
    return sz * nits;
}
