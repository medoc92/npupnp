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


#include "config.h"

#if EXCLUDE_GENA == 0
#ifdef INCLUDE_CLIENT_APIS

#include <curl/curl.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>

#include "gena.h"
#include "genut.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "uri.h"

#ifdef USE_EXPAT
#include "expatmm.h"
#define XMLPARSERTP inputRefXMLParser
#else
#include "picoxml.h"
#define XMLPARSERTP PicoXMLParser
#endif

#define UPNP_NOPE static_cast<Upnp_LogLevel>(UPNP_ALL+1)

extern TimerThread *gTimerThread;

/* Mutex to synchronize client subscription processing */
std::mutex GlobalClientSubscribeMutex;

#define SubscribeLock() do {                                            \
    UpnpPrintf(UPNP_NOPE, GENA, __FILE__, __LINE__, "Trying Subscribe Lock\n"); \
    GlobalClientSubscribeMutex.lock();                                  \
    UpnpPrintf(UPNP_NOPE, GENA, __FILE__, __LINE__, "Subscribe Lock\n"); \
    } while(0)


#define SubscribeUnlock() do {                                          \
    UpnpPrintf(UPNP_NOPE, GENA, __FILE__,__LINE__, "Trying Subscribe UnLock\n"); \
    GlobalClientSubscribeMutex.unlock();                                \
    UpnpPrintf(UPNP_NOPE, GENA, __FILE__, __LINE__, "Subscribe UnLock\n"); \
    } while(0)


static void clientCancelRenew(ClientSubscription *sub)
{
    if (nullptr == sub) {
        return;
    }
    int renewEventId = sub->renewEventId;
    sub->renewEventId = -1;
    sub->SID.clear();
    sub->eventURL.clear();
    if (renewEventId != -1) {
        gTimerThread->remove(renewEventId);
    }
}

class AutoRenewSubscriptionJobWorker : public JobWorker {
public:
    explicit AutoRenewSubscriptionJobWorker(
        int h, std::string s, int e, const std::string& url, int t) {
        handle = h;
        sub.Sid = std::move(s);
        sub.ErrCode = e;
        upnp_strlcpy(sub.PublisherUrl, url, NAME_SIZE);
        sub.TimeOut = t;
    }
    void work() override;
    int handle;
    struct Upnp_Event_Subscribe sub;
};

/*!
 * \brief This is a thread function to send the renewal just before the
 * subscription times out.
 */
void AutoRenewSubscriptionJobWorker::work()
{
    int send_callback = 0;
    Upnp_EventType eventType = UPNP_EVENT_AUTORENEWAL_FAILED;

#if AUTO_RENEW_TIME == 0
    // We are compile-time configured for no auto-renewal.
    UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, "GENA SUB EXPIRED\n");
    sub.ErrCode = UPNP_E_SUCCESS;
    send_callback = 1;
    eventType = UPNP_EVENT_SUBSCRIPTION_EXPIRED;
#else
    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__, "GENA AUTO RENEW\n");
    int timeout = sub.TimeOut;
    int errCode = genaRenewSubscription(handle, sub.Sid, &timeout);
    sub.ErrCode = errCode;
    sub.TimeOut = timeout;
    if (errCode != UPNP_E_SUCCESS &&
        errCode != UPNP_E_INVALID_SID &&
        errCode != UPNP_E_INVALID_HANDLE) {
        send_callback = 1;
    }
#endif

    if (send_callback) {
        Upnp_FunPtr callback_fun;
        struct Handle_Info *handle_info;
        void *cookie;
        {
            HANDLELOCK();
            if (GetHandleInfo(handle, &handle_info) != HND_CLIENT) {
                return;
            }
            callback_fun = handle_info->Callback;
            cookie = handle_info->Cookie;
        }
        callback_fun(eventType, &sub, cookie);
    }
}


/*!
 * \brief Schedules a job to renew the subscription just before time out.
 *
 * \return GENA_E_SUCCESS if successful, otherwise returns the appropriate
 *     error code.
 */
static int ScheduleGenaAutoRenew(
    /*! [in] Handle that also contains the subscription list. */
    int client_handle,
    /*! [in] The time out value of the subscription. */
    int TimeOut,
    /*! [in] Subscription being renewed. */
    ClientSubscription *sub)
{
    if (TimeOut == UPNP_INFINITE) {
        return UPNP_E_SUCCESS;
    }

    /* Schedule the job */
    auto worker = std::make_unique<AutoRenewSubscriptionJobWorker>(
        client_handle, sub->SID, UPNP_E_SUCCESS, sub->eventURL, TimeOut);
    int return_code = gTimerThread->schedule(
        TimerThread::SHORT_TERM, TimerThread::REL_SEC, TimeOut - AUTO_RENEW_TIME,
        &sub->renewEventId, std::move(worker));

    if (return_code != UPNP_E_SUCCESS) {
        return return_code;
    }
    return UPNP_E_SUCCESS;
}


/*!
 * \brief Sends the UNSUBCRIBE gena request
 *
 * \returns 0 if successful, otherwise returns the appropriate error code.
 */
static int gena_unsubscribe(
    /*! [in] Event URL of the service. */
    const std::string& url,
    /*! [in] The subcription ID. */
    const std::string& sid,
    int timeoutms)
{
    int return_code;
    uri_type dest_url;

    UpnpPrintf(UPNP_ALL,GENA,__FILE__,__LINE__, "gena_unsubscribe: SID [%s] url [%s]\n",
               sid.c_str(), url.c_str());

    /* parse url */
    return_code = http_FixStrUrl(url, &dest_url);
    if (return_code != 0) {
        return return_code;
    }

    CURL *easy = curl_easy_init();
    char curlerrormessage[CURL_ERROR_SIZE];
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curlerrormessage);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback_null_curl);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "UNSUBSCRIBE");
    std::string surl =  uri_asurlstr(dest_url);
    curl_easy_setopt(easy, CURLOPT_URL, surl.c_str());
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, timeoutms);

    struct curl_slist *list = nullptr;
    list = curl_slist_append(list, (std::string("SID: ") + sid).c_str());
    list = curl_slist_append(list, (std::string("USER-AGENT: ") + get_sdk_client_info()).c_str());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);

    CURLcode code = curl_easy_perform(easy);

    if (code != CURLE_OK) {
        curl_easy_cleanup(easy);
        curl_slist_free_all(list);
        /* We may want to detail things here, depending on the curl error */
        UpnpPrintf(UPNP_ERROR,GENA,__FILE__,__LINE__, "CURL ERROR MESSAGE %s\n", curlerrormessage);
        return UPNP_E_SOCKET_CONNECT;
    }

    long http_status;
    curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_status);

    curl_easy_cleanup(easy);
    curl_slist_free_all(list);

    if (http_status != HTTP_OK) {
        return_code = UPNP_E_UNSUBSCRIBE_UNACCEPTED;
    }

    return return_code;
}

// localaddr is already in inet_ntop-provided dot or ipv6 format
static std::string myCallbackUrl(const NetIF::IPAddr& netaddr)
{
    std::ostringstream oss;
    oss << "http://";
    if (netaddr.family() == NetIF::IPAddr::Family::IPV6) {
        oss << "[";
    }
    oss << netaddr.straddr();
    if (netaddr.family() == NetIF::IPAddr::Family::IPV6) {
        oss << "]";
    }
    oss << ":" << (netaddr.family() == NetIF::IPAddr::Family::IPV6 ? LOCAL_PORT_V6 : LOCAL_PORT_V4);
    return oss.str();
}

struct CurlGuard {
    CurlGuard(CURL* t, curl_slist* l) : htalk(t), hlist(l) {}
    ~CurlGuard() {
        if (htalk) {
            curl_easy_cleanup(htalk);
        }
        if (hlist) {
            curl_slist_free_all(hlist);
        }
    }
    CurlGuard(const CurlGuard&) = delete;
    CurlGuard& operator=(const CurlGuard&) = delete;
    CURL *htalk;
    struct curl_slist *hlist;
};

/*!
 * \brief Subscribes or renew subscription.
 *
 * \return 0 if successful, otherwise returns the appropriate error code.
 */
static int gena_subscribe(
    /*! [in] URL of service to subscribe. */
    const std::string& url,
    /*! [in,out] Subscription time desired (in secs). */
    int *timeout,
    /*! [in] for renewal, this contains a currently held subscription SID.
     * For first time subscription, this must be empty. */
    const std::string& renewal_sid,
    /*! [out] SID returned by the subscription or renew msg. */
    std::string *sid,
    int timeoutms)
{
    int local_timeout = CP_MINIMUM_SUBSCRIPTION_TIME;

    sid->clear();

    /* request timeout to string */
    if (timeout == nullptr) {
        timeout = &local_timeout;
    }
    std::ostringstream timostr;
    if (*timeout < 0) {
        timostr << "infinite";
    } else if (*timeout < CP_MINIMUM_SUBSCRIPTION_TIME) {
        timostr << CP_MINIMUM_SUBSCRIPTION_TIME;
    } else {
        timostr << *timeout;
    }

    /* parse url */
    uri_type dest_url;
    int return_code = http_FixStrUrl(url, &dest_url);
    if (return_code != 0) {
        return return_code;
    }
    std::string urlforcurl = uri_asurlstr(dest_url);
    NetIF::IPAddr destaddr(reinterpret_cast<struct sockaddr*>(&dest_url.hostport.IPaddress));

    // Determine a suitable address for the callback. We choose one on the interface for the
    // destination address. Another possible approach would be to actually connect to the URL and
    // use getsockname(), which would let the routing code do the main job, at the cost of a
    // supplementary connection.
    NetIF::IPAddr myaddr;
    const NetIF::Interface *ifp =
        NetIF::Interfaces::theInterfaces()->interfaceForAddress(destaddr, g_netifs, myaddr);
    if (nullptr == ifp) {
        UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
                   "Could not find the interface for the destination address\n");
        return UPNP_E_SOCKET_CONNECT;
    }

    std::map<std::string, std::string> http_headers;
    char curlerrormessage[CURL_ERROR_SIZE];

    auto hdls = CurlGuard(curl_easy_init(), nullptr);
    curl_easy_setopt(hdls.htalk, CURLOPT_ERRORBUFFER, curlerrormessage);
    curl_easy_setopt(hdls.htalk, CURLOPT_WRITEFUNCTION, write_callback_null_curl);
    curl_easy_setopt(hdls.htalk, CURLOPT_CUSTOMREQUEST, "SUBSCRIBE");
    curl_easy_setopt(hdls.htalk, CURLOPT_URL, urlforcurl.c_str());
    curl_easy_setopt(hdls.htalk, CURLOPT_TIMEOUT_MS, timeoutms);
    curl_easy_setopt(hdls.htalk, CURLOPT_HEADERFUNCTION, header_callback_curl);
    curl_easy_setopt(hdls.htalk, CURLOPT_HEADERDATA, &http_headers);
    std::string descript;
    if (renewal_sid.empty()) {
        std::string cbheader{"CALLBACK: <"};
        cbheader += myCallbackUrl(myaddr) + "/>";
        hdls.hlist = curl_slist_append(hdls.hlist, cbheader.c_str());
        hdls.hlist = curl_slist_append(hdls.hlist, "NT: upnp:event");
        descript = std::string("(init) ") + "url [" + urlforcurl  + "] cb [" +
            myCallbackUrl(myaddr) + "] timeout [" + timostr.str() + "]";
        UpnpPrintf(UPNP_ALL,GENA,__FILE__,__LINE__, "gena_subscribe: %s\n", descript.c_str());
    } else {
        hdls.hlist = curl_slist_append(
            hdls.hlist, (std::string("SID: ") + renewal_sid).c_str());
        descript = std::string("(renew) ") + "url [" + urlforcurl  + "] SID [" +
            renewal_sid + "] timeout [" + timostr.str() + "]";
        UpnpPrintf(UPNP_ALL,GENA,__FILE__,__LINE__, "gena_subscribe: %s\n", descript.c_str());
    }
    hdls.hlist = curl_slist_append(
        hdls.hlist, (std::string("TIMEOUT: Second-") + timostr.str()).c_str());
    hdls.hlist = curl_slist_append(
        hdls.hlist, (std::string("USER-AGENT: ")+get_sdk_client_info()).c_str());
    curl_easy_setopt(hdls.htalk, CURLOPT_HTTPHEADER, hdls.hlist);

    CURLcode curlcode = curl_easy_perform(hdls.htalk);
    if (curlcode != CURLE_OK) {
        /* We may want to detail things here, depending on the curl error */
        UpnpPrintf(UPNP_ERROR,GENA,__FILE__,__LINE__,
                   "gena_subscribe: %s: CURL ERROR MESSAGE %s\n", descript.c_str(),curlerrormessage);
        return UPNP_E_SOCKET_CONNECT;
    }

    long http_status;
    curl_easy_getinfo (hdls.htalk, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status != HTTP_OK) {
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__,
                   "gena_subscribe: %s: HTTP status %d\n", descript.c_str(), int(http_status));
        return UPNP_E_SUBSCRIBE_UNACCEPTED;
    }

    /* get SID and TIMEOUT. the header callback lowercases the header names */
    const auto itsid = http_headers.find("sid");
    const auto ittimeout = http_headers.find("timeout");
    if (itsid == http_headers.end() || ittimeout == http_headers.end()) {
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__, "Subscribe error: no SID in answer\n");
        return UPNP_E_BAD_RESPONSE;
    }

    /* save timeout */
    if (!timeout_header_value(http_headers, timeout)) {
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__, "Subscribe error: no timeout in answer\n");
        return UPNP_E_BAD_RESPONSE;
    }

    /* save SID */
    *sid = itsid->second;
    UpnpPrintf(UPNP_ALL,GENA,__FILE__,__LINE__, "gena_subscribe ok: SID [%s] timeout %d\n",
               itsid->second.c_str(), *timeout);

    return UPNP_E_SUCCESS;
}


int genaUnregisterClient(UpnpClient_Handle client_handle)
{
    struct Handle_Info *handle_info;
    int timeoutms;
    ClientSubscription sub_copy;
    
    while (true) {
        {
            HANDLELOCK();

            if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
                return UPNP_E_INVALID_HANDLE;
            }
            if (handle_info->ClientSubList.empty()) {
                break;
            }
            sub_copy = handle_info->ClientSubList.front();
            handle_info->ClientSubList.remove_if(
                [sub_copy] (const ClientSubscription& e) {return e.SID == sub_copy.SID;});

            timeoutms = handle_info->SubsOpsTimeoutMS;
        }

        gena_unsubscribe(sub_copy.eventURL, sub_copy.SID, timeoutms);
        clientCancelRenew(&sub_copy);
    }

    return UPNP_E_SUCCESS;
}


int genaUnSubscribe(UpnpClient_Handle client_handle, const std::string& in_sid)
{
    struct Handle_Info *handle_info;
    int timeoutms;
    ClientSubscription sub_copy;
    {
        /* validate handle and sid */
        HANDLELOCK();
        if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
            return UPNP_E_INVALID_HANDLE;
        }
        auto sub = std::find_if(handle_info->ClientSubList.begin(), handle_info->ClientSubList.end(),
                                [in_sid](const ClientSubscription& e){return e.SID == in_sid;});
        if (handle_info->ClientSubList.end() == sub) {
            return UPNP_E_INVALID_SID;
        }
        timeoutms = handle_info->SubsOpsTimeoutMS;
        sub_copy = *sub;
    }

    gena_unsubscribe(sub_copy.eventURL, sub_copy.SID, timeoutms);
    clientCancelRenew(&sub_copy);

    HANDLELOCK();
    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        return UPNP_E_INVALID_HANDLE;
    }
    handle_info->ClientSubList.remove_if(
        [in_sid] (const ClientSubscription& e) {return e.SID == in_sid;});

    return UPNP_E_SUCCESS;
}


int genaSubscribe(
    UpnpClient_Handle client_handle,
    const std::string& PublisherURL,
    int *TimeOut,
    std::string *out_sid)
{
    int return_code = UPNP_E_SUCCESS;
    std::string SID;
    std::string EventURL;
    struct Handle_Info *handle_info;
    int timeoutms;
    
    out_sid->clear();

    {
        HANDLELOCK();
        /* validate handle */
        if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
            return UPNP_E_INVALID_HANDLE;
        }
        timeoutms = handle_info->SubsOpsTimeoutMS;
    }

    /* subscribe */
    SubscribeLock();
    return_code = gena_subscribe(PublisherURL, TimeOut, std::string(), &SID, timeoutms);
    if (return_code != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
                   "genaSubscribe: subscribe error, return %d\n", return_code);
        goto error_handler;
    }

    {
        HANDLELOCK();
        if(GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
            return_code = UPNP_E_INVALID_HANDLE;
            goto error_handler;
        }

        /* create event url */
        EventURL = PublisherURL;
        out_sid->assign(SID);
        handle_info->ClientSubList.emplace_front(-1, std::move(SID), std::move(EventURL));

        /* schedule expiration event */
        return_code = ScheduleGenaAutoRenew(client_handle, *TimeOut,
                                            &handle_info->ClientSubList.front());
    }

error_handler:
    SubscribeUnlock();
    return return_code;
}


int genaRenewSubscription(
    UpnpClient_Handle client_handle,
    const std::string& in_sid,
    int *TimeOut)
{
    struct Handle_Info *handle_info;
    int timeoutms;
    ClientSubscription sub_copy;
    {
        HANDLELOCK();

        /* validate handle and sid */
        if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
            return UPNP_E_INVALID_HANDLE;
        }

        auto sub = std::find_if(handle_info->ClientSubList.begin(), handle_info->ClientSubList.end(),
                                [in_sid](const ClientSubscription& e){return e.SID == in_sid;});
        if (handle_info->ClientSubList.end() == sub) {
            return UPNP_E_INVALID_SID;
        }
        timeoutms = handle_info->SubsOpsTimeoutMS;

        /* remove old events */
        gTimerThread->remove(sub->renewEventId);

        sub->renewEventId = -1;
        sub_copy = *sub;
    }

    std::string SID;
    int return_code = gena_subscribe(sub_copy.eventURL, TimeOut, sub_copy.SID, &SID, timeoutms);

    HANDLELOCK();

    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        return UPNP_E_INVALID_HANDLE;
    }

    if (return_code != UPNP_E_SUCCESS) {
        /* network failure (remove client sub) */
        handle_info->ClientSubList.remove_if(
            [in_sid] (const ClientSubscription& e) {return e.SID == in_sid;});
        clientCancelRenew(&sub_copy);
        return return_code;
    }

    /* get subscription */
    auto sub = std::find_if(handle_info->ClientSubList.begin(), handle_info->ClientSubList.end(),
                            [in_sid](const ClientSubscription& e){return e.SID == in_sid;});
    if (handle_info->ClientSubList.end() == sub) {
        clientCancelRenew(&sub_copy);
        return UPNP_E_INVALID_SID;
    }

    /* Remember SID */
    sub->SID = SID;

    /* start renew subscription timer */
    return_code = ScheduleGenaAutoRenew(client_handle, *TimeOut, &*sub);
    if (return_code != UPNP_E_SUCCESS) {
        handle_info->ClientSubList.remove_if(
            [sub] (const ClientSubscription& e) {return e.SID == sub->SID;});
    }
    clientCancelRenew(&sub_copy);

    return return_code;
}

class UPnPPropertysetParser : public XMLPARSERTP {
public:
    UPnPPropertysetParser(
        // XML to be parsed
        const std::string& input,
        // Output data
        std::unordered_map<std::string, std::string>& propd)
        : XMLPARSERTP(input),  propdata(propd) {
    }

protected:
    void EndElement(const XML_Char *name) override {
        const std::string& parentname = (m_path.size() == 1) ?
            "root" : m_path[m_path.size()-2].name;
        trimstring(m_chardata, " \t\n\r");

        if (!dom_cmp_name(parentname, "property")) {
            propdata[name] = m_chardata;
        }
        m_chardata.clear();
    }

    void CharacterData(const XML_Char *s, int len) override {
        if (s == nullptr || *s == 0)
            return;
        m_chardata.append(s, len);
    }

private:
    std::string m_chardata;
    std::unordered_map<std::string, std::string>& propdata;
};

void gena_process_notification_event(MHDTransaction *mhdt)
{
    UpnpPrintf(UPNP_ALL, GENA, __FILE__, __LINE__, "gena_process_notification_event\n");

    auto itsid = mhdt->headers.find("sid");
    /* get SID */
    if (itsid == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__, "gena_process_notification_event: no SID\n");
        return;
    }
    const std::string& sid = itsid->second;

    auto itseq = mhdt->headers.find("seq");
    /* get event key */
    if (itseq == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__, "gena_process_notification_event: no SEQ\n");
        return;
    }
    char cb[2];
    int eventKey;
    if (sscanf(itseq->second.c_str(), "%d%1c", &eventKey, cb) != 1) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__, "gena_process_notification_event: bad seq\n");
        return;
    }

    auto itnt = mhdt->headers.find("nt");
    auto itnts = mhdt->headers.find("nts");
    /* get NT and NTS headers */
    if (itnt == mhdt->headers.end() || itnts == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG,GENA,__FILE__,__LINE__, "gena_process_notification_event: no NTS\n");
        return;
    }

    /* verify NT and NTS headers */
    if (itnt->second != "upnp:event" || itnts->second != "upnp:propchange") {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: bad nt or nts\n");
        return;
    }

    /* parse the content (should be XML) */
    if (!has_xml_content_type(mhdt) || mhdt->postdata.empty()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: empty or not xml\n");
        return;
    }
    std::unordered_map<std::string, std::string> propset;
    UPnPPropertysetParser parser(mhdt->postdata, propset);
    if (!parser.Parse()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: xml parse failed: [%s]\n",
                   mhdt->postdata.c_str());
        return;
    }
    globalHndLock.lock();

    /* get client info */
    struct Handle_Info *handle_info;
    UpnpClient_Handle client_handle;
    if (GetClientHandleInfo(&client_handle, &handle_info) != HND_CLIENT) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        globalHndLock.unlock();
        return;
    }

    /* get subscription based on SID */
    auto subscription = std::find_if(
        handle_info->ClientSubList.begin(), handle_info->ClientSubList.end(),
        [sid](const ClientSubscription& e){return e.SID == sid;});
    if (handle_info->ClientSubList.end() == subscription) {
        if (eventKey == 0) {
            /* wait until we've finished processing a subscription  */
            /*   (if we are in the middle) */
            /* this is to avoid mistakenly rejecting the first event if we  */
            /*   receive it before the subscription response */
            globalHndLock.unlock();

            /* try and get Subscription Lock  */
            /*   (in case we are in the process of subscribing) */
            SubscribeLock();

            /* get HandleLock again */
            globalHndLock.lock();

            if (GetClientHandleInfo(&client_handle,&handle_info) != HND_CLIENT) {
                http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
                SubscribeUnlock();
                globalHndLock.unlock();
                return;
            }

            subscription = std::find_if(
                handle_info->ClientSubList.begin(), handle_info->ClientSubList.end(),
                [sid](const ClientSubscription& e){return e.SID == sid;});
            if (handle_info->ClientSubList.end() == subscription) {
                http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
                SubscribeUnlock();
                globalHndLock.unlock();
                return;
            }

            SubscribeUnlock();
        } else {
            UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                       "gena_process_notification_event: could not find subscription "
                       "but event key not 0 (%d)\n", eventKey);

            http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
            globalHndLock.unlock();
            return;
        }
    }

    /* success */
    http_SendStatusResponse(mhdt, HTTP_OK);

    /* fill event struct */
    struct Upnp_Event event_struct;
    event_struct.Sid = subscription->SID;
    event_struct.EventKey = eventKey;
    event_struct.ChangedVariables = propset;

    /* copy callback */
    auto callback = handle_info->Callback;
    auto cookie = handle_info->Cookie;

    globalHndLock.unlock();

    /* make callback with event struct */
    /* In future, should find a way of mainting */
    /* that the handle is not unregistered in the middle of a */
    /* callback */
    callback(UPNP_EVENT_RECEIVED, &event_struct, cookie);
}


#endif /* INCLUDE_CLIENT_APIS */
#endif /* EXCLUDE_GENA */
