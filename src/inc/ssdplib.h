#ifndef SSDPLIB_H
#define SSDPLIB_H 

/**************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (C) 2011-2012 France Telecom All rights reserved. 
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

#include "miniserver.h"
#include "ssdpparser.h"
#include "upnp.h"
#include "upnpinet.h"

#include <string>

/* Standard defined values for the UPnP multicast addresses */
#define SSDP_IP   "239.255.255.250"
#define SSDP_IPV6_LINKLOCAL "FF02::C"
#define SSDP_IPV6_SITELOCAL "FF05::C"
#define SSDP_PORT 1900

/*! can be overwritten by configure CFLAGS argument. */
#ifndef X_USER_AGENT
/*! @name X_USER_AGENT
 *  The {\tt X_USER_AGENT} constant specifies the value of the X-User-Agent:
 *  HTTP header. The value "redsonic" is needed for the DSM-320. See
 *  https://sourceforge.net/forum/message.php?msg_id=3166856 for more
 * information
 */
#define X_USER_AGENT "redsonic"
#endif

/*! Enumeration to define all different types of ssdp searches */
typedef enum SsdpSearchType {
    /*! Unknown search command. */
    SSDP_SERROR = -1,
    SSDP_ALL,
    SSDP_ROOTDEVICE,
    SSDP_DEVICEUDN,
    SSDP_DEVICETYPE,
    SSDP_SERVICE
} SType;

// Struct used to remember what searches a client CP has outstanding.
// These are stored in a list on the client handle entry. We compare
// each incoming search response packet to what is on the list and
// call the client back if there is a match.
struct SsdpSearchArg {
    SsdpSearchArg(int i, const char* st, void* ck, SsdpSearchType rt)
        : timeoutEventId(i), requestType(rt), searchTarget(st), cookie(ck)
    {
    }
    // TimerThread timeout event used to find the search entry when
    // the timeout fires.
    int timeoutEventId;
    // The request type and search target are matched against incoming
    // search responses to determine if we need to call back.
    enum SsdpSearchType requestType;
    std::string searchTarget;
    // Client cookie for the callback
    void *cookie;
};

/* Storage for the data extracted from a received M-SEARCH Search
   Target (ST) or NOTIFY Notification Type (NT) header. */
struct SsdpEntity {
    enum SsdpSearchType RequestType{SSDP_SERROR};
    std::string UDN;
    std::string DeviceType;
    std::string ServiceType;
#if 0
    void dump(std::ostream& ostr) {
        ostr << " RequestType " << RequestType << " UDN " << UDN <<
            " DeviceType "<< DeviceType << " ServiceType " << ServiceType<<"\n";
    }
#endif
};

/* Globals */
#ifdef INCLUDE_CLIENT_APIS
extern SOCKET gSsdpReqSocket4;
#ifdef UPNP_ENABLE_IPV6
extern SOCKET gSsdpReqSocket6;
#endif /* UPNP_ENABLE_IPV6 */
#endif /* INCLUDE_CLIENT_APIS */


// Reasons for calling AvertiseAndReply
enum SSDPDevMessageType {MSGTYPE_SHUTDOWN, MSGTYPE_ADVERTISEMENT, MSGTYPE_REPLY};

/*!
 * \brief Sends SSDP device advertisements, replies and shutdown messages.
 *
 * \return UPNP_E_SUCCESS if successful else appropriate error.
 */
int AdvertiseAndReply(
    /* [in] Device handle. */
    UpnpDevice_Handle Hnd, 
    /* [in] Message type: notify alive/shutdown, or search reply */
    SSDPDevMessageType tp,
    /* [in] Advertisement max-age or search response random delay base. */
    int Exp,
    struct sockaddr_storage *repDestAddr,
    /* [in] Additional descriptive data for a search request */
    const SsdpEntity& sdata
);

/*!
 * \brief Fills the fields of the event structure like DeviceType, Device UDN
 * and Service Type.
 *
 * \return  0 if successful else -1.
 */
int unique_service_name(
    /* [in] Service Name string. */
    const char *cmd,
    /* [out] The SSDP event structure partially filled by all the
     * function. */
    SsdpEntity *Evt);

/*!
 * \brief This function figures out the type of the SSDP search in the in the
 * request.
 *
 * \return enum SsdpSearchType. Returns appropriate search type,
 * else returns SSDP_ERROR
 */
enum SsdpSearchType ssdp_request_type1(
    /* [in] command came in the ssdp request. */
    const char *cmd);

/*!
 * Interpret the NT or ST notification/Search type field in the parsed SSDP 
 * packet, and extract UDN/DeviceType/ServiceType if present.
 *
 * \return 0 on success; -1 on error.
 */
int ssdp_request_type(
    /* [in] command came in the ssdp request. */
    const char *cmd,
    /* [out] The event structure partially filled by this function. */
    SsdpEntity *Evt);

/*!
 * \brief This function reads the data from the ssdp socket.
 */
void readFromSSDPSocket(
    /* [in] SSDP socket. */
    SOCKET socket);

/*!
 * \brief Creates the IPv4 and IPv6 ssdp sockets required by the
 *  control point and device operation.
 *
 * \return UPNP_E_SUCCESS if successful else returns appropriate error.
 */
int get_ssdp_sockets(
    /* [out] Array of SSDP sockets. */
    MiniServerSockArray *out,
    /* [in] Port to listen on. -1 for default */
    int port);



/*!
 * \name SSDP Control Point Functions
 *
 * @{
 */

/*!
 * \brief This function handles the ssdp messages from the devices. These
 * messages includes the search replies, advertisement of device coming alive
 * and bye byes.
 */
void ssdp_handle_ctrlpt_msg(
    /* [in] SSDP message from the device. */
    SSDPPacketParser& parser,
    /* [in] Address of the device. */
    struct sockaddr_storage *dest_addr,
    /* [in] Cookie stored by the control point application. This cookie will
     * be returned to the control point in the callback.
     * Only in search reply. */
    void *cookie);

/*!
 * \brief Creates and send the search request for a specific URL.
 *
 * This function implements the search request of the discovery phase.
 * A M-SEARCH request is sent on the SSDP channel for both IPv4 and
 * IPv6 addresses. The search target(ST) is required and must be one of
 * the following:
 *     \li "ssdp:all" : Search for all devices and services.
 *     \li "ssdp:rootdevice" : Search for root devices only.
 *     \li "uuid:<device-uuid>" : Search for a particular device.
 *     \li "urn:schemas-upnp-org:device:<deviceType:v>"
 *     \li "urn:schemas-upnp-org:service:<serviceType:v>"
 *     \li "urn:<domain-name>:device:<deviceType:v>"
 *     \li "urn:<domain-name>:service:<serviceType:v>"
 *
 * \return 1 if successful else appropriate error.
 */
int SearchByTarget(
    /* [in] Number of seconds to wait, to collect all the responses. 0 if this is unicast search */
    int Mx,
    /* [in] Search target. */
    const char *St,
    /* [in] saddress are set iff Mx is 0, meaning that we request a unicast search */
    const char *saddress,
    int port,
    /* [in] Cookie provided by control point application. This cokie will
     * be returned to application in the callback. */
    void *Cookie);

/* @} SSDP Control Point Functions */

/*!
 * \brief Handles the search request. It does the sanity checks of the
 * request and then schedules a thread to send a random time reply
 * (random within maximum time given by the control point to reply).
 */
#ifdef INCLUDE_DEVICE_APIS

void ssdp_handle_device_request(
    /* [in] . */
    SSDPPacketParser& parser,
    /* [in] . */
    struct sockaddr_storage *dest_addr);

#else /* INCLUDE_DEVICE_APIS */

static UPNP_INLINE void ssdp_handle_device_request(
    SSDPPacketParser&, struct sockaddr_storage *) {}

#endif /* INCLUDE_DEVICE_APIS */

#endif /* SSDPLIB_H */
