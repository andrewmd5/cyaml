#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "cyaml.h"
#include "cyaml_utf8.h"
#include <ctype.h>
#include "compat/dirent.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define PATH_SEP "\\"
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#include <strings.h>
#include <limits.h>
#define PATH_SEP "/"
#endif

// #region File I/O

static inline char* dbg_read_file(const char* path, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

static inline bool dbg_is_directory(const char* path)
{
    DIR* d = opendir(path);
    if (d) {
        closedir(d);
        return true;
    }
    return false;
}

// #endregion

// #region String Formatting

//! Create hex dump string (caller frees)
static inline char* dbg_hex_dump(const char* data, size_t len)
{
    if (!data || len == 0)
        return cyaml_strdup("");
    size_t buf_size = len * 3 + 1;
    char* buf = malloc(buf_size);
    if (!buf)
        return NULL;
    char* p = buf;
    char* end = buf + buf_size;
    for (size_t i = 0; i < len; i++) {
        p += snprintf(p, (size_t)(end - p), "%02x ", (unsigned char)data[i]);
    }
    if (p > buf)
        p[-1] = '\0';
    return buf;
}

//! Create escaped string representation (caller frees)
static inline char* dbg_escaped(const char* data, size_t len)
{
    if (!data || len == 0)
        return cyaml_strdup("");
    size_t buf_size = len * 4 + 1;
    char* buf = malloc(buf_size);
    if (!buf)
        return NULL;
    char* p = buf;
    char* end = buf + buf_size;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\n') {
            *p++ = '\\';
            *p++ = 'n';
        } else if (c == '\r') {
            *p++ = '\\';
            *p++ = 'r';
        } else if (c == '\t') {
            *p++ = '\\';
            *p++ = 't';
        } else if (c == '\\') {
            *p++ = '\\';
            *p++ = '\\';
        } else if (c >= 32 && c < 127) {
            *p++ = (char)c;
        } else {
            p += snprintf(p, (size_t)(end - p), "\\x%02x", c);
        }
    }
    *p = '\0';
    return buf;
}

//! Format byte as printable char or hex
static inline void dbg_format_byte(char* out, size_t out_size, unsigned char c)
{
    if (c >= 32 && c < 127)
        snprintf(out, out_size, "'%c' (0x%02x)", c, c);
    else if (c == '\n')
        snprintf(out, out_size, "'\\n' (0x0a)");
    else if (c == '\r')
        snprintf(out, out_size, "'\\r' (0x0d)");
    else if (c == '\t')
        snprintf(out, out_size, "'\\t' (0x09)");
    else if (c == ' ')
        snprintf(out, out_size, "' ' (0x20)");
    else
        snprintf(out, out_size, "0x%02x", c);
}

static inline const char* dbg_style_name(cyaml_style_t style)
{
    switch (style) {
    case CYAML_PLAIN:
        return "plain";
    case CYAML_SINGLE:
        return "single-quoted";
    case CYAML_DOUBLE:
        return "double-quoted";
    case CYAML_LITERAL:
        return "literal";
    case CYAML_FOLDED:
        return "folded";
    default:
        return "unknown";
    }
}

static inline const char* dbg_type_name(cyaml_type_t type)
{
    switch (type) {
    case CYAML_NONE:
        return "none";
    case CYAML_NULL:
        return "null";
    case CYAML_SCALAR:
        return "scalar";
    case CYAML_SEQ:
        return "sequence";
    case CYAML_MAP:
        return "mapping";
    case CYAML_ALIAS:
        return "alias";
    default:
        return "unknown";
    }
}

// #endregion

// #region Diff Analysis

typedef struct {
    bool match;
    size_t diff_offset;
    size_t diff_line;
    size_t diff_col;
    size_t expected_len;
    size_t got_len;
    unsigned char expected_byte;
    unsigned char got_byte;
    char context_expected[80];
    char context_got[80];
} dbg_diff_t;

static inline void dbg_extract_context(const char* str, size_t len, size_t offset,
    char* out, size_t out_size)
{
    size_t start = (offset > 20) ? offset - 20 : 0;
    size_t end = (offset + 30 < len) ? offset + 30 : len;

    char* p = out;
    char* out_end = out + out_size - 1;

    if (start > 0) {
        *p++ = '.';
        *p++ = '.';
        *p++ = '.';
    }

    for (size_t i = start; i < end && p < out_end - 4; i++) {
        unsigned char c = (unsigned char)str[i];
        if (i == offset)
            *p++ = '[';
        if (c == '\n') {
            *p++ = '\\';
            *p++ = 'n';
        } else if (c == '\r') {
            *p++ = '\\';
            *p++ = 'r';
        } else if (c == '\t') {
            *p++ = '\\';
            *p++ = 't';
        } else if (c >= 32 && c < 127) {
            *p++ = (char)c;
        } else {
            p += snprintf(p, (size_t)(out_end - p), "\\x%02x", c);
        }
        if (i == offset)
            *p++ = ']';
    }

    if (end < len) {
        *p++ = '.';
        *p++ = '.';
        *p++ = '.';
    }
    *p = '\0';
}

static inline dbg_diff_t dbg_compare_strings(const char* expected, size_t exp_len,
    const char* got, size_t got_len)
{
    dbg_diff_t diff = { 0 };
    diff.expected_len = exp_len;
    diff.got_len = got_len;

    if (exp_len == got_len && memcmp(expected, got, exp_len) == 0) {
        diff.match = true;
        return diff;
    }

    diff.match = false;
    diff.diff_line = 1;
    diff.diff_col = 1;

    size_t min_len = exp_len < got_len ? exp_len : got_len;
    for (size_t i = 0; i < min_len; i++) {
        if (expected[i] != got[i]) {
            diff.diff_offset = i;
            diff.expected_byte = (unsigned char)expected[i];
            diff.got_byte = (unsigned char)got[i];
            dbg_extract_context(expected, exp_len, i, diff.context_expected, sizeof(diff.context_expected));
            dbg_extract_context(got, got_len, i, diff.context_got, sizeof(diff.context_got));
            return diff;
        }
        if (expected[i] == '\n') {
            diff.diff_line++;
            diff.diff_col = 1;
        } else {
            diff.diff_col++;
        }
    }

    // Lengths differ
    diff.diff_offset = min_len;
    if (exp_len > got_len) {
        diff.expected_byte = (unsigned char)expected[min_len];
        diff.got_byte = 0;
        dbg_extract_context(expected, exp_len, min_len, diff.context_expected, sizeof(diff.context_expected));
        snprintf(diff.context_got, sizeof(diff.context_got), "(end of string)");
    } else {
        diff.expected_byte = 0;
        diff.got_byte = (unsigned char)got[min_len];
        snprintf(diff.context_expected, sizeof(diff.context_expected), "(end of string)");
        dbg_extract_context(got, got_len, min_len, diff.context_got, sizeof(diff.context_got));
    }

    return diff;
}

// #endregion

// #region YAML Output Helpers

static inline void dbg_add_diff_info(cyaml_doc_t* out, cyaml_node_t* parent,
    const char* key, dbg_diff_t* diff)
{
    cyaml_node_t* info = cyaml_new_map(out);
    cyaml_map_set(out, info, "match", cyaml_new_bool(out, diff->match));

    if (!diff->match) {
        cyaml_map_set(out, info, "offset", cyaml_new_int(out, (int64_t)diff->diff_offset));

        char loc[32];
        snprintf(loc, sizeof(loc), "%zu:%zu", diff->diff_line, diff->diff_col);
        cyaml_map_set(out, info, "location", cyaml_new_cstr(out, loc));

        char exp_byte[20], got_byte[20];
        dbg_format_byte(exp_byte, sizeof(exp_byte), diff->expected_byte);
        dbg_format_byte(got_byte, sizeof(got_byte), diff->got_byte);
        cyaml_map_set(out, info, "expected_byte", cyaml_new_cstr(out, exp_byte));
        cyaml_map_set(out, info, "got_byte", cyaml_new_cstr(out, got_byte));

        if (diff->expected_len != diff->got_len) {
            cyaml_map_set(out, info, "expected_len", cyaml_new_int(out, (int64_t)diff->expected_len));
            cyaml_map_set(out, info, "got_len", cyaml_new_int(out, (int64_t)diff->got_len));
        }

        cyaml_map_set(out, info, "expected_context", cyaml_new_cstr(out, diff->context_expected));
        cyaml_map_set(out, info, "got_context", cyaml_new_cstr(out, diff->context_got));
    }

    cyaml_map_set(out, parent, key, info);
}

static inline void dbg_add_raw_info(cyaml_doc_t* out, cyaml_node_t* parent,
    const char* key, const char* data, size_t len)
{
    cyaml_node_t* info = cyaml_new_map(out);
    cyaml_map_set(out, info, "raw", cyaml_new_str(out, data, len));
    cyaml_map_set(out, info, "length", cyaml_new_int(out, (int64_t)len));

    char* hex = dbg_hex_dump(data, len);
    if (hex) {
        cyaml_map_set(out, info, "hex", cyaml_new_cstr(out, hex));
        free(hex);
    }

    char* esc = dbg_escaped(data, len);
    if (esc) {
        cyaml_map_set(out, info, "escaped", cyaml_new_cstr(out, esc));
        free(esc);
    }

    cyaml_map_set(out, parent, key, info);
}

static inline void dbg_add_node_info(cyaml_doc_t* out, cyaml_node_t* info,
    cyaml_doc_t* src_doc, cyaml_node_t* node, int depth)
{
    if (!node || depth > 10)
        return;

    cyaml_map_set(out, info, "type", cyaml_new_cstr(out, dbg_type_name(node->type)));
    cyaml_map_set(out, info, "style", cyaml_new_cstr(out, dbg_style_name(node->style)));

    if (node->type == CYAML_SCALAR) {
        char* val = cyaml_scalar_str(src_doc, node);
        if (val) {
            cyaml_map_set(out, info, "value", cyaml_new_cstr(out, val));
            cyaml_map_set(out, info, "length", cyaml_new_int(out, (int64_t)strlen(val)));
            char* esc = dbg_escaped(val, strlen(val));
            if (esc) {
                cyaml_map_set(out, info, "escaped", cyaml_new_cstr(out, esc));
                free(esc);
            }
            free(val);
        }
        if (node->style == CYAML_LITERAL || node->style == CYAML_FOLDED) {
            cyaml_map_set(out, info, "indent", cyaml_new_int(out, node->indent));
            cyaml_map_set(out, info, "chomp", cyaml_new_int(out, node->chomp));
            cyaml_map_set(out, info, "trailing_breaks", cyaml_new_int(out, node->trailing_breaks));
        }
    } else if (node->type == CYAML_SEQ) {
        cyaml_map_set(out, info, "count", cyaml_new_int(out, node->seq.count));
        if (depth < 3 && node->seq.count > 0) {
            cyaml_node_t* items = cyaml_new_seq(out);
            uint32_t limit = node->seq.count < 10 ? node->seq.count : 10;
            for (uint32_t i = 0; i < limit; i++) {
                cyaml_node_t* item_info = cyaml_new_map(out);
                dbg_add_node_info(out, item_info, src_doc, node->seq.items[i], depth + 1);
                cyaml_seq_push(items, item_info);
            }
            if (node->seq.count > 10) {
                cyaml_node_t* more = cyaml_new_map(out);
                char msg[32];
                snprintf(msg, sizeof(msg), "... and %u more", node->seq.count - 10);
                cyaml_map_set(out, more, "note", cyaml_new_cstr(out, msg));
                cyaml_seq_push(items, more);
            }
            cyaml_map_set(out, info, "items", items);
        }
    } else if (node->type == CYAML_MAP) {
        cyaml_map_set(out, info, "count", cyaml_new_int(out, node->map.count));
        if (depth < 3 && node->map.count > 0) {
            cyaml_node_t* pairs = cyaml_new_seq(out);
            uint32_t limit = node->map.count < 10 ? node->map.count : 10;
            for (uint32_t i = 0; i < limit; i++) {
                cyaml_node_t* pair_info = cyaml_new_map(out);
                char* key = cyaml_scalar_str(src_doc, node->map.pairs[i].key);
                if (key) {
                    cyaml_map_set(out, pair_info, "key", cyaml_new_cstr(out, key));
                    free(key);
                }
                cyaml_node_t* val_info = cyaml_new_map(out);
                dbg_add_node_info(out, val_info, src_doc, node->map.pairs[i].val, depth + 1);
                cyaml_map_set(out, pair_info, "value", val_info);
                cyaml_seq_push(pairs, pair_info);
            }
            if (node->map.count > 10) {
                cyaml_node_t* more = cyaml_new_map(out);
                char msg[32];
                snprintf(msg, sizeof(msg), "... and %u more", node->map.count - 10);
                cyaml_map_set(out, more, "note", cyaml_new_cstr(out, msg));
                cyaml_seq_push(pairs, more);
            }
            cyaml_map_set(out, info, "pairs", pairs);
        }
    } else if (node->type == CYAML_ALIAS) {
        cyaml_map_set(out, info, "has_target", cyaml_new_bool(out, node->alias.target != NULL));
    }
}

// #endregion

// #region Multi-doc JSON Parsing

static inline const char* dbg_skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

static inline const char* dbg_find_json_end(const char* p, const char* end)
{
    p = dbg_skip_ws(p, end);
    if (p >= end)
        return NULL;

    switch (*p) {
    case '{': {
        int depth = 1;
        p++;
        while (p < end && depth > 0) {
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end)
                        p += 2;
                    else
                        p++;
                }
                if (p < end)
                    p++;
            } else if (*p == '{') {
                depth++;
                p++;
            } else if (*p == '}') {
                depth--;
                p++;
            } else
                p++;
        }
        return depth == 0 ? p : NULL;
    }
    case '[': {
        int depth = 1;
        p++;
        while (p < end && depth > 0) {
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end)
                        p += 2;
                    else
                        p++;
                }
                if (p < end)
                    p++;
            } else if (*p == '[') {
                depth++;
                p++;
            } else if (*p == ']') {
                depth--;
                p++;
            } else
                p++;
        }
        return depth == 0 ? p : NULL;
    }
    case '"': {
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end)
                p += 2;
            else
                p++;
        }
        return (p < end) ? p + 1 : NULL;
    }
    default:
        while (p < end && !isspace(*p) && *p != ',' && *p != '}' && *p != ']')
            p++;
        return p;
    }
}

static inline cyaml_stream_t** dbg_parse_multi_json(const char* json, size_t len, uint32_t* count)
{
    *count = 0;
    if (!json || len == 0)
        return NULL;

    const char *p = json, *end = json + len;
    uint32_t doc_count = 0;

    while (p < end) {
        p = dbg_skip_ws(p, end);
        if (p >= end)
            break;
        const char* e = dbg_find_json_end(p, end);
        if (!e)
            break;
        doc_count++;
        p = e;
    }

    if (doc_count == 0)
        return NULL;

    cyaml_stream_t** streams = calloc(doc_count + 1, sizeof(cyaml_stream_t*));
    if (!streams)
        return NULL;

    p = json;
    uint32_t i = 0;
    while (p < end && i < doc_count) {
        p = dbg_skip_ws(p, end);
        if (p >= end)
            break;
        const char* doc_start = p;
        const char* doc_end = dbg_find_json_end(p, end);
        if (!doc_end)
            break;

        cyaml_error_t err;
        streams[i] = cyaml_parse_stream(doc_start, (size_t)(doc_end - doc_start), NULL, &err);
        if (!streams[i]) {
            for (uint32_t j = 0; j < i; j++)
                cyaml_stream_free(streams[j]);
            free(streams);
            return NULL;
        }
        i++;
        p = doc_end;
    }
    *count = i;
    return streams;
}

static inline void dbg_free_multi_json(cyaml_stream_t** streams, uint32_t count)
{
    if (!streams)
        return;
    for (uint32_t i = 0; i < count; i++) {
        if (streams[i])
            cyaml_stream_free(streams[i]);
    }
    free(streams);
}

// #endregion

// #region Semantic Comparison

typedef struct {
    cyaml_doc_t* out;
    cyaml_node_t* diffs;
    int max_diffs;
    int diff_count;
} dbg_cmp_ctx_t;

static inline bool dbg_nodes_equal(dbg_cmp_ctx_t* ctx,
    cyaml_doc_t* doc1, cyaml_node_t* n1,
    cyaml_doc_t* doc2, cyaml_node_t* n2,
    int depth, const char* path);

static inline void dbg_add_semantic_diff(dbg_cmp_ctx_t* ctx, const char* path,
    const char* diff_type, const char* detail)
{
    if (ctx->diff_count >= ctx->max_diffs)
        return;
    ctx->diff_count++;

    cyaml_node_t* diff = cyaml_new_map(ctx->out);
    cyaml_map_set(ctx->out, diff, "path", cyaml_new_cstr(ctx->out, path));
    cyaml_map_set(ctx->out, diff, "type", cyaml_new_cstr(ctx->out, diff_type));
    if (detail)
        cyaml_map_set(ctx->out, diff, "detail", cyaml_new_cstr(ctx->out, detail));
    cyaml_seq_push(ctx->diffs, diff);
}

static inline bool dbg_scalars_equal(dbg_cmp_ctx_t* ctx,
    cyaml_doc_t* doc1, cyaml_node_t* n1,
    cyaml_doc_t* doc2, cyaml_node_t* n2,
    const char* path)
{
    char* s1 = cyaml_scalar_str(doc1, n1);
    char* s2 = cyaml_scalar_str(doc2, n2);

    bool is_null1 = (!s1 || strlen(s1) == 0 || (strlen(s1) == 4 && strcasecmp(s1, "null") == 0) || (strlen(s1) == 1 && s1[0] == '~'));
    bool is_null2 = (!s2 || strlen(s2) == 0 || (strlen(s2) == 4 && strcasecmp(s2, "null") == 0) || (strlen(s2) == 1 && s2[0] == '~'));

    if (is_null1 && is_null2) {
        free(s1);
        free(s2);
        return true;
    }
    if (is_null1 || is_null2) {
        char detail[128];
        snprintf(detail, sizeof(detail), "'%s' vs '%s'", s1 ? s1 : "(null)", s2 ? s2 : "(null)");
        dbg_add_semantic_diff(ctx, path, "null_mismatch", detail);
        free(s1);
        free(s2);
        return false;
    }

    bool eq = (strcmp(s1, s2) == 0);

    // Try numeric comparison (handles hex vs decimal, trailing zeros)
    if (!eq) {
        char *end1, *end2;
        double d1 = strtod(s1, &end1);
        double d2 = strtod(s2, &end2);
        bool num1_ok = (*end1 == '\0' && end1 != s1);
        bool num2_ok = (*end2 == '\0' && end2 != s2);

        if (!num1_ok && strlen(s1) > 2 && s1[0] == '0' && (s1[1] == 'x' || s1[1] == 'X')) {
            d1 = (double)strtoll(s1, &end1, 16);
            num1_ok = (*end1 == '\0');
        }
        if (!num2_ok && strlen(s2) > 2 && s2[0] == '0' && (s2[1] == 'x' || s2[1] == 'X')) {
            d2 = (double)strtoll(s2, &end2, 16);
            num2_ok = (*end2 == '\0');
        }

        if (num1_ok && num2_ok && d1 == d2)
            eq = true;
    }

    if (!eq) {
        char detail[256];
        snprintf(detail, sizeof(detail), "'%s' vs '%s'", s1, s2);
        dbg_add_semantic_diff(ctx, path, "value_mismatch", detail);
    }

    free(s1);
    free(s2);
    return eq;
}

static inline bool dbg_nodes_equal(dbg_cmp_ctx_t* ctx,
    cyaml_doc_t* doc1, cyaml_node_t* n1,
    cyaml_doc_t* doc2, cyaml_node_t* n2,
    int depth, const char* path)
{
    if (!n1 && !n2)
        return true;
    if (!n1 || !n2) {
        dbg_add_semantic_diff(ctx, path, "null_node",
            n1 ? "got has value, expected null" : "expected has value, got null");
        return false;
    }

    // Resolve aliases
    while (n1->type == CYAML_ALIAS && n1->alias.target)
        n1 = n1->alias.target;
    while (n2->type == CYAML_ALIAS && n2->alias.target)
        n2 = n2->alias.target;

    // Both null
    if ((n1->type == CYAML_NULL || n1->type == CYAML_NONE) && (n2->type == CYAML_NULL || n2->type == CYAML_NONE)) {
        return true;
    }

    // Scalar comparison
    if (n1->type == CYAML_SCALAR || n2->type == CYAML_SCALAR || n1->type == CYAML_NULL || n2->type == CYAML_NULL || n1->type == CYAML_NONE || n2->type == CYAML_NONE) {
        return dbg_scalars_equal(ctx, doc1, n1, doc2, n2, path);
    }

    // Type mismatch
    if (n1->type != n2->type) {
        char detail[64];
        snprintf(detail, sizeof(detail), "%s vs %s", dbg_type_name(n1->type), dbg_type_name(n2->type));
        dbg_add_semantic_diff(ctx, path, "type_mismatch", detail);
        return false;
    }

    char subpath[256];

    if (n1->type == CYAML_SEQ) {
        if (n1->seq.count != n2->seq.count) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%u vs %u items", n1->seq.count, n2->seq.count);
            dbg_add_semantic_diff(ctx, path, "seq_length_mismatch", detail);
            return false;
        }
        for (uint32_t i = 0; i < n1->seq.count; i++) {
            snprintf(subpath, sizeof(subpath), "%s[%u]", path, i);
            if (!dbg_nodes_equal(ctx, doc1, n1->seq.items[i], doc2, n2->seq.items[i], depth + 1, subpath))
                return false;
        }
        return true;
    }

    if (n1->type == CYAML_MAP) {
        if (n1->map.count != n2->map.count) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%u vs %u pairs", n1->map.count, n2->map.count);
            dbg_add_semantic_diff(ctx, path, "map_size_mismatch", detail);
            return false;
        }
        for (uint32_t i = 0; i < n1->map.count; i++) {
            char* key1 = cyaml_scalar_str(doc1, n1->map.pairs[i].key);
            if (!key1)
                continue;

            bool found = false;
            for (uint32_t j = 0; j < n2->map.count; j++) {
                char* key2 = cyaml_scalar_str(doc2, n2->map.pairs[j].key);
                if (key2 && strcmp(key1, key2) == 0) {
                    free(key2);
                    snprintf(subpath, sizeof(subpath), "%s.%s", path, key1);
                    found = dbg_nodes_equal(ctx, doc1, n1->map.pairs[i].val,
                        doc2, n2->map.pairs[j].val, depth + 1, subpath);
                    break;
                }
                free(key2);
            }
            if (!found) {
                char detail[128];
                snprintf(detail, sizeof(detail), "key '%s' not found or value differs", key1);
                dbg_add_semantic_diff(ctx, path, "key_mismatch", detail);
                free(key1);
                return false;
            }
            free(key1);
        }
        return true;
    }

    return false;
}

// #endregion

// #region Test File Loading

typedef struct {
    char test_dir[PATH_MAX];
    char full_id[80];
    char* name;
    char* yaml;
    size_t yaml_len;
    char* out_yaml;
    char* emit_yaml;
    char* in_json;
    char* test_event;
} dbg_test_files_t;

static inline bool dbg_load_test(dbg_test_files_t* t, const char* suite_dir,
    const char* test_id, int case_idx)
{
    memset(t, 0, sizeof(*t));
    snprintf(t->test_dir, sizeof(t->test_dir), "%s" PATH_SEP "%s", suite_dir, test_id);

    char subdir[PATH_MAX];
    snprintf(subdir, sizeof(subdir), "%s" PATH_SEP "00", t->test_dir);
    char path[PATH_MAX];

    if (dbg_is_directory(subdir)) {
        char case_dir[16];
        snprintf(case_dir, sizeof(case_dir), "%02d", case_idx);
        snprintf(subdir, sizeof(subdir), "%s" PATH_SEP "%s", t->test_dir, case_dir);
        snprintf(t->full_id, sizeof(t->full_id), "%s:%d", test_id, case_idx + 1);

        snprintf(path, sizeof(path), "%s" PATH_SEP "===", subdir);
        t->name = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "in.yaml", subdir);
        t->yaml = dbg_read_file(path, &t->yaml_len);

        snprintf(path, sizeof(path), "%s" PATH_SEP "out.yaml", subdir);
        t->out_yaml = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "emit.yaml", subdir);
        t->emit_yaml = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "in.json", subdir);
        t->in_json = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "test.event", subdir);
        t->test_event = dbg_read_file(path, NULL);
    } else {
        cyaml_strlcpy(t->full_id, test_id, sizeof(t->full_id));

        snprintf(path, sizeof(path), "%s" PATH_SEP "===", t->test_dir);
        t->name = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "in.yaml", t->test_dir);
        t->yaml = dbg_read_file(path, &t->yaml_len);

        snprintf(path, sizeof(path), "%s" PATH_SEP "out.yaml", t->test_dir);
        t->out_yaml = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "emit.yaml", t->test_dir);
        t->emit_yaml = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "in.json", t->test_dir);
        t->in_json = dbg_read_file(path, NULL);

        snprintf(path, sizeof(path), "%s" PATH_SEP "test.event", t->test_dir);
        t->test_event = dbg_read_file(path, NULL);
    }

    if (t->name) {
        size_t n = strlen(t->name);
        if (n > 0 && t->name[n - 1] == '\n')
            t->name[n - 1] = '\0';
    }

    return t->yaml != NULL;
}

static inline void dbg_free_test(dbg_test_files_t* t)
{
    free(t->name);
    free(t->yaml);
    free(t->out_yaml);
    free(t->emit_yaml);
    free(t->in_json);
    free(t->test_event);
    memset(t, 0, sizeof(*t));
}

// #endregion

#endif // DEBUG_UTILS_H
