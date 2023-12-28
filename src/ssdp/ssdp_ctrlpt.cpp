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
#include <string.h>

#include <string>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>

#include "upnp.h"
#include "ssdpparser.h"
#include "httputils.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "upnpinet.h"
#include "ThreadPool.h"
#include "genut.h"
#include "inet_pton.h"

/*! Structure to contain Discovery response. */
struct ResultData {
    struct Upnp_Discovery param;
    void *cookie;
    Upnp_FunPtr ctrlpt_callback;
};

/** Calls back the control point application with a search result. */
class SearchResultJobWorker : public JobWorker {
public:
    explicit SearchResultJobWorker(ResultData *res)
        : m_resultdata(res) {}
    ~SearchResultJobWorker() override {
        delete m_resultdata;
    }
    SearchResultJobWorker(const SearchResultJobWorker&) = delete;
    SearchResultJobWorker& operator=(const SearchResultJobWorker&) = delete;
    void work() override {
        m_resultdata->ctrlpt_callback(UPNP_DISCOVERY_SEARCH_RESULT, &m_resultdata->param,
                                      m_resultdata->cookie);
    }
    ResultData *m_resultdata;
};

/* Worker to send the search messages. */
class SearchSendJobWorker : public JobWorker {
public:
    explicit SearchSendJobWorker(const std::string& v4, const std::string& v6)
        : ReqBufv4(v4), ReqBufv6(v6) {}
    std::string ReqBufv4;
    std::string ReqBufv6;
    ~SearchSendJobWorker() override {}
    SearchSendJobWorker(const SearchSendJobWorker&) = delete;
    SearchSendJobWorker& operator=(const SearchSendJobWorker&) = delete;
    void work() override;

};

void SearchSendJobWorker::work()
{
    bool needv4 = !ReqBufv4.empty();
    bool needv6 = !ReqBufv6.empty();
    fd_set wrSet;
    FD_ZERO(&wrSet);
    int max_fd = -1;
    if (needv4) {
        if (gSsdpReqSocket4 != INVALID_SOCKET) {
            FD_SET(gSsdpReqSocket4, &wrSet);
#ifdef _WIN32
            // max_fd is ignored under windows. Just set it to not -1.
            max_fd = 0;
#else
            max_fd = std::max(max_fd, gSsdpReqSocket4);
#endif
        }
    }
#ifdef UPNP_ENABLE_IPV6
    if (needv6) {
        if (gSsdpReqSocket6 != INVALID_SOCKET) {
            FD_SET(gSsdpReqSocket6, &wrSet);
#ifdef _WIN32
            // max_fd is ignored under windows. Just set it to not -1.
            max_fd = 0;
#else
            max_fd = std::max(max_fd, gSsdpReqSocket6);
#endif
        }
    }
#endif

    if (max_fd == -1) {
        UpnpPrintf(UPNP_ERROR, SSDP, __FILE__, __LINE__,
                   "SSDP_LIB: neither ipv4 nor ipv6 are active !\n");
        return;
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
        return;
    }

#ifdef UPNP_ENABLE_IPV6
    if (needv6 && gSsdpReqSocket6 != INVALID_SOCKET && FD_ISSET(gSsdpReqSocket6, &wrSet)) {
        struct sockaddr_storage ssv6 = {};
        auto destAddr6 = reinterpret_cast<struct sockaddr_in6 *>(&ssv6);
        destAddr6->sin6_family = static_cast<sa_family_t>(AF_INET6);
        inet_pton(AF_INET6, SSDP_IPV6_LINKLOCAL, &destAddr6->sin6_addr);
        destAddr6->sin6_port = htons(SSDP_PORT);
        destAddr6->sin6_scope_id = apiFirstIPV6Index();
        UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                   ">>> SSDP SEND M-SEARCH >>>\n%s\n", ReqBufv6.c_str());
        sendto(gSsdpReqSocket6, ReqBufv6.c_str(), ReqBufv6.size(), 0,
               reinterpret_cast<struct sockaddr *>(destAddr6), sizeof(struct sockaddr_in6));
    }
#endif /* UPNP_ENABLE_IPV6 */

    if (needv4 && gSsdpReqSocket4 != INVALID_SOCKET && FD_ISSET(gSsdpReqSocket4, &wrSet)) {
        struct sockaddr_storage ssv4 = {};
        auto destAddr4 = reinterpret_cast<struct sockaddr_in *>(&ssv4);
        destAddr4->sin_family = static_cast<sa_family_t>(AF_INET);
        inet_pton(AF_INET, SSDP_IP, &destAddr4->sin_addr);
        destAddr4->sin_port = htons(SSDP_PORT);
        UpnpPrintf(UPNP_DEBUG, SSDP, __FILE__, __LINE__,
                   ">>> SSDP SEND M-SEARCH >>>\n%s\n", ReqBufv4.c_str());
        sendto(gSsdpReqSocket4, ReqBufv4.c_str(), ReqBufv4.size(), 0,
               reinterpret_cast<struct sockaddr *>(destAddr4), sizeof(struct sockaddr_in));
    }
}

void ssdp_handle_ctrlpt_msg(SSDPPacketParser& parser, struct sockaddr_storage *dest_addr, void *)
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
        for (auto&& searchArg : ctrlpt_info->SsdpSearchList) {
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
                auto threadData = new ResultData;
                threadData->param = param;
                threadData->cookie = searchArg->cookie;
                threadData->ctrlpt_callback = ctrlpt_callback;
                auto worker = std::make_unique<SearchResultJobWorker>(threadData);
                gRecvThreadPool.addJob(std::move(worker));
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
    std::string& RqstBuf, int Mx, const char *SearchTarget,
    int AddressFamily, const char *saddress, int port )
{
    static const char *command = "M-SEARCH * HTTP/1.1\r\n";
    static const char *man = R"(MAN: "ssdp:discover")" "\r\n";

    std::ostringstream str;

    str << command;

    switch (AddressFamily) {
    case AF_INET:
        str << "HOST: " << saddress << ":" << port << "\r\n";
        break;
    case AF_INET6:
        str << "HOST: [" << saddress << "]:" << port << "\r\n";
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

    str << "USER-AGENT: " << get_sdk_client_info() << "\r\n";

    str << "\r\n";

    RqstBuf = str.str();
    return UPNP_E_SUCCESS;
}

class SearchExpiredJobWorker : public JobWorker {
public:
    SearchExpiredJobWorker() = default;
    ~SearchExpiredJobWorker() override = default;
    void work() override;
    int m_id;
};

void SearchExpiredJobWorker::work()
{
    HandleLock();

    int handle;
    struct Handle_Info *ctrlpt_info;
    if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
        HandleUnlock();
        return;
    }

    Upnp_FunPtr ctrlpt_callback = ctrlpt_info->Callback;
    void *cookie;
    int found = 0;
    int id = m_id;
    auto it = std::find_if(
        ctrlpt_info->SsdpSearchList.begin(),
        ctrlpt_info->SsdpSearchList.end(),
        [id](const std::unique_ptr<SsdpSearchArg>& item) {return item->timeoutEventId == id;});

    if (it != ctrlpt_info->SsdpSearchList.end()) {
        cookie = (*it)->cookie;
        found = 1;
        ctrlpt_info->SsdpSearchList.erase(it);
    }
    HandleUnlock();

    if (found)
        ctrlpt_callback(UPNP_DISCOVERY_SEARCH_TIMEOUT, nullptr, cookie);
}

int SearchByTarget(int Mx, const char *St, const char *saddress, int port, void *Cookie)
{
    enum SsdpSearchType requestType = ssdp_request_type1(St);
    if (requestType == SSDP_SERROR)
        return UPNP_E_INVALID_PARAM;

    bool needv4{true}, needv6{true};
    const char *saddress4, *saddress6;
    if (Mx == 0) {
        // Unicast request
        needv4 = nullptr != strchr(saddress, '.');
        needv6 = !needv4;
        // Only one will be used, init both, simpler
        saddress4 = saddress;
        saddress6 = saddress;
    } else {
        if (Mx < UPNP_MIN_SEARCH_TIME)
            Mx = UPNP_MIN_SEARCH_TIME;
        else if (Mx > UPNP_MAX_SEARCH_TIME)
            Mx = UPNP_MAX_SEARCH_TIME;
        port = SSDP_PORT;
        saddress4 = SSDP_IP;
        saddress6 = SSDP_IPV6_LINKLOCAL;
    }

    std::string ReqBufv4;
    if (needv4) {
        int retVal = CreateClientRequestPacket(ReqBufv4, Mx, St, AF_INET, saddress4, port);
        if (retVal != UPNP_E_SUCCESS)
            return retVal;
    }
    std::string ReqBufv6;
#ifdef UPNP_ENABLE_IPV6
    if (needv6) {
        auto retVal = CreateClientRequestPacket(ReqBufv6, Mx, St, AF_INET6, saddress6, port);
        if (retVal != UPNP_E_SUCCESS)
            return retVal;
    }
#else
    if (needv6) {
        return UPNP_E_INVALID_PARAM;
    }
#endif

    /* Add the search criteria and callback to the list and schedule a timeout event to remove
       it when the search window closes */
    HandleLock();
    int handle;
    struct Handle_Info *ctrlpt_info;
    if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
        HandleUnlock();
        return UPNP_E_INTERNAL_ERROR;
    }
    auto newArg = std::make_unique<SsdpSearchArg>(St, Cookie, requestType);
    auto worker = std::make_unique<SearchExpiredJobWorker>();
    int *idp = &(worker->m_id);
    gTimerThread->schedule(TimerThread::SHORT_TERM, TimerThread::REL_SEC, Mx ? Mx + 1 : 2,
                           idp, std::move(worker));
    newArg->timeoutEventId = *idp;
    ctrlpt_info->SsdpSearchList.push_back(std::move(newArg));

    HandleUnlock();

    // Schedule sending the packets.
    std::unique_ptr<SearchSendJobWorker> sworker;
    int delay = 0;
    for (int i = 0; i < NUM_SSDP_COPY; i++) {
        sworker = std::make_unique<SearchSendJobWorker>(ReqBufv4, ReqBufv6);
        gTimerThread->schedule(TimerThread::SHORT_TERM, std::chrono::milliseconds(delay),
                               nullptr, std::move(sworker));
        delay += SSDP_PAUSE;
    }
    return UPNP_E_SUCCESS;
}

#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_CLIENT_APIS */
