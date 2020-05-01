/*******************************************************************************
 *
 * Copyright (c) 2019 J.F. Dockes
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
#ifndef _SMALLUT_H_INCLUDED_
#define _SMALLUT_H_INCLUDED_

#include <string.h>

extern size_t upnp_strlcpy(char *dst, const char *src, size_t dsize);

/* Size of the errorBuffer variable, passed to the strerror_r() function */
#define ERROR_BUFFER_LEN (size_t)256

#if !defined(_WIN32)
inline char *_check_strerror_r(int, char *errbuf) {
	return errbuf;
}
inline char *_check_strerror_r(char *cp, char *) {
	return cp;
}
inline int posix_strerror_r(int err, char *buf, size_t len) {
	char *cp = _check_strerror_r(strerror_r(err, buf, len), buf);
	if (cp != buf) {
		upnp_strlcpy(buf, cp, len);
	}
	return 0;
}
#else /* -> _WIN32 */

#define posix_strerror_r(errno,buf,len) strerror_s(buf,len,errno)

#ifndef PRIu64
#define PRIu64 "I64u"
#define PRIi64 "I64i"
#endif /* PRIu64 */

#endif /* _WIN32 */

#ifdef __cplusplus
#include <string>

extern void stringtolower(std::string& io);
extern std::string stringtolower(const std::string& i);
extern int stringlowercmp(const std::string& s1,
                          const std::string& s2);
/** Remove instances of characters belonging to set (default {space,
    tab}) at beginning and end of input string */
extern void trimstring(std::string& s, const char *ws = " \t");
extern void rtrimstring(std::string& s, const char *ws = " \t");
extern void ltrimstring(std::string& s, const char *ws = " \t");

inline size_t upnp_strlcpy(char *dst, const std::string& src, size_t dsize) {
	return upnp_strlcpy(dst, src.c_str(), dsize);
}

std::string xmlQuote(const std::string& in);

/* Compare element names, ignoring namespaces */
int dom_cmp_name(const std::string& domname, const std::string& ref);

#endif /* __cplusplus */

#endif /* _SMALLUT_H_INCLUDED_ */
