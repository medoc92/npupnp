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

#ifndef GENLIB_NET_URI_H
#define GENLIB_NET_URI_H

#include <string>
#include <vector>

#include "UpnpGlobal.h" 
#include "UpnpInet.h"

#include <string.h>
#include <sys/types.h>

#ifdef _WIN32
#include <Winsock2.h>
#else
#include <sys/socket.h>
#endif

/*!
 * \brief Represents a host port: e.g. "127.127.0.1:80", "www.recoll.org" 
*/
struct hostport_type {
    hostport_type() {
        IPaddress = {};
    }
    /*! Full "host:port" or "host" text. This is mostly useful when it is
      separated from the rest of an URL parse_hostport() */
    std::string text;
    /*! Separate host copy: original string value, before a possible name resolution. */
    std::string strhost;
    /*! Set to true by parse_hostport if strhost is a host name instead of an IP address */
    bool hostisname{false};
    /*! Possibly empty separated port string */
    std::string strport;
    /* Address as computed by inet_pton() from a numeric address or
     * getaddrinfo() from a host name. May not be set if
     * parse_hostport was called with noresolve==true and we had a
     * host name */
    struct sockaddr_storage IPaddress;
};

/*!
 * \brief Parse a string representing a host and port (e.g. "127.127.0.1:80"
 * or "localhost"), *possibly followed by the rest of an URL* and fill
 * out a hostport_type struct.
 */
int parse_hostport(
    /*! [in] String of characters representing host and port, e.g. 192.168.4.1:49152,
      [fe80::224:1dff:fede:6868]:49152, www.recoll.org */
    const char *in,
    /*! [out] Parsed output. Validated syntax, separate host, port and host:port strings, 
     *  possibly computed binary  address. */
    hostport_type *out,
    /*! [in] Do not call the resolver if the input contains a host name */
    bool noresolve = false
    );

enum uriType  {
    URITP_ABSOLUTE,
    URITP_RELATIVE
};

enum pathType {
    ABS_PATH,
    REL_PATH,
    OPAQUE_PART
};

/*!
 * \brief Represents a URI used in parse_uri and elsewhere
 */
struct uri_type {
    enum uriType type;
    std::string scheme;
    enum pathType path_type;
    std::string path;
    std::string query;
    std::string fragment;
    hostport_type hostport;
};

inline std::string uri_asurlstr(const uri_type& u)
{
    std::string surl(u.scheme);
    if (!u.scheme.empty()) {
        surl += ":";
    }
    if (!u.hostport.text.empty()) {
        surl += "//";
        surl += u.hostport.text;
    }
    if (u.path.empty())
        surl += "/";
    else 
        surl += u.path;
    if (!u.query.empty()) {
        surl += "?";
        surl += u.query;
    }
    return surl;
}

/*!
 * Removes http escaped characters such as: "%20" and replaces them with
 * their character representation. i.e. "hello%20foo" -> "hello foo".
 */
std::string remove_escaped_chars(const std::string& in);

/* Removes ".", and ".." from a path.
 *
 * If a ".." can not be resolved (i.e. the .. would go past the root of the
 * path) an error is returned as an empty string.
 */
std::string remove_dots(const std::string& in);

/*!
 * \brief resolves a relative url with a base url returning a new url
 *
 * If the base_url is empty, then a copy of the  rel_url is passed back if
 * the rel_url is absolute then a copy of the rel_url is passed back if neither
 * the base nor the rel_url are Absolute then NULL is returned. Otherwise it
 * tries and resolves the relative url with the base as described in
 * http://www.ietf.org/rfc/rfc2396.txt (RFCs explaining URIs).
 *
 */
std::string resolve_rel_url(const std::string& base, const std::string& rel);

/*!
 * \brief Parses a uri as defined in http://www.ietf.org/rfc/rfc2396.txt
 * (RFC explaining URIs).
 *
 * Handles absolute, relative, and opaque uris. Parses into the following
 * pieces: scheme, hostport, pathquery, fragment (path and query are treated
 * as one token)
 *
 * Caller should check for the pieces they require.
 *
 * \return UPNP_E_SUCCESS / UPNP_E_OTHER
 */
int parse_uri(const std::string& in, uri_type *out);

/* Possibly qualify the address part of the URL with a scope id, if needed */
std::string maybeScopeUrlAddr(const char *inurl, const struct sockaddr_storage *remoteaddr);
std::string maybeScopeUrlAddr(
    const char *inurl, uri_type& prsduri, const struct sockaddr_storage *remoteaddr);

#endif /* GENLIB_NET_URI_H */
