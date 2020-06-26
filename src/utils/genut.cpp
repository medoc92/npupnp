/* Copyright (C) 2006-2019 J.F.Dockes
 *
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


#include "genut.h"

#include <cctype>
#include <list>
#include <numeric>
#include <vector>
#include <set>
#include <unordered_set>

using namespace std;


size_t upnp_strlcpy(char *dst, const char *src, size_t dsize)
{
    if (nullptr == dst || 0 == dsize)
        return strlen(src) + 1;

    // Copy until either output full or end of src. Final zero not copied
    size_t cnt = dsize;
    while (*src && cnt > 0) {
        *dst++ = *src++;
        cnt--;
    }

    if (cnt == 0) {
        // Stopped because output full. dst now points beyond the
        // buffer, set the final zero before it, and count how many
        // more bytes we would need.
        dst[-1] = 0;
        while (*src++) {
            dsize++;
        }
    } else {
        // Stopped because end of input, set the final zero.
        dst[0] = 0;
    }
    return dsize - cnt + 1;
}

string xmlQuote(const string& in)
{
    return std::accumulate(in.begin(), in.end(), string(""), [](const string& o, char i) { switch (i) {
         case '"':
              return o + "&quot;";
          case '&':
              return o + "&amp;";
          case '<':
              return o + "&lt;";
          case '>':
              return o + "&gt;";
          case '\'':
              return o + "&apos;";
          default:
              return o + i;
      } });
}

int dom_cmp_name(const std::string& domname, const std::string& ref)
{
    std::string::size_type colon = domname.find(':');
    return colon == std::string::npos ?
        domname.compare(ref) : domname.compare(colon+1, std::string::npos, ref);
}
