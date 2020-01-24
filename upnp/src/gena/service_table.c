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
#include <upnp/ixml.h>

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
	auto& sublist(service->subscriptionList);
	for (auto it = sublist.begin(); it != sublist.end(); ) {
		if (!strcmp(sid, it->sid)) {
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
	service_table *table, const char *eventURLPath)
{
	if (nullptr == table) {
		return nullptr;
	}

	uri_type parsed_url_in;
	if (parse_uri(eventURLPath, strlen(eventURLPath), &parsed_url_in)
		!= UPNP_E_SUCCESS) {
		return nullptr;
	}

	for (auto& entry : table->serviceList) {
		if (entry.eventURL.empty()) {
			continue;
		}
		uri_type parsed_url;
		if (parse_uri(entry.eventURL.c_str(), entry.eventURL.size(),
					  &parsed_url) != UPNP_E_SUCCESS) {
			continue;
		}

		if (parsed_url.pathquery == parsed_url_in.pathquery) {
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
	service_table *table, const char *controlURLPath)
{
	if (nullptr == table) {
		return NULL;
	}
	
	uri_type parsed_url_in;
	if (parse_uri(controlURLPath, strlen(controlURLPath), &parsed_url_in)
		!= UPNP_E_SUCCESS) {
		return nullptr;
	}

	for (auto& entry : table->serviceList) {
		if (entry.controlURL.empty()) {
			continue;
		}
		uri_type parsed_url;
		if ((parse_uri(entry.controlURL.c_str(), entry.controlURL.size(),
					   &parsed_url) != UPNP_E_SUCCESS)) {
			continue;
		}
		if (parsed_url.pathquery == parsed_url_in.pathquery) {
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
	UpnpPrintf(level, module, __FILE__, __LINE__,
			   "URL_BASE: %s\n", table->URLBase.c_str());
	UpnpPrintf(level, module, __FILE__, __LINE__,  "Services: \n");
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
 *	Function :	getElementValue
 *
 *	Parameters :
 *		IXML_Node *node ;	Input node which provides the list of child 
 *							nodes
 *
 *	Description :	Returns the clone of the element value
 *
 *	Return : std::string
 *
 *	Note : value must be freed with DOMString_free
 ************************************************************************/
static std::string getElementValue(IXML_Node * node)
{
	IXML_Node *child = (IXML_Node *)ixmlNode_getFirstChild(node);
	if (child && ixmlNode_getNodeType(child) == eTEXT_NODE) {
		return ixmlNode_getNodeValue(child);
	} else {
		return std::string();
	}
}

/************************************************************************
 *	Function :	getSubElement
 *
 *	Parameters :
 *		const char *element_name ;	sub element name to be searched for
 *		IXML_Node *node ;	Input node which provides the list of child 
 *							nodes
 *		IXML_Node **out ;	Ouput node to which the matched child node is
 *							returned.
 *
 *	Description :	Traverses through a list of XML nodes to find the 
 *		node with the known element name.
 *
 *	Return : int ;
 *		1 - On Success
 *		0 - On Failure
 *
 *	Note :
 ************************************************************************/
int getSubElement(const char *element_name, IXML_Node *node, IXML_Node **out)
{
	IXML_Node *child = (IXML_Node *) ixmlNode_getFirstChild(node);

	*out = nullptr;

	while  (child != nullptr) {
		switch (ixmlNode_getNodeType(child)) {
		case eELEMENT_NODE:
		{
			const DOMString NodeName = ixmlNode_getNodeName(child);
			if(!strcmp(NodeName, element_name)) {
				*out = child;
				return 1;
			}
		}
		break;

		default:
			break;
		}
		child = (IXML_Node *)ixmlNode_getNextSibling(child);
	}

	return 0;
}

/************************************************************************
 *	Function :	getServiceList
 *
 *	Parameters :
 *		IXML_Node *node ;	"device" element.
 *		service_table stable ; entry to update.
 *
 *	Description: adds the device's services to the serviceList
 *
 *	Return:
 *
 *	Note :
 ************************************************************************/
int getServiceList(IXML_Node *node, service_table *stable)
{
	IXML_Node *serviceList = NULL;
	IXML_Node *current_service = NULL;
	IXML_Node *UDN = NULL;
	IXML_Node *serviceType = NULL;
	IXML_Node *serviceId = NULL;
	IXML_Node *SCPDURL = NULL;
	IXML_Node *controlURL = NULL;
	IXML_Node *eventURL = NULL;
	std::string tempDOMString;
	IXML_NodeList *serviceNodeList = NULL;

	long unsigned int i = 0lu;
	int fail = 0;

	if (!getSubElement("UDN", node, &UDN) ||
		!getSubElement("serviceList", node, &serviceList)) {
		return 0;
	}

	serviceNodeList = ixmlElement_getElementsByTagName(
		(IXML_Element *)serviceList, "service");
	if (!serviceNodeList) {
		return 0;
	}

	for (i = 0; i < ixmlNodeList_length(serviceNodeList); i++) {
		current_service = ixmlNodeList_item(serviceNodeList, i);
		fail = 0;
		std::list<service_info>::iterator current =
			stable->serviceList.emplace(stable->serviceList.end());
		current->active = 1;
		if ((current->UDN = getElementValue(UDN)).empty())
			fail = 1;
		if (!getSubElement("serviceType", current_service, &serviceType) ||
			(current->serviceType = getElementValue(serviceType)).empty())
			fail = 1;
		if (!getSubElement("serviceId", current_service, &serviceId) ||
			(current->serviceId = getElementValue(serviceId)).empty())
			fail = 1;
		if (!getSubElement("SCPDURL", current_service, &SCPDURL) ||
			(tempDOMString = getElementValue(SCPDURL)).empty() ||
			(current->SCPDURL = resolve_rel_url(
				stable->URLBase.c_str(), tempDOMString.c_str())).empty())
			fail = 1;
		if (!(getSubElement("controlURL", current_service, &controlURL)) ||
			(tempDOMString = getElementValue(controlURL)).empty() ||
			(current->controlURL = resolve_rel_url(
				stable->URLBase.c_str(), tempDOMString.c_str())).empty()) {
			UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
					   "BAD OR MISSING CONTROL URL");
			UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
					   "CONTROL URL SET TO NULL IN SERVICE INFO");
			fail = 1;
		}
		if (!getSubElement("eventSubURL", current_service, &eventURL) ||
			(tempDOMString = getElementValue(eventURL)).empty() ||
			(current->eventURL = resolve_rel_url(
				stable->URLBase.c_str(), tempDOMString.c_str())).empty()) {
			UpnpPrintf(UPNP_INFO, GENA, __FILE__, __LINE__,
					   "BAD OR MISSING EVENT URL");
			UpnpPrintf(UPNP_INFO, GENA, __FILE__,  __LINE__,
					   "EVENT URL SET TO NULL IN SERVICE INFO");
			fail = 1;
		}
		if (fail) {
			stable->serviceList.erase(current);
		}
	}
	ixmlNodeList_free(serviceNodeList);

	return stable->serviceList.size() != 0;
}

/************************************************************************
 * Function : getAllServiceList
 *
 * Parameters :
 *	IXML_Node *node ;	description "root" node.
 *	service_table servtable the service_table on which to update the services
 *
 * Description:
 *
 * Return : 
 *
 * Note :
 ************************************************************************/
int getAllServiceList(IXML_Node *node, service_table *servtable)
{
	servtable->serviceList.clear();

	IXML_NodeList *deviceList = ixmlElement_getElementsByTagName(
		(IXML_Element *)node, "device");
	if (nullptr == deviceList) {
		return 0;
	}

	for (long unsigned int i = 0; i < ixmlNodeList_length(deviceList); i++) {
		IXML_Node *currentDevice = ixmlNodeList_item(deviceList, i);
		getServiceList(currentDevice, servtable);
	}

	ixmlNodeList_free(deviceList);
	return 1;
}

/************************************************************************
 *	Function :	removeServiceTable
 *
 *	Parameters :
 *		IXML_Node *node ;	description top node.
 *		service_table *in ;	service table from which services will be 
 *							removed
 *
 *	Description :	This function assumes that services for a particular 
 *		root device are placed linearly in the service table, and in the 
 *		order in which they are found in the description document
 *		all services for this root device are removed from the list
 *
 *	Return : int ;
 *
 *	Note :
 ************************************************************************/
int removeServiceTable(IXML_Node *node, service_table *stable)
{
	IXML_Node *root = NULL;
	IXML_Node *currentUDN = NULL;
	auto& servlist = stable->serviceList;

	if (!getSubElement("root", node, &root)) {
		return 1;
	}
	IXML_NodeList *deviceList = 
		ixmlElement_getElementsByTagName((IXML_Element *) root, "device");
	if (deviceList == NULL) {
		return 1;
	}

	for (unsigned i = 0; i < ixmlNodeList_length(deviceList); i++) {
		std::string UDN;
		if (!getSubElement("UDN", node, &currentUDN)
			|| (UDN = getElementValue(currentUDN)).empty()) {
			continue;
		}

		/*There used to be an optimization based on the order of
		  creation of Services. This was somewhat fragile, and not
		  very useful as these lists are short anyway. Now always
		  start from the bottom */
		auto current = servlist.begin();
		while (current != servlist.end()) {
			if (current->UDN == UDN) {
				current = servlist.erase(current);
			} else {
				current++;
			}
		}
	}

	ixmlNodeList_free(deviceList);
	return 1;
}


/************************************************************************
 * Function : getServiceTable
 *
 * Parameters :
 *	IXML_Node *node ;	XML node information
 *	service_table *out ;	output parameter which will contain the
 *				service list and URL
 *	const char *DefaultURLBase ; Default base URL on which the URL
 *				will be returned.
 *
 * Description : Create service table from description document
 *
 * Return : 0 for failure, 1 if ok
 *
 ************************************************************************/
int getServiceTable(
	IXML_Node *node, service_table *out, const char *DefaultURLBase)
{
	IXML_Node *root = NULL;
	if (!getSubElement("root", node, &root)) {
		return 0;
	}

	IXML_Node *URLBase;
	if (getSubElement("URLBase", root, &URLBase)) {
		out->URLBase = getElementValue(URLBase);
	} else {
		if (DefaultURLBase) {
			out->URLBase = DefaultURLBase;
		}
	}

	return getAllServiceList(root, out);
}

#endif /* EXCLUDE_GENA */

#endif /* INCLUDE_DEVICE_APIS */
