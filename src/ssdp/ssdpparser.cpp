/* Copyright (C) 2020 J.F.Dockes
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

#include "ssdpparser.h"

#include <iostream>
#include <cstring>
#include <mutex>
#include <string>

#include "upnpdebug.h"
#include "UpnpGlobal.h"

#if defined(_WIN32) && defined(_MSC_VER)
#define strcasecmp _stricmp
#endif

#define CCRLF "\r\n"

static const char *notify_start = "NOTIFY * HTTP/1.1\r\n";
static const size_t notify_start_len = strlen(notify_start);
static const char *msearch_start = "M-SEARCH * HTTP/1.1\r\n";
static const size_t msearch_start_len = strlen(msearch_start);
static const char *response_start = "HTTP/1.1 200 OK\r\n";
static const size_t response_start_len = strlen(response_start);


void SSDPPacketParser::trimright(char *cp, size_t len) {
    while (len > 0) {
        if (cp[len-1] == ' ' ||    cp[len-1] == '\t') {
            len--;
        } else {
            break;
        }
    }
    cp[len] = 0;
}

void SSDPPacketParser::dump(std::ostream& os) const {
    os <<
        " bootid " << (bootid ? bootid : "(null)") <<
        " nextbootid " << (nextbootid ? nextbootid : "(null)") <<
        " configid " << (configid ? configid : "(null)") <<
        " opt " << (opt ? opt : "(null)") <<
        " cache_control " << (cache_control ? cache_control : "(null)") <<
        " date " << (date ? date : "(null)") <<
        " ext " << (ext ? "true" : "false") <<
        " host " << (host ? host : "(null)") <<
        " location " << (location ? location : "(null)") <<
        " man " << (man ? man : "(null)") <<
        " method " << (method ? method : "(null)") <<
        " mx " << (mx ? mx : "(null)") <<
        " nt " << (nt ? nt : "(null)") <<
        " nts " << (nts ? nts : "(null)") <<
        " protocol " << (protocol ? protocol : "(null)") <<
        " searchport " << (searchport ? searchport : "(null)") <<
        " server " << (server ? server : "(null)") <<
        " st " << (st ? st : "(null)") <<
        " status " << (status ? status : "(null)") <<
        " url " << (url ? url : "(null)") <<
        " user_agent " << (user_agent ? user_agent : "(null)") <<
        " usn " << (usn ? usn : "(null)") <<
        " version " << (version ? version : "(null)") <<
        "\n";
}

bool SSDPPacketParser::parse()
{
    protocol = "HTTP";
    version = "1.1";
    char *cp;
    if (!strncmp(m_packet, notify_start, notify_start_len)) {
        method = "NOTIFY";
        url = "*";
        cp = m_packet + notify_start_len;
    } else if (!strncmp(m_packet, msearch_start, msearch_start_len)) {
        method = "M-SEARCH";
        url = "*";
        cp = m_packet + msearch_start_len;
    } else if (!strncmp(m_packet, response_start, response_start_len)) {
        isresponse = true;
        status  = "200";
        cp = m_packet + response_start_len;
    } else {
        UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                   "SSDP parser: bad first line in [%s]\n", m_packet);
        return false;
    }

    for (;;) {
        char *nm = cp;
        char *colon = strchr(cp, ':');
        if (nullptr == colon) {
            bool ret = strcmp(cp, "\r\n") == 0;
            if (!ret) {
                UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__, "SSDP parser: "
                           "no empty line at end of packet: [%s]\n", cp);
            }
            return ret;
        }

        *colon = 0;
        cp = colon + 1;
        // Get rid of white space after colon.
        while (*cp == ' ' || *cp == '\t') {
            cp++;
        }
        char *eol = strstr(cp, "\r\n");
        if (nullptr == eol) {
            // This is an error
            UpnpPrintf(UPNP_INFO, SSDP, __FILE__, __LINE__,
                       "SSDP parser: no EOL after: [%s]\n",   cp);
            break;
        }
        char *val = cp;
        *eol = 0;
        trimright(val, eol - val);
        cp = eol + 2;
        
        bool known{false};
        switch (nm[0]) {
        case 'b': case 'B':
            if (!strcasecmp("BOOTID.UPNP.ORG", nm)) {
                bootid = val; known = true;
            }
            break;
        case 'c': case 'C':
            if (!strcasecmp("CACHE-CONTROL", nm)) {
                cache_control = val; known = true;
            } else if (!strcasecmp("CONFIGID.UPNP.ORG", nm)) {
                configid = val; known = true;
            }
            break;
        case 'd': case 'D':
            if (!strcasecmp("DATE", nm)) {
                date = val; known = true;
            }
            break;
        case 'e': case 'E':
            if (!strcasecmp("EXT", nm)) {
                ext = true; known = true;
            }
            break;
        case 'h': case 'H':
            if (!strcasecmp("HOST", nm)) {
                host = val; known = true;
            }
            break;
        case 'l': case 'L':
            if (!strcasecmp("LOCATION", nm)) {
                location = val; known = true;
            }
            break;
        case 'm': case 'M':
            if (!strcasecmp("MAN", nm)) {
                man = val; known = true;
            } else if (!strcasecmp("MX", nm)) {
                mx = val; known = true;
            }
            break;
        case 'n': case 'N':
            if (!strcasecmp("NT", nm)) {
                nt = val; known = true;
            } else if (!strcasecmp("NTS", nm)) {
                nts = val; known = true;
            } else if (!strcasecmp("NEXTBOOTID.UPNP.ORG", nm)) {
                nextbootid = val; known = true;
            }
            break;
        case 'o': case 'O':
            if (!strcasecmp("OPT", nm)) {
                opt = val; known = true;
            }
            break;
        case 's': case 'S':
            if (!strcasecmp("SERVER", nm)) {
                server = val; known = true;
            } else if (!strcasecmp("ST", nm)) {
                st = val; known = true;
            } else if (!strcasecmp("SEARCHPORT.UPNP.ORG", nm)) {
                searchport = val; known = true;
            }
            break;
        case 'u': case 'U':
            if (!strcasecmp("USER-AGENT", nm)) {
                user_agent = val; known = true;
            } else if (!strcasecmp("USN", nm)) {
                usn = val; known = true;
            }
            break;
        default:
            break;
        }
        if (!known) {
            UpnpPrintf(UPNP_ALL, SSDP, __FILE__, __LINE__,
                       "SSDP parser: unknown header name [%s]\n", nm);
        }            
    }
    return false;
}
