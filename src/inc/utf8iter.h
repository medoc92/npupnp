/* Copyright (C) 2004 J.F.Dockes
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _UTF8ITER_H_INCLUDED_
#define _UTF8ITER_H_INCLUDED_

#ifdef UTF8ITER_CHECK
#include "assert.h"
#endif

#include <cstddef>
#include <cstdint>
#include <string>

/** 
 * A small helper class to iterate over utf8 strings. This is not an
 * STL iterator and does not much error checking. It is designed purely
 * for recoll usage, where the utf-8 string comes out of iconv in most cases
 * and is assumed legal. We just try to catch cases where there would be 
 * a risk of crash.
 */
class Utf8Iter {
public:
    explicit Utf8Iter(const std::string &in) 
        : m_sp(&in) {
        update_cl();
    }

    const std::string& buffer() const {
        return *m_sp;
    }
    
    void rewind() {
        m_cl = 0; 
        m_pos = 0; 
        m_charpos = 0; 
        update_cl();
    }

    void retryfurther() {
        if (eof())
            return;
        m_pos++;
        if (eof()) {
            return;
        }
        update_cl();
    }
    
    /** "Direct" access. Awfully inefficient as we skip from start or current
     * position at best. This can only be useful for a lookahead from the
     * current position */
    uint32_t operator[](std::string::size_type charpos) const {
        std::string::size_type mypos = 0;
        unsigned int mycp = 0;
        if (charpos >= m_charpos) {
            mypos = m_pos;
            mycp = m_charpos;
        }
        int l;
        while (mypos < m_sp->length() && mycp != charpos) {
            l = get_cl(mypos);
            if (l <= 0 || !poslok(mypos, l) || !checkvalidat(mypos, l))
                return uint32_t(-1);
            mypos += l;
            ++mycp;
        }
        if (mypos < m_sp->length() && mycp == charpos) {
            l = get_cl(mypos);
            if (poslok(mypos, l) && checkvalidat(mypos, l))
                return getvalueat(mypos, l);
        }
        return uint32_t(-1);
    }

    /** Increment current position to next utf-8 char */
    std::string::size_type operator++(int) {
        // Note: m_cl may be zero at eof if user's test not right
        // this shouldn't crash the program until actual data access
#ifdef UTF8ITER_CHECK
        assert(m_cl != 0);
#endif
        if (m_cl == 0)
            return std::string::npos;

        m_pos += m_cl;
        m_charpos++;
        update_cl();
        return m_pos;
    }

    /** operator* returns the ucs4 value as a machine integer*/
    uint32_t operator*() {
#ifdef UTF8ITER_CHECK
        assert(m_cl > 0);
#endif
        return m_cl == 0 ? uint32_t(-1) : getvalueat(m_pos, m_cl);
    }

    /** Append current utf-8 possibly multi-byte character to string param.
        This needs to be fast. No error checking. */
    unsigned int appendchartostring(std::string &out) const {
#ifdef UTF8ITER_CHECK
        assert(m_cl != 0);
#endif
        out.append(&(*m_sp)[m_pos], m_cl);
        return m_cl;
    }

    /** Return current character as string */
    explicit operator std::string() {
#ifdef UTF8ITER_CHECK
        assert(m_cl != 0);
#endif
        return m_cl > 0 ? m_sp->substr(m_pos, m_cl) : std::string();
    }

    bool eof() const {
        return m_pos == m_sp->length();
    }

    bool error() const {
        return m_cl == 0;
    }

    /** Return current byte offset in input string */
    std::string::size_type getBpos() const {
        return m_pos;
    }

    /** Return current character length */
    std::string::size_type getBlen() const {
        return m_cl;
    }

    /** Return current unicode character offset in input string */
    std::string::size_type getCpos() const {
        return m_charpos;
    }

private:
    // String we're working with
    const std::string*     m_sp; 
    // Character length at current position. A value of zero indicates
    // an error.
    unsigned int m_cl{0};
    // Current byte offset in string.
    std::string::size_type m_pos{0}; 
    // Current character position
    unsigned int      m_charpos{0}; 

    // Check position and cl against string length
    bool poslok(std::string::size_type p, int l) const {
        return p != std::string::npos && l > 0 && p + l <= m_sp->length();
    }

    // Update current char length in object state, check
    // for errors
    void update_cl() {
        m_cl = 0;
        if (m_pos >= m_sp->length())
            return;
        m_cl = get_cl(m_pos);
        if (!poslok(m_pos, m_cl)) {
            // Used to set eof here for safety, but this is bad because it
            // basically prevents the caller to discriminate error and eof.
            //        m_pos = m_sp->length();
            m_cl = 0;
            return;
        }
        if (!checkvalidat(m_pos, m_cl)) {
            m_cl = 0;
        }
    }

    bool checkvalidat(std::string::size_type p, int l) const {
        switch (l) {
        case 1: 
            return uint8_t((*m_sp)[p]) < 128;
        case 2: 
            return uint8_t((*m_sp)[p] & 224) == 192
                                               && uint8_t((*m_sp)[p+1] & 192) == 128;
        case 3: 
            return uint8_t((*m_sp)[p] & 240) == 224
                                               && uint8_t((*m_sp)[p+1] & 192) ==  128
                                               && uint8_t((*m_sp)[p+2] & 192) ==  128
                                               ;
        case 4: 
            return uint8_t((*m_sp)[p] & 248) == 240
                                               && uint8_t((*m_sp)[p+1] & 192) ==  128
                                               && uint8_t((*m_sp)[p+2] & 192) ==  128
                                               && uint8_t((*m_sp)[p+3] & 192) ==  128
                                               ;
        default:
            return false;
        }
    }

    // Get character byte length at specified position. Returns 0 for error.
    int get_cl(std::string::size_type p) const {
        unsigned int z = uint8_t((*m_sp)[p]);
        if (z <= 127) {
            return 1;
        } else if ((z & 224) == 192) {
            return 2;
        } else if ((z & 240) == 224) {
            return 3;
        } else if ((z & 248) == 240) {
            return 4;
        }
#ifdef UTF8ITER_CHECK
        assert(z <= 127 || (z & 224) == 192 || (z & 240) == 224 ||
               (z & 248) == 240);
#endif
        return 0;
    }

    // Compute value at given position. No error checking.
    unsigned int getvalueat(std::string::size_type p, int l) const {
        switch (l) {
        case 1: 
#ifdef UTF8ITER_CHECK
            assert((unsigned char)(*m_sp)[p] < 128);
#endif
            return uint8_t((*m_sp)[p]);
        case 2: 
#ifdef UTF8ITER_CHECK
            assert(
                uint8_t((*m_sp)[p] & 224) == 192
                && ((unsigned char)(*m_sp)[p+1] & 192) ==  128
                );
#endif
            return uint8_t((*m_sp)[p] - 192) * 64 + 
                uint8_t((*m_sp)[p+1] - 128);
        case 3: 
#ifdef UTF8ITER_CHECK
            assert(
                (((unsigned char)(*m_sp)[p]) & 240) == 224
                && (((unsigned char)(*m_sp)[p+1]) & 192) ==  128
                && (((unsigned char)(*m_sp)[p+2]) & 192) ==  128
                );
#endif

            return uint8_t((*m_sp)[p] - 224) * 4096 + 
                uint8_t((*m_sp)[p+1] - 128) * 64 + 
                uint8_t((*m_sp)[p+2] - 128);
        case 4: 
#ifdef UTF8ITER_CHECK
            assert(
                uint8_t((*m_sp)[p] & 248) == 240
                && uint8_t((*m_sp)[p+1] & 192) ==  128
                && uint8_t((*m_sp)[p+2] & 192) ==  128
                && uint8_t((*m_sp)[p+3] & 192) ==  128
                );
#endif

            return uint8_t((*m_sp)[p]-240)*262144 + 
                uint8_t((*m_sp)[p+1]-128)*4096 + 
                uint8_t((*m_sp)[p+2]-128)*64 + 
                uint8_t((*m_sp)[p+3]-128);

        default:
#ifdef UTF8ITER_CHECK
            assert(l <= 4);
#endif
            return uint32_t(-1);
        }
    }

};


enum Utf8TruncateFlag {UTF8T_NONE, UTF8T_ATWORD, UTF8T_ELLIPSIS};

/** Truncate utf8 string, maintaining encoding integrity
 * @param s input string to be modified in place
 * @param maxlen maximum size after truncation in bytes
 * @param flags Specify cutting at word position, adding an ellipsis
 */
void utf8truncate(std::string& s, int maxlen, int flags = 0,
                  const std::string& ellipsis = "...",
                  const std::string& ws = " \t\n\r");

/** Compute length in characters of utf-8 string */
size_t utf8len(const std::string& s);

/** Return number of bytes for Unicode character */
inline int utf8codepointsize(uint32_t codepoint)
{
    if (codepoint <= 0x7F)
        return 1;
    if (codepoint <= 0x7FF)
        return 2;
    if (codepoint < 0xFFFF)
        return 3;
    return 4;
}

/** @brief Check and possibly fix string by replacing badly encoded
 * characters with the standard question mark replacement character.
 *
 * @param in the string to check
 * @param[out] if fixit is true, the fixed output string
 * @param fixit if true, copy a fixed string to out
 * @param maxrepl maximum replacements before we bail out
 * @return -1 for failure (fixit false or maxrepl reached). 
 *   0 or positive: replacement count.
 */
int utf8check(
    const std::string& in, bool fixit=false, std::string* out = nullptr, int maxrepl=100);

#endif /* _UTF8ITER_H_INCLUDED_ */
