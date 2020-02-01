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

/************************************************************************
* Purpose: This file defines status codes, buffers to store the status	*
* messages and functions to manipulate those buffers					*
************************************************************************/

#include "config.h"
#include <map>
#include <string>

#include "statcodes.h"

static const std::map<int, std::string> httpcodes {
	{HTTP_CONTINUE, "Continue"},
	{HTTP_SWITCHING_PROCOTOLS , "Switching Protocols"},
	{HTTP_OK, "OK"},
	{HTTP_CREATED , "Created"},
	{HTTP_ACCEPTED , "Accepted"},
	{HTTP_NON_AUTHORATATIVE , "Non-Authoritative Information"},
	{HTTP_NO_CONTENT, "No Content"},
	{HTTP_RESET_CONTENT , "Reset Content"},
	{HTTP_PARTIAL_CONTENT , "Partial Content"},
	{HTTP_MULTIPLE_CHOICES , "Multiple Choices"},
	{HTTP_MOVED_PERMANENTLY , "Moved Permanently"},
	{HTTP_FOUND , "Found"},
	{HTTP_SEE_OTHER , "See Other"},
	{HTTP_NOT_MODIFIED , "Not Modified"},
	{HTTP_USE_PROXY , "Use Proxy"},
	{HTTP_TEMPORARY_REDIRECT, "Temporary Redirect"},
	{HTTP_BAD_REQUEST , "Bad Request"},
	{HTTP_UNAUTHORIZED , "Unauthorized"},
	{HTTP_PAYMENT_REQD , "Payment Required"},
	{HTTP_FORBIDDEN , "Forbidden"},
	{HTTP_NOT_FOUND , "Not Found"},
	{HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed"},
	{HTTP_NOT_ACCEPTABLE , "Not Acceptable"},
	{HTTP_PROXY_AUTH_REQD , "Proxy Authentication Required"},
	{HTTP_REQUEST_TIMEOUT , "Request Timeout"},
	{HTTP_CONFLICT , "Conflict"},
	{HTTP_GONE , "Gone"},
	{HTTP_LENGTH_REQUIRED , "Length Required"},
	{HTTP_PRECONDITION_FAILED , "Precondition Failed"},
	{HTTP_REQ_ENTITY_TOO_LARGE , "Request Entity Too Large"},
	{HTTP_REQ_URI_TOO_LONG , "Request-URI Too Long"},
	{HTTP_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type"},
	{HTTP_REQUEST_RANGE_NOT_SATISFIABLE, "Requested Range Not Satisfiable"},
	{HTTP_EXPECTATION_FAILED, "Expectation Failed"},
	{HTTP_INTERNAL_SERVER_ERROR , "Internal Server Error"},
	{HTTP_NOT_IMPLEMENTED , "Not Implemented"},
	{HTTP_BAD_GATEWAY , "Bad Gateway"},
	{HTTP_SERVICE_UNAVAILABLE , "Service Unavailable"},
	{HTTP_GATEWAY_TIMEOUT , "Gateway Timeout"},
	{HTTP_HTTP_VERSION_NOT_SUPPORTED , "HTTP Version Not Supported"},
	{HTTP_VARIANT_ALSO_NEGOTIATES , "Variant Also Negotiates"},
	{HTTP_INSUFFICIENT_STORAGE , "Insufficient Storage"},
	{HTTP_LOOP_DETECTED , "Loop Detected"},
	{HTTP_NOT_EXTENDED , "Not Extended"},
	};

/************************************************************************
 * Function: http_get_code_text 
 * 
 * Parameters: 
 * int statusCode ; Status code based on which the status table and 
 * status message is returned 
 * 
 * Description: Return the right status message based on the passed in 
 * int statusCode input parameter 
 * 
 * Returns: 
 * const char* ptr - pointer to the status message string 
 ************************************************************************/
const char *http_get_code_text(int statusCode)
{
	const auto it = httpcodes.find(statusCode);
	return it == httpcodes.end() ? "" : it->second.c_str();
}
