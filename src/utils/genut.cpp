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
#include <vector>
#include <set>
#include <unordered_set>

using namespace std;

void stringtolower(string& io)
{
	string::iterator it = io.begin();
	string::iterator ite = io.end();
	while (it != ite) {
		*it = ::tolower(*it);
		it++;
	}
}
string stringtolower(const string& i)
{
	string o = i;
	stringtolower(o);
	return o;
}

//	s1 is already lowercase
int stringlowercmp(const string& s1, const string& s2)
{
	string::const_iterator it1 = s1.begin();
	string::const_iterator it2 = s2.begin();
	string::size_type size1 = s1.length(), size2 = s2.length();
	char c2;

	if (size1 < size2) {
		while (it1 != s1.end()) {
			c2 = ::tolower(*it2);
			if (*it1 != c2) {
				return *it1 > c2 ? 1 : -1;
			}
			++it1;
			++it2;
		}
		return size1 == size2 ? 0 : -1;
	}

	while (it2 != s2.end()) {
		c2 = ::tolower(*it2);
		if (*it1 != c2) {
			return *it1 > c2 ? 1 : -1;
		}
		++it1;
		++it2;
	}

	return size1 == size2 ? 0 : 1;
}

void trimstring(string& s, const char *ws)
{
	rtrimstring(s, ws);
	ltrimstring(s, ws);
}

void rtrimstring(string& s, const char *ws)
{
	string::size_type pos = s.find_last_not_of(ws);
	if (pos == string::npos) {
		s.clear();
	} else if (pos != s.length() - 1) {
		s.replace(pos + 1, string::npos, string());
	}
}

void ltrimstring(string& s, const char *ws)
{
	string::size_type pos = s.find_first_not_of(ws);
	if (pos == string::npos) {
		s.clear();
		return;
	}
	s.replace(0, pos, string());
}

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
	string out;
	for (char i : in) {
		switch (i) {
		case '"':
			out += "&quot;";
			break;
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '\'':
			out += "&apos;";
			break;
		default:
			out += i;
		}
	}
	return out;
}

int dom_cmp_name(const std::string& domname, const std::string& ref)
{
	std::string::size_type colon = domname.find(':');
	return colon == std::string::npos ?
		domname.compare(ref) : domname.compare(colon+1, std::string::npos, ref);
}

template <class T> bool stringToStrings(const std::string& s, T& tokens,
										const std::string& addseps)
{
	std::string current;
	tokens.clear();
	enum states {SPACE, TOKEN, INQUOTE, ESCAPE};
	states state = SPACE;
	for (unsigned int i = 0; i < s.length(); i++) {
		switch (s[i]) {
		case '"':
			switch (state) {
			case SPACE:
				state = INQUOTE;
				continue;
			case TOKEN:
				current += '"';
				continue;
			case INQUOTE:
				tokens.insert(tokens.end(), current);
				current.clear();
				state = SPACE;
				continue;
			case ESCAPE:
				current += '"';
				state = INQUOTE;
				continue;
			}
			break;
		case '\\':
			switch (state) {
			case SPACE:
			case TOKEN:
				current += '\\';
				state = TOKEN;
				continue;
			case INQUOTE:
				state = ESCAPE;
				continue;
			case ESCAPE:
				current += '\\';
				state = INQUOTE;
				continue;
			}
			break;

		case ' ':
		case '\t':
		case '\n':
		case '\r':
			switch (state) {
			case SPACE:
				continue;
			case TOKEN:
				tokens.insert(tokens.end(), current);
				current.clear();
				state = SPACE;
				continue;
			case INQUOTE:
			case ESCAPE:
				current += s[i];
				continue;
			}
			break;

		default:
			if (!addseps.empty() && addseps.find(s[i]) != std::string::npos) {
				switch (state) {
				case ESCAPE:
					state = INQUOTE;
					break;
				case INQUOTE:
					break;
				case SPACE:
					tokens.insert(tokens.end(), std::string(1, s[i]));
					continue;
				case TOKEN:
					tokens.insert(tokens.end(), current);
					current.erase();
					tokens.insert(tokens.end(), std::string(1, s[i]));
					state = SPACE;
					continue;
				}
			} else switch (state) {
				case ESCAPE:
					state = INQUOTE;
					break;
				case SPACE:
					state = TOKEN;
					break;
				case TOKEN:
				case INQUOTE:
					break;
				}
			current += s[i];
		}
	}
	switch (state) {
	case SPACE:
		break;
	case TOKEN:
		tokens.insert(tokens.end(), current);
		break;
	case INQUOTE:
	case ESCAPE:
		return false;
	}
	return true;
}

template bool stringToStrings<std::list<std::string>>(
	const std::string&, std::list<std::string>&, const std::string&);
template bool stringToStrings<std::vector<std::string>>(
	const std::string&, std::vector<std::string>&, const std::string&);
template bool stringToStrings<std::set<std::string>>(
	const std::string&, std::set<std::string>&, const std::string&);
template bool stringToStrings<std::unordered_set<std::string> >(
	const std::string&, std::unordered_set<std::string>&, const std::string&);
template <class T> void stringsToString(const T& tokens, string& s)
{
    for (typename T::const_iterator it = tokens.begin();
         it != tokens.end(); it++) {
        bool hasblanks = false;
        if (it->find_first_of(" \t\n") != string::npos) {
            hasblanks = true;
        }
        if (it != tokens.begin()) {
            s.append(1, ' ');
        }
        if (hasblanks) {
            s.append(1, '"');
        }
        for (unsigned int i = 0; i < it->length(); i++) {
            char car = it->at(i);
            if (car == '"') {
                s.append(1, '\\');
                s.append(1, car);
            } else {
                s.append(1, car);
            }
        }
        if (hasblanks) {
            s.append(1, '"');
        }
    }
}
template void stringsToString<list<string> >(const list<string>&, string&);
template void stringsToString<vector<string> >(const vector<string>&, string&);
template void stringsToString<set<string> >(const set<string>&, string&);
template void stringsToString<unordered_set<string> >(const unordered_set<string>&, string&);
template <class T> string stringsToString(const T& tokens)
{
    string out;
    stringsToString<T>(tokens, out);
    return out;
}
template string stringsToString<list<string> >(const list<string>&);
template string stringsToString<vector<string> >(const vector<string>&);
template string stringsToString<set<string> >(const set<string>&);
template string stringsToString<unordered_set<string> >(const unordered_set<string>&);
