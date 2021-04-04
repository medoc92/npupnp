/**************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (C) 2012 France Telecom All rights reserved.
 *
nnn * Redistribution and use in source and binary forms, with or without 
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


#ifdef INCLUDE_CLIENT_APIS
#if EXCLUDE_SSDP == 0

#include <string>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>

#include "upnp.h"
#include "ssdpparser.h"
#include "httputils.h"
#include "ssdp_ResultData.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "UpnpInet.h"
#include "ThreadPool.h"
#include "genut.h"
#include "inet_pton.h"

/*!
 * \brief Sends a callback to the control point application with a SEARCH
 * result.
 */
static void* thread_cb_search_result(void *data)
{
    auto temp = static_cast<ResultData *>(data);
    temp->ctrlpt_callback(UPNP_DISCOVERY_SEARCH_RESULT, &temp->param,
                          temp->cookie);
    return nullptr;
}

void ssdp_handle_ctrlpt_msg(SSDPPacketParser& parser,
                            struct sockaddr_storage *dest_addr,
                            void *)
{
    int handle;
    struct Handle_Info *ctrlpt_info = nullptr;
    int is_byebye;
    struct Upnp_Discovery param;
    SsdpEntity event;
    int nt_found;
    int usn_found;
    int st_found;
    Upnp_EventType event_type;
    Upnp_FunPtr ctrlpt_callback;
    void *ctrlpt_cookie;
    int matched = 0;
    ResultData *threadData = nullptr;

    /* Get client info. We are assuming that there can be only one
       client supported at a time */
    HandleReadLock();
    if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
        HandleUnlock();
        return;
    }
    ctrlpt_callback = ctrlpt_info->Callback;
    ctrlpt_cookie = ctrlpt_info->Cookie;
    HandleUnlock();

    param.ErrCode = UPNP_E_SUCCESS;
    /* MAX-AGE, assume error */
    param.Expires = -1;
    if (parser.cache_control) {
        std::string s{parser.cache_control};
        stringtolower(s);
        char cb[2];
        if (sscanf(s.c_str(), "max-age = %d%1c", &param.Expires, cb) != 1) {
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                       "BAD CACHE-CONTROL value: [%s]\n", parser.cache_control);
            return;
        }
    }
    /* DATE */
    param.Date[0] = '\0';
    if (parser.date) {
        upnp_strlcpy(param.Date, parser.date, LINE_SIZE);
    }
    /* remote addr */
    memcpy(&param.DestAddr, dest_addr, sizeof(struct sockaddr_storage));
    /* EXT is upnp 1.0 compat. It has no value in 1.1 */
    param.Ext[0] = '\0';
    /* LOCATION. If the URL contains an IPV6 link-local address, we
       need to qualify it with the scope id, else, later on, when the
       client code calls, e.g. UpnpSendAction, we won't know what
       interface this is for (the socket addr is not part of the
       call). */
    param.Location[0] = '\0';
    if (parser.location) {
        std::string scopedloc = maybeScopeUrlAddr(parser.location, dest_addr);
        if (scopedloc.empty()) {
            return;
        }
        upnp_strlcpy(param.Location, scopedloc.c_str(), LINE_SIZE);
    }
    /* SERVER / USER-AGENT */
    param.Os[0] = '\0';
    if (parser.server) {
        upnp_strlcpy(param.Os, parser.server, LINE_SIZE);
    } else if (parser.user_agent) {
        upnp_strlcpy(param.Os, parser.user_agent, LINE_SIZE);
    }
    /* clear everything */
    memset(param.DeviceId, 0, sizeof(param.DeviceId));
    memset(param.DeviceType, 0, sizeof(param.DeviceType));
    memset(param.ServiceType, 0, sizeof(param.ServiceType));
    /* not used; version is in ServiceType */
    param.ServiceVer[0] = '\0';
    nt_found = 0;
    if (parser.nt) {
        nt_found = (ssdp_request_type(parser.nt, &event) == 0);
    }
    usn_found = 0;
    if (parser.usn) {
        usn_found = (unique_service_name(parser.usn, &event) == 0);
    }
    if (nt_found || usn_found) {
        upnp_strlcpy(param.DeviceId, event.UDN, sizeof(param.DeviceId));
        upnp_strlcpy(param.DeviceType, event.DeviceType, sizeof(param.DeviceType));
        upnp_strlcpy(param.ServiceType, event.ServiceType,sizeof(param.ServiceType));
    }
    /* ADVERT. OR BYEBYE */
    if (!parser.isresponse) {
        /* use NTS hdr to determine advert., or byebye */
        if (!parser.nts) {
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                       "NO NTS header in advert/byebye message\n");
            return;    /* error; NTS header not found */
        }
        if (!strcmp(parser.nts, "ssdp:alive")) {
            is_byebye = 0;
        } else if (!strcmp(parser.nts, "ssdp:byebye")) {
            is_byebye = 1;
        } else {
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                       "BAD NTS header [%s] in advert/byebye message\n",
                       parser.nts);
            return;    /* bad value */
        }
        if (is_byebye) {
            /* check device byebye */
            if (!nt_found || !usn_found) {
                UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                           "SSDP BYE BYE no NT or USN !\n");
                return;    /* bad byebye */
            }
            event_type = UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE;
        } else {
            /* check advertisement.
             * Expires is valid if positive. This is for testing
             * only. Expires should be greater than 1800 (30 mins) */
            if (!nt_found || !usn_found ||
                strlen(param.Location) == 0 || param.Expires <= 0) {
                return;    /* bad advertisement */
            }
            event_type = UPNP_DISCOVERY_ADVERTISEMENT_ALIVE;
        }
        /* call callback */
        ctrlpt_callback(event_type, &param, ctrlpt_cookie);
    } else {
        /* reply (to a SEARCH) */
        /* only checking to see if there is a valid ST header */
        st_found = 0;
        if (parser.st) {
            st_found = ssdp_request_type(parser.st, &event) == 0;
        }
        if (!parser.status || strcmp(parser.status, "200") != 0 ||
            param.Expires <= 0 ||
            strlen(param.Location) == 0 || !usn_found || !st_found) {
            return;    /* bad reply */
        }
        /* check each current search */
        HandleLock();
        if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
            HandleUnlock();
            return;
        }
        size_t stlen = strlen(parser.st);
        for (auto searchArg : ctrlpt_info->SsdpSearchList) {
            /* check for match of ST header and search target */
            switch (searchArg->requestType) {
            case SSDP_ALL:
                matched = 1;
                break;
            case SSDP_ROOTDEVICE:
                matched = (event.RequestType == SSDP_ROOTDEVICE);
                break;
            case SSDP_DEVICEUDN:
                matched = !strncmp(searchArg->searchTarget.c_str(), parser.st, stlen);
                break;
            case SSDP_DEVICETYPE:
            {
                size_t m = std::min(stlen, searchArg->searchTarget.size());
                matched =!strncmp(searchArg->searchTarget.c_str(), parser.st, m);
                break;
            }
            case SSDP_SERVICE:
            {
                size_t m = std::min(stlen, searchArg->searchTarget.size());
                matched = !strncmp(searchArg->searchTarget.c_str(),parser.st, m);
                break;
            }
            default:
                matched = 0;
                break;
            }
            if (matched) {
                /* schedule call back */
                threadData = static_cast<ResultData *>(malloc(sizeof(ResultData)));
                if (threadData != nullptr) {
                    threadData->param = param;
                    threadData->cookie = searchArg->cookie;
                    threadData->ctrlpt_callback = ctrlpt_callback;
                    if (gRecvThreadPool.addJob(
                            thread_cb_search_result, threadData,
                            static_cast<ThreadPool::free_routine>(free)) != 0) {
                        free(threadData);
                    }
                }
            }
        }

        HandleUnlock();
    }
}

/*!
 * \brief Creates a HTTP search request packet depending on the input
 * parameter.
 */
static int CreateClientRequestPacket(
    std::string& RqstBuf,
    int Mx,
    char *SearchTarget,
    int AddressFamily, bool sitelocal = false)
{
    static const char *command = "M-SEARCH * HTTP/1.1\r\n";
    static const char *man = R"(MAN: "ssdp:discover")" "\r\n";

    std::ostringstream str;

    str << command;

    switch (AddressFamily) {
    case AF_INET:
        str << "HOST: " << SSDP_IP << ":" << SSDP_PORT << "\r\n";
        break;
    case AF_INET6:
        str << "HOST: [" <<
            (sitelocal?SSDP_IPV6_SITELOCAL:SSDP_IPV6_LINKLOCAL) << 
            "]:" << SSDP_PORT << "\r\n";
        break;
    default:
        return UPNP_E_INVALID_ARGUMENT;
    }

    str << man;

    if (Mx > 0) {
        str << "MX: " << Mx << "\r\n";
    }

    if (SearchTarget != nullptr) {
        str << "ST: " << SearchTarget << "\r\n";
    }
    str << "\r\n";
    RqstBuf = str.str();
    return UPNP_E_SUCCESS;
}

#ifdef UPNP_ENABLE_IPV6_AND_ULAGUA
static int CreateClientRequestPacketUlaGua(
    std::string& RqstBuf,
    int Mx,
    char *SearchTarget,
    int addrfam)
{
    return CreateClientRequestPacket(RqstBuf, Mx, SearchTarget, addrfam, true);
}
#endif /* UPNP_ENABLE_IPV6 */

static void* thread_searchexpired(void *arg)
{
    int *id = static_cast<int *>(arg);
    int handle = -1;
    struct Handle_Info *ctrlpt_info = nullptr;
    Upnp_FunPtr ctrlpt_callback;
    void *cookie = nullptr;
    int found = 0;

    HandleLock();

    if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
        free(id);
        HandleUnlock();
        return nullptr;
    }
    ctrlpt_callback = ctrlpt_info->Callback;
    for (auto it = ctrlpt_info->SsdpSearchList.begin();
         it != ctrlpt_info->SsdpSearchList.end(); it++) {
        SsdpSearchArg *item = *it;
        if (item->timeoutEventId == *id) {
            cookie = item->cookie;
            found = 1;
            delete item;
            ctrlpt_info->SsdpSearchList.erase(it);
            break;
        }
    }
    HandleUnlock();

    if (found)
        ctrlpt_callback(UPNP_DISCOVERY_SEARCH_TIMEOUT, nullptr, cookie);
    return nullptr;
}

int SearchByTarget(int Mx, char *St, void *Cookie)
{
    enum SsdpSearchType requestType = ssdp_request_type1(St);
    if (requestType == SSDP_SERROR)
        return UPNP_E_INVALID_PARAM;

    int timeTillRead = Mx;
    if (timeTillRead < MIN_SEARCH_TIME)
        timeTillRead = MIN_SEARCH_TIME;
    else if (timeTillRead > MAX_SEARCH_TIME)
        timeTillRead = MAX_SEARCH_TIME;

    std::string ReqBufv4;
    int retVal = CreateClientRequestPacket(ReqBufv4, timeTillRead, St, AF_INET);
    if (retVal != UPNP_E_SUCCESS)
        return retVal;

#ifdef UPNP_ENABLE_IPV6
    std::string ReqBufv6;
    retVal = CreateClientRequestPacket(ReqBufv6, timeTillRead, St, AF_INET6);
    if (retVal != UPNP_E_SUCCESS)
        return retVal;
#endif

    /* add search criteria to list */
    HandleLock();
    int handle;
    struct Handle_Info *ctrlpt_info;
    if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
        HandleUnlock();
        return UPNP_E_INTERNAL_ERROR;
    }
    auto newArg = new SsdpSearchArg(St, Cookie, requestType);
    auto id = static_cast<int *>(malloc(sizeof(int)));

    /* Schedule a timeout event to remove search Arg */
    gTimerThread->schedule(
        TimerThread::SHORT_TERM, TimerThread::REL_SEC, timeTillRead,  id,
        thread_searchexpired, id, static_cast<ThreadPool::free_routine>(free));

    newArg->timeoutEventId = *id;
    ctrlpt_info->SsdpSearchList.push_back(newArg);

    HandleUnlock();

    fd_set wrSet;
    FD_ZERO(&wrSet);
    SOCKET max_fd = INVALID_SOCKET;
    if (gSsdpReqSocket4 != INVALID_SOCKET) {
        FD_SET(gSsdpReqSocket4, &wrSet);
#ifdef _WIN32
        // max_fd is ignored under windows. invalid_socket is not 0
        max_fd = 0;
#else
        max_fd = std::max(max_fd, gSsdpReqSocket4);
#endif
    }
#ifdef UPNP_ENABLE_IPV6
    if (gSsdpReqSocket6 != INVALID_SOCKET) {
        FD_SET(gSsdpReqSocket6, &wrSet);
#ifdef _WIN32
        // max_fd is ignored under windows. invalid_socket is not 0
        max_fd = 0;
#else
        max_fd = std::max(max_fd, gSsdpReqSocket6);
#endif
    }
#endif

    if (max_fd == INVALID_SOCKET) {
        UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__,
                   "SSDP_LIB: neither ipv4 nor ipv6 are active !\n");
        return UPNP_E_INTERNAL_ERROR;
    }
    int ret = select(max_fd + 1, nullptr, &wrSet, nullptr, nullptr);
    if (ret == -1) {
        char errorBuffer[ERROR_BUFFER_LEN];
        posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
        UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__,
                   "SSDP_LIB: Error in select(): %s\n", errorBuffer);
        UpnpCloseSocket(gSsdpReqSocket4);
#ifdef UPNP_ENABLE_IPV6
        UpnpCloseSocket(gSsdpReqSocket6);
#endif
        return UPNP_E_INTERNAL_ERROR;
    }

#ifdef UPNP_ENABLE_IPV6
    if (gSsdpReqSocket6 != INVALID_SOCKET && FD_ISSET(gSsdpReqSocket6, &wrSet)) {
        struct sockaddr_storage ssv6 = {};

        auto destAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&ssv6);
        destAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
        inet_pton(AF_INET6, SSDP_IPV6_LINKLOCAL, &destAddr6->sin6_addr);
        destAddr6->sin6_port = htons(SSDP_PORT);
        destAddr6->sin6_scope_id = apiFirstIPV6Index();

        for (int cnt = 0; cnt < NUM_SSDP_COPY; cnt++) {
            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                       ">>> SSDP SEND M-SEARCH >>>\n%s\n", ReqBufv6.c_str());
            sendto(gSsdpReqSocket6, ReqBufv6.c_str(), ReqBufv6.size(), 0,
                   reinterpret_cast<struct sockaddr *>(destAddr6),
                   sizeof(struct sockaddr_in6));
            std::this_thread::sleep_for(std::chrono::milliseconds(SSDP_PAUSE));
        }
    }
#endif /* IPv6 */

    if (gSsdpReqSocket4 != INVALID_SOCKET && FD_ISSET(gSsdpReqSocket4, &wrSet)) {
        struct sockaddr_storage ssv4 = {};

        auto destAddr4 = reinterpret_cast<struct sockaddr_in *>(&ssv4);
        destAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
        inet_pton(AF_INET, SSDP_IP, &destAddr4->sin_addr);
        destAddr4->sin_port = htons(SSDP_PORT);

        int NumCopy = 0;
        while (NumCopy < NUM_SSDP_COPY) {
            UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                       ">>> SSDP SEND M-SEARCH >>>\n%s\n", ReqBufv4.c_str());
            sendto(gSsdpReqSocket4, ReqBufv4.c_str(), ReqBufv4.size(), 0,
                   reinterpret_cast<struct sockaddr *>(destAddr4),
                   sizeof(struct sockaddr_in));
            NumCopy++;
            std::this_thread::sleep_for(std::chrono::milliseconds(SSDP_PAUSE));
        }
    }

    return 1;
}
#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_CLIENT_APIS */
