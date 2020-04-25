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


/************************************************************************
 * Purpose: This file defines the functions for services. It defines 
 * functions for adding and removing services to and from the service table, 
 * adding and accessing subscription and other attributes pertaining to the 
 * service 
 ************************************************************************/

#include "config.h"

#include <algorithm>
#include <iostream>

#include "service_table.h"

#ifdef INCLUDE_DEVICE_APIS

#if EXCLUDE_GENA == 0
/************************************************************************
 *	Function :	copy_subscription
 *
 *	Parameters :
 *		subscription *in ;	Source subscription
 *		subscription *out ;	Destination subscription
 *
 *	Description :	Makes a copy of the subscription
 *
 *	Return : int ;
 *		UPNP_E_SUCCESS - On success
 *
 *	Note :
 ************************************************************************/
int copy_subscription(subscription *in, subscription *out)
{
	memcpy(out->sid, in->sid, SID_SIZE);
	out->sid[SID_SIZE] = 0;
	out->ToSendEventKey = in->ToSendEventKey;
	out->expireTime = in->expireTime;
	out->active = in->active;
	out->DeliveryURLs = in->DeliveryURLs;
	return UPNP_E_SUCCESS;
}

/************************************************************************
 *	Function :	RemoveSubscriptionSID
 *
 *	Parameters :
 *		Upnp_SID sid ;	subscription ID
 *		service_info * service ;	service object providing the list of
 *						subscriptions
 *
 *	Description :	Remove the subscription represented by the
 *		const Upnp_SID sid parameter from the service table and update 
 *		the service table.
 *
 *	Return : void ;
 *
 *	Note :
 ************************************************************************/
void RemoveSubscriptionSID(Upnp_SID sid, service_info *service)
{
	UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__, "RemoveSubscriptionSID\n");
	auto& sublist(service->subscriptionList);
	for (auto it = sublist.begin(); it != sublist.end(); ) {
		if (!strcmp(sid, it->sid)) {
			UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
					   "RemoveSubscriptionSID: found\n");
			it = sublist.erase(it);
			service->TotalSubscriptions--;
		} else {
			it++;
		}
	}
}


subscription *GetSubscriptionSID(const Upnp_SID sid, service_info *service)
{
	auto& sublist(service->subscriptionList);
	auto found = find_if(sublist.begin(), sublist.end(),
						 [sid](const subscription& s)->bool{
							 return !strcmp(sid, s.sid);});
	if (found == sublist.end()) {
		return nullptr;
	}
	/*get the current_time */
	time_t current_time = time(0);
	if ((found->expireTime != 0) && (found->expireTime < current_time)) {
		UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
				   "GetSubscriptionSID: erasing expired subscription\n");
		sublist.erase(found);
		service->TotalSubscriptions--;
		return nullptr;
	} else {
		return &(*found);
	}
}

std::list<subscription>::iterator GetNextSubscription(
	service_info *service, std::list<subscription>::iterator current,
	bool getfirst)
{
	auto& sublist(service->subscriptionList);

	time_t current_time = time(0);
	if (!getfirst) {
		current++;
	}
	while (current != sublist.end()) {
		if ((current->expireTime != 0)
			&& (current->expireTime < current_time)) {
			UpnpPrintf(UPNP_DEBUG, GENA, __FILE__, __LINE__,
					   "GetNextSubscription: erasing expired subscription\n");
			current = sublist.erase(current);
			service->TotalSubscriptions--;
		} else if (current->active) {
			return current;
		} else {
			current++;
		}
	}
	return sublist.end();
}

std::list<subscription>::iterator GetFirstSubscription(service_info *service)
{
	auto& sublist(service->subscriptionList);
	return GetNextSubscription(service, sublist.begin(), true);
}

subscription::~subscription()
{
	freeSubscriptionQueuedEvents(this);
}

/************************************************************************
 *	Function :	FindServiceId
 *
 *	Parameters :
 *		service_table *table ;	service table
 *		const std::string& serviceId ;string representing the service id 
 *								to be found among those in the table	
 *		const std::string& UDN ;		string representing the UDN 
 *								to be found among those in the table	
 *
 *	Description :	Traverses through the service table and returns a 
 *		pointer to the service node that matches a known service  id 
 *		and a known UDN
 *
 *	Return : service_info * - pointer to the matching service_info node;
 *
 *	Note :
 ************************************************************************/
service_info *FindServiceId(
	service_table *table, const std::string& serviceId, const std::string& UDN)
{
	auto it = std::find_if(
		table->serviceList.begin(), table->serviceList.end(),
		[serviceId,UDN](const service_info& si)->bool{
			return !serviceId.compare(si.serviceId) && 
				!UDN.compare(si.UDN);});
	if (it == table->serviceList.end()) {
		return nullptr;
	} else {
		return &(*it);
	}
}

/************************************************************************
 *	Function :	FindServiceEventURLPath
 *
 *	Parameters :
 *		service_table *table ;	service table
 *		char * eventURLPath ;	event URL path used to find a service 
 *								from the table	
 *
 *	Description :	Traverses the service table and finds the node whose
 *		event URL Path matches a know value 
 *
 *	Return : service_info * - pointer to the service list node from the 
 *		service table whose event URL matches a known event URL;
 *
 *	Note :
 ************************************************************************/
service_info *FindServiceEventURLPath(
	service_table *table, const std::string& eventURLPath)
{
	if (nullptr == table) {
		return nullptr;
	}

	uri_type parsed_url_in;
	if (parse_uri(eventURLPath, &parsed_url_in)
		!= UPNP_E_SUCCESS) {
		return nullptr;
	}

	for (auto& entry : table->serviceList) {
		if (entry.eventURL.empty()) {
			continue;
		}
		uri_type parsed_url;
		if (parse_uri(entry.eventURL, &parsed_url) != UPNP_E_SUCCESS) {
			continue;
		}
		if (parsed_url.path == parsed_url_in.path &&
			parsed_url.query == parsed_url_in.query) {
			return &entry;
		}
	}

	return nullptr;
}
#endif /* EXCLUDE_GENA */

/************************************************************************
 *	Function :	FindServiceControlURLPath
 *
 *	Parameters :
 *		service_table * table ;	service table
 *		char * controlURLPath ;	control URL path used to find a service 
 *								from the table	
 *
 *	Description :	Traverses the service table and finds the node whose
 *		control URL Path matches a know value 
 *
 *	Return : service_info * - pointer to the service list node from the 
 *		service table whose control URL Path matches a known value;
 *
 *	Note :
 ************************************************************************/
#if EXCLUDE_SOAP == 0
service_info *FindServiceControlURLPath(
	service_table *table, const std::string& controlURLPath)
{
	if (nullptr == table) {
		return NULL;
	}
	
	uri_type parsed_url_in;
	if (parse_uri(controlURLPath, &parsed_url_in)
		!= UPNP_E_SUCCESS) {
		return nullptr;
	}

	for (auto& entry : table->serviceList) {
		if (entry.controlURL.empty()) {
			continue;
		}
		uri_type parsed_url;
		if ((parse_uri(entry.controlURL, &parsed_url) != UPNP_E_SUCCESS)) {
			continue;
		}
		if (parsed_url.path == parsed_url_in.path &&
			parsed_url.query == parsed_url_in.query) {
			return &entry;
		}
	}

	return nullptr;
}
#endif /* EXCLUDE_SOAP */

/************************************************************************
 *	Function :	printService
 *
 *	Parameters :
 *		service_info *service ;Service whose information is to be printed
 *		Upnp_LogLevel level ; Debug level specified to the print function
 *		Dbg_Module module ;	Debug module specified to the print function
 *
 *	Description :	For debugging purposes prints information from the 
 *		service passed into the function.
 *
 *	Return : void ;
 *
 *	Note :
 ************************************************************************/
#ifdef DEBUG
void printService(
	const service_info *service, Upnp_LogLevel level, Dbg_Module module)
{
	if(service) {
		UpnpPrintf(level, module, __FILE__, __LINE__,
				   "serviceType: %s\n", service->serviceType.c_str());
		UpnpPrintf(level, module, __FILE__, __LINE__,
				   "serviceId: %s\n", service->serviceId.c_str());
		UpnpPrintf(level, module, __FILE__, __LINE__,
				   "SCPDURL: %s\n", service->SCPDURL.c_str());
		UpnpPrintf(level, module, __FILE__, __LINE__,
				   "controlURL: %s\n", service->controlURL.c_str());
		UpnpPrintf(level, module, __FILE__, __LINE__,
				   "eventURL: %s\n", service->eventURL.c_str());
		UpnpPrintf(level, module, __FILE__, __LINE__,
				   "UDN: %s\n\n", service->UDN.c_str());
		if(service->active) {
			UpnpPrintf(level, module, __FILE__, __LINE__,
					   "Service is active\n");
		} else {
			UpnpPrintf(level, module, __FILE__, __LINE__,
					   "Service is inactive\n");
		}
	}
}

/************************************************************************
 *	Function :	printServiceList
 *
 *	Parameters :
 *		service_info *service ;	Service whose information is to be printed
 *		Upnp_LogLevel level ;	Debug level specified to the print function
 *		Dbg_Module module ;	Debug module specified to the print function
 *
 *	Description :	For debugging purposes prints information of each 
 *		service from the service table passed into the function.
 *
 *	Return : void ;
 *
 *	Note :
 ************************************************************************/
void printServiceList(
	service_table *table, Upnp_LogLevel level, Dbg_Module module)
{
	for (const auto& entry: table->serviceList) {
		printService(&entry, level, module);
	}
}

/************************************************************************
 *	Function :	printServiceTable
 *
 *	Parameters :
 *		service_table * table ;	Service table to be printed
 *		Upnp_LogLevel level ;	Debug level specified to the print function
 *		Dbg_Module module ;	Debug module specified to the print function
 *
 *	Description :	For debugging purposes prints the URL base of the table
 *		and information of each service from the service table passed into 
 *		the function.
 *
 *	Return : void ;
 *
 *	Note :
 ************************************************************************/
void printServiceTable(
	service_table * table,
	Upnp_LogLevel level,
	Dbg_Module module)
{
	UpnpPrintf(level, module, __FILE__, __LINE__,  "service_table:Services: \n");
	printServiceList(table, level, module);}
#endif

#if EXCLUDE_GENA == 0

/************************************************************************
 *	Function :	freeServiceTable
 *
 *	Parameters :
 *		service_table * table ;	Service table whose memory needs to be 
 *								freed
 *
 *	Description : Free's dynamic memory in table.
 *		(does not free table, only memory within the structure)
 *
 *	Return : void ;
 *
 *	Note :
 ************************************************************************/
void freeServiceTable(service_table *table)
{
	table->serviceList.clear();
}

/************************************************************************
 *	Function :	fillServiceList
 *
 *	Parameters :
 *		service_table stable ; entry to update.
 *
 *	Description: adds the device's services to the serviceList
 *
 *	Return:
 *
 *	Note :
 ************************************************************************/
static int fillServiceList(const UPnPDeviceDesc& dev, service_table *stable)
{

	for (const UPnPServiceDesc& sdesc : dev.services) {
		int fail = 0;
		std::list<service_info>::iterator current =
			stable->serviceList.emplace(stable->serviceList.end());
		current->active = 1;
		current->UDN = dev.UDN;
		current->serviceType = sdesc.serviceType;
		current->serviceId = sdesc.serviceId;
		current->SCPDURL = resolve_rel_url(dev.URLBase, sdesc.SCPDURL);
		//std::cerr<<"getServLst:SCPDURL: "<<current->SCPDURL<<std::endl;
		if (current->SCPDURL.empty()) {
			UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
					   "BAD OR MISSING SCPDURL");
		}
		current->controlURL = resolve_rel_url(dev.URLBase, sdesc.controlURL);
		//std::cerr<<"getServLst:controlURL: "<<current->controlURL<<std::endl;
		if (current->controlURL.empty()) {
			UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,"Bad/No CONTROL URL");
			fail = 1;
		}
		current->eventURL = resolve_rel_url(dev.URLBase, sdesc.eventSubURL);
		//std::cerr<<"getServLst:eventURL: "<<current->eventURL<<std::endl;
		if (current->eventURL.empty()) {
			UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__, "Bad/No EVENT URL");
		}
		if (fail) {
			stable->serviceList.erase(current);
		}
	}
	return !stable->serviceList.empty();
}

/************************************************************************
 * Function : initServiceTable
 *
 * Parameters :
 *  const UPnPDeviceDesc& devdesc, device description, with the service list.
 *	service_table *out serviceTable to initialize from the Description and URL
 *
 * Description : Set the urlbase and create the serviceList. Note that services
 *    for the root and all embedded devices are set on the same list.
 *
 * Return : 0 for failure, 1 if ok
 *
 ************************************************************************/
int initServiceTable(const UPnPDeviceDesc& devdesc,	service_table *out)
{
	out->serviceList.clear();
	fillServiceList(devdesc, out);
	for (const auto& dev : devdesc.embedded) {
		fillServiceList(dev, out);
	}
	return 1;
}

#endif /* EXCLUDE_GENA */

#endif /* INCLUDE_DEVICE_APIS */
