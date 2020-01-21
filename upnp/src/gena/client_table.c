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
 * Purpose: This file defines the functions for clients. It defines 
 * functions for adding and removing clients to and from the client table, 
 * adding and accessing subscription and other attributes pertaining to the 
 * client  
 ************************************************************************/


#include "config.h"


#ifdef INCLUDE_CLIENT_APIS
#if EXCLUDE_GENA == 0

#include "client_table.h"
#include "upnp_timeout.h"
#include "TimerThread.h"

extern TimerThread gTimerThread;

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

struct ClientSubscription {
	int m_renewEventId{0};
	std::string m_SID;
	std::string m_actualSID;
	std::string m_eventURL;
	ClientSubscription *m_next{nullptr};
};


/** Constructor */
ClientSubscription *UpnpClientSubscription_new()
{
	return new ClientSubscription;
}


/** Destructor */
void UpnpClientSubscription_delete(ClientSubscription *p)
{
	delete p;
}


/** Copy Constructor */
ClientSubscription *UpnpClientSubscription_dup(const ClientSubscription *p)
{
	ClientSubscription *q = UpnpClientSubscription_new();
	UpnpClientSubscription_assign(q, p);
	return q;
}


/** Assignment operator */
void UpnpClientSubscription_assign(ClientSubscription *q,
								   const ClientSubscription *p)
{
	if (q != p) {
		/* Do not copy RenewEventId */
		q->m_renewEventId = -1;
		UpnpClientSubscription_set_SID(q, UpnpClientSubscription_get_SID(p));
		UpnpClientSubscription_set_ActualSID(
			q, UpnpClientSubscription_get_ActualSID(p));
		UpnpClientSubscription_set_EventURL(
			q, UpnpClientSubscription_get_EventURL(p));
		/* Do not copy m_next */
		q->m_next = NULL;
	}
}


int UpnpClientSubscription_get_RenewEventId(const ClientSubscription *p)
{
	return p->m_renewEventId;
}


void UpnpClientSubscription_set_RenewEventId(ClientSubscription *p, int n)
{
	p->m_renewEventId = n;
}


const std::string& UpnpClientSubscription_get_SID(const ClientSubscription *p)
{
	return p->m_SID;
}

const char *UpnpClientSubscription_get_SID_cstr(const ClientSubscription *p)
{
	return UpnpClientSubscription_get_SID(p).c_str();
}


void UpnpClientSubscription_set_SID(ClientSubscription *p, const std::string& s)
{
	p->m_SID = s;
}


void UpnpClientSubscription_strcpy_SID(ClientSubscription *p, const char *s)
{
	p->m_SID = s;
}


const std::string& UpnpClientSubscription_get_ActualSID(
	const ClientSubscription *p)
{
	return p->m_actualSID;
}



void UpnpClientSubscription_set_ActualSID(ClientSubscription *p,
										  const std::string& s)
{
	p->m_actualSID = s;
}


void UpnpClientSubscription_strcpy_ActualSID(ClientSubscription *p,
											 const char *s)
{
	p->m_actualSID = s;
}


const std::string& UpnpClientSubscription_get_EventURL(
	const ClientSubscription *p)
{
	return p->m_eventURL;
}


void UpnpClientSubscription_set_EventURL(ClientSubscription *p,
										 const std::string& s)
{
	p->m_eventURL = s;
}


void UpnpClientSubscription_strcpy_EventURL(ClientSubscription *p, const char *s)
{
	p->m_eventURL = s;
}


ClientSubscription *UpnpClientSubscription_get_Next(const ClientSubscription *p)
{
	return p->m_next;
}


void UpnpClientSubscription_set_Next(ClientSubscription *p,
									 ClientSubscription *q)
{
	p->m_next = q;
}


void free_client_subscription(ClientSubscription *sub)
{
	upnp_timeout *event;
	ThreadPoolJob tempJob;
	if (sub) {
		int renewEventId = UpnpClientSubscription_get_RenewEventId(sub);
		UpnpClientSubscription_strcpy_ActualSID(sub, "");
		UpnpClientSubscription_strcpy_EventURL(sub, "");
		if (renewEventId != -1) {
			/* do not remove timer event of copy */
			/* invalid timer event id */
			if (TimerThreadRemove(&gTimerThread, renewEventId, &tempJob) == 0) {
				event = (upnp_timeout *)tempJob.arg;
				free_upnp_timeout(event);
			}
		}
		UpnpClientSubscription_set_RenewEventId(sub, -1);
	}
}


void freeClientSubList(ClientSubscription *list)
{
	ClientSubscription *next;
	while (list) {
		free_client_subscription(list);
		next = UpnpClientSubscription_get_Next(list);
		UpnpClientSubscription_delete(list);
		list = next;
	}
}


void RemoveClientSubClientSID(ClientSubscription **head, const std::string& sid)
{
	ClientSubscription *finger = *head;
	ClientSubscription *previous = NULL;
	int found = 0;
	while (finger) {
		found = sid == finger->m_SID;
		if (found) {
			if (previous) {
				UpnpClientSubscription_set_Next(previous,
					UpnpClientSubscription_get_Next(finger));
			} else {
				*head = UpnpClientSubscription_get_Next(finger);
			}
			UpnpClientSubscription_set_Next(finger, NULL);
			freeClientSubList(finger);
			finger = NULL;
		} else {
			previous = finger;
			finger = UpnpClientSubscription_get_Next(finger);
		}
	}
}


ClientSubscription *GetClientSubClientSID(ClientSubscription *head,
										  const std::string& sid)
{
	ClientSubscription *next = head;
	int found = 0;
	while (next) {
		found = next->m_SID == sid;
		if(found) {
			break;
		} else {
			next = UpnpClientSubscription_get_Next(next);
		}
	}

	return next;
}


ClientSubscription *GetClientSubActualSID(ClientSubscription *head,
										  const std::string& sid)
{
	ClientSubscription *next = head;
	while (next) {
		if (UpnpClientSubscription_get_ActualSID(next) == sid) {
			break;
		} else {
			next = UpnpClientSubscription_get_Next(next);
		}
	}

	return next;
}

#endif /* EXCLUDE_GENA */
#endif /* INCLUDE_CLIENT_APIS */

