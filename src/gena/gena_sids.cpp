/****************************************************************************
 *
 * Copyright (c) 2020 J.F. Dockes <jf@dockes.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer. 
 * * Redistributions in binary form must reproduce the above copyright
 *   notice,this list of conditions and the following disclaimer in the 
 *   documentation and/or other materials provided with the distribution. 
 * * Neither name of Intel Corporation nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software 
 *   without specific prior written permission.
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

#include "gena_sids.h"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>

#ifndef _MSC_VER
#include <unistd.h>
#else
#include <process.h>
#endif

#include "md5.h"
#include "netif.h"

static std::mutex uuid_mutex;

// We generate time-based pseudo-random uuids with a slight effort to
// avoid cross-host collisions, based on a random number (which is
// what the old pupnp did). It now based on an interface ethernet
// address, if possible, random number else.
std::string gena_sid_uuid()
{
    std::scoped_lock mylock(uuid_mutex);

    auto now = std::chrono::high_resolution_clock::now();
    int64_t tp = now.time_since_epoch().count();

    static int counter;
    counter++;

    static std::string hwaddr;
    if (hwaddr.empty()) {
        NetIF::Interfaces *ifs = NetIF::Interfaces::theInterfaces();
        NetIF::Interfaces::Filter filt;
            filt.needs = {NetIF::Interface::Flags::HASHWADDR,
                          NetIF::Interface::Flags::HASIPV4};
            filt.rejects = {NetIF::Interface::Flags::LOOPBACK};

        auto selected = ifs->select(filt);
        for (const auto& entry : selected) {
            hwaddr = entry.gethexhwaddr();
            if (!hwaddr.empty()) {
                break;
            }
        }
        if (hwaddr.empty()) {
            srand(static_cast<unsigned int>(tp & 0xffffffff));
            hwaddr = std::to_string(rand());
        }
    }

    std::ostringstream str;
    str << tp << getpid() << counter << hwaddr;

    MD5_CTX c;
    unsigned char hash[16];
    MD5Init(&c);
    MD5Update(&c, reinterpret_cast<const unsigned char*>(str.str().c_str()), str.str().size());
    MD5Final(hash, &c);

    std::string out;
    out.reserve(37);
    static const char hex[]="0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out.append(1, hex[hash[i] >> 4]);
        out.append(1, hex[hash[i] & 0x0f]);
        if (i==3 || i == 5 || i == 7 || i == 9) {
            out.append("-");
        }
    }
    return out;
}
