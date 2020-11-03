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

#include <string>
#include <map>
#include <sstream>

#include "gena.h"
#include "httputils.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "upnp_timeout.h"
#include "genut.h"
#include "TimerThread.h"
#include "gena_sids.h"
#include "netif.h"

#ifdef USE_EXPAT
#include "expatmm.hxx"
#define XMLPARSERTP inputRefXMLParser
#else
#include "picoxml.h"
#define XMLPARSERTP PicoXMLParser
#endif

extern TimerThread *gTimerThread;

static void clientCancelRenew(ClientSubscription *sub)
{
    if (nullptr == sub) {
        return;
    }
    int renewEventId = sub->renewEventId;
    sub->renewEventId = -1;
    sub->actualSID.clear();
    sub->eventURL.clear();
    if (renewEventId != -1) {
        gTimerThread->remove(renewEventId);
    }
}

/*!
 * \brief This is a thread function to send the renewal just before the
 * subscription times out.
 */
static void *thread_autorenewsubscription(
    /*! [in] Thread data(upnp_timeout *) needed to send the renewal. */
    void *input)
{
    auto event = static_cast<upnp_timeout *>(input);
    auto sub_struct =
        static_cast<struct Upnp_Event_Subscribe *>(event->Event);
    void *cookie;
    Upnp_FunPtr callback_fun;
    struct Handle_Info *handle_info;
    int send_callback = 0;
    int eventType = 0;
    int timeout = 0;
    int errCode = 0;
    std::string tmpSID;

    if (AUTO_RENEW_TIME == 0) {
        UpnpPrintf( UPNP_INFO, GENA, __FILE__, __LINE__, "GENA SUB EXPIRED\n");
        sub_struct->ErrCode = UPNP_E_SUCCESS;
        send_callback = 1;
        eventType = UPNP_EVENT_SUBSCRIPTION_EXPIRED;
    } else {
        UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, "GENA AUTO RENEW\n");
        timeout = sub_struct->TimeOut;
        tmpSID = sub_struct->Sid;
        errCode = genaRenewSubscription(
            event->handle,
            tmpSID,
            &timeout);
        sub_struct->ErrCode = errCode;
        sub_struct->TimeOut = timeout;
        if (errCode != UPNP_E_SUCCESS &&
            errCode != GENA_E_BAD_SID &&
            errCode != GENA_E_BAD_HANDLE) {
            send_callback = 1;
            eventType = UPNP_EVENT_AUTORENEWAL_FAILED;
        }
    }

    if (send_callback) {
        HandleReadLock();
        if( GetHandleInfo( event->handle, &handle_info ) != HND_CLIENT ) {
            HandleUnlock();
            free_upnp_timeout(event);
            goto end_function;
        }

        /* make callback */
        callback_fun = handle_info->Callback;
        cookie = handle_info->Cookie;
        HandleUnlock();
        callback_fun(static_cast<Upnp_EventType>(eventType), event->Event, cookie);
    }

end_function:
    return nullptr;
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
    struct Upnp_Event_Subscribe *RenewEventStruct = nullptr;
    upnp_timeout *RenewEvent = nullptr;
    int return_code = GENA_SUCCESS;
    const std::string& tmpSID = sub->SID;
    const std::string& tmpEventURL = sub->eventURL;

    if (TimeOut == UPNP_INFINITE) {
        return_code = GENA_SUCCESS;
        goto end_function;
    }

    RenewEventStruct = static_cast<struct Upnp_Event_Subscribe *>(malloc(
        sizeof(struct Upnp_Event_Subscribe)));
    if (RenewEventStruct == nullptr) {
        return_code = UPNP_E_OUTOF_MEMORY;
        goto end_function;
    }

    RenewEvent =  new upnp_timeout;
    if (nullptr == RenewEvent) {
        free(RenewEventStruct);
        return_code = UPNP_E_OUTOF_MEMORY;
        goto end_function;
    }

    /* schedule expire event */
    RenewEventStruct = {};
    RenewEventStruct->ErrCode = UPNP_E_SUCCESS;
    RenewEventStruct->TimeOut = TimeOut;
    upnp_strlcpy(RenewEventStruct->Sid, tmpSID, sizeof(RenewEventStruct->Sid));
    upnp_strlcpy(RenewEventStruct->PublisherUrl, tmpEventURL, NAME_SIZE);

    RenewEvent->handle = client_handle;
    RenewEvent->Event = RenewEventStruct;

    /* Schedule the job */
    return_code = gTimerThread->schedule(
        TimerThread::SHORT_TERM, TimerThread::REL_SEC, TimeOut - AUTO_RENEW_TIME,
        &(RenewEvent->eventId),    thread_autorenewsubscription, RenewEvent,
        reinterpret_cast<ThreadPool::free_routine>(free_upnp_timeout));

    if (return_code != UPNP_E_SUCCESS) {
        free_upnp_timeout(RenewEvent);
        goto end_function;
    }

    sub->renewEventId = RenewEvent->eventId;

    return_code = GENA_SUCCESS;

end_function:
    return return_code;
}


/*!
 * \brief Sends the UNSUBCRIBE gena request and recieves the response from the
 *     device and returns it as a parameter.
 *
 * \returns 0 if successful, otherwise returns the appropriate error code.
 */
static int gena_unsubscribe(
    /*! [in] Event URL of the service. */
    const std::string& url,
    /*! [in] The subcription ID. */
    const std::string& sid)
{
    int return_code;
    uri_type dest_url;

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
    // If this is an ipv6 url (which we check rather cavalierly),
    // this may be a link-local address, and it needs an interface
    // index (scope_id). This is a temporary hack to work with a
    // single ipv6 interface until we do the right thing which
    // would be to store and transport the appropriate scope for
    // every endpoint
    if (using_ipv6() && surl.find('[') != std::string::npos) {
        curl_easy_setopt(easy, CURLOPT_ADDRESS_SCOPE, apiFirstIPV6Index());
    }
    
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, HTTP_DEFAULT_TIMEOUT);

    struct curl_slist *list = nullptr;
    list = curl_slist_append(list, (std::string("SID: ") + sid).c_str());
    list = curl_slist_append(
        list, (std::string("USER-AGENT: ") + get_sdk_info()).c_str());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);

    CURLcode code = curl_easy_perform(easy);

    if (code != CURLE_OK) {
        curl_easy_cleanup(easy);
        curl_slist_free_all(list);
        /* We may want to detail things here, depending on the curl error */
        UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
                   "CURL ERROR MESSAGE %s\n", curlerrormessage);
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
static std::string myCallbackUrl(NetIF::IPAddr& netaddr)
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
    oss << ":" << (netaddr.family() == NetIF::IPAddr::Family::IPV6 ?
                   LOCAL_PORT_V6 : LOCAL_PORT_V4);
    return oss.str();
}

struct CurlGuard {
    CURL *htalk{nullptr};
    struct curl_slist *hlist{nullptr};
    ~CurlGuard() {
        if (htalk) {
            curl_easy_cleanup(htalk);
        }
        if (hlist) {
            curl_slist_free_all(hlist);
        }
    }
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
    std::string *sid)
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
    NetIF::IPAddr destaddr(
        reinterpret_cast<struct sockaddr*>(&dest_url.hostport.IPaddress));
    NetIF::IPAddr myaddr;
    const NetIF::Interface *ifp =
        NetIF::Interfaces::theInterfaces()->interfaceForAddress(destaddr,myaddr);
    if (nullptr == ifp) {
        UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
                   "Could not find the interface for the destination address\n");
        return UPNP_E_SOCKET_CONNECT;
    }
        
    CurlGuard hdls;
    std::map<std::string, std::string> http_headers;
    char curlerrormessage[CURL_ERROR_SIZE];
    
    hdls.htalk = curl_easy_init();
    curl_easy_setopt(hdls.htalk, CURLOPT_ERRORBUFFER, curlerrormessage);
    curl_easy_setopt(hdls.htalk, CURLOPT_WRITEFUNCTION,write_callback_null_curl);
    curl_easy_setopt(hdls.htalk, CURLOPT_CUSTOMREQUEST, "SUBSCRIBE");
    curl_easy_setopt(hdls.htalk, CURLOPT_URL, urlforcurl.c_str());
    // If this is an ipv6 url (which we check rather cavalierly),
    // this may be a link-local address, and it needs an interface
    // index (scope_id). This is a temporary hack to work with a
    // single ipv6 interface until we do the right thing which
    // would be to store and transport the appropriate scope for
    // every endpoint
    if (using_ipv6() && urlforcurl.find('[') != std::string::npos) {
        curl_easy_setopt(hdls.htalk, CURLOPT_ADDRESS_SCOPE, apiFirstIPV6Index());
    }
    
    curl_easy_setopt(hdls.htalk, CURLOPT_TIMEOUT, HTTP_DEFAULT_TIMEOUT);
    curl_easy_setopt(hdls.htalk, CURLOPT_HEADERFUNCTION, header_callback_curl);
    curl_easy_setopt(hdls.htalk, CURLOPT_HEADERDATA, &http_headers);
    if (renewal_sid.empty()) {
        std::string cbheader{"CALLBACK: <"};
        cbheader += myCallbackUrl(myaddr) + "/>";
        hdls.hlist = curl_slist_append(hdls.hlist, cbheader.c_str());
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_subscribe: callback: %s\n", cbheader.c_str());
        hdls.hlist = curl_slist_append(hdls.hlist, "NT: upnp:event");
    } else {
        hdls.hlist = curl_slist_append(
            hdls.hlist, (std::string("SID: ") + renewal_sid).c_str());
    }
    hdls.hlist = curl_slist_append(
        hdls.hlist, (std::string("TIMEOUT: Second-") + timostr.str()).c_str());
    curl_easy_setopt(hdls.htalk, CURLOPT_HTTPHEADER, hdls.hlist);

    CURLcode curlcode = curl_easy_perform(hdls.htalk);
    if (curlcode != CURLE_OK) {
        /* We may want to detail things here, depending on the curl error */
        UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
                   "CURL ERROR MESSAGE %s\n", curlerrormessage);
        return UPNP_E_SOCKET_CONNECT;
    }

    long http_status;
    curl_easy_getinfo (hdls.htalk, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status != HTTP_OK) {
        return UPNP_E_SUBSCRIBE_UNACCEPTED;
    }

    /* get SID and TIMEOUT. the header callback lowercases the header names */
    const auto itsid = http_headers.find("sid");
    const auto ittimeout = http_headers.find("timeout");
    if (itsid == http_headers.end() || ittimeout == http_headers.end()) {
        return UPNP_E_BAD_RESPONSE;
    }

    /* save timeout */
    if (!timeout_header_value(http_headers, timeout)) {
        return UPNP_E_BAD_RESPONSE;
    }

    /* save SID */
    *sid = itsid->second;
    
    return UPNP_E_SUCCESS;
}


int genaUnregisterClient(UpnpClient_Handle client_handle)
{
    int return_code = UPNP_E_SUCCESS;
    struct Handle_Info *handle_info = nullptr;

    while (true) {
        HandleLock();

        if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
            HandleUnlock();
            return GENA_E_BAD_HANDLE;
        }
        if (handle_info->ClientSubList.empty()) {
            return_code = UPNP_E_SUCCESS;
            break;
        }
        ClientSubscription sub_copy = handle_info->ClientSubList.front();
        RemoveClientSubClientSID(handle_info->ClientSubList, sub_copy.SID);

        HandleUnlock();

        gena_unsubscribe(sub_copy.eventURL, sub_copy.actualSID);
        clientCancelRenew(&sub_copy);
    }

    handle_info->ClientSubList.clear();
    HandleUnlock();

    return return_code;
}


int genaUnSubscribe(
    UpnpClient_Handle client_handle,
    const std::string& in_sid)
{
    ClientSubscription *sub = nullptr;
    int return_code = GENA_SUCCESS;
    struct Handle_Info *handle_info;
    ClientSubscription sub_copy;

    /* validate handle and sid */
    HandleLock();
    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        HandleUnlock();
        return_code = GENA_E_BAD_HANDLE;
        goto exit_function;
    }
    sub = GetClientSubClientSID(handle_info->ClientSubList, in_sid);
    if (nullptr == sub) {
        HandleUnlock();
        return_code = GENA_E_BAD_SID;
        goto exit_function;
    }
    sub_copy = *sub;
    HandleUnlock();

    return_code = gena_unsubscribe(sub_copy.eventURL, sub_copy.actualSID);
    clientCancelRenew(&sub_copy);

    HandleLock();
    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        HandleUnlock();
        return_code = GENA_E_BAD_HANDLE;
        goto exit_function;
    }
    RemoveClientSubClientSID(handle_info->ClientSubList, in_sid);
    HandleUnlock();

exit_function:
    return return_code;
}


int genaSubscribe(
    UpnpClient_Handle client_handle,
    const std::string& PublisherURL,
    int *TimeOut,
    std::string *out_sid)
{
    int return_code = GENA_SUCCESS;
    ClientSubscription newSubscription;
    std::string ActualSID;
    std::string EventURL;
    struct Handle_Info *handle_info;

    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__, "genaSubscribe\n");

    out_sid->clear();

    HandleReadLock();
    /* validate handle */
    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        return_code = GENA_E_BAD_HANDLE;
        SubscribeLock();
        goto error_handler;
    }
    HandleUnlock();

    /* subscribe */
    SubscribeLock();
    return_code = gena_subscribe(PublisherURL,TimeOut,std::string(), &ActualSID);
    HandleLock();
    if (return_code != UPNP_E_SUCCESS) {
        UpnpPrintf(UPNP_ERROR, GENA, __FILE__, __LINE__,
                   "genSubscribe: subscribe error, return %d\n", return_code);
        goto error_handler;
    }

    if(GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        return_code = GENA_E_BAD_HANDLE;
        goto error_handler;
    }

    /* generate client SID */
    out_sid->assign(std::string("uuid:") + gena_sid_uuid());

    /* create event url */
    EventURL = PublisherURL;

    newSubscription.renewEventId = -1;
    newSubscription.SID = *out_sid;
    newSubscription.actualSID = ActualSID;
    newSubscription.eventURL = EventURL;
    handle_info->ClientSubList.push_front(newSubscription);

    /* schedule expiration event */
    return_code = ScheduleGenaAutoRenew(client_handle, *TimeOut,
                                        &handle_info->ClientSubList.front());

error_handler:
    HandleUnlock();
    SubscribeUnlock();

    return return_code;
}


int genaRenewSubscription(
    UpnpClient_Handle client_handle,
    const std::string& in_sid,
    int *TimeOut)
{
    int return_code = GENA_SUCCESS;
    ClientSubscription *sub = nullptr;
    ClientSubscription sub_copy;
    struct Handle_Info *handle_info;
    std::string ActualSID;

    HandleLock();

    /* validate handle and sid */
    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        HandleUnlock();

        return_code = GENA_E_BAD_HANDLE;
        goto exit_function;
    }

    sub = GetClientSubClientSID(handle_info->ClientSubList, in_sid);
    if (sub == nullptr) {
        HandleUnlock();

        return_code = GENA_E_BAD_SID;
        goto exit_function;
    }
    /* remove old events */
    gTimerThread->remove(sub->renewEventId);

    sub->renewEventId = -1;
    sub_copy = *sub;

    HandleUnlock();

    return_code = gena_subscribe(sub_copy.eventURL, TimeOut, sub_copy.actualSID,
                                 &ActualSID);

    HandleLock();

    if (GetHandleInfo(client_handle, &handle_info) != HND_CLIENT) {
        HandleUnlock();
        return_code = GENA_E_BAD_HANDLE;
        goto exit_function;
    }

    if (return_code != UPNP_E_SUCCESS) {
        /* network failure (remove client sub) */
        RemoveClientSubClientSID(handle_info->ClientSubList, in_sid);
        clientCancelRenew(&sub_copy);
        HandleUnlock();
        goto exit_function;
    }

    /* get subscription */
    sub = GetClientSubClientSID(handle_info->ClientSubList, in_sid);
    if (sub == nullptr) {
        clientCancelRenew(&sub_copy);
        HandleUnlock();
        return_code = GENA_E_BAD_SID;
        goto exit_function;
    }

    /* store actual sid */
    sub->actualSID = ActualSID;

    /* start renew subscription timer */
    return_code = ScheduleGenaAutoRenew(client_handle, *TimeOut, sub);
    if (return_code != GENA_SUCCESS) {
        RemoveClientSubClientSID(handle_info->ClientSubList, sub->SID);
    }
    clientCancelRenew(&sub_copy);
    HandleUnlock();

exit_function:
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
    struct Upnp_Event event_struct;
    int eventKey;
    ClientSubscription *subscription = nullptr;
    struct Handle_Info *handle_info;
    void *cookie;
    Upnp_FunPtr callback;
    UpnpClient_Handle client_handle;
    std::string tmpSID;

    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
               "gena_process_notification_event\n");
    
    auto itsid = mhdt->headers.find("sid");
    /* get SID */
    if (itsid == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: no SID\n");
        return;
    }
    const std::string& sid = itsid->second;

    auto itseq = mhdt->headers.find("seq");
    /* get event key */
    if (itseq == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: no SEQ\n");
        return;
    }
    char cb[2];
    if (sscanf(itseq->second.c_str(), "%d%1c", &eventKey, cb) != 1) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: bad seq\n");
        return;
    }

    auto itnt = mhdt->headers.find("nt");
    auto itnts = mhdt->headers.find("nts");
    /* get NT and NTS headers */
    if (itnt == mhdt->headers.end() || itnts == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_notification_event: no NTS\n");
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
    HandleLock();

    /* get client info */
    if (GetClientHandleInfo(&client_handle, &handle_info) != HND_CLIENT) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        HandleUnlock();
        return;
    }

    /* get subscription based on SID */
    subscription = GetClientSubActualSID(handle_info->ClientSubList, sid);
    if (subscription == nullptr) {
        if (eventKey == 0) {
            /* wait until we've finished processing a subscription  */
            /*   (if we are in the middle) */
            /* this is to avoid mistakenly rejecting the first event if we  */
            /*   receive it before the subscription response */
            HandleUnlock();

            /* try and get Subscription Lock  */
            /*   (in case we are in the process of subscribing) */
            SubscribeLock();

            /* get HandleLock again */
            HandleLock();

            if (GetClientHandleInfo(&client_handle,&handle_info) != HND_CLIENT) {
                http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
                SubscribeUnlock();
                HandleUnlock();
                return;
            }

            subscription = GetClientSubActualSID(handle_info->ClientSubList,sid);
            if (subscription == nullptr) {
                http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
                SubscribeUnlock();
                HandleUnlock();
                return;
            }

            SubscribeUnlock();
        } else {
            http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
            HandleUnlock();
            return;
        }
    }

    /* success */
    http_SendStatusResponse(mhdt, HTTP_OK);

    /* fill event struct */
    tmpSID = subscription->SID;
    memset(event_struct.Sid, 0, sizeof(event_struct.Sid));
    upnp_strlcpy(event_struct.Sid, tmpSID, sizeof(event_struct.Sid));
    event_struct.EventKey = eventKey;
    event_struct.ChangedVariables = propset;

    /* copy callback */
    callback = handle_info->Callback;
    cookie = handle_info->Cookie;

    HandleUnlock();

    /* make callback with event struct */
    /* In future, should find a way of mainting */
    /* that the handle is not unregistered in the middle of a */
    /* callback */
    callback(UPNP_EVENT_RECEIVED, &event_struct, cookie);
}


#endif /* INCLUDE_CLIENT_APIS */
#endif /* EXCLUDE_GENA */
