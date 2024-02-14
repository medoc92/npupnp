/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 * Copyright (c) 2012 France Telecom All rights reserved.
 * Copyright (c) 2020 J.F. Dockes <jf@dockes.org>
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
#ifdef INCLUDE_DEVICE_APIS

#include <iostream>
#include <sstream>

#include <curl/curl.h>

#include "gena.h"
#include "gena_sids.h"
#include "genut.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "uri.h"

static constexpr auto XML_PROPERTYSET_HEADER =
    R"(<e:propertyset xmlns:e="urn:schemas-upnp-org:event-1-0">)" "\n";

/*!
 * \brief Unregisters a device.
 *
 * \return UPNP_E_SUCCESS on success, GENA_E_BAD_HANDLE on failure.
 */
int genaUnregisterDevice(UpnpDevice_Handle device_handle)
{
    int ret = 0;
    struct Handle_Info *handle_info;

    HandleLock();
    if (GetHandleInfo(device_handle, &handle_info) != HND_DEVICE) {
        UpnpPrintf(UPNP_CRITICAL, GENA, __FILE__, __LINE__,
            "genaUnregisterDevice: BAD Handle: %d\n", device_handle);
        ret = GENA_E_BAD_HANDLE;
    } else {
        clearServiceTable(handle_info->serviceTable);
        ret = UPNP_E_SUCCESS;
    }
    HandleUnlock();

    return ret;
}

/*!
 * \brief Generates XML property set for notifications.
 *
 * \return UPNP_E_SUCCESS if successful else returns GENA_E_BAD_HANDLE.
 *
 * \note The XML_VERSION comment is NOT sent due to interoperability issues
 *     with other UPnP vendors.
 */
static int GeneratePropertySet(
    /*! [in] Array of variable names (go in the event notify). */
    char **names,
    /*! [in] Array of variable values (go in the event notify). */
    char **values,
    /*! [in] number of variables. */
    int count,
    /*! [out] PropertySet node in the string format. */
    std::string *pout)
{
    std::string& out = *pout;
    out = XML_PROPERTYSET_HEADER;
    for (int counter = 0; counter < count; counter++) {
        out += "<e:property>\n";
        out += std::string("<") +names[counter] + ">"+values[counter]+ "</" +
            names[counter] + ">\n</e:property>\n";
    }
    out += "</e:propertyset>\n\n";
    return UPNP_E_SUCCESS;
}


/*!
 * \brief Function to Notify a particular subscription of a particular event.
 *
 * This is called by a thread pool thread, and the subscription is a
 * copy of the handle table data, so, no locking.
 *
 * NOTIFY http request is sent and the reply is processed.
 *
 * \return GENA_SUCCESS if the event was delivered, otherwise returns the
 *     appropriate error code.
 *
 * The only code which has specific processing is
 * GENA_E_NOTIFY_UNACCEPTED_REMOVE_SUB which results in clearing the
 * subscription. This can only happen if the HTTP transaction is
 * network-successful but has an HTTP status of 412, which results in
 * clearing the subscription (else, subscriptions are only removed
 * when they time-out).
 * The previous version had more detailed error codes, but all other
 * error codes were just ignored, except for message printing.
 */
static int genaNotify(const std::string& propertySet, const subscription *sub)
{
    std::string mid_msg;
    int return_code = -1;

    long http_code = 0;
    /* send a notify to each url until one goes thru */
    for (const auto& url : sub->DeliveryURLs) {
        CURL *easy = curl_easy_init();
        char curlerrormessage[CURL_ERROR_SIZE];

        curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curlerrormessage);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback_null_curl);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, nullptr);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT,
                         long(GENA_NOTIFICATION_SENDING_TIMEOUT +
                              GENA_NOTIFICATION_ANSWERING_TIMEOUT)/2);
        curl_easy_setopt(easy, CURLOPT_POST, long(1));
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, propertySet.c_str());
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "NOTIFY");

        struct curl_slist *list = nullptr;
        list = curl_slist_append(list, "NT: upnp:event");
        list = curl_slist_append(list, "NTS: upnp:propchange");
        list = curl_slist_append(list,(std::string("SID: ") + sub->sid).c_str());
        auto buff = std::to_string(sub->ToSendEventKey);
        list = curl_slist_append(list, (std::string("SEQ: ") + buff).c_str());

        list = curl_slist_append(list, "Accept:");
        list = curl_slist_append(list, "Expect:");
        list = curl_slist_append(list, R"(Content-Type: text/xml; charset="utf-8")");
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());

        CURLcode code = curl_easy_perform(easy);
        if (code == CURLE_OK) {
            return_code = UPNP_E_SUCCESS;
            curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_code);
        } else {
            // Note: this is common: e.g. client exited without unsubscribing
            UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                       "CURL ERROR MESSAGE %s\n", curlerrormessage);
            return_code = UPNP_E_BAD_RESPONSE;
        }
        /* Clean-up. */
        curl_slist_free_all(list);
        curl_easy_cleanup(easy);

        if (return_code == UPNP_E_SUCCESS)
            break;
    }

    if (return_code == UPNP_E_SUCCESS) {
        if (http_code == HTTP_OK) {
            return_code = GENA_SUCCESS;
        } else {
            if (http_code == HTTP_PRECONDITION_FAILED)
                /*Invalid SID gets removed */
                return_code = GENA_E_NOTIFY_UNACCEPTED_REMOVE_SUB;
            else
                return_code = GENA_E_NOTIFY_UNACCEPTED;
        }
    }
    return return_code;
}


/* Notification structures are queued on the output queue of every
   subscription.  They hold some common data because the same event is
   sent to all subscribed CPs. Probably not worth the trouble sharing
   though (e.g. with shared_ptr for the propertySet string. We don't
   typically have thousands of subscribed CPs... */
struct Notification {
    Notification(std::string sid, std::string udn, std::string pset, Upnp_SID uSID,
                 time_t ct, UpnpDevice_Handle dh)
        : device_handle(dh), UDN(std::move(udn)), servId(std::move(sid)), sid(std::move(uSID)),
          propertySet(std::move(pset)), ctime(ct) {}
    UpnpDevice_Handle device_handle; //
    std::string UDN;                 // Device
    std::string servId;  // Service
    Upnp_SID sid;        // Subscription
    std::string propertySet; // Message content
    time_t ctime;        // Age
};

class GenaNotifyJobWorker : public JobWorker {
public:
    explicit GenaNotifyJobWorker(std::shared_ptr<Notification> in)
        : m_input(in) {}
    ~GenaNotifyJobWorker() override = default;
    GenaNotifyJobWorker(const GenaNotifyJobWorker&) = delete;
    GenaNotifyJobWorker& operator=(const GenaNotifyJobWorker&) = delete;
    void work() override;
    // This is actually shared with the outgoing list head.  Note: 2024-02: as far as I understand,
    // the only reason to keep a shared pointer to the active notification at the head of the
    // subscription outgoing queue is as an indicator that we don't need to kickstart the
    // queue. Else when adding an event and seeing an empty queue, we would not know if the event
    // should be queued or directly sent to the Thread Pool. This could probably be replaced by a
    // flag, which would allow to replace the shared_ptr with unique_ptr.
    std::shared_ptr<Notification> m_input;
};

/*!
 * \brief Thread job to Notify a control point.
 *
 * It validates and copies the subscription so that the lock can be
 * released during the actual network transfer (done by genaNotify())
 */
void GenaNotifyJobWorker::work()
{
    subscription *sub;
    service_info *service;
    subscription sub_copy;
    int return_code;
    struct Handle_Info *handle_info;

    /* This should be a HandleLock and not a HandleReadLock otherwise if there
     * is a lot of notifications, then multiple threads will acquire a read
     * lock and the thread which sends the notification will be blocked forever
     * on the HandleLock at the end of this function. */
    /*HandleReadLock(); */
    HandleLock();
    /* validate context */

    if (GetHandleInfo(m_input->device_handle, &handle_info) != HND_DEVICE) {
        HandleUnlock();
        return;
    }

    if (!(service = FindServiceId(handle_info->serviceTable, m_input->servId, m_input->UDN)) ||
        !service->active ||
        !(sub = GetSubscriptionSID(m_input->sid, service)) ||
        copy_subscription(sub, &sub_copy) != UPNP_E_SUCCESS) {
        HandleUnlock();
        return;
    }

    HandleUnlock();

    /* send the notify */
    return_code = genaNotify(m_input->propertySet, &sub_copy);
    HandleLock();
    if (GetHandleInfo(m_input->device_handle, &handle_info) != HND_DEVICE) {
        HandleUnlock();
        return;
    }
    /* validate context */
    if (!(service = FindServiceId(handle_info->serviceTable, m_input->servId, m_input->UDN)) ||
        !service->active ||
        !(sub = GetSubscriptionSID(m_input->sid, service))) {
        HandleUnlock();
        return;
    }
    sub->ToSendEventKey++;
    if (sub->ToSendEventKey < 0)
        /* wrap to 1 for overflow */
        sub->ToSendEventKey = 1;

    /* Remove head of event queue. */
    if (!sub->outgoing.empty()) {
        sub->outgoing.pop_front();
    }
    /* Possibly activate next */
    if (!sub->outgoing.empty()) {
        auto notif = sub->outgoing.begin();
        auto worker = std::make_unique<GenaNotifyJobWorker>(*notif);
        gSendThreadPool.addJob(std::move(worker));
    }

    // No idea why we do this after sending one more event. Was the
    // same in pupnp. It would seem saner to call this right after we
    // get the error and do nothing else? Would have to take care with
    // the first Notif then, because it's the only case where it's not
    // managed by a ThreadPool Job (potentially creating a mem leak).
    if (return_code == GENA_E_NOTIFY_UNACCEPTED_REMOVE_SUB)
        RemoveSubscriptionSID(m_input->sid, service);

    HandleUnlock();
}


int genaInitNotifyXML(
    UpnpDevice_Handle device_handle,
    char *UDN,
    char *servId,
    const std::string& propertySet,
    const Upnp_SID& sid)
{
    int ret = GENA_SUCCESS;
    int line = 0;
    std::shared_ptr<Notification> thread_struct;
    subscription *sub = nullptr;
    service_info *service = nullptr;
    struct Handle_Info *handle_info;

    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
               "genaInitNotifyXML: props: %s\n", propertySet.c_str());

    HandleLock();

    if (GetHandleInfo(device_handle, &handle_info) != HND_DEVICE) {
        line = __LINE__;
        ret = GENA_E_BAD_HANDLE;
        goto ExitFunction;
    }

    service = FindServiceId(handle_info->serviceTable, servId, UDN);
    if (service == nullptr) {
        line = __LINE__;
        ret = GENA_E_BAD_SERVICE;
        goto ExitFunction;
    }

    sub = GetSubscriptionSID(sid, service);
    if (sub == nullptr || sub->active) {
        line = __LINE__;
        ret = GENA_E_BAD_SID;
        goto ExitFunction;
    }
    sub->active = 1;

    /* schedule thread for initial notification */
    thread_struct = std::make_shared<Notification>(
        servId, UDN, propertySet, sid, time(nullptr), device_handle);
    {
        auto worker = std::make_unique<GenaNotifyJobWorker>(thread_struct);
        ret = gSendThreadPool.addJob(std::move(worker));
    }
    if (ret != 0) {
        line = __LINE__;
        ret = UPNP_E_OUTOF_MEMORY;
    } else {
        line = __LINE__;
        sub->outgoing.push_back(thread_struct);
        ret = GENA_SUCCESS;
    }

ExitFunction:
    HandleUnlock();
    UpnpPrintf(UPNP_ALL, GENA, __FILE__, line, "genaInitNotifyCommon: ret %d\n", ret);
    return ret;
}

int genaInitNotifyVars(
    UpnpDevice_Handle device_handle,
    char *UDN,
    char *servId,
    char **VarNames,
    char **VarValues,
    int var_count,
    const Upnp_SID& sid)
{
    int ret = GENA_SUCCESS;
    int line = 0;
    std::string propertySet;

    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__, "genaInitNotifyVars varcnt %d\n", var_count);

    if (var_count <= 0) {
        line = __LINE__;
        ret = GENA_SUCCESS;
        goto ExitFunction;
    }

    ret = GeneratePropertySet(VarNames, VarValues, var_count, &propertySet);
    if (ret != UPNP_E_SUCCESS) {
        line = __LINE__;
        goto ExitFunction;
    }

    ret = genaInitNotifyXML(device_handle, UDN, servId, propertySet, sid);

ExitFunction:
    UpnpPrintf(UPNP_ALL, GENA, __FILE__,line, "genaInitNotify: ret = %d\n", ret);
    return ret;
}

void freeSubscriptionQueuedEvents(subscription *sub)
{
    sub->outgoing.clear();
}

/*
 * This gets called before queuing a new event, with the handLock held.
 * - The list size can never go over MAX_SUBSCRIPTION_QUEUED_EVENTS so we
 *   discard the oldest non-active event if it is already at the max
 * - We also discard any non-active event older than MAX_SUBSCRIPTION_EVENT_AGE.
 * non-active: any but the head of queue, which is already copied to
 * the thread pool
 */
static void maybeDiscardEvents(std::list<std::shared_ptr<Notification>>& outgoing)
{
    time_t now = time(nullptr);

    auto it = outgoing.begin();
    // Skip first event: it's in the pool already
    if (it != outgoing.end())
        it++;
    while (it != outgoing.end()) {
        if (outgoing.size() > unsigned(g_UpnpSdkEQMaxLen) ||
            now - (*it)->ctime > g_UpnpSdkEQMaxAge) {
            it = outgoing.erase(it);
        } else {
            /* If the list is smaller than the max and the oldest
             * task is young enough, stop pruning */
            break;
        }
    }
}

int genaNotifyAllXML(
    UpnpDevice_Handle device_handle, char *UDN, char *servId, const std::string& propertySet)
{
    int ret = GENA_SUCCESS;
    int line = 0;
    std::list<subscription>::iterator finger;
    std::shared_ptr<Notification> thread_struct;
    service_info *service = nullptr;
    struct Handle_Info *handle_info;

    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
               "genaNotifyAllXML: props: %s\n", propertySet.c_str());

    HandleLock();

    if (GetHandleInfo(device_handle, &handle_info) != HND_DEVICE) {
        line = __LINE__;
        ret = GENA_E_BAD_HANDLE;
        goto ExitFunction;
    }

    service = FindServiceId(handle_info->serviceTable, servId, UDN);
    if (service == nullptr) {
        line = __LINE__;
        ret = GENA_E_BAD_SERVICE;
        goto ExitFunction;
    }

    finger = GetFirstSubscription(service);
    while (finger != service->subscriptionList.end()) {
        thread_struct = std::make_shared<Notification>(
            servId, UDN, propertySet, finger->sid, time(nullptr), device_handle);
        maybeDiscardEvents(finger->outgoing);
        finger->outgoing.push_back(thread_struct);

        /* If there is only one element on the list (just added), kickstart the threadpool */
        if (finger->outgoing.size() == 1) {
            auto worker = std::make_unique<GenaNotifyJobWorker>(thread_struct);
            ret = gSendThreadPool.addJob(std::move(worker));
            if (ret != 0) {
                line = __LINE__;
                if (ret == EOUTOFMEM) {
                    line = __LINE__;
                    ret = UPNP_E_OUTOF_MEMORY;
                }
                break;
            }
        }
        finger = GetNextSubscription(service, finger);
    }

ExitFunction:
    HandleUnlock();
    UpnpPrintf(UPNP_ALL, GENA, __FILE__, line, "genaNotifyAllCommon: ret = %d\n", ret);
    return ret;
}

int genaNotifyAll(
    UpnpDevice_Handle device_handle,
    char *UDN,
    char *servId,
    char **VarNames,
    char **VarValues,
    int var_count)
{
    int ret = GENA_SUCCESS;
    int line = 0;

    UpnpPrintf(UPNP_ALL, GENA, __FILE__, __LINE__, "genaNotifyAll\n");

    std::string propertySet;
    ret = GeneratePropertySet(VarNames, VarValues, var_count, &propertySet);
    if (ret != UPNP_E_SUCCESS) {
        line = __LINE__;
        goto ExitFunction;
    }

    ret = genaNotifyAllXML(device_handle, UDN, servId, propertySet);

ExitFunction:
    UpnpPrintf(UPNP_ALL, GENA, __FILE__, line, "genaNotifyAll ret = %d\n", ret);
    return ret;
}


/*!
 * \brief Returns OK message in the case of a subscription request.
 *
 * \return UPNP_E_SUCCESS if successful, otherwise the appropriate error code.
 */
static int respond_ok(MHDTransaction *mhdt, int time_out, subscription *sub,
                      const std::string& prodvers)
{
    std::ostringstream ts;

    if (time_out >= 0) {
        ts << "Second-" << time_out;
    } else {
        ts << "Second-" << "Second-infinite";
    }
    mhdt->httpstatus = HTTP_OK;
    mhdt->response =
        MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(mhdt->response,    "SID", sub->sid.c_str());
    MHD_add_response_header(mhdt->response,    "TIMEOUT", ts.str().c_str());
    MHD_add_response_header(mhdt->response,
                            "SERVER", get_sdk_device_info(prodvers).c_str());
    return UPNP_E_SUCCESS;
}


// Callstranger (CVE-2020-12695)) https://kb.cert.org/vuls/id/339275
//    The Open Connectivity Foundation UPnP specification
//    before 2020-04-17 does not forbid the acceptance of a
//    subscription request with a delivery URL on a different
//    network segment than the fully qualified
//    event-subscription URL, aka the CallStranger issue. This
//    tool checks for the vulnerability.
// So we need to check the delivery address against our own incoming
// connection address. TBD? currently, we just check that it matches
// one of our supported addresses, not necessarily the one this
// particular client connected to.
static bool callStrangerCheck(
    const std::string& surl, uri_type& temp, const NetIF::Interface *clnetif, NetIF::IPAddr& claddr)
{
    NetIF::IPAddr subsaddr(reinterpret_cast<struct sockaddr*>(&temp.hostport.IPaddress));
    if (!subsaddr.ok()) {
        UpnpPrintf(UPNP_INFO,GENA,__FILE__,__LINE__,"create_url_list: bad addr %s\n",surl.c_str());
        return false;
    }
    // For IPV6, we check that the address is link-local. We only
    // use a globally set network interface anyway, the client has
    // no way to send us elsewhere. For IPV4, we let NetIF find
    // the right interface (checking the address against the
    // IP/netmask for each network interface). If this succeeds,
    // and it's the same interface, we're ok
    if (subsaddr.family() == NetIF::IPAddr::Family::IPV6) {
        if (subsaddr.scopetype() != NetIF::IPAddr::Scope::LINK) {
            UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                       "create_url_list: not link-local: %s\n",surl.c_str());
            return false;
        }
    } else {
        NetIF::IPAddr hostaddr1;
        const auto subsnetif = NetIF::Interfaces::interfaceForAddress(subsaddr,g_netifs,hostaddr1);
        if (subsnetif != clnetif) {
            UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                       "create_url_list: diff. segment: client %s sub %s\n",
                       claddr.straddr().c_str(), surl.c_str());
            return false;
        }
    }
    return true;
}

/*!
 * \brief Function to parse the Callback header value in subscription requests.
 *
 * Takes in a buffer containing URLS delimited by '<' and '>'. The
 * entire buffer is copied into dynamic memory and stored in the
 * URL_list. Only URLs with network
 * addresses are considered (i.e. host:port or domain name).
 *
 * \return The number of URLs parsed.
 */
static int create_url_list(
    MHDTransaction *mhdt, const std::string& ulist, std::vector<std::string> *out)
{
    out->clear();

    // Get information for the client address
    NetIF::IPAddr claddr(reinterpret_cast<struct sockaddr*>(&mhdt->client_address));
    if (!claddr.ok()) {
        UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                   "create_url_list: can't determine client addr\n");
        return UPNP_E_INVALID_INTERFACE;
    }
    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
               "create_url_list: client address: %s\n",claddr.straddr().c_str());
    NetIF::IPAddr hostaddr;
    const auto clnetif = NetIF::Interfaces::interfaceForAddress(claddr, g_netifs, hostaddr);
    if (nullptr == clnetif) {
        UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                   "create_url_list: no interface for client address %s\n",
                   claddr.straddr().c_str());
        return UPNP_E_INVALID_INTERFACE;
    }

    std::string::size_type openpos = 0;
    std::string::size_type closepos = 0;
    for (;;) {
        if ((openpos = ulist.find('<', closepos)) == std::string::npos) {
            break;
        }
        if ((closepos = ulist.find('>', openpos)) == std::string::npos) {
            break;
        }
        uri_type temp;
        if (closepos - 1 <= openpos) {
            UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                       "create_url_list: malformed list %s\n", ulist.c_str());
            out->clear();
            return UPNP_E_INVALID_URL;
        }
        std::string surl = ulist.substr(openpos+1, closepos-openpos-1);
        if (parse_uri(surl, &temp) != UPNP_E_SUCCESS ||
            temp.hostport.text.empty()) {
            UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                       "create_url_list: bad url in list %s\n", surl.c_str());
            out->clear();
            return UPNP_E_INVALID_URL;
        }

        if (!callStrangerCheck(surl, temp, clnetif, claddr)) {
            out->clear();
            return UPNP_E_INVALID_URL;
        }

        // Rebuild a string url from the parsed one
        surl = uri_asurlstr(temp);

        // Maybe qualify the host with a scope id (IPV6 link-local
        // addresses). This returns surl if there is nothing to do.
        std::string qsurl = maybeScopeUrlAddr(surl.c_str(), temp, &claddr.getaddr());
        if (qsurl.empty()) {
            UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
                       "create_url_list: maybeScopeUrlAddr failed for %s\n", surl.c_str());
            continue;
        }

        UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "create_url_list: cb url ok:  %s\n", qsurl.c_str());
        out->push_back(std::move(qsurl));
    }
    return UPNP_E_SUCCESS;
}

void gena_process_subscription_request(MHDTransaction *mhdt)
{
    struct Upnp_Subscription_Request request_struct = {};
    int return_code = 1;
    int time_out = 1801;
    service_info *service;
    struct Handle_Info *handle_info;
    void *cookie;
    Upnp_FunPtr callback_fun;
    UpnpDevice_Handle device_handle;
    std::list<subscription>::iterator sub;

    UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
               "Subscription Request Received:\n");

    auto itnt = mhdt->headers.find("nt");
    if (itnt == mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        return;
    }
    std::string ntvalue(itnt->second);
    stringtolower(ntvalue);
    if (ntvalue != "upnp:event") {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        return;
    }

    /* if a SID is present, bad request "incompatible headers" */
    if (mhdt->headers.find("sid") != mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        return;
    }
    /* look up service by eventURL */
    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
               "SubscriptionRequest for event URL path: %s\n",mhdt->url.c_str());

    HandleLock();

    if (GetDeviceHandleInfoForPath(
            mhdt->url, &device_handle, &handle_info, &service) != HND_DEVICE) {
        http_SendStatusResponse(mhdt, HTTP_INTERNAL_SERVER_ERROR);
        HandleUnlock();
        return;
    }

    if (service == nullptr || !service->active) {
        http_SendStatusResponse(mhdt, HTTP_NOT_FOUND);
        HandleUnlock();
        return;
    }

    UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
               "Subscription Request: current subscriptions count %d max %d\n",
               service->TotalSubscriptions, handle_info->MaxSubscriptions);

    /* too many subscriptions */
    if (handle_info->MaxSubscriptions != -1 &&
        service->TotalSubscriptions >= handle_info->MaxSubscriptions) {
        http_SendStatusResponse(mhdt, HTTP_INTERNAL_SERVER_ERROR);
        HandleUnlock();
        return;
    }

    std::vector<std::string> tmpUrls;
    /* check for valid callbacks */
    {
        auto itcb = mhdt->headers.find("callback");
        if (itcb == mhdt->headers.end()) {
            http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
            HandleUnlock();
            return;
        }
        return_code = create_url_list(mhdt, itcb->second, &tmpUrls);
        if (return_code != UPNP_E_SUCCESS) {
            http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
            HandleUnlock();
            return;
        }
    }

    /* generate new subscription */
    sub = service->subscriptionList.emplace(service->subscriptionList.end());
    if (sub == service->subscriptionList.end()) {
        http_SendStatusResponse(mhdt, HTTP_INTERNAL_SERVER_ERROR);
        HandleUnlock();
        return;
    }

    sub->DeliveryURLs = tmpUrls;
    /* set the timeout */
    if (!timeout_header_value(mhdt->headers, &time_out)) {
        time_out = GENA_DEFAULT_TIMEOUT;
    }

    /* replace infinite timeout with max timeout, if possible */
    if (handle_info->MaxSubscriptionTimeOut != -1) {
        if (time_out == -1 || time_out > handle_info->MaxSubscriptionTimeOut) {
            time_out = handle_info->MaxSubscriptionTimeOut;
        }
    }
    if (time_out >= 0) {
        sub->expireTime = time(nullptr) + time_out;
    } else {
        /* infinite time */
        sub->expireTime = 0;
    }

    /* generate SID */
    sub->sid = std::string("uuid:") + gena_sid_uuid();

    /* respond OK */
    if (respond_ok(mhdt, time_out, &(*sub), handle_info->productversion) != UPNP_E_SUCCESS) {
        service->subscriptionList.pop_back();
        HandleUnlock();
        return;
    }
    service->TotalSubscriptions++;
    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
               "Subscription Request granted\n");

    /* finally generate callback for init table dump */
    request_struct.ServiceId = service->serviceId.c_str();
    request_struct.UDN = service->UDN.c_str();
    request_struct.Sid = sub->sid;

    /* copy callback */
    callback_fun = handle_info->Callback;
    cookie = handle_info->Cookie;

    HandleUnlock();

    /* make call back with request struct */
    /* in the future should find a way of mainting that the handle */
    /* is not unregistered in the middle of a callback */
    callback_fun(UPNP_EVENT_SUBSCRIPTION_REQUEST, &request_struct, cookie);
}


void gena_process_subscription_renewal_request(MHDTransaction *mhdt)
{
    subscription *sub;
    service_info *service;
    struct Handle_Info *handle_info;
    UpnpDevice_Handle device_handle;
    std::string event_url_path;

    /* if a CALLBACK or NT header is present, then it is an error */
    if (mhdt->headers.find("callback") != mhdt->headers.end() ||
        mhdt->headers.find("nt")  != mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        return;
    }

    /* get SID */
    auto itsid = mhdt->headers.find("sid");
    if (itsid == mhdt->headers.end() || itsid->second.size() > SID_SIZE) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        return;
    }

    Upnp_SID sid = itsid->second;

    HandleLock();

    if (GetDeviceHandleInfoForPath(
            mhdt->url, &device_handle, &handle_info, &service) != HND_DEVICE ) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        HandleUnlock();
        return;
    }

    /* get subscription */
    if(service == nullptr || !service->active ||
       ((sub = GetSubscriptionSID( sid, service )) == nullptr)) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        HandleUnlock();
        return;
    }

    UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
               "Renew Request: current subscriptions count %d max %d\n",
               service->TotalSubscriptions, handle_info->MaxSubscriptions);

    /* too many subscriptions */
    if(handle_info->MaxSubscriptions != -1 &&
       service->TotalSubscriptions > handle_info->MaxSubscriptions ) {
        http_SendStatusResponse(mhdt, HTTP_INTERNAL_SERVER_ERROR);
        RemoveSubscriptionSID(sub->sid, service);
        HandleUnlock();
        return;
    }
    /* set the timeout */
    int time_out;
    if (!timeout_header_value(mhdt->headers, &time_out)) {
        time_out = GENA_DEFAULT_TIMEOUT;
    }

    /* replace infinite timeout with max timeout, if possible */
    if (handle_info->MaxSubscriptionTimeOut != -1) {
        if(time_out == -1 || time_out > handle_info->MaxSubscriptionTimeOut) {
            time_out = handle_info->MaxSubscriptionTimeOut;
        }
    }

    if(time_out == -1) {
        sub->expireTime = 0;
    } else {
        sub->expireTime = time(nullptr) + time_out;
    }

    if (respond_ok(mhdt, time_out, sub, handle_info->productversion)
        != UPNP_E_SUCCESS) {
        RemoveSubscriptionSID(sub->sid, service);
    }

    HandleUnlock();
}


void gena_process_unsubscribe_request(MHDTransaction *mhdt)
{
    UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
                   "gena_process_unsubscribe_request\n");

    service_info *service;
    struct Handle_Info *handle_info;
    UpnpDevice_Handle device_handle;

    /* if a CALLBACK or NT header is present, then it is an error */
    if (mhdt->headers.find("callback") != mhdt->headers.end() ||
        mhdt->headers.find("nt")  != mhdt->headers.end()) {
        http_SendStatusResponse(mhdt, HTTP_BAD_REQUEST);
        return;
    }
    /* get SID */
    auto itsid = mhdt->headers.find("sid");
    if (itsid == mhdt->headers.end() || itsid->second.size() > SID_SIZE) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        return;
    }
    Upnp_SID sid = itsid->second;

    HandleLock();

    if (GetDeviceHandleInfoForPath(
            mhdt->url, &device_handle, &handle_info, &service) != HND_DEVICE) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        HandleUnlock();
        return;
    }

    /* validate service */
    if (service == nullptr || !service->active || GetSubscriptionSID(sid, service) == nullptr) {
        http_SendStatusResponse(mhdt, HTTP_PRECONDITION_FAILED);
        HandleUnlock();
        return;
    }

    RemoveSubscriptionSID(sid, service);
    http_SendStatusResponse(mhdt, HTTP_OK);

    HandleUnlock();
}

#endif /* INCLUDE_DEVICE_APIS */
#endif /* EXCLUDE_GENA */
