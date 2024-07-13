/**************************************************************************
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
 **************************************************************************/

#ifndef GENLIB_NET_HTTP_WEBSERVER_H
#define GENLIB_NET_HTTP_WEBSERVER_H

#include <ctime>
#include <string>


typedef enum {
    WEB_SERVER_DISABLED,
    WEB_SERVER_ENABLED
} WebServerState;

extern WebServerState bWebServerState;


/*!
 * \brief Initilialize the different documents. Initialize the memory
 * for root directory for web server. Call to initialize global XML
 * document. Sets bWebServerState to WEB_SERVER_ENABLED.
 *
 * \return
 * \li \c 0 - OK
 * \li \c UPNP_E_OUTOF_MEMORY
 */
int web_server_init(void);


/*!
 * \brief Release memory allocated for the global web server root
 * directory and the global XML document. Resets the flag bWebServerState
 * to WEB_SERVER_DISABLED.
 */
void web_server_destroy(void);


/*!
 * \brief Assign the path specfied by the input const char* root_dir parameter
 * to the global Document root directory. Also check for path names ending
 * in '/'.
 *
 * \return Integer.
 */
int web_server_set_root_dir(
    /*! [in] String having the root directory for the document. */
    const char* root_dir);

/*!
 * \brief Assign the Access-Control-Allow-Origin specfied by the input
 * const char* cors_string parameterto the global CORS string
 *
 * \return Integer.
 */
int web_server_set_cors(
    /*! [in] String having the Access-Control-Allow-Origin string. */
    const char *cors_string);

/* Add a locally served path */
int web_server_set_localdoc(
    const std::string& path, const std::string& data, time_t last_modified);
int web_server_unset_localdoc(const std::string& path);

int web_server_add_virtual_dir(
    const char *dirname, const void *cookie, const void **oldcookie);
int web_server_remove_virtual_dir(const char *dirname);
void web_server_clear_virtual_dirs();

#endif /* GENLIB_NET_HTTP_WEBSERVER_H */

