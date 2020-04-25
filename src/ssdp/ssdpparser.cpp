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

#include "ssdpparser.h"

#include <iostream>
#include <string>
#include <regex>

#define CCRLF "\r\n"

static const std::string CRLF{CCRLF};
// As per RFC 2616 2.2 Basic Rules
static const std::string SEPARATORS{ "]()<>@,;:\\\"/[?={} \t"};
static const std::string WS{"[ \t]+"};
static const std::string WSMAYBE{"[ \t]*"};

// Start token
static const std::string sttoken_s = std::string("^([^") + SEPARATORS + "]+)";

// Header line
static const std::string header_res
= sttoken_s + ":" + WSMAYBE + "([^\r]+)?\r\n";

// Request line
static const std::string request_res
= sttoken_s + WS + "([^ \t]+)" + WS + "([A-Za-z]+)/([0-9]+.[0-9]+)[ \t]*\r\n";

// Response line
static const std::string response_res
=sttoken_s + "/" + "([0-9].[0-9])" + WS + "([0-9]+)" + WS + "([^\r]+)" + "\r\n";

static std::regex header_re(header_res, std::regex_constants::extended);
static std::regex request_re(request_res, std::regex_constants::extended);
static std::regex response_re(response_res, std::regex_constants::extended);

void SSDPPacketParser::trimright(char *cp, size_t len) {
	while (len > 0) {
		if (cp[len-1] == ' ' ||	cp[len-1] == '\t') {
			len--;
		} else {
			break;
		}
	}
	cp[len] = 0;
}

void SSDPPacketParser::dump(std::ostream& os) {
	os <<
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
		" server " << (server ? server : "(null)") <<
		" st " << (st ? st : "(null)") <<
		" status " << (status ? status : "(null)") <<
		" url " << (url ? url : "(null)") <<
		" user_agent " << (user_agent ? user_agent : "(null)") <<
		" usn " << (usn ? usn : "(null)") <<
		" version " << (version ? version : "(null)") <<
		std::endl;
}

bool SSDPPacketParser::parse()
{
	std::cmatch m;
	if (regex_search(m_packet, m, request_re)) {
		method = (m_packet + m.position(1));
		method[m[1].length()] = 0;
		url = (m_packet + m.position(2));
		url[m[2].length()] = 0;
		protocol = (m_packet + m.position(3));
		protocol[m[3].length()] = 0;
		version = (m_packet + m.position(4));
		version[m[4].length()] = 0;
	} else if (regex_search(m_packet, m, response_re)) {
		isresponse = true;
		protocol = (m_packet + m.position(1));
		protocol[m[1].length()] = 0;
		version = (m_packet + m.position(2));
		version[m[2].length()] = 0;
		status  = (m_packet + m.position(3));
		status[m[3].length()] = 0;
	} else {
		//std::cerr << "NO match for msearch request/response line\n";
		return false;
	}
		
	char *cp = m_packet+ m.length();
	
	for (;;) {
		if (!regex_search(cp, m, header_re)) {
			break;
		}

		char *nm = (cp + m.position(1));
		nm[m[1].length()] = 0;

		char *val = (cp + m.position(2));
		val[m[2].length()] = 0;
		trimright(val, m[2].length());

		bool known{false};
		switch (nm[0]) {
		case 'c': case 'C':
			if (!strcasecmp("CACHE-CONTROL", nm)) {
				cache_control = val; known = true;
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
			}
			break;
		case 's': case 'S':
			if (!strcasecmp("SERVER", nm)) {
				server = val; known = true;
			} else if (!strcasecmp("ST", nm)) {
				st = val; known = true;
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
		known = known;
#if 0
		if (known) {
			cerr << "NM [" << nm << "] VAL [" << val << "]\n";
		} else { 
			cerr << "Unknown header name [" << nm << "]\n";
		}
#endif
		cp += m.length();
	}

	return strcmp(cp, "\r\n") == 0;
}
