/**************************************************************************
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
 **************************************************************************/


#include "config.h"

#if UPNP_HAVE_TOOLS
#include "upnp.h"
#include "upnptools.h"
#include "uri.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>


/*! Maximum action header buffer length. */
#define HEADER_LENGTH 2000

/*!
 * \brief Structure to maintain a error code and string associated with the
 * error code.
 */
struct ErrorString {
    /*! Error code. */
    int rc;
    /*! Error description. */
    const char *rcError;
};


/*!
 * \brief Array of error structures.
 */
static constexpr struct ErrorString ErrorMessages[] = {
    {UPNP_E_SUCCESS, "UPNP_E_SUCCESS"},
    {UPNP_E_INVALID_HANDLE, "UPNP_E_INVALID_HANDLE"},
    {UPNP_E_INVALID_PARAM, "UPNP_E_INVALID_PARAM"},
    {UPNP_E_OUTOF_HANDLE, "UPNP_E_OUTOF_HANDLE"},
    {UPNP_E_OUTOF_CONTEXT, "UPNP_E_OUTOF_CONTEXT"},
    {UPNP_E_OUTOF_MEMORY, "UPNP_E_OUTOF_MEMORY"},
    {UPNP_E_INIT, "UPNP_E_INIT"},
    {UPNP_E_BUFFER_TOO_SMALL, "UPNP_E_BUFFER_TOO_SMALL"},
    {UPNP_E_INVALID_DESC, "UPNP_E_INVALID_DESC"},
    {UPNP_E_INVALID_URL, "UPNP_E_INVALID_URL"},
    {UPNP_E_INVALID_SID, "UPNP_E_INVALID_SID"},
    {UPNP_E_INVALID_DEVICE, "UPNP_E_INVALID_DEVICE"},
    {UPNP_E_INVALID_SERVICE, "UPNP_E_INVALID_SERVICE"},
    {UPNP_E_BAD_RESPONSE, "UPNP_E_BAD_RESPONSE"},
    {UPNP_E_BAD_REQUEST, "UPNP_E_BAD_REQUEST"},
    {UPNP_E_INVALID_ACTION, "UPNP_E_INVALID_ACTION"},
    {UPNP_E_FINISH, "UPNP_E_FINISH"},
    {UPNP_E_INIT_FAILED, "UPNP_E_INIT_FAILED"},
    {UPNP_E_URL_TOO_BIG, "UPNP_E_URL_TOO_BIG"},
    {UPNP_E_BAD_HTTPMSG, "UPNP_E_BAD_HTTPMSG"},
    {UPNP_E_ALREADY_REGISTERED, "UPNP_E_ALREADY_REGISTERED"},
    {UPNP_E_INVALID_INTERFACE, "UPNP_E_INVALID_INTERFACE"},
    {UPNP_E_NETWORK_ERROR, "UPNP_E_NETWORK_ERROR"},
    {UPNP_E_SOCKET_WRITE, "UPNP_E_SOCKET_WRITE"},
    {UPNP_E_SOCKET_READ, "UPNP_E_SOCKET_READ"},
    {UPNP_E_SOCKET_BIND, "UPNP_E_SOCKET_BIND"},
    {UPNP_E_SOCKET_CONNECT, "UPNP_E_SOCKET_CONNECT"},
    {UPNP_E_OUTOF_SOCKET, "UPNP_E_OUTOF_SOCKET"},
    {UPNP_E_LISTEN, "UPNP_E_LISTEN"},
    {UPNP_E_TIMEDOUT, "UPNP_E_TIMEDOUT"},
    {UPNP_E_SOCKET_ERROR, "UPNP_E_SOCKET_ERROR"},
    {UPNP_E_FILE_WRITE_ERROR, "UPNP_E_FILE_WRITE_ERROR"},
    {UPNP_E_CANCELED, "UPNP_E_CANCELED"},
    {UPNP_E_EVENT_PROTOCOL, "UPNP_E_EVENT_PROTOCOL"},
    {UPNP_E_SUBSCRIBE_UNACCEPTED, "UPNP_E_SUBSCRIBE_UNACCEPTED"},
    {UPNP_E_UNSUBSCRIBE_UNACCEPTED, "UPNP_E_UNSUBSCRIBE_UNACCEPTED"},
    {UPNP_E_NOTIFY_UNACCEPTED, "UPNP_E_NOTIFY_UNACCEPTED"},
    {UPNP_E_INVALID_ARGUMENT, "UPNP_E_INVALID_ARGUMENT"},
    {UPNP_E_FILE_NOT_FOUND, "UPNP_E_FILE_NOT_FOUND"},
    {UPNP_E_FILE_READ_ERROR, "UPNP_E_FILE_READ_ERROR"},
    {UPNP_E_EXT_NOT_XML, "UPNP_E_EXT_NOT_XML"},
    {UPNP_E_NO_WEB_SERVER, "UPNP_E_NO_WEB_SERVER"},
    {UPNP_E_OUTOF_BOUNDS, "UPNP_E_OUTOF_BOUNDS"},
    {UPNP_E_NOT_FOUND, "UPNP_E_NOT_FOUND"},
    {UPNP_E_INTERNAL_ERROR, "UPNP_E_INTERNAL_ERROR"},
};

const char *UpnpGetErrorMessage(int rc)
{
    for (const auto& i : ErrorMessages) {
        if (rc == i.rc) {
            return i.rcError;
        }
    }

    return "Unknown error code";
}

int UpnpResolveURL(const char *BaseURL, const char *RelURL, char *AbsURL)
{
    if (!RelURL) {
        return UPNP_E_INVALID_PARAM;
    }
    std::string tempRel = resolve_rel_url(BaseURL, RelURL);
    if (tempRel.empty()) {
        return UPNP_E_INVALID_URL;
    }
    strcpy(AbsURL, tempRel.c_str());
    return UPNP_E_SUCCESS;
}


int UpnpResolveURL2(const char *BaseURL, const char *RelURL, char **AbsURL)
{
    int ret = UPNP_E_SUCCESS;

    if (!RelURL) {
        return UPNP_E_INVALID_PARAM;
    }
    std::string temp = resolve_rel_url(const_cast<char *>(BaseURL), const_cast<char *>(RelURL));
    if (!temp.empty()) {
        *AbsURL = strdup(temp.c_str());
    } else {
        ret = UPNP_E_INVALID_URL;
    }
    return ret;
}

#endif // UPNP_HAVE_TOOLS
