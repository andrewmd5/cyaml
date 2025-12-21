#ifndef CYAML_UTF8_H
#define CYAML_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// #region Codepoint Types

typedef int32_t cyaml_cp_t; // Unicode codepoint

#define CYAML_CP_INVALID (-1)
#define CYAML_CP_BOM 0xFEFF
#define CYAML_CP_NEL 0x85 // Next Line
#define CYAML_CP_NBSP 0xA0 // Non-breaking space
#define CYAML_CP_LS 0x2028 // Line Separator
#define CYAML_CP_PS 0x2029 // Paragraph Separator

// #endregion

// #region UTF-8 Decoding/Encoding

//! Decode one UTF-8 codepoint from string
//! Returns bytes consumed, or 0 on error
int cyaml_utf8_decode(const char* s, size_t len, cyaml_cp_t* cp);

//! Encode one codepoint to UTF-8
//! Returns bytes written (1-4), or 0 on error
//! dst must have room for 4 bytes
int cyaml_utf8_encode(cyaml_cp_t cp, char* dst);

//! Get length of UTF-8 sequence starting with byte c
//! Returns 1-4, or 0 for invalid start byte
int cyaml_utf8_len(unsigned char c);

//! Validate entire UTF-8 string
bool cyaml_utf8_valid(const char* s, size_t len);

// #endregion

// #region Character Classification (YAML 1.2 Spec Chapter 5)

//! ASCII byte (single-byte UTF-8)
#define CYAML_IS_ASCII(c) (((unsigned char)(c)) < 0x80)

//! [5.4] b-char (Rule 026) - line break
#define CYAML_IS_BREAK(c) ((c) == '\n' || (c) == '\r')

//! [5.5] s-white (Rule 033) - whitespace
#define CYAML_IS_WHITE(c) ((c) == ' ' || (c) == '\t')

//! Blank = white or break
#define CYAML_IS_BLANK(c) (CYAML_IS_WHITE(c) || CYAML_IS_BREAK(c))

//! Blank or end of input
#define CYAML_IS_BLANKZ(c) (CYAML_IS_BLANK(c) || (c) == '\0')

//! [5.6] ns-dec-digit (Rule 035)
#define CYAML_IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

//! [5.6] ns-hex-digit (Rule 036)
#define CYAML_IS_HEX(c) (CYAML_IS_DIGIT(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f'))

//! Convert hex digit to value (0-15), -1 if not hex
#define CYAML_HEX_VALUE(c)                                                                \
    (((c) >= '0' && (c) <= '9') ? (c) - '0' : ((c) >= 'A' && (c) <= 'F') ? (c) - 'A' + 10 \
            : ((c) >= 'a' && (c) <= 'f')                                 ? (c) - 'a' + 10 \
                                                                         : -1)

//! [5.6] ns-ascii-letter (Rule 037)
#define CYAML_IS_ALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

//! [5.6] ns-word-char (Rule 038)
#define CYAML_IS_WORD(c) (CYAML_IS_DIGIT(c) || CYAML_IS_ALPHA(c) || (c) == '-')

//! [5.3] c-indicator (Rule 022)
#define CYAML_IS_INDICATOR(c) \
    ((c) == '-' || (c) == '?' || (c) == ':' || (c) == ',' || (c) == '[' || (c) == ']' || (c) == '{' || (c) == '}' || (c) == '#' || (c) == '&' || (c) == '*' || (c) == '!' || (c) == '|' || (c) == '>' || (c) == '\'' || (c) == '"' || (c) == '%' || (c) == '@' || (c) == '`')

//! [5.3] c-flow-indicator (Rule 023)
#define CYAML_IS_FLOW(c) \
    ((c) == ',' || (c) == '[' || (c) == ']' || (c) == '{' || (c) == '}')

//! [5.2] c-byte-order-mark (Rule 003)
#define CYAML_IS_BOM(c) ((c) == CYAML_CP_BOM)

//! [5.4] b-line-feed (Rule 024)
#define CYAML_IS_LF(c) ((c) == 0x0A)

//! [5.4] b-carriage-return (Rule 025)
#define CYAML_IS_CR(c) ((c) == 0x0D)

//! [5.5] s-space (Rule 031)
#define CYAML_IS_SPACE(c) ((c) == ' ')

//! [5.5] s-tab (Rule 032)
#define CYAML_IS_TAB(c) ((c) == '\t')

//! Case conversion (ASCII only)
#define CYAML_TOLOWER(c) (((c) >= 'A' && (c) <= 'Z') ? (c) + 32 : (c))

// #endregion

// #region Extended Character Classification (require function calls)

//! [5.1] c-printable (Rule 001) - full Unicode range check
bool cyaml_is_printable(cyaml_cp_t cp);

//! [5.1] nb-json (Rule 002)
bool cyaml_is_json(cyaml_cp_t cp);

//! [5.4] nb-char (Rule 027)
//! c-printable - b-char - c-byte-order-mark
bool cyaml_is_nb_char(cyaml_cp_t cp);

//! [5.7] ns-uri-char (Rule 039)
bool cyaml_is_uri_char(cyaml_cp_t cp);

//! [5.7] ns-tag-char (Rule 040)
bool cyaml_is_tag_char(cyaml_cp_t cp);

// #endregion

// #region Escape Sequence Handling (5.7)

//! Parse escape sequence, returns codepoint
//! s points to character after backslash
//! Returns bytes consumed in *consumed, or 0 on error
cyaml_cp_t cyaml_parse_escape(const char* s, size_t len, int* consumed);

//! Write escape sequence for codepoint
//! Returns bytes written, or 0 if no escape needed
int cyaml_write_escape(cyaml_cp_t cp, char* dst, size_t dst_len);

// #endregion

// #region String Utilities

//! Case-insensitive codepoint comparison
#define CYAML_CP_IEQ(a, b) (CYAML_TOLOWER(a) == CYAML_TOLOWER(b))

//! Case-insensitive memory compare
static inline int cyaml_memicmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int d = CYAML_TOLOWER((unsigned char)a[i]) - CYAML_TOLOWER((unsigned char)b[i]);
        if (d != 0)
            return d;
    }
    return 0;
}

//! Portable strdup - duplicate string
//! @param s  String to duplicate (NULL safe)
//! @return Allocated copy (caller must free) or NULL on error
char* cyaml_strdup(const char* s);

//! Portable strndup - duplicate string up to n bytes
//! @param s  String to duplicate (NULL safe)
//! @param n  Maximum bytes to copy
//! @return Allocated copy (caller must free) or NULL on error
char* cyaml_strndup(const char* s, size_t n);

//! Safe string copy with guaranteed null termination
//! @param dst       Destination buffer
//! @param src       Source string (NULL safe)
//! @param dst_size  Size of destination buffer
//! @return Length of string copied (excluding null terminator)
size_t cyaml_strlcpy(char* dst, const char* src, size_t dst_size);

//! Portable strnlen - get string length up to max bytes
//! @param s    String to measure (NULL safe)
//! @param max  Maximum bytes to scan
//! @return Length of string, or max if no null found within max bytes
size_t cyaml_strnlen(const char* s, size_t max);

// #endregion

// #region Number Parsing (locale-independent)

//! Parse unsigned decimal integer, advances *s past consumed digits
//! Returns parsed value, sets *overflow true if overflow occurred
//! Stops at first non-digit character
uint64_t cyaml_parse_u64(const char** s, bool* overflow);

//! Bounded version - stops at end pointer or first non-digit
uint64_t cyaml_parse_u64_n(const char** s, const char* end, bool* overflow);

//! Parse unsigned hexadecimal integer (call after consuming 0x prefix)
uint64_t cyaml_parse_hex64(const char** s, bool* overflow);

//! Parse unsigned octal integer (call after consuming 0o prefix)
uint64_t cyaml_parse_oct64(const char** s, bool* overflow);

//! Parse complete integer string with auto base detection
//! Handles: decimal, 0x hex, 0o octal, optional +/- sign
//! @param s      Input string (null-terminated)
//! @param end    Set to first unconsumed character (like strtol)
//! @param out    Parsed value output
//! @return true if valid integer parsed without overflow
bool cyaml_str_to_i64(const char* s, const char** end, int64_t* out);
bool cyaml_str_to_u64(const char* s, const char** end, uint64_t* out);

//! Parse float string in C locale (always uses '.' as decimal separator)
//! @param s      Input string (null-terminated)
//! @param end    Set to first unconsumed character (like strtod)
//! @param out    Parsed value output
//! @return true if valid float parsed
bool cyaml_str_to_f64(const char* s, const char** end, double* out);

//! Scan bounded string for integer pattern
//! Accepts: decimal, 0x hex, 0o octal, optional +/- sign
//! @param p    Start of string
//! @param len  Length of string
//! @return true if entire string is valid integer
bool cyaml_scan_int(const char* p, size_t len);

//! Scan bounded string for float pattern
//! Accepts: decimal with optional fraction/exponent, but NOT .inf/.nan
//! @param p    Start of string
//! @param len  Length of string
//! @return true if entire string is valid float (has . or exponent)
bool cyaml_scan_float(const char* p, size_t len);

// #endregion

#ifdef __cplusplus
}
#endif

#endif // CYAML_UTF8_H
