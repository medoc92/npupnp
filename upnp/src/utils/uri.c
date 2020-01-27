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

#ifdef __FreeBSD__
#include <osreldate.h>
#if __FreeBSD_version < 601103
#include <lwres/netdb.h>
#endif
#endif
#include <assert.h>

#include "uri.h"
#include "upnputil.h"
#include "upnpapi.h"


/*!
 * \brief Returns a 1 if a char is a RESERVED char as defined in 
 * http://www.ietf.org/rfc/rfc2396.txt RFC explaining URIs).
 *
 * \return 1 if char is a RESERVED char.
 */
static int is_reserved(char in)
{
	/*! added {} for compatibility */
	static const char *RESERVED = ";/?:@&=+$,{}";
	if (strchr(RESERVED, (int)in)) {
		return 1;
	} else {
		return 0;
	}
}


/*!
 * \brief Returns a 1 if a char is a MARK char as defined in
 * http://www.ietf.org/rfc/rfc2396.txt (RFC explaining URIs).
 *
 * \return 1 if char is a MARKED char.
 */
static int is_mark(char in)
{
	static const char * MARK = "-_.!~*'()";
	if (strchr(MARK, (int)in)) {
		return 1;
	} else {
		return 0;
	}
}


/*!
 * \brief Returns a 1 if a char is an UNRESERVED char as defined in
 * http://www.ietf.org/rfc/rfc2396.txt (RFC explaining URIs).
 *
 * \return 1 if char is a UNRESERVED char.
 */
static int is_unreserved(char in)
{
	if (isalnum(in) || is_mark(in)) {
		return 1;
	} else {
		return 0;
	}
}


/*!
 * \brief Returns a 1 if a char[3] sequence is ESCAPED as defined in
 * http://www.ietf.org/rfc/rfc2396.txt (RFC explaining URIs).
 *
 * Size of array is NOT checked (MUST be checked by caller).
 *
 * \return 1 if char is a ESCAPED char.
 */
static int is_escaped(const char *in)
{
	if (in[0] == '%' && isxdigit(in[1]) && isxdigit(in[2])) {
		return 1;
	} else {
		return 0;
	}
}

/*!
 * \brief Replaces an escaped sequence with its unescaped version as in
 * http://www.ietf.org/rfc/rfc2396.txt	(RFC explaining URIs)
 *
 * Size of array is NOT checked (MUST be checked by caller)
 *
 * \note This function modifies the string. If the sequence is an escaped
 * sequence it is replaced, the other characters in the string are shifted
 * over, and NULL characters are placed at the end of the string.
 *
 * \return 
 */
static void replace_escaped(char *in, size_t index, size_t *max)
{
	int tempInt = 0;
	char tempChar = 0;
	size_t i = (size_t)0;
	size_t j = (size_t)0;

	if (in[index] == '%' && isxdigit(in[index + (size_t)1]) &&
		isxdigit(in[index + (size_t)2])) {
		/* Note the "%2x", makes sure that we convert a maximum of two
		 * characters. */
		if (sscanf(&in[index + (size_t)1], "%2x", &tempInt) != 1) {
			return;
		}
		tempChar = (char)tempInt;
		for (i = index + (size_t)3, j = index; j < *max; i++, j++) {
			in[j] = tempChar;
			if (i < *max) {
				tempChar = in[i];
			} else {
				tempChar = 0;
			}
		}
		*max -= (size_t)2;
	}
}


/*!
 * \brief Parses a string of uric characters starting at in[0] as defined in
 * http://www.ietf.org/rfc/rfc2396.txt (RFC explaining URIs).
 */
static size_t parse_uric(const char *in, size_t max, std::string& out)
{
	size_t i = 0;

	while (i < max && (is_unreserved(in[i]) || is_reserved(in[i]) ||
					   ((i + 2 < max) && is_escaped(&in[i])))) {
		i++;
	}

	out.assign(in, i);
	return i;
}


/*!
 * \brief Parses a string representing a host and port (e.g. "127.127.0.1:80"
 * or "localhost") and fills out a hostport_type struct with internet address
 * and a token representing the full host and port.
 *
 * Uses gethostbyname.
 */
static int parse_hostport(
	/*! [in] String of characters representing host and port. */
	const char *in,
	/*! [out] Output parameter where the host and port are represented as
	 * an internet address. */
	hostport_type *out)
{
	char workbuf[256];
	char *c;
	struct sockaddr_in *sai4 = (struct sockaddr_in *)&out->IPaddress;
	struct sockaddr_in6 *sai6 = (struct sockaddr_in6 *)&out->IPaddress;
	char *srvname = NULL;
	char *srvport = NULL;
	char *last_dot = NULL;
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
		*c++ = '\0';	/* overwrite the ']' */
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
		if (has_port == 1)
			c++;
		if (last_dot != NULL && isdigit(*(last_dot + 1)))
			/* Must be an IPv4 address. */
			af = AF_INET;
		else {
			/* Must be a host name. */
			struct addrinfo hints, *res, *res0;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;

			ret = getaddrinfo(srvname, NULL, &hints, &res0);
			if (ret == 0) {
				for (res = res0; res; res = res->ai_next) {
					switch (res->ai_family) {
					case AF_INET:
					case AF_INET6:
						/* Found a valid IPv4 or IPv6 address. */
						memcpy(&out->IPaddress,
							   res->ai_addr,
							   res->ai_addrlen);
						goto found;
					}
				}
			found:
				freeaddrinfo(res0);
				if (res == NULL)
					/* Didn't find an AF_INET or AF_INET6 address. */
					return UPNP_E_INVALID_URL;
			} else
				/* getaddrinfo failed. */
				return UPNP_E_INVALID_URL;
		}
	}
	/* Check if a port is specified. */
	if (has_port == 1) {
		/* Port is specified. */
		srvport = c;
		while (*c != '\0' && isdigit(*c))
			c++;
		port = (unsigned short int)atoi(srvport);
		if (port == 0)
			/* Bad port number. */
			return UPNP_E_INVALID_URL;
	} else
		/* Port was not specified, use default port. */
		port = 80u;
	/* The length of the host and port string can be calculated by */
	/* subtracting pointers. */
	hostport_size = (size_t)c - (size_t)workbuf;
	/* Fill in the 'out' information. */
	switch (af) {
	case AF_INET:
		sai4->sin_family = (sa_family_t)af;
		sai4->sin_port = htons(port);
		ret = inet_pton(AF_INET, srvname, &sai4->sin_addr);
		break;
	case AF_INET6:
		sai6->sin6_family = (sa_family_t)af;
		sai6->sin6_port = htons(port);
		sai6->sin6_scope_id = gIF_INDEX;
		ret = inet_pton(AF_INET6, srvname, &sai6->sin6_addr);
		break;
	default:
		/* IP address was set by the hostname (getaddrinfo). */
		/* Override port: */
		if (out->IPaddress.ss_family == (sa_family_t)AF_INET)
			sai4->sin_port = htons(port);
		else
			sai6->sin6_port = htons(port);
		ret = 1;
	}
	/* Check if address was converted successfully. */
	if (ret <= 0)
		return UPNP_E_INVALID_URL;
	out->text.assign(in, hostport_size);

	return (int)hostport_size;
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
static size_t parse_scheme(
	/*! [in] String of characters representing a scheme. */
	const char *in,
	/*! [in] Maximum number of characters. */
	size_t max,
	/*! [out] Output parameter whose buffer is filled in with the scheme. */
	std::string& out)
{
	size_t i = 0;

	out.clear();

	// A scheme begins with an alphabetic character
	if (max == 0 || !isalpha(in[0]))
		return (size_t)0;
	i++;

	// [::alphanum::+-.]* follows until ':' if any
	while (i < max && in[i] != ':') {
		if (!(isalnum(in[i]) || in[i] == '+' || in[i] == '-' || in[i] == '.'))
			return 0;
		i++;
	}

	if (i == max) {
		// : not found
		return 0;
	}
		
	out.assign(in, i);
	return i;
}


int remove_escaped_chars(char *in, size_t *size)
{
	for (size_t i = 0; i < *size; i++) {
		replace_escaped(in, i, size);
	}

	return UPNP_E_SUCCESS;
}


static UPNP_INLINE int is_end_path(char c) {
	switch (c) {
	case '?':
	case '#':
	case '\0':
		return 1;
	}
	return 0;
}


/* This function directly implements the "Remove Dot Segments"
 * algorithm described in RFC 3986 section 5.2.4. */
int remove_dots(char *buf, size_t size)
{
	char *in = buf;
	char *out = buf;
	char *max = buf + size;

	while (!is_end_path(in[0])) {
		assert (buf <= out);
		assert (out <= in);
		assert (in < max);

		/* case 2.A: */
		if (strncmp(in, "./", 2) == 0) {
			in += 2;
		} else if (strncmp(in, "../", 3) == 0) {
			in += 3;
			/* case 2.B: */
		} else if (strncmp(in, "/./", 3) == 0) {
			in += 2;
		} else if (strncmp(in, "/.", 2) == 0 && is_end_path(in[2])) {
			in += 1;
			in[0] = '/';
			/* case 2.C: */
		} else if (strncmp(in, "/../", 4) == 0 || (strncmp(in, "/..", 3) == 0 &&
												   is_end_path(in[3]))) {
			/* Make the next character in the input buffer a '/': */
			if (is_end_path(in[3])) { /* terminating "/.." case */
				in += 2;
				in[0] = '/';
			} else { /* "/../" prefix case */
				in += 3;
			}
			/* Trim the last component from the output buffer, or empty it. */
			while (buf < out)
				if (*--out == '/')
					break;
#ifdef DEBUG
			if (out < in)
				out[0] = '\0';
#endif
			/* case 2.D: */
		} else if (strncmp(in, ".", 1) == 0 && is_end_path(in[1])) {
			in += 1;
		} else if (strncmp(in, "..", 2) == 0 && is_end_path(in[2])) {
			in += 2;
			/* case 2.E */
		} else {
			/* move initial '/' character (if any) */
			if (in[0] == '/')
				*out++ = *in++;
			/* move first segment up to, but not including, the next
			 * '/' character */
			while (in < max && in[0] != '/' && !is_end_path(in[0]))
				*out++ = *in++;
#ifdef DEBUG
			if (out < in)
				out[0] = '\0';
#endif
		}
	}
	while (in < max)
		*out++ = *in++;
	if (out < max)
		out[0] = '\0';
	return UPNP_E_SUCCESS;
}


std::string resolve_rel_url(const char *base_url, const char *rel_url)
{
	uri_type base;
	uri_type rel;
	int rv;

	if (!base_url) {
		if (!rel_url)
			return std::string();
		return rel_url;
	}

	size_t len_rel = strlen(rel_url);
	if (parse_uri(rel_url, len_rel, &rel) != UPNP_E_SUCCESS)
		return std::string();
	if (rel.type == (enum uriType)URITP_ABSOLUTE)
		return rel_url;

	size_t len_base = strlen(base_url);
	if ((parse_uri(base_url, len_base, &base) != UPNP_E_SUCCESS)
		|| (base.type != (enum uriType)URITP_ABSOLUTE))
		return std::string();
	if (len_rel == (size_t)0)
		return base_url;

	size_t len = len_base + len_rel + (size_t)2;
	char *out = (char *)malloc(len);
	if (out == NULL)
		return std::string();
	memset(out, 0, len);
	char *out_finger = out;
	char *path;
	
	/* scheme */
	rv = snprintf(out_finger, len, "%.*s:",
				  (int)base.scheme.size(), base.scheme.c_str());
	if (rv < 0 || rv >= (int)len)
		goto error;
	out_finger += rv;
	len -= (size_t)rv;

	/* authority */
	if (rel.hostport.text.size() > (size_t)0) {
		rv = snprintf(out_finger, len, "%s", rel_url);
		if (rv < 0 || rv >= (int)len)
			goto error;
		{
			std::string sout(out);
			free(out);
			return sout;
		}
	}
	if (base.hostport.text.size() > (size_t)0) {
		rv = snprintf(out_finger, len, "//%.*s",
					  (int)base.hostport.text.size(),base.hostport.text.c_str());
		if (rv < 0 || rv >= (int)len)
			goto error;
		out_finger += rv;
		len -= (size_t)rv;
	}

	/* path */
	path = out_finger;
	if (rel.path_type == (enum pathType)ABS_PATH) {
		rv = snprintf(out_finger, len, "%s", rel_url);
	} else if (base.pathquery.empty()) {
		rv = snprintf(out_finger, len, "/%s", rel_url);
	} else {
		if (rel.pathquery.empty()) {
			rv = snprintf(out_finger, len, "%.*s",
						  (int)base.pathquery.size(), base.pathquery.c_str());
		} else {
			if (len < base.pathquery.size())
				goto error;
			size_t i = (size_t)0, prefix = (size_t)1;
			while (i < base.pathquery.size()) {
				out_finger[i] = base.pathquery[i];
				switch (base.pathquery[i++]) {
				case '/':
					prefix = i;
					/* fall-through */
				default:
					continue;
				case '?': /* query */
					if (rel.pathquery[0] == '?')
						prefix = --i;
				}
				break;
			}
			out_finger += prefix;
			len -= prefix;
			rv = snprintf(out_finger, len, "%.*s",
						  (int)rel.pathquery.size(), rel.pathquery.c_str());
		}
		if (rv < 0 || rv >= (int)len)
			goto error;
		out_finger += rv;
		len -= (size_t)rv;

		/* fragment */
		if (rel.fragment.size() > (size_t)0)
			rv = snprintf(out_finger, len, "#%.*s",
						  (int)rel.fragment.size(), rel.fragment.c_str());
		else if (base.fragment.size() > (size_t)0)
			rv = snprintf(out_finger, len, "#%.*s",
						  (int)base.fragment.size(), base.fragment.c_str());
		else
			rv = 0;
	}
	if (rv < 0 || rv >= (int)len)
		goto error;
	out_finger += rv;
	len -= (size_t)rv;

	if (remove_dots(path, (size_t)(out_finger - path)) != UPNP_E_SUCCESS)
		goto error;

	{
		std::string sout(out);
		free(out);
		return sout;
	}

error:
	free(out);
	return std::string();
}


int parse_uri(const char *in, size_t max, uri_type *out)
{
	size_t begin_hostport = parse_scheme(in, max, out->scheme);
	if (begin_hostport) {
		out->type = URITP_ABSOLUTE;
		out->path_type = OPAQUE_PART;
		begin_hostport++; // Skip ':'
	} else {
		out->type = URITP_RELATIVE;
		out->path_type = REL_PATH;
	}

	int begin_path = 0;
	if (begin_hostport + 1 < max && in[begin_hostport] == '/' &&
		in[begin_hostport + 1] == '/') {
		begin_hostport += 2;
		begin_path = parse_hostport(in + begin_hostport, &out->hostport);
		if (begin_path >= 0) {
			begin_path += begin_hostport;
		} else {
			return begin_path;
		}
	} else {
		begin_path = (int)begin_hostport;
	}
	
	size_t begin_fragment =
		parse_uric(in + begin_path, max - (size_t)begin_path, out->pathquery) +
		(size_t)begin_path;
	if (out->pathquery.size() && out->pathquery[0] == '/') {
		out->path_type = ABS_PATH;
	}
	if (begin_fragment < max && in[begin_fragment] == '#') {
		begin_fragment++;
		parse_uric(in + begin_fragment, max - begin_fragment, out->fragment);
	}

	return UPNP_E_SUCCESS;
}
