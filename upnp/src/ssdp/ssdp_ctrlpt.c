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

/*!
 * \addtogroup SSDPlib
 *
 * @{
 * 
 * \file
 */

#include "config.h"


#ifdef INCLUDE_CLIENT_APIS
#if EXCLUDE_SSDP == 0

#include <string>
#include <sstream>
#include <iostream>

#include "ssdpparser.h"
#include "httputils.h"
#include "ssdp_ResultData.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "upnpapi.h"
#include "UpnpInet.h"
#include "ThreadPool.h"
#include "upnputil.h"
#include "smallut.h"

#ifdef WIN32
#include <string.h>
#define snprintf _snprintf
#endif /* WIN32 */

/*!
 * \brief Sends a callback to the control point application with a SEARCH
 * result.
 */
static void send_search_result(
	/* [in] Search reply from the device. */
	void *data)
{
	ResultData *temp = (ResultData *) data;

	temp->ctrlpt_callback(UPNP_DISCOVERY_SEARCH_RESULT, &temp->param,
						  temp->cookie);
	free(temp);
}

static void strnzcpy(char *dest, const char *src, size_t bufsize)
{
	strncpy(dest, src, bufsize-1);
	dest[bufsize-1] = 0;
}

void ssdp_handle_ctrlpt_msg(SSDPPacketParser& parser,
							struct sockaddr_storage *dest_addr,
							int timeout, void *cookie)
{
	int handle;
	struct Handle_Info *ctrlpt_info = NULL;
	int is_byebye;
	struct Upnp_Discovery param;
	SsdpEvent event;
	int nt_found;
	int usn_found;
	int st_found;
	Upnp_EventType event_type;
	Upnp_FunPtr ctrlpt_callback;
	void *ctrlpt_cookie;
	int matched = 0;
	ResultData *threadData = NULL;
	ThreadPoolJob job;

	memset(&job, 0, sizeof(job));

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

	/* Search timeout. JFD: I don't think that this is ever reached, I
	   can't find a call to this routine with timeout set. Probably
	   they forgot to code it... */
	if (timeout) {
		ctrlpt_callback(UPNP_DISCOVERY_SEARCH_TIMEOUT, NULL, cookie);
		return;
	}
	
	param.ErrCode = UPNP_E_SUCCESS;
	/* MAX-AGE, assume error */
	param.Expires = -1;
	if (parser.cache_control) {
		std::string s{parser.cache_control};
		stringtolower(s);
		char cb[2];
		if (sscanf(s.c_str(), "max-age = %d%1c", &param.Expires, cb) != 1) {
			std::cerr << "BAD CACHE-CONTROL value: ["  << parser.cache_control
					  << "]\n";
			return;
		}
	}
	/* DATE */
	param.Date[0] = '\0';
	if (parser.date) {
		strnzcpy(param.Date, parser.date, LINE_SIZE);
	}
	/* dest addr */
	memcpy(&param.DestAddr, dest_addr, sizeof(struct sockaddr_storage));
	/* EXT is upnp 1.0 compat. It has no value in 1.1 */
	param.Ext[0] = '\0';
	/* LOCATION */
	param.Location[0] = '\0';
	if (parser.location) {
		strnzcpy(param.Location, parser.location, LINE_SIZE);
	}
	/* SERVER / USER-AGENT */
	param.Os[0] = '\0';
	if (parser.server) {
		strnzcpy(param.Os, parser.server, LINE_SIZE);
	} else if (parser.user_agent) {
		strnzcpy(param.Os, parser.user_agent, LINE_SIZE);
	}
	/* clear everything */
	memset(param.DeviceId, 0, sizeof(param.DeviceId));
	memset(param.DeviceType, 0, sizeof(param.DeviceType));
	memset(param.ServiceType, 0, sizeof(param.ServiceType));
	/* not used; version is in ServiceType */
	param.ServiceVer[0] = '\0';
	event.UDN[0] = '\0';
	event.DeviceType[0] = '\0';
	event.ServiceType[0] = '\0';
	nt_found = FALSE;
	if (parser.nt) {
		nt_found = (ssdp_request_type(parser.nt, &event) == 0);
	}
	usn_found = FALSE;
	if (parser.usn) {
		usn_found = (unique_service_name(parser.usn, &event) == 0);
	}
	if (nt_found || usn_found) {
		strncpy(param.DeviceId, event.UDN, sizeof(param.DeviceId) - 1);
		strncpy(param.DeviceType, event.DeviceType,
				sizeof(param.DeviceType) - 1);
		strncpy(param.ServiceType, event.ServiceType,
				sizeof(param.ServiceType) - 1);
	}
	/* ADVERT. OR BYEBYE */
	if (!parser.isresponse) {
		/* use NTS hdr to determine advert., or byebye */
		if (!parser.nts) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
					   "NO NTS header in advert/byebye message\n");
			return;	/* error; NTS header not found */
		}
		if (!strcmp(parser.nts, "ssdp:alive")) {
			is_byebye = 0;
		} else if (!strcmp(parser.nts, "ssdp:byebye")) {
			is_byebye = 1;
		} else {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
					   "BAD NTS header [%s] in advert/byebye message\n",
					   parser.nts);
			return;	/* bad value */
		}
		if (is_byebye) {
			/* check device byebye */
			if (!nt_found || !usn_found) {
				UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
						   "SSDP BYE BYE no NT or USN !\n");
				return;	/* bad byebye */
			}
			event_type = UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE;
		} else {
			/* check advertisement.
			 * Expires is valid if positive. This is for testing
			 * only. Expires should be greater than 1800 (30 mins) */
			if (!nt_found || !usn_found ||
			    strlen(param.Location) == 0 || param.Expires <= 0) {
				return;	/* bad advertisement */
			}
			event_type = UPNP_DISCOVERY_ADVERTISEMENT_ALIVE;
		}
		/* call callback */
		ctrlpt_callback(event_type, &param, ctrlpt_cookie);
	} else {
		/* reply (to a SEARCH) */
		/* only checking to see if there is a valid ST header */
		st_found = FALSE;
		if (parser.st) {
			st_found = ssdp_request_type(parser.st, &event) == 0;
		}
		if (!parser.status || strcmp(parser.status, "200") ||
			param.Expires <= 0 ||
			strlen(param.Location) == 0 || !usn_found || !st_found) {
			return;	/* bad reply */
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
				matched = !strncmp(searchArg->searchTarget.c_str(),
								   parser.st, stlen);
				break;
			case SSDP_DEVICETYPE:
			{
				size_t m = MIN(stlen, searchArg->searchTarget.size());
				matched =!strncmp(searchArg->searchTarget.c_str(), parser.st, m);
				break;
			}
			case SSDP_SERVICE:
			{
				size_t m = MIN(stlen, searchArg->searchTarget.size());
				matched = !strncmp(searchArg->searchTarget.c_str(),parser.st, m);
				break;
			}
			default:
				matched = 0;
				break;
			}
			if (matched) {
				/* schedule call back */
				threadData = (ResultData *)malloc(sizeof(ResultData));
				if (threadData != NULL) {
					threadData->param = param;
					threadData->cookie = searchArg->cookie;
					threadData->ctrlpt_callback = ctrlpt_callback;
					TPJobInit(&job, (start_routine)send_search_result,
							  threadData);
					TPJobSetPriority(&job, MED_PRIORITY);
					TPJobSetFreeFunction(&job, (free_routine)free);
					if (ThreadPoolAdd(&gRecvThreadPool, &job, NULL) != 0) {
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
	static const char *man = "MAN: \"ssdp:discover\"\r\n";

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

	if (SearchTarget != NULL) {
		str << "ST: " << SearchTarget << "\r\n";
	}
	str << "\r\n";
	RqstBuf = str.str();
	return UPNP_E_SUCCESS;
}

/*!
 * \brief
 */
#ifdef UPNP_ENABLE_IPV6
static int CreateClientRequestPacketUlaGua(
	std::string& RqstBuf,
	int Mx,
	char *SearchTarget,
	int AddressFamily)
{
	return CreateClientRequestPacket(
		RqstBuf, Mx, SearchTarget,	AddressFamily, true);
}
#endif /* UPNP_ENABLE_IPV6 */

/*!
 * \brief
 */
static void searchExpired(
	/* [in] . */
	void *arg)
{
	int *id = (int *)arg;
	int handle = -1;
	struct Handle_Info *ctrlpt_info = NULL;

	/* remove search Target from list and call client back */
	Upnp_FunPtr ctrlpt_callback;
	void *cookie = NULL;
	int found = 0;

	HandleLock();

	/* remove search target from search list */
	if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
		free(id);
		HandleUnlock();
		return;
	}
	ctrlpt_callback = ctrlpt_info->Callback;
	for (auto it = ctrlpt_info->SsdpSearchList.begin();
		 it != ctrlpt_info->SsdpSearchList.begin(); it++) {
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
		ctrlpt_callback(UPNP_DISCOVERY_SEARCH_TIMEOUT, NULL, cookie);

	free(id);
}

int SearchByTarget(int Mx, char *St, void *Cookie)
{
	char errorBuffer[ERROR_BUFFER_LEN];
	int *id = NULL;
	int ret = 0;
	std::string ReqBufv4;
#ifdef UPNP_ENABLE_IPV6
	std::string ReqBufv6;
	std::string ReqBufv6UlaGua;
#endif
	struct sockaddr_storage __ss_v4;
#ifdef UPNP_ENABLE_IPV6
	struct sockaddr_storage __ss_v6;
#endif
	struct sockaddr_in *destAddr4 = (struct sockaddr_in *)&__ss_v4;
#ifdef UPNP_ENABLE_IPV6
	struct sockaddr_in6 *destAddr6 = (struct sockaddr_in6 *)&__ss_v6;
#endif
	fd_set wrSet;
	int timeTillRead = 0;
	int handle;
	struct Handle_Info *ctrlpt_info = NULL;
	enum SsdpSearchType requestType;
	unsigned long addrv4 = inet_addr(gIF_IPV4);
	SOCKET max_fd = 0;
	int retVal;

	/*ThreadData *ThData; */
	ThreadPoolJob job;

	memset(&job, 0, sizeof(job));

	requestType = ssdp_request_type1(St);
	if (requestType == SSDP_SERROR)
		return UPNP_E_INVALID_PARAM;
	UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
			   "Inside SearchByTarget\n");
	timeTillRead = Mx;
	if (timeTillRead < MIN_SEARCH_TIME)
		timeTillRead = MIN_SEARCH_TIME;
	else if (timeTillRead > MAX_SEARCH_TIME)
		timeTillRead = MAX_SEARCH_TIME;
	retVal = CreateClientRequestPacket(ReqBufv4, timeTillRead, St, AF_INET);
	if (retVal != UPNP_E_SUCCESS)
		return retVal;
#ifdef UPNP_ENABLE_IPV6
	retVal = CreateClientRequestPacket(ReqBufv6, timeTillRead, St, AF_INET6);
	if (retVal != UPNP_E_SUCCESS)
		return retVal;
	retVal = CreateClientRequestPacketUlaGua(ReqBufv6UlaGua,, timeTillRead,
											 St, AF_INET6);
	if (retVal != UPNP_E_SUCCESS)
		return retVal;
#endif

	memset(&__ss_v4, 0, sizeof(__ss_v4));
	destAddr4->sin_family = (sa_family_t)AF_INET;
	inet_pton(AF_INET, SSDP_IP, &destAddr4->sin_addr);
	destAddr4->sin_port = htons(SSDP_PORT);

#ifdef UPNP_ENABLE_IPV6
	memset(&__ss_v6, 0, sizeof(__ss_v6));
	destAddr6->sin6_family = (sa_family_t)AF_INET6;
	inet_pton(AF_INET6, SSDP_IPV6_SITELOCAL, &destAddr6->sin6_addr);
	destAddr6->sin6_port = htons(SSDP_PORT);
	destAddr6->sin6_scope_id = gIF_INDEX;
#endif

	/* add search criteria to list */
	HandleLock();
	if (GetClientHandleInfo(&handle, &ctrlpt_info) != HND_CLIENT) {
		HandleUnlock();
		return UPNP_E_INTERNAL_ERROR;
	}
	SsdpSearchArg *newArg = new SsdpSearchArg(St, Cookie, requestType);
	id = (int *)malloc(sizeof(int));
	TPJobInit(&job, (start_routine) searchExpired, id);
	TPJobSetPriority(&job, MED_PRIORITY);
	TPJobSetFreeFunction(&job, (free_routine) free);
	/* Schedule a timeout event to remove search Arg */
	gTimerThread->schedule(timeTillRead, REL_SEC, &job, SHORT_TERM, id);
	newArg->timeoutEventId = *id;
	ctrlpt_info->SsdpSearchList.push_back(newArg);
	HandleUnlock();
	/* End of lock */

	FD_ZERO(&wrSet);
	if (gSsdpReqSocket4 != INVALID_SOCKET) {
		setsockopt(gSsdpReqSocket4, IPPROTO_IP, IP_MULTICAST_IF,
				   (char *)&addrv4, sizeof(addrv4));
		FD_SET(gSsdpReqSocket4, &wrSet);
		max_fd = MAX(max_fd, gSsdpReqSocket4);
	}
#ifdef UPNP_ENABLE_IPV6
	if (gSsdpReqSocket6 != INVALID_SOCKET) {
		setsockopt(gSsdpReqSocket6, IPPROTO_IPV6, IPV6_MULTICAST_IF,
				   (char *)&gIF_INDEX, sizeof(gIF_INDEX));
		FD_SET(gSsdpReqSocket6, &wrSet);
		max_fd = MAX(max_fd, gSsdpReqSocket6);
	}
#endif
	ret = select(max_fd + 1, NULL, &wrSet, NULL, NULL);
	if (ret == -1) {
		posix_strerror_r(errno, errorBuffer, ERROR_BUFFER_LEN);
		UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
				   "SSDP_LIB: Error in select(): %s\n", errorBuffer);
		UpnpCloseSocket(gSsdpReqSocket4);
#ifdef UPNP_ENABLE_IPV6
		UpnpCloseSocket(gSsdpReqSocket6);
#endif
		return UPNP_E_INTERNAL_ERROR;
	}
#ifdef UPNP_ENABLE_IPV6
	if (gSsdpReqSocket6 != INVALID_SOCKET &&
	    FD_ISSET(gSsdpReqSocket6, &wrSet)) {
		int NumCopy = 0;

		while (NumCopy < NUM_SSDP_COPY) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
					   ">>> SSDP SEND M-SEARCH >>>\n%s\n",
					   ReqBufv6UlaGua.c_str());
			sendto(gSsdpReqSocket6,
			       ReqBufv6UlaGua.c_str(), ReqBufv6UlaGua.size(), 0,
			       (struct sockaddr *)&__ss_v6,
			       sizeof(struct sockaddr_in6));
			NumCopy++;
			imillisleep(SSDP_PAUSE);
		}
		NumCopy = 0;
		inet_pton(AF_INET6, SSDP_IPV6_LINKLOCAL, &destAddr6->sin6_addr);
		while (NumCopy < NUM_SSDP_COPY) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
					   ">>> SSDP SEND M-SEARCH >>>\n%s\n",
					   ReqBufv6.c_str());
			sendto(gSsdpReqSocket6,
			       ReqBufv6.c_str(), ReqBufv6.size(), 0,
			       (struct sockaddr *)&__ss_v6,
			       sizeof(struct sockaddr_in6));
			NumCopy++;
			imillisleep(SSDP_PAUSE);
		}
	}
#endif /* IPv6 */
	if (gSsdpReqSocket4 != INVALID_SOCKET &&
		FD_ISSET(gSsdpReqSocket4, &wrSet)) {
		int NumCopy = 0;
		while (NumCopy < NUM_SSDP_COPY) {
			UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
					   ">>> SSDP SEND M-SEARCH >>>\n%s\n",
					   ReqBufv4.c_str());
			sendto(gSsdpReqSocket4,
			       ReqBufv4.c_str(), ReqBufv4.size(), 0,
			       (struct sockaddr *)&__ss_v4,
			       sizeof(struct sockaddr_in));
			NumCopy++;
			imillisleep(SSDP_PAUSE);
		}
	}

	return 1;
}
#endif /* EXCLUDE_SSDP */
#endif /* INCLUDE_CLIENT_APIS */

/* @} SSDPlib */
