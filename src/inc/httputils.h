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

#ifndef _HTTPUTILS_H_
#define _HTTPUTILS_H_

#include <map>
#include <string>

#include <microhttpd.h>

struct uri_type;

/*! timeout in secs. */
#define HTTP_DEFAULT_TIMEOUT    30

/* method in a HTTP request. */
typedef enum {
    HTTPMETHOD_POST, 
    HTTPMETHOD_MPOST, 
    HTTPMETHOD_SUBSCRIBE, 
    HTTPMETHOD_UNSUBSCRIBE, 
    HTTPMETHOD_NOTIFY, 
    HTTPMETHOD_GET,
    HTTPMETHOD_HEAD, 
    HTTPMETHOD_MSEARCH, 
    HTTPMETHOD_UNKNOWN,
    SOAPMETHOD_POST,
    HTTPMETHOD_SIMPLEGET
} http_method_t;

/* Translate method name to numeric. Methname must be uppercase */
http_method_t httpmethod_str2enum(const char *methname);

/* Translate header name to numeric. The webserver received header names are
 * always lower-cased, and lower case is expected by the function */
int httpheader_str2int(const std::string& headername);

std::string query_encode(const std::string& qs);

/* Context for a microhttpd request/response */
struct MHDTransaction {
public:
    struct MHD_Connection *conn{nullptr};
    struct sockaddr_storage *client_address;
    std::string url;
    http_method_t method;
    std::string version;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> queryvalues;
    std::string postdata;
    /* Set by callback */
    struct MHD_Response *response{nullptr};
    int httpstatus;

    ~MHDTransaction() {
    }
    void copyClientAddress(struct sockaddr_storage *dest) const;
    // Returns false if header not found, else copies it
    bool copyHeader(const std::string& name, std::string& value);
};

/* different types of HTTP headers */
#define HDR_UNKNOWN            -1
#define HDR_CACHE_CONTROL        1
#define HDR_CALLBACK            2
#define HDR_CONTENT_LENGTH        3
#define HDR_CONTENT_TYPE        4
#define HDR_DATE            5
#define HDR_EXT                6
#define HDR_HOST            7
/*define HDR_IF_MODIFIED_SINCE        8 */
/*define HDR_IF_UNMODIFIED_SINCE    9 */
/*define HDR_LAST_MODIFIED        10 */
#define HDR_LOCATION            11
#define HDR_MAN                12
#define HDR_MX                13
#define HDR_NT                14
#define HDR_NTS                15
#define HDR_SERVER            16
#define HDR_SEQ                17
#define HDR_SID                18
#define HDR_SOAPACTION            19
#define HDR_ST                20
#define HDR_TIMEOUT            21
#define HDR_TRANSFER_ENCODING        22
#define HDR_USN                23
#define HDR_USER_AGENT            24

/* Adding new header difinition */
#define HDR_ACCEPT            25
#define HDR_ACCEPT_ENCODING        26
#define HDR_ACCEPT_CHARSET        27
#define HDR_ACCEPT_LANGUAGE        28
#define HDR_ACCEPT_RANGE        29
#define HDR_CONTENT_ENCODING        30
#define HDR_CONTENT_LANGUAGE        31
#define HDR_CONTENT_LOCATION        32
#define HDR_CONTENT_RANGE        33
#define HDR_IF_RANGE            34
#define HDR_RANGE            35
#define HDR_TE                36


/*!
 * \brief Parse and validate URL.
 *
 * \return
 *     \li \c UPNP_E_INVALID_URL
 *     \li \c UPNP_E_SUCCESS
 */
int http_FixStrUrl(const std::string& url, uri_type *fixed_url);

/* Download and store in memory the document designated by the input url */
int http_Download(const char *url, int timeout_secs, char** document,
                  size_t *doc_length, char* content_type);

/** CURL: callback function to accumulate curl response headers in a map<s:s> */
extern size_t header_callback_curl(char *buf, size_t size, size_t nitems, void *s);

/** CURL: null write callback to avoid curl sending stuff to stdout */
size_t write_callback_null_curl(char *buf, size_t size, size_t nitems, void *);

/* CURL: callback to accumulate data in an std::string */
size_t write_callback_str_curl(char *buf, size_t sz, size_t nits, void *s);

/* Generate and send a status response message (response with status +
   bit of explanatory HTML) */
int http_SendStatusResponse(MHDTransaction *mhdt, int http_status_code);

/* Check presence and text/xml value of content-type header */
bool has_xml_content_type(MHDTransaction *);

/* Check for TIMEOUT header and return value (used both with curl and mhd) */
bool timeout_header_value(std::map<std::string,std::string>& headers,
                          int *time_out);

/* Produce HTTP date string */
extern std::string make_date_string(time_t thetime);

/* Return the server information for the operating system */
std::string get_sdk_info();

#endif /* _HTTPUTILS_H_ */
