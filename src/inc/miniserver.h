#ifndef MINISERVER_H
#define MINISERVER_H

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

#include "config.h"

#include <cstdint>

#include "httputils.h"
#include "upnpinet.h"
#include <vector>

struct MiniServerSockArray {
    /*! Socket for stopping miniserver */
    SOCKET miniServerStopSock{INVALID_SOCKET};
    /*! IPv4 SSDP Socket for incoming advertisments and search requests. */
    SOCKET ssdpSock4{INVALID_SOCKET};
    /*! IPv6 SSDP Socket for incoming advertisments and search requests. */
    SOCKET ssdpSock6{INVALID_SOCKET};
    /*! IPv6 SSDP Socket for incoming advertisments and search requests. */
    SOCKET ssdpSock6UlaGua{INVALID_SOCKET};

    uint16_t stopPort{0};
    uint16_t miniServerPort4{0};
    uint16_t miniServerPort6{0};
#ifdef INCLUDE_CLIENT_APIS
    /*! SSDP sockets for sending search requests and receiving search replies */
    std::vector<SOCKET> ssdpReqSock4List {};

#ifdef UPNP_ENABLE_IPV6
    std::vector<SOCKET> ssdpReqSock6List {};
#endif /* UPNP_ENABLE_IPV6 */

#endif /* INCLUDE_CLIENT_APIS */

    MiniServerSockArray() = default;
    ~MiniServerSockArray() {
        maybeClose(miniServerStopSock);
        maybeClose(ssdpSock4);
        maybeClose(ssdpSock6);
        maybeClose(ssdpSock6UlaGua);
#ifdef INCLUDE_CLIENT_APIS
        for (SOCKET socket:ssdpReqSock4List) { maybeClose(socket); }

#ifdef UPNP_ENABLE_IPV6
        for (SOCKET socket:ssdpReqSock6List) { maybeClose(socket); }
#endif /* UPNP_ENABLE_IPV6 */

#endif /* INCLUDE_CLIENT_APIS */
    }

    MiniServerSockArray(const MiniServerSockArray&) = delete;
    MiniServerSockArray& operator=(const MiniServerSockArray&) = delete;

private:
    static void maybeClose(SOCKET s) {
        if (s != INVALID_SOCKET) {
            UpnpCloseSocket(s);
        }
    }
};

struct MHDTransaction;
typedef void (*MiniServerCallback) (MHDTransaction*);

/*!
 * \brief Set HTTP Get Callback.
 */
void SetHTTPGetCallback(
    /*! [in] HTTP Callback to be invoked . */
    MiniServerCallback callback);

/*!
 * \brief Set SOAP Callback.
 */
#ifdef INCLUDE_DEVICE_APIS
void SetSoapCallback(
    /*! [in] SOAP Callback to be invoked . */
    MiniServerCallback callback);
#else /* INCLUDE_DEVICE_APIS */
    static UPNP_INLINE void SetSoapCallback(MiniServerCallback callback) {}
#endif /* INCLUDE_DEVICE_APIS */

/*!
 * \brief Set GENA Callback.
 */
void SetGenaCallback(
    /*! [in] GENA Callback to be invoked. */
    MiniServerCallback callback);

/*!
 * \brief Initialize the sockets functionality for the Miniserver.
 *
 * Initialize a thread pool job to run the MiniServer and the job to the
 * thread pool.
 *
 * If listen port is 0, port is dynamically picked.
 *
 * Use timer mechanism to start the MiniServer, failure to meet the
 * allowed delay aborts the attempt to launch the MiniServer.
 *
 * \return
 *    \li On success: UPNP_E_SUCCESS.
 *    \li On error: UPNP_E_XXX.
 */
int StartMiniServer(
    /*! [in,out] Port on which the server listens for incoming IPv4
     * connections. */
    uint16_t *listen_port4,
    /*! [in,out] Port on which the server listens for incoming IPv6
     * connections. */
    uint16_t *listen_port6);

/*!
 * \brief Stop and Shutdown the MiniServer and free socket resources.
 *
 * \return Always returns 0.
 */
int StopMiniServer();

#endif /* MINISERVER_H */
