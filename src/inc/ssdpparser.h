#ifndef _SSDPARSE_H_
#define _SSDPARSE_H_
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

#include <iostream>

// Simple parser for an SSDP request or response packet.
class SSDPPacketParser {
public:
	// We take ownership of the argument, will modify and free it.
	SSDPPacketParser(char *packet) : m_packet(packet) {}

	~SSDPPacketParser() {
		free(m_packet);
	}

	bool parse();
	static void trimright(char *cp, size_t len);
	void dump(std::ostream& os);

	// Results. After parsing, the set fields point into the original buffer.
	bool isresponse{false};
	char *cache_control{nullptr};
	char *date{nullptr};
	bool  ext{false};
	char *host{nullptr};
	char *location{nullptr};
	char *man{nullptr};
	char *method{nullptr};
	char *mx{nullptr};
	char *nt{nullptr};
	char *nts{nullptr};
	char *protocol{nullptr};
	char *server{nullptr};
	char *st{nullptr};
	char *status{nullptr};
	char *url{nullptr};
	char *user_agent{nullptr};
	char *usn{nullptr};
	char *version{nullptr};
	
private:
	char *m_packet;
};

#endif /* _SSDPARSE_H_ */
