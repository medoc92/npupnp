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
    explicit SSDPPacketParser(char *packet) : m_packet(packet) {}

    ~SSDPPacketParser() {
        free(m_packet);
    }

    SSDPPacketParser(const SSDPPacketParser&) = delete;
    SSDPPacketParser& operator=(const SSDPPacketParser&) = delete;

    bool parse();
    void dump(std::ostream& os) const;

    // Results. After parsing, the set fields point into the original buffer.
    bool isresponse{false};
    const char *bootid{nullptr};
    const char *cache_control{nullptr};
    const char *configid{nullptr};
    const char *date{nullptr};
    bool  ext{false};
    const char *host{nullptr};
    const char *location{nullptr};
    const char *man{nullptr};
    const char *method{nullptr};
    const char *mx{nullptr};
    const char *nextbootid{nullptr};
    const char *nt{nullptr};
    const char *nts{nullptr};
    const char *opt{nullptr};
    const char *protocol{nullptr};
    const char *searchport{nullptr};
    const char *server{nullptr};
    const char *st{nullptr};
    const char *status{nullptr};
    const char *url{nullptr};
    const char *user_agent{nullptr};
    const char *usn{nullptr};
    const char *version{nullptr};

private:
    char *m_packet;
};

#endif /* _SSDPARSE_H_ */
