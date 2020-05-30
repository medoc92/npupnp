/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
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

#include <sstream>

#include "gena.h"
#include "gena_device.h"
#include "gena_ctrlpt.h"

#include "httputils.h"
#include "statcodes.h"

/************************************************************************
 * Function : genaCallback                                    
 *                                                                    
 * Parameters:                                                        
 *
 * Description:                                                        
 *    This is the callback function called by the miniserver to handle 
 *    incoming GENA requests. 
 *
 * Returns: int
 *    UPNP_E_SUCCESS if successful else appropriate error
 ***************************************************************************/
void genaCallback(MHDTransaction *mhdt)
{
    bool found_function{false};

    if (mhdt->method == HTTPMETHOD_SUBSCRIBE) {
#ifdef INCLUDE_DEVICE_APIS
        found_function = true;
        auto it = mhdt->headers.find("nt");
        if (it == mhdt->headers.end()) {
            /* renew subscription */
            gena_process_subscription_renewal_request(mhdt);
        } else {
            /* subscribe */
            gena_process_subscription_request(mhdt);
        }
        UpnpPrintf(UPNP_ALL, GENA, __FILE__, __LINE__,
                   "got subscription request\n");
    } else if(mhdt->method == HTTPMETHOD_UNSUBSCRIBE) {
        found_function = true;
        /* unsubscribe */
        gena_process_unsubscribe_request(mhdt);
#endif
    } else if (mhdt->method == HTTPMETHOD_NOTIFY) {
#ifdef INCLUDE_CLIENT_APIS
        found_function = true;
        /* notify */
        gena_process_notification_event(mhdt);
#endif
    }

    if (!found_function) {
        /* handle missing functions of device or ctrl pt */
        http_SendStatusResponse(mhdt, HTTP_NOT_IMPLEMENTED);
    }
}
#endif /* EXCLUDE_GENA */

