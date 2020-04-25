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

#include "upnpapi.h"

#include <mutex>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <unistd.h>

#include "md5.h"

static std::mutex uuid_mutex;

// We generate time-based pseudo-random uuids with a slight effort to
// avoid cross-host collisions (not that they would be likely in this
// context).
std::string gena_sid_uuid()
{
	std::lock_guard<std::mutex> mylock(uuid_mutex);

    auto now = std::chrono::high_resolution_clock::now();
	int64_t tp = now.time_since_epoch().count();

	std::string ip;
	unsigned int port;
	if (gIF_IPV4[0]) {
		ip = gIF_IPV4;
		port = LOCAL_PORT_V4;
	} else if (gIF_IPV6_ULA_GUA[0]) {
		ip = gIF_IPV6_ULA_GUA;
		port = LOCAL_PORT_V6;
	} else if (gIF_IPV6[0]) {
		ip = gIF_IPV6;
		port = LOCAL_PORT_V6;
	} else {
        port = 0;
    }
	
	std::ostringstream str;
	str << tp << "-" << getpid() << "-" << ip << "-" << port;
	// std::cerr << "UUID SOURCE [" << str.str() << "]\n";

	MD5_CTX c;
	unsigned char hash[16];
	MD5Init(&c);
	MD5Update(&c, (unsigned char *)str.str().c_str(), str.str().size());
	MD5Final(hash, &c);

	std::string out;
	// 4-2-2-2-6->20
    out.reserve(21);
    static const char hex[]="0123456789abcdef";
    for (int i = 0; i < 16; i++) {
		out.append(1, hex[hash[i] >> 4]);
		out.append(1, hex[hash[i] & 0x0f]);
		if (i==3 || i == 5 || i == 7 || i == 9) {
			out.append("-");
		}
    }
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return out;
}
