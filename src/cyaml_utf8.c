// Platform feature detection - must come before includes
#if defined(__linux__) || defined(__ANDROID__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include "cyaml_utf8.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <xlocale.h>
#else
#include <locale.h>
#endif

// #region UTF-8 Width Lookup Table

//! Lookup table for UTF-8 width from first byte
//! Index by first byte >> 3 (top 5 bits)
static const int8_t utf8_width_table[32] = {
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1, // 0x00-0x3F: ASCII (0xxxxxxx)
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1, // 0x40-0x7F: ASCII (0xxxxxxx)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x80-0xBF: continuation bytes (10xxxxxx) - invalid start
    2,
    2,
    2,
    2,
    3,
    3,
    4,
    0, // 0xC0-0xFF: multi-byte starts
};

// #endregion

// #region UTF-8 Decoding/Encoding

int cyaml_utf8_decode(const char* s, size_t len, cyaml_cp_t* cp)
{
    if (!s || len == 0) {
        *cp = CYAML_CP_INVALID;
        return 0;
    }

    const uint8_t* p = (const uint8_t*)s;
    uint8_t a = p[0];

    // 1-byte sequence: 0xxxxxxx
    if (a < 0x80) {
        *cp = a;
        return 1;
    }

    // Check for valid start byte
    int width = utf8_width_table[a >> 3];
    if (width == 0 || (size_t)width > len) {
        *cp = CYAML_CP_INVALID;
        return 0;
    }

    uint32_t code;

    if (width == 2) {
        // 2-byte sequence: 110xxxxx 10xxxxxx
        uint8_t b = p[1];
        if ((b & 0xC0) != 0x80) {
            *cp = CYAML_CP_INVALID;
            return 0;
        }
        code = (uint32_t)(((a & 0x1F) << 6) | (b & 0x3F));
        if (code < 0x80) {
            *cp = CYAML_CP_INVALID;
            return 0;
        } // overlong
    } else if (width == 3) {
        // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
        uint8_t b = p[1], c = p[2];
        if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80) {
            *cp = CYAML_CP_INVALID;
            return 0;
        }
        code = (uint32_t)(((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F));
        if (code < 0x800) {
            *cp = CYAML_CP_INVALID;
            return 0;
        } // overlong
        if (code >= 0xD800 && code <= 0xDFFF) {
            *cp = CYAML_CP_INVALID;
            return 0;
        } // surrogate
    } else {
        // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        uint8_t b = p[1], c = p[2], d = p[3];
        if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 || (d & 0xC0) != 0x80) {
            *cp = CYAML_CP_INVALID;
            return 0;
        }
        code = (uint32_t)(((a & 0x07) << 18) | ((b & 0x3F) << 12) | ((c & 0x3F) << 6) | (d & 0x3F));
        if (code < 0x10000 || code > 0x10FFFF) {
            *cp = CYAML_CP_INVALID;
            return 0;
        }
    }

    *cp = (cyaml_cp_t)code;
    return width;
}

int cyaml_utf8_encode(cyaml_cp_t cp, char* dst)
{
    if (!dst)
        return 0;

    uint8_t* s = (uint8_t*)dst;

    if (cp < 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        return 0; // Invalid codepoint
    }

    if (cp < 0x80) {
        s[0] = (uint8_t)cp;
        return 1;
    } else if (cp < 0x800) {
        s[0] = (uint8_t)((cp >> 6) | 0xC0);
        s[1] = (uint8_t)((cp & 0x3F) | 0x80);
        return 2;
    } else if (cp < 0x10000) {
        s[0] = (uint8_t)((cp >> 12) | 0xE0);
        s[1] = (uint8_t)(((cp >> 6) & 0x3F) | 0x80);
        s[2] = (uint8_t)((cp & 0x3F) | 0x80);
        return 3;
    } else {
        s[0] = (uint8_t)((cp >> 18) | 0xF0);
        s[1] = (uint8_t)(((cp >> 12) & 0x3F) | 0x80);
        s[2] = (uint8_t)(((cp >> 6) & 0x3F) | 0x80);
        s[3] = (uint8_t)((cp & 0x3F) | 0x80);
        return 4;
    }
}

int cyaml_utf8_len(unsigned char c)
{
    return utf8_width_table[c >> 3];
}

bool cyaml_utf8_valid(const char* s, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        cyaml_cp_t cp;
        int consumed = cyaml_utf8_decode(s + pos, len - pos, &cp);
        if (consumed <= 0 || cp == CYAML_CP_INVALID)
            return false;
        pos += (size_t)consumed;
    }
    return true;
}

// #endregion

// #region YAML 1.2 Character Classification

//! [5.1] c-printable (Rule 001)
bool cyaml_is_printable(cyaml_cp_t cp)
{
    if (cp == 0x09)
        return true; // TAB
    if (cp == 0x0A)
        return true; // LF
    if (cp == 0x0D)
        return true; // CR
    if (cp >= 0x20 && cp <= 0x7E)
        return true; // Printable ASCII
    if (cp == 0x85)
        return true; // NEL (Next Line)
    if (cp >= 0xA0 && cp <= 0xD7FF)
        return true; // BMP (excluding surrogates)
    if (cp >= 0xE000 && cp <= 0xFFFD)
        return true; // BMP private use + specials
    if (cp >= 0x10000 && cp <= 0x10FFFF)
        return true; // Supplementary planes
    return false;
}

//! [5.1] nb-json (Rule 002)
bool cyaml_is_json(cyaml_cp_t cp)
{
    if (cp == 0x09)
        return true;
    if (cp >= 0x20 && cp <= 0x10FFFF)
        return true;
    return false;
}

//! [5.4] nb-char (Rule 027)
bool cyaml_is_nb_char(cyaml_cp_t cp)
{
    if (!cyaml_is_printable(cp))
        return false;
    if (CYAML_IS_BREAK(cp))
        return false;
    if (CYAML_IS_BOM(cp))
        return false;
    return true;
}

//! [5.7] ns-uri-char (Rule 039)
bool cyaml_is_uri_char(cyaml_cp_t cp)
{
    if (CYAML_IS_WORD(cp))
        return true;
    switch (cp) {
    case '#':
    case ';':
    case '/':
    case '?':
    case ':':
    case '@':
    case '&':
    case '=':
    case '+':
    case '$':
    case ',':
    case '_':
    case '.':
    case '!':
    case '~':
    case '*':
    case '\'':
    case '(':
    case ')':
    case '[':
    case ']':
    case '%':
        return true;
    default:
        return false;
    }
}

//! [5.7] ns-tag-char (Rule 040)
bool cyaml_is_tag_char(cyaml_cp_t cp)
{
    if (!cyaml_is_uri_char(cp))
        return false;
    if (cp == '!')
        return false;
    if (cp == ',' || cp == '[' || cp == ']' || cp == '{' || cp == '}')
        return false;
    return true;
}

// #endregion

// #region Escape Sequence Handling (5.7)

cyaml_cp_t cyaml_parse_escape(const char* s, size_t len, int* consumed)
{
    if (!s || len == 0) {
        *consumed = 0;
        return CYAML_CP_INVALID;
    }

    char c = s[0];
    *consumed = 1;

    switch (c) {
    case '0':
        return 0x00;
    case 'a':
        return 0x07;
    case 'b':
        return 0x08;
    case 't':
        return 0x09;
    case 'n':
        return 0x0A;
    case 'v':
        return 0x0B;
    case 'f':
        return 0x0C;
    case 'r':
        return 0x0D;
    case 'e':
        return 0x1B;
    case ' ':
        return 0x20;
    case '"':
        return 0x22;
    case '/':
        return 0x2F;
    case '\\':
        return 0x5C;
    case 'N':
        return 0x85;
    case '_':
        return 0xA0;
    case 'L':
        return 0x2028;
    case 'P':
        return 0x2029;

    case 'x': {
        if (len < 3) {
            *consumed = 0;
            return CYAML_CP_INVALID;
        }
        if (!CYAML_IS_HEX(s[1]) || !CYAML_IS_HEX(s[2])) {
            *consumed = 0;
            return CYAML_CP_INVALID;
        }
        unsigned int val = 0;
        for (int i = 1; i <= 2; i++) {
            val <<= 4;
            char h = s[i];
            if (h >= '0' && h <= '9')
                val |= (unsigned)(h - '0');
            else if (h >= 'A' && h <= 'F')
                val |= (unsigned)(h - 'A' + 10);
            else if (h >= 'a' && h <= 'f')
                val |= (unsigned)(h - 'a' + 10);
        }
        *consumed = 3;
        return (cyaml_cp_t)val;
    }

    case 'u': {
        if (len < 5) {
            *consumed = 0;
            return CYAML_CP_INVALID;
        }
        for (int i = 1; i <= 4; i++) {
            if (!CYAML_IS_HEX(s[i])) {
                *consumed = 0;
                return CYAML_CP_INVALID;
            }
        }
        unsigned int val = 0;
        for (int i = 1; i <= 4; i++) {
            val <<= 4;
            char h = s[i];
            if (h >= '0' && h <= '9')
                val |= (unsigned)(h - '0');
            else if (h >= 'A' && h <= 'F')
                val |= (unsigned)(h - 'A' + 10);
            else if (h >= 'a' && h <= 'f')
                val |= (unsigned)(h - 'a' + 10);
        }
        *consumed = 5;
        return (cyaml_cp_t)val;
    }

    case 'U': {
        if (len < 9) {
            *consumed = 0;
            return CYAML_CP_INVALID;
        }
        for (int i = 1; i <= 8; i++) {
            if (!CYAML_IS_HEX(s[i])) {
                *consumed = 0;
                return CYAML_CP_INVALID;
            }
        }
        unsigned int val = 0;
        for (int i = 1; i <= 8; i++) {
            val <<= 4;
            char h = s[i];
            if (h >= '0' && h <= '9')
                val |= (unsigned)(h - '0');
            else if (h >= 'A' && h <= 'F')
                val |= (unsigned)(h - 'A' + 10);
            else if (h >= 'a' && h <= 'f')
                val |= (unsigned)(h - 'a' + 10);
        }
        *consumed = 9;
        if (val > 0x10FFFF) {
            *consumed = 0;
            return CYAML_CP_INVALID;
        }
        return (cyaml_cp_t)val;
    }

    default:
        *consumed = 0;
        return CYAML_CP_INVALID;
    }
}

int cyaml_write_escape(cyaml_cp_t cp, char* dst, size_t dst_len)
{
    if (!dst || dst_len < 2)
        return 0;

    const char* simple = NULL;
    switch (cp) {
    case 0x00:
        simple = "\\0";
        break;
    case 0x07:
        simple = "\\a";
        break;
    case 0x08:
        simple = "\\b";
        break;
    case 0x09:
        simple = "\\t";
        break;
    case 0x0A:
        simple = "\\n";
        break;
    case 0x0B:
        simple = "\\v";
        break;
    case 0x0C:
        simple = "\\f";
        break;
    case 0x0D:
        simple = "\\r";
        break;
    case 0x1B:
        simple = "\\e";
        break;
    case 0x22:
        simple = "\\\"";
        break;
    case 0x5C:
        simple = "\\\\";
        break;
    case 0x85:
        simple = "\\N";
        break;
    case 0xA0:
        simple = "\\_";
        break;
    case 0x2028:
        simple = "\\L";
        break;
    case 0x2029:
        simple = "\\P";
        break;
    default:
        break;
    }

    if (simple) {
        size_t slen = strlen(simple);
        if (dst_len < slen)
            return 0;
        memcpy(dst, simple, slen);
        return (int)slen;
    }

    if (!cyaml_is_printable(cp) || cp < 0x20) {
        static const char hex[] = "0123456789ABCDEF";
        if (cp <= 0xFF && dst_len >= 4) {
            dst[0] = '\\';
            dst[1] = 'x';
            dst[2] = hex[(cp >> 4) & 0xF];
            dst[3] = hex[cp & 0xF];
            return 4;
        } else if (cp <= 0xFFFF && dst_len >= 6) {
            dst[0] = '\\';
            dst[1] = 'u';
            dst[2] = hex[(cp >> 12) & 0xF];
            dst[3] = hex[(cp >> 8) & 0xF];
            dst[4] = hex[(cp >> 4) & 0xF];
            dst[5] = hex[cp & 0xF];
            return 6;
        } else if (dst_len >= 10) {
            dst[0] = '\\';
            dst[1] = 'U';
            dst[2] = hex[(cp >> 28) & 0xF];
            dst[3] = hex[(cp >> 24) & 0xF];
            dst[4] = hex[(cp >> 20) & 0xF];
            dst[5] = hex[(cp >> 16) & 0xF];
            dst[6] = hex[(cp >> 12) & 0xF];
            dst[7] = hex[(cp >> 8) & 0xF];
            dst[8] = hex[(cp >> 4) & 0xF];
            dst[9] = hex[cp & 0xF];
            return 10;
        }
    }

    return 0;
}

// #endregion

// #region String Utilities

char* cyaml_strdup(const char* s)
{
    if (s == NULL) {
#ifdef EINVAL
        errno = EINVAL;
#endif
        return NULL;
    }

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    return strdup(s);
#elif defined(_WIN32)
    return _strdup(s);
#else
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy != NULL) {
        memcpy(copy, s, len);
    } else {
#ifdef ENOMEM
        errno = ENOMEM;
#endif
    }
    return copy;
#endif
}

char* cyaml_strndup(const char* s, size_t n)
{
    if (s == NULL) {
#ifdef EINVAL
        errno = EINVAL;
#endif
        return NULL;
    }

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    return strndup(s, n);
#else
    size_t len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }
    char* copy = malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    } else {
#ifdef ENOMEM
        errno = ENOMEM;
#endif
    }
    return copy;
#endif
}

size_t cyaml_strlcpy(char* dst, const char* src, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
#ifdef EINVAL
        errno = EINVAL;
#endif
        return 0;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return 0;
    }

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return strlcpy(dst, src, dst_size);
#elif defined(_WIN32)
    errno_t err = strncpy_s(dst, dst_size, src, _TRUNCATE);
    if (err != 0 && err != STRUNCATE) {
        dst[0] = '\0';
        return 0;
    }
    return strlen(src);
#else
    size_t src_len = strlen(src);
    if (src_len >= dst_size) {
        memcpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    } else {
        memcpy(dst, src, src_len + 1);
    }
    return src_len;
#endif
}

size_t cyaml_strnlen(const char* s, size_t max)
{
    if (!s || max == 0)
        return 0;

#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return strnlen(s, max);
#elif defined(_WIN32)
    return strnlen_s(s, max);
#else
    const char* p = s;
    const char* end = s + max;
    while (p < end && *p)
        p++;
    return (size_t)(p - s);
#endif
}

// #endregion

// #region Number Parsing

//! Digit value lookup: 0-9 for decimal, 10-15 for hex a-f/A-F, 0xFF for invalid
static const uint8_t digit_val[256] = {
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 00-0F
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 10-1F
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 20-2F
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 30-3F '0'-'9'
    0xFF,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 40-4F 'A'-'F'
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 50-5F
    0xFF,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 60-6F 'a'-'f'
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 70-7F
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 80-8F
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // 90-9F
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // A0-AF
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // B0-BF
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // C0-CF
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // D0-DF
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // E0-EF
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF, // F0-FF
};

uint64_t cyaml_parse_u64(const char** s, bool* overflow)
{
    uint64_t acc = 0, prev;
    bool of = false;
    const char* p = *s;
    uint8_t d;

    while ((d = digit_val[(unsigned char)*p]) < 10) {
        prev = acc;
        acc = acc * 10 + d;
        if (acc < prev)
            of = true;
        p++;
    }

    *s = p;
    if (overflow)
        *overflow = of;
    return of ? UINT64_MAX : acc;
}

uint64_t cyaml_parse_u64_n(const char** s, const char* end, bool* overflow)
{
    uint64_t acc = 0, prev;
    bool of = false;
    const char* p = *s;
    uint8_t d;

    while (p < end && (d = digit_val[(unsigned char)*p]) < 10) {
        prev = acc;
        acc = acc * 10 + d;
        if (acc < prev)
            of = true;
        p++;
    }

    *s = p;
    if (overflow)
        *overflow = of;
    return of ? UINT64_MAX : acc;
}

uint64_t cyaml_parse_hex64(const char** s, bool* overflow)
{
    uint64_t acc = 0, prev;
    bool of = false;
    const char* p = *s;
    uint8_t d;

    while ((d = digit_val[(unsigned char)*p]) < 16) {
        prev = acc;
        acc = (acc << 4) | d;
        if (acc < prev)
            of = true;
        p++;
    }

    *s = p;
    if (overflow)
        *overflow = of;
    return of ? UINT64_MAX : acc;
}

uint64_t cyaml_parse_oct64(const char** s, bool* overflow)
{
    uint64_t acc = 0, prev;
    bool of = false;
    const char* p = *s;
    uint8_t d;

    while ((d = digit_val[(unsigned char)*p]) < 8) {
        prev = acc;
        acc = (acc << 3) | d;
        if (acc < prev)
            of = true;
        p++;
    }

    *s = p;
    if (overflow)
        *overflow = of;
    return of ? UINT64_MAX : acc;
}

bool cyaml_str_to_u64(const char* s, const char** end, uint64_t* out)
{
    if (!s || !out)
        return false;

    const char* p = s;
    bool overflow = false;
    uint64_t val;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!CYAML_IS_HEX(*p)) {
            if (end)
                *end = s;
            return false;
        }
        val = cyaml_parse_hex64(&p, &overflow);
    } else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) {
        p += 2;
        if (!(*p >= '0' && *p <= '7')) {
            if (end)
                *end = s;
            return false;
        }
        val = cyaml_parse_oct64(&p, &overflow);
    } else {
        if (!CYAML_IS_DIGIT(*p)) {
            if (end)
                *end = s;
            return false;
        }
        val = cyaml_parse_u64(&p, &overflow);
    }

    if (end)
        *end = p;
    if (overflow)
        return false;
    *out = val;
    return true;
}

bool cyaml_str_to_i64(const char* s, const char** end, int64_t* out)
{
    if (!s || !out)
        return false;

    const char* p = s;
    bool neg = false;

    if (*p == '-') {
        neg = true;
        p++;
    } else if (*p == '+') {
        p++;
    }

    uint64_t uval;
    if (!cyaml_str_to_u64(p, &p, &uval)) {
        if (end)
            *end = s;
        return false;
    }

    if (end)
        *end = p;

    if (neg) {
        if (uval > (uint64_t)INT64_MAX + 1)
            return false;
        *out = -(int64_t)uval;
    } else {
        if (uval > (uint64_t)INT64_MAX)
            return false;
        *out = (int64_t)uval;
    }
    return true;
}

#if defined(_MSC_VER)
#define CYAML_HAS_STRTOD_L 1
static _locale_t cyaml_c_locale(void)
{
    static _locale_t loc = NULL;
    if (!loc)
        loc = _create_locale(LC_ALL, "C");
    return loc;
}
#elif defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define CYAML_HAS_STRTOD_L 1
static locale_t cyaml_c_locale(void)
{
    static locale_t loc = (locale_t)0;
    if (!loc)
        loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    return loc;
}
#endif

bool cyaml_str_to_f64(const char* s, const char** end, double* out)
{
    if (!s || !out)
        return false;

    char* endp;
    errno = 0;

#if CYAML_HAS_STRTOD_L
#if defined(_MSC_VER)
    double val = _strtod_l(s, &endp, cyaml_c_locale());
#else
    double val = strtod_l(s, &endp, cyaml_c_locale());
#endif
#else
    double val = strtod(s, &endp);
#endif

    if (end)
        *end = endp;
    if (errno != 0 || endp == s)
        return false;
    *out = val;
    return true;
}

bool cyaml_scan_int(const char* p, size_t len)
{
    if (!p || len == 0)
        return false;

    const char* end = p + len;

    if (*p == '+' || *p == '-') {
        p++;
        if (p >= end)
            return false;
    }
    if (p + 1 < end && p[0] == '0') {
        if (p[1] == 'x' || p[1] == 'X') {
            p += 2;
            if (p >= end || !CYAML_IS_HEX(*p))
                return false;
            while (p < end && CYAML_IS_HEX(*p))
                p++;
            return p == end;
        }
        if (p[1] == 'o' || p[1] == 'O') {
            p += 2;
            if (p >= end || *p < '0' || *p > '7')
                return false;
            while (p < end && *p >= '0' && *p <= '7')
                p++;
            return p == end;
        }
    }

    if (!CYAML_IS_DIGIT(*p))
        return false;
    while (p < end && CYAML_IS_DIGIT(*p))
        p++;

    return p == end;
}

bool cyaml_scan_float(const char* p, size_t len)
{
    if (!p || len == 0)
        return false;

    const char* end = p + len;

    if (*p == '+' || *p == '-') {
        p++;
        if (p >= end)
            return false;
    }

    bool has_digits = false;
    bool has_dot = false;
    bool has_exp = false;

    while (p < end && CYAML_IS_DIGIT(*p)) {
        has_digits = true;
        p++;
    }

    if (p < end && *p == '.') {
        has_dot = true;
        p++;
        while (p < end && CYAML_IS_DIGIT(*p)) {
            has_digits = true;
            p++;
        }
    }

    if (!has_digits)
        return false;

    if (p < end && (*p == 'e' || *p == 'E')) {
        has_exp = true;
        p++;
        if (p < end && (*p == '+' || *p == '-'))
            p++;
        if (p >= end || !CYAML_IS_DIGIT(*p))
            return false;
        while (p < end && CYAML_IS_DIGIT(*p))
            p++;
    }
    return p == end && (has_dot || has_exp);
}

// #endregion
