/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (c) 2012 France Telecom All rights reserved. 
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


/*!
 * \file
 *
 * \brief Contains functions for uri, url parsing utility.
 */
#include "config.h"

#include <iostream>
#include <numeric>

#ifdef __FreeBSD__
#include <osreldate.h>
#if __FreeBSD_version < 601103
#include <lwres/netdb.h>
#endif
#endif /* __FreeBSD__ */

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif /* _WIN32 */

#include <cassert>

#include "uri.h"
#include "genut.h"
#include "upnpapi.h"
#include "inet_pton.h"

int parse_hostport(const char *in, hostport_type *out, bool noresolve)
{
    char workbuf[256];
    char *c;
    auto sai4 = reinterpret_cast<struct sockaddr_in *>(&out->IPaddress);
    auto sai6 = reinterpret_cast<struct sockaddr_in6 *>(&out->IPaddress);
    char *srvname = nullptr;
    char *srvport = nullptr;
    char *last_dot = nullptr;
    unsigned short int port;
    int af = AF_UNSPEC;
    size_t hostport_size;
    int has_port = 0;
    int ret;

    *out = hostport_type();
    /* Work on a copy of the input string. */
    upnp_strlcpy(workbuf, in, sizeof(workbuf));
    c = workbuf;
    if (*c == '[') {
        /* IPv6 addresses are enclosed in square brackets. */
        srvname = ++c;
        while (*c != '\0' && *c != ']')
            c++;
        if (*c == '\0')
            /* did not find closing bracket. */
            return UPNP_E_INVALID_URL;
        /* NULL terminate the srvname and then increment c. */
        *c++ = '\0';    /* overwrite the ']' */
        out->strhost = srvname;
        if (*c == ':') {
            has_port = 1;
            c++;
        }
        af = AF_INET6;
    } else {
        /* IPv4 address -OR- host name. */
        srvname = c;
        while (*c != ':' && *c != '/' &&
               (isalnum(*c) || *c == '.' || *c == '-')) {
            if (*c == '.')
                last_dot = c;
            c++;
        }
        has_port = (*c == ':') ? 1 : 0;
        /* NULL terminate the srvname */
        *c = '\0';
        out->strhost = srvname;
        if (has_port == 1)
            c++;
        if (last_dot != nullptr && isdigit(*(last_dot + 1))) {
            /* Must be an IPv4 address, because no top-level domain
               begins with a digit, at least at the moment */
            af = AF_INET;
        } else {
            /* Must be a host name. */
            out->hostisname = true;
            if (!noresolve) {
                struct addrinfo hints = {}, *res, *res0;

                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;

                ret = getaddrinfo(srvname, nullptr, &hints, &res0);
                if (ret == 0) {
                    for (res = res0; res; res = res->ai_next) {
                        switch (res->ai_family) {
                        case AF_INET:
                        case AF_INET6:
                            /* Found a valid IPv4 or IPv6 address. */
                            memcpy(&out->IPaddress, res->ai_addr, res->ai_addrlen);
                            goto found;
                        }
                    }
                found:
                    freeaddrinfo(res0);
                    if (res == nullptr) {
                        /* Didn't find an AF_INET or AF_INET6 address. */
                        return UPNP_E_INVALID_URL;
                    }
                } else {
                    /* getaddrinfo failed. */
                    return UPNP_E_INVALID_URL;
                }
            }
        }
    }
    /* Check if a port is specified. */
    if (has_port == 1) {
        /* Port is specified. */
        srvport = c;
        while (*c != '\0' && isdigit(*c))
            c++;
        out->strport = std::string(srvport, c - srvport);
        port = static_cast<unsigned short int>(atoi(srvport));
        if (port == 0)
            /* Bad port number. */
            return UPNP_E_INVALID_URL;
    } else {
        /* Port was not specified, use default port. */
        port = 80U;
    }
    /* The length of the host and port string can be calculated by */
    /* subtracting pointers. */
    hostport_size = size_t(c) - size_t(workbuf);
    /* Fill in the 'out' information. */
    switch (af) {
    case AF_INET:
        sai4->sin_family = static_cast<sa_family_t>(af);
        sai4->sin_port = htons(port);
        ret = inet_pton(AF_INET, srvname, &sai4->sin_addr);
        break;
    case AF_INET6:
    {
        int scopeidx = 0;
        auto pc = strchr(srvname, '%');
        if (pc) {
            *pc = 0;
            pc++;
            // Trying to guess if this is url-encoded. if the index is
            // 25x, we're out of luck.
            if (*pc == '2' && *(pc+1) == '5' && isdigit(*(pc+2))) {
                scopeidx = atoi(pc+2);
            } else {
                scopeidx = atoi(pc);
            }
        }
        sai6->sin6_family = static_cast<sa_family_t>(af);
        sai6->sin6_port = htons(port);
        sai6->sin6_scope_id = scopeidx;
        ret = inet_pton(AF_INET6, srvname, &sai6->sin6_addr);
    }
    break;
    default:
        /* IP address was set by the hostname (getaddrinfo). */
        /* Override port: */
        if (out->IPaddress.ss_family == static_cast<sa_family_t>(AF_INET))
            sai4->sin_port = htons(port);
        else
            sai6->sin6_port = htons(port);
        ret = 1;
    }
    /* Check if address was converted successfully. */
    if (ret <= 0)
        return UPNP_E_INVALID_URL;
    out->text.assign(in, hostport_size);
    return static_cast<int>(hostport_size);
}

/*!
 * \brief parses a uri scheme starting at in[0] as defined in 
 * http://www.ietf.org/rfc/rfc2396.txt (RFC explaining URIs).
 *
 * (e.g. "http:" -> scheme= "http").
 *
 * \note String MUST include ':' within the max charcters.
 *
 * \return 
 */
static size_t parse_scheme(const std::string& in, std::string& out)
{
    out.clear();

    // A scheme begins with an alphabetic character
    if (in.empty() || !isalpha(in[0]))
        return 0;

    // Need a colon
    std::string::size_type colon = in.find(':');
    if (colon == std::string::npos) {
        return 0;
    }
    // Check contents: "[::alphanum::+-.]*:"
    for (size_t i = 0; i < colon; i++) {
        if (!isalnum(in[i]) && in[i] != '+' && in[i] != '-' && in[i] != '.')
            return 0;
    }
    out = in.substr(0, colon);
    return out.size();
}


/*!
 * \brief Replaces an escaped sequences with theur unescaped version as in
 * http://www.ietf.org/rfc/rfc2396.txt    (RFC explaining URIs)
 */
static inline int h2d(int c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('A' <= c && c <= 'F')
        return 10 + c - 'A';

    return -1;
}

std::string remove_escaped_chars(const std::string& in)
{
    if (in.size() <= 2)
        return in;
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    for (; i < in.size() - 2; i++) {
        if (in[i] == '%') {
            int d1 = h2d(in[i+1]);
            int d2 = h2d(in[i+2]);
            if (d1 != -1 && d2 != -1) {
                out += (d1 << 4) + d2;
            } else {
                out += '%';
                out += in[i+1];
                out += in[i+2];
            }
            i += 2;
        } else {
            out += in[i];
        }
    }
    while (i < in.size()) {
        out += in[i++];
    }
    return out;
}


std::string remove_dots(const std::string& in)
{
    static const std::string markers("/?");
    std::vector<std::string> vpath;
    if (in.empty()) {
        return in;
    }
    bool isabs = in[0] == '/';
    bool endslash = in.back() == '/';
    std::string::size_type pos = 0;
    while (pos != std::string::npos) {
        std::string::size_type epos = in.find_first_of(markers, pos);
        if (epos != std::string::npos && in[epos] == '?') {
            // done
            epos = std::string::npos;
        }
        if (epos == pos) {
            pos++;
            continue;
        }
        std::string elt = (epos == std::string::npos) ?
            in.substr(pos) : in.substr(pos, epos - pos);
        if (elt.empty() || elt == ".") {
            // Do nothing, // or /./ are ignored
        } else if (elt == "..") {
            if (vpath.empty()) {
                // This is an error: trying to go behind /
                return {};
            }
            vpath.pop_back();
        } else {
            vpath.push_back(elt);
        }
        pos = epos;
    }
    std::string out = std::accumulate(vpath.begin(), vpath.end(), isabs ? std::string("/") : "",
        [](const std::string& o, const std::string& elt){ return o + elt + "/"; });

    // Pop the last / if the original path did not end with /
    if (!endslash && out.size() > 1 && out.back() == '/')
        out.pop_back();
    return out;
}

std::string resolve_rel_url(
    const std::string& base_url, const std::string& rel_url)
{
    uri_type base;
    uri_type rel;
    uri_type url;

    // Base can't be empty, it needs at least a scheme.
    if (base_url.empty()) {
        return {};
    }
    if ((parse_uri(base_url, &base) != UPNP_E_SUCCESS)
        || (base.type != URITP_ABSOLUTE)) {
        return {};
    }
    if (rel_url.empty())
        return base_url;

    if (parse_uri(rel_url, &rel) != UPNP_E_SUCCESS) {
        return {};
    }

    rel.path = remove_dots(rel.path);

    if (rel.type == URITP_ABSOLUTE) {
        return uri_asurlstr(rel);
    }

    url.scheme = base.scheme;
    url.fragment = rel.fragment;

    if (!rel.hostport.text.empty()) {
        url.hostport = rel.hostport;
        url.path = rel.path;
        url.query = rel.query;
        return uri_asurlstr(url);
    }

    url.hostport = base.hostport;

    if (rel.path.empty()) {
        url.path = base.path;
        if (!rel.query.empty()) {
            url.query = rel.query;
        } else {
            url.query = base.query;
        }
    } else {
        if (rel.path[0] == '/') {
            url.path = rel.path;
        } else {
            // Merge paths
            if (base.path.empty()) {
                url.path = std::string("/") + rel.path;
            } else {
                if (base.path == "/") {
                    url.path = base.path + rel.path;
                } else {
                    if (base.path.back() == '/') {
                        base.path.pop_back();
                    }
                    std::string::size_type pos = base.path.rfind('/');
                    url.path = base.path.substr(0, pos+1) + rel.path;
                }
                url.query = rel.query;
            }
        }
    }
    return uri_asurlstr(url);
}

int parse_uri(const std::string& in, uri_type *out)
{
    size_t begin_hostport = parse_scheme(in, out->scheme);
    if (begin_hostport) {
        out->type = URITP_ABSOLUTE;
        out->path_type = OPAQUE_PART;
        begin_hostport++; // Skip ':'
    } else {
        out->type = URITP_RELATIVE;
        out->path_type = REL_PATH;
    }

    size_t begin_path = 0;
    if (begin_hostport + 1 < in.size() && in[begin_hostport] == '/' &&
        in[begin_hostport + 1] == '/') {
        begin_hostport += 2;
        begin_path = parse_hostport(in.c_str() + begin_hostport, &out->hostport);
        if (begin_path == 0)
            return begin_path;
        begin_path += begin_hostport;
    } else {
        begin_path = static_cast<int>(begin_hostport);
    }
    std::string::size_type question = in.find('?', begin_path);
    std::string::size_type hash = in.find('#', begin_path);
    if (question == std::string::npos &&
        hash == std::string::npos) {
        out->path = in.substr(begin_path);
    } else if (question != std::string::npos && hash == std::string::npos) {
        out->path = in.substr(begin_path, question - begin_path);
        out->query = in.substr(question+1);
    } else if (question == std::string::npos && hash != std::string::npos) {
        out->path = in.substr(begin_path, hash - begin_path);
        out->fragment = in.substr(hash+1);
    } else {
        if (hash < question) {
            out->path = in.substr(begin_path, hash - begin_path);
            out->fragment = in.substr(hash+1);
        } else {
            out->path = in.substr(begin_path, question - begin_path);
            out->query = in.substr(question + 1, hash - question - 1);
            out->fragment = in.substr(hash+1);
        }
    }

    if (!out->path.empty() && out->path[0] == '/') {
        out->path_type = ABS_PATH;
    }

    return UPNP_E_SUCCESS;
}

std::string maybeScopeUrlAddr(
    const char *inurl, uri_type& prsduri, const struct sockaddr_storage *remoteaddr)
{
    NetIF::IPAddr urlip(reinterpret_cast<const struct sockaddr*>(&prsduri.hostport.IPaddress));

    if (urlip.family() != NetIF::IPAddr::Family::IPV6 ||
        urlip.scopetype() != NetIF::IPAddr::Scope::LINK) {
        // Can use URL as is
        return inurl;
    }

    // Set the scope from the one in the remote address.
    NetIF::IPAddr remip(reinterpret_cast<const struct sockaddr*>(remoteaddr));
    urlip.setScopeIdx(remip);
    std::string scopedaddr = urlip.straddr(true, true);

    auto sa6 = reinterpret_cast<struct sockaddr_in6*>(&prsduri.hostport.IPaddress);
    auto portbuf = std::to_string(ntohs(sa6->sin6_port));
    prsduri.hostport.text = std::string("[") + scopedaddr + "]:" + portbuf;
    return uri_asurlstr(prsduri);
}

std::string maybeScopeUrlAddr(const char *inurl, const struct sockaddr_storage *remoteaddr)
{
    uri_type prsduri;
    if (parse_uri(inurl, &prsduri) != UPNP_E_SUCCESS || prsduri.hostport.text.empty()) {
        return {};
    }
    return maybeScopeUrlAddr(inurl, prsduri, remoteaddr);
}
