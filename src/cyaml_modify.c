#include "cyaml_internal.h"
#include <inttypes.h>

// #region Format Specifier Types

typedef enum {
    FMT_INT,
    FMT_UINT,
    FMT_LONG,
    FMT_ULONG,
    FMT_LLONG,
    FMT_ULLONG,
    FMT_FLOAT,
    FMT_DOUBLE,
    FMT_STRING,
    FMT_BOOL,
    FMT_NODE
} fmt_type_t;

typedef struct {
    fmt_type_t type;
    int width;
} fmt_spec_t;

// #endregion

// #region Format String Parsing

static inline const char* skip_ws(const char* p)
{
    while (CYAML_IS_WHITE(*p))
        p++;
    return p;
}

static const char* scan_path(const char* p, size_t* len)
{
    p = skip_ws(p);
    const char* start = p;
    while (*p && !CYAML_IS_WHITE(*p) && *p != '%')
        p++;
    *len = (size_t)(p - start);
    return start;
}

static const char* scan_fmt(const char* p, fmt_spec_t* spec)
{
    p = skip_ws(p);
    if (*p != '%')
        return NULL;
    p++;

    spec->width = 0;
    while (CYAML_IS_DIGIT(*p))
        spec->width = spec->width * 10 + (*p++ - '0');

    if (*p == 'l') {
        p++;
        if (*p == 'l') {
            p++;
            if (*p == 'd' || *p == 'i') {
                spec->type = FMT_LLONG;
                return p + 1;
            }
            if (*p == 'u') {
                spec->type = FMT_ULLONG;
                return p + 1;
            }
            return NULL;
        }
        if (*p == 'd' || *p == 'i') {
            spec->type = FMT_LONG;
            return p + 1;
        }
        if (*p == 'u') {
            spec->type = FMT_ULONG;
            return p + 1;
        }
        if (*p == 'f') {
            spec->type = FMT_DOUBLE;
            return p + 1;
        }
        return NULL;
    }

    switch (*p) {
    case 'd':
    case 'i':
        spec->type = FMT_INT;
        return p + 1;
    case 'u':
        spec->type = FMT_UINT;
        return p + 1;
    case 'f':
        spec->type = FMT_FLOAT;
        return p + 1;
    case 's':
        spec->type = FMT_STRING;
        return p + 1;
    case 'b':
        spec->type = FMT_BOOL;
        return p + 1;
    case 'n':
        spec->type = FMT_NODE;
        return p + 1;
    default:
        return NULL;
    }
}

// #endregion

// #region Scanf Implementation

#define SCANF_INT(ap, type, doc, node)      \
    do {                                    \
        int64_t _v;                         \
        if (cyaml_as_int(doc, node, &_v)) { \
            *va_arg(ap, type*) = (type)_v;  \
            ok = true;                      \
        }                                   \
    } while (0)

#define SCANF_UINT(ap, type, doc, node)      \
    do {                                     \
        uint64_t _v;                         \
        if (cyaml_as_uint(doc, node, &_v)) { \
            *va_arg(ap, type*) = (type)_v;   \
            ok = true;                       \
        }                                    \
    } while (0)

CYAML_API int cyaml_node_vscanf(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* format, va_list ap)
{
    if (!doc || !format)
        return -1;
    if (!node)
        node = doc->root;
    if (!node)
        return 0;

    const char* src = cyaml_src(doc);
    if (!src)
        return 0;

    int count = 0;
    const char* p = format;
    char path_buf[512];

    while (*p) {
        size_t path_len;
        const char* path_start = scan_path(p, &path_len);
        if (path_len == 0)
            break;
        if (path_len >= sizeof(path_buf))
            return -1;

        memcpy(path_buf, path_start, path_len);
        path_buf[path_len] = C_NUL;
        p = path_start + path_len;

        fmt_spec_t spec;
        const char* next = scan_fmt(p, &spec);
        if (!next)
            return -1;
        p = next;

        cyaml_node_t* target = (path_buf[0] == '/')
            ? cyaml_path(doc, path_buf)
            : cyaml_path_first(doc, node, path_buf);

        if (!target)
            continue;

        bool ok = false;
        switch (spec.type) {
        case FMT_INT:
            SCANF_INT(ap, int, doc, target);
            break;
        case FMT_UINT:
            SCANF_UINT(ap, unsigned int, doc, target);
            break;
        case FMT_LONG:
            SCANF_INT(ap, long, doc, target);
            break;
        case FMT_ULONG:
            SCANF_UINT(ap, unsigned long, doc, target);
            break;
        case FMT_LLONG:
            SCANF_INT(ap, int64_t, doc, target);
            break;
        case FMT_ULLONG:
            SCANF_UINT(ap, uint64_t, doc, target);
            break;
        case FMT_FLOAT: {
            double v;
            if (cyaml_as_float(doc, target, &v)) {
                *va_arg(ap, float*) = (float)v;
                ok = true;
            }
            break;
        }
        case FMT_DOUBLE: {
            double v;
            if (cyaml_as_float(doc, target, &v)) {
                *va_arg(ap, double*) = v;
                ok = true;
            }
            break;
        }
        case FMT_STRING: {
            char* out = va_arg(ap, char*);
            size_t max_len = spec.width > 0 ? (size_t)spec.width : SIZE_MAX;
            if (target->type == CYAML_SCALAR && target->style == CYAML_PLAIN) {
                size_t len = target->span.len;
                if (len >= max_len)
                    len = max_len - 1;
                memcpy(out, src + target->span.off, len);
                out[len] = C_NUL;
                ok = true;
            } else {
                char* str = cyaml_scalar_str(doc, target);
                if (str) {
                    cyaml_strlcpy(out, str, max_len);
                    free(str);
                    ok = true;
                }
            }
            break;
        }
        case FMT_BOOL: {
            bool v;
            if (cyaml_as_bool(doc, target, &v)) {
                *va_arg(ap, bool*) = v;
                ok = true;
            }
            break;
        }
        case FMT_NODE:
            *va_arg(ap, cyaml_node_t**) = target;
            ok = true;
            break;
        }
        if (ok)
            count++;
    }
    return count;
}

CYAML_API int cyaml_node_scanf(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = cyaml_node_vscanf(doc, node, format, ap);
    va_end(ap);
    return result;
}

CYAML_API int cyaml_vscanf(const cyaml_doc_t* doc, const char* format, va_list ap)
{
    return cyaml_node_vscanf(doc, NULL, format, ap);
}

CYAML_API int cyaml_scanf(const cyaml_doc_t* doc, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = cyaml_node_vscanf(doc, NULL, format, ap);
    va_end(ap);
    return result;
}

// #endregion

// #region Buildf Implementation

#define BUILDF_BUF_SIZE 4096

static inline bool buildf_needs_quoting(const char* s)
{
    if (!s || !*s)
        return true;
    if (CYAML_IS_INDICATOR(*s) || CYAML_IS_WHITE(*s))
        return true;

    size_t len = cyaml_strnlen(s, BUILDF_BUF_SIZE);
    if (len >= BUILDF_BUF_SIZE)
        return true;

    for (size_t i = 0; i < len; i++)
        if (CYAML_IS_BREAK(s[i]) || s[i] == ':' || s[i] == C_HASH)
            return true;

    if ((len == L_TRUE && memcmp(s, S_TRUE, L_TRUE) == 0) || (len == L_FALSE && memcmp(s, S_FALSE, L_FALSE) == 0) || (len == L_NULL && memcmp(s, S_NULL, L_NULL) == 0) || (len == L_TILDE && *s == C_TILDE))
        return true;

    if (CYAML_IS_DIGIT(*s) || ((*s == '-' || *s == '+') && CYAML_IS_DIGIT(s[1])) || *s == '.')
        return true;

    return false;
}

static char* emit_quoted(char* out, char* end, const char* s)
{
    if (out >= end - 1)
        return out;
    *out++ = '"';
    if (s) {
        size_t n = 0;
        for (; *s && out < end - 2 && n < BUILDF_BUF_SIZE; s++, n++) {
            switch (*s) {
            case '"':
                if (out < end - 2) {
                    *out++ = C_BSLASH;
                    *out++ = '"';
                }
                break;
            case C_BSLASH:
                if (out < end - 2) {
                    *out++ = C_BSLASH;
                    *out++ = C_BSLASH;
                }
                break;
            case C_LF:
                if (out < end - 2) {
                    *out++ = C_BSLASH;
                    *out++ = 'n';
                }
                break;
            case C_CR:
                if (out < end - 2) {
                    *out++ = C_BSLASH;
                    *out++ = 'r';
                }
                break;
            case C_TAB:
                if (out < end - 2) {
                    *out++ = C_BSLASH;
                    *out++ = 't';
                }
                break;
            default:
                *out++ = *s;
                break;
            }
        }
    }
    if (out < end)
        *out++ = '"';
    return out;
}

CYAML_API cyaml_node_t* cyaml_vbuildf(cyaml_doc_t* doc, const char* format, va_list ap)
{
    if (!doc || !format)
        return NULL;

    char buf[BUILDF_BUF_SIZE];
    char *out = buf, *end = buf + BUILDF_BUF_SIZE - 1;
    const char* p = format;

    while (*p && out < end) {
        if (*p != '%') {
            *out++ = *p++;
            continue;
        }
        p++;
        if (*p == '%') {
            *out++ = '%';
            p++;
            continue;
        }

        int precision = -1;
        while (CYAML_IS_DIGIT(*p))
            p++;
        if (*p == '.') {
            p++;
            precision = 0;
            while (CYAML_IS_DIGIT(*p))
                precision = precision * 10 + (*p++ - '0');
        }

        int is_long = 0;
        if (*p == 'l') {
            p++;
            is_long = 1;
            if (*p == 'l') {
                p++;
                is_long = 2;
            }
        }

        int written = 0;
        switch (*p) {
        case 'd':
        case 'i': {
            int64_t v = (is_long == 2) ? va_arg(ap, int64_t)
                : (is_long == 1)       ? va_arg(ap, long)
                                       : va_arg(ap, int);
            written = snprintf(out, (size_t)(end - out), "%" PRId64, v);
            break;
        }
        case 'u': {
            uint64_t v = (is_long == 2) ? va_arg(ap, uint64_t)
                : (is_long == 1)        ? va_arg(ap, unsigned long)
                                        : va_arg(ap, unsigned int);
            written = snprintf(out, (size_t)(end - out), "%" PRIu64, v);
            break;
        }
        case 'f': {
            double v = va_arg(ap, double);
            written = (precision >= 0)
                ? snprintf(out, (size_t)(end - out), "%.*f", precision, v)
                : snprintf(out, (size_t)(end - out), "%g", v);
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s)
                s = S_NULL;
            if (buildf_needs_quoting(s)) {
                out = emit_quoted(out, end, s);
                written = 0;
            } else
                written = snprintf(out, (size_t)(end - out), "%s", s);
            break;
        }
        case 'b': {
            int v = va_arg(ap, int);
            written = snprintf(out, (size_t)(end - out), "%s", v ? S_TRUE : S_FALSE);
            break;
        }
        default:
            if (out < end)
                *out++ = '%';
            if (*p && out < end)
                *out++ = *p;
            written = 0;
            break;
        }
        if (written > 0)
            out += written;
        if (*p)
            p++;
    }
    *out = C_NUL;

    cyaml_error_t err;
    cyaml_doc_t* tmp = cyaml_parse(buf, (size_t)(out - buf), NULL, &err);
    if (!tmp)
        return NULL;

    cyaml_node_t* result = cyaml_node_copy(doc, tmp, tmp->root);
    cyaml_free(tmp);
    return result;
}

CYAML_API cyaml_node_t* cyaml_buildf(cyaml_doc_t* doc, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    cyaml_node_t* result = cyaml_vbuildf(doc, format, ap);
    va_end(ap);
    return result;
}

// #endregion

// #region Path Splitting

static bool path_split(const char* path, char* parent, size_t parent_size, const char** key)
{
    size_t len = strlen(path);
    if (len == 0)
        return false;

    const char* last_sep = NULL;
    int bracket_depth = 0;
    for (const char* p = path; *p; p++) {
        if (*p == '[')
            bracket_depth++;
        else if (*p == ']')
            bracket_depth--;
        else if (*p == '/' && bracket_depth == 0)
            last_sep = p;
    }

    if (!last_sep || last_sep == path) {
        parent[0] = '/';
        parent[1] = C_NUL;
        *key = (last_sep == path) ? path + 1 : path;
    } else {
        size_t plen = (size_t)(last_sep - path);
        if (plen >= parent_size)
            return false;
        memcpy(parent, path, plen);
        parent[plen] = C_NUL;
        *key = last_sep + 1;
    }
    return true;
}

static cyaml_node_t* ensure_path(cyaml_doc_t* doc, const char* path)
{
    if (!doc || !path || *path != '/')
        return NULL;

    if (!doc->root) {
        doc->root = cyaml_new_map(doc);
        if (!doc->root)
            return NULL;
    }

    cyaml_node_t* cur = doc->root;
    const char* p = path + 1;
    char seg[256];

    while (*p) {
        const char* start = p;
        while (*p && *p != '/' && *p != '[')
            p++;
        size_t slen = (size_t)(p - start);

        if (slen > 0) {
            if (slen >= sizeof(seg))
                return NULL;
            memcpy(seg, start, slen);
            seg[slen] = C_NUL;

            if (cur->type == CYAML_MAP) {
                cyaml_node_t* child = cyaml_get(doc, cur, seg);
                if (!child) {
                    child = cyaml_new_map(doc);
                    if (!child || !cyaml_map_set(doc, cur, seg, child))
                        return NULL;
                }
                cur = child;
            } else if (cur->type == CYAML_SEQ) {
                int64_t idx;
                if (!cyaml_str_to_i64(seg, NULL, &idx))
                    return NULL;
                if (idx < 0)
                    idx += (int64_t)cur->seq.count;
                if (idx < 0 || idx >= (int64_t)cur->seq.count)
                    return NULL;
                cur = cur->seq.items[idx];
            } else {
                return NULL;
            }
        }

        while (*p == '[') {
            p++;
            const char* idx_start = p;
            while (*p && *p != ']')
                p++;
            if (*p != ']')
                return NULL;

            size_t ilen = (size_t)(p - idx_start);
            p++;

            if (cur->type != CYAML_SEQ || ilen >= sizeof(seg))
                return NULL;
            memcpy(seg, idx_start, ilen);
            seg[ilen] = C_NUL;

            int64_t idx;
            if (!cyaml_str_to_i64(seg, NULL, &idx))
                return NULL;
            if (idx < 0)
                idx += (int64_t)cur->seq.count;
            if (idx < 0 || idx >= (int64_t)cur->seq.count)
                return NULL;
            cur = cur->seq.items[idx];
        }
        if (*p == '/')
            p++;
    }
    return cur;
}

// #endregion

// #region Path-based Modification

CYAML_API bool cyaml_insert_at(cyaml_doc_t* doc, const char* path, cyaml_node_t* val)
{
    if (!doc || !path || !val || !*path)
        return false;

    if (path[0] == '/' && path[1] == C_NUL) {
        doc->root = val;
        return true;
    }

    char parent_path[512];
    const char* key;
    if (!path_split(path, parent_path, sizeof(parent_path), &key) || !*key)
        return false;

    cyaml_node_t* parent = ensure_path(doc, parent_path);
    if (!parent)
        return false;

    if (parent->type == CYAML_MAP)
        return cyaml_map_set(doc, parent, key, val);

    if (parent->type == CYAML_SEQ) {
        int64_t idx;
        if (!cyaml_str_to_i64(key, NULL, &idx))
            return false;
        if (idx < 0)
            idx += (int64_t)parent->seq.count;
        if (idx < 0 || idx > (int64_t)parent->seq.count)
            return false;
        if (idx == (int64_t)parent->seq.count)
            return cyaml_seq_push(parent, val);
        parent->seq.items[idx] = val;
        return true;
    }
    return false;
}

CYAML_API bool cyaml_insertf(cyaml_doc_t* doc, const char* path, const char* format, ...)
{
    if (!doc || !path || !format)
        return false;
    va_list ap;
    va_start(ap, format);
    cyaml_node_t* val = cyaml_vbuildf(doc, format, ap);
    va_end(ap);
    return val ? cyaml_insert_at(doc, path, val) : false;
}

CYAML_API bool cyaml_delete_at(cyaml_doc_t* doc, const char* path)
{
    if (!doc || !path || !*path)
        return false;

    char parent_path[512];
    const char* key;
    if (!path_split(path, parent_path, sizeof(parent_path), &key))
        return false;

    cyaml_node_t* parent = cyaml_path(doc, parent_path);
    if (!parent)
        return false;

    const char* src = cyaml_src(doc);

    if (parent->type == CYAML_MAP) {
        size_t klen = strlen(key);
        for (uint32_t i = 0; i < parent->map.count; i++) {
            cyaml_node_t* k = parent->map.pairs[i].key;
            if (k && k->type == CYAML_SCALAR && k->span.len == klen && memcmp(src + k->span.off, key, klen) == 0) {
                memmove(&parent->map.pairs[i], &parent->map.pairs[i + 1],
                    (parent->map.count - i - 1) * sizeof(cyaml_pair_t));
                parent->map.count--;
                return true;
            }
        }
        return false;
    }

    if (parent->type == CYAML_SEQ) {
        int64_t idx;
        if (!cyaml_str_to_i64(key, NULL, &idx))
            return false;
        if (idx < 0)
            idx += (int64_t)parent->seq.count;
        if (idx < 0 || idx >= (int64_t)parent->seq.count)
            return false;
        memmove(&parent->seq.items[idx], &parent->seq.items[idx + 1],
            (parent->seq.count - (uint32_t)idx - 1) * sizeof(cyaml_node_t*));
        parent->seq.count--;
        return true;
    }
    return false;
}

CYAML_API bool cyaml_append_at(cyaml_doc_t* doc, const char* path, cyaml_node_t* val)
{
    if (!doc || !path || !val)
        return false;
    cyaml_node_t* seq = cyaml_path(doc, path);
    if (!seq || seq->type != CYAML_SEQ)
        return false;
    return cyaml_seq_push(seq, val);
}

CYAML_API bool cyaml_appendf(cyaml_doc_t* doc, const char* path, const char* format, ...)
{
    if (!doc || !path || !format)
        return false;
    va_list ap;
    va_start(ap, format);
    cyaml_node_t* val = cyaml_vbuildf(doc, format, ap);
    va_end(ap);
    return val ? cyaml_append_at(doc, path, val) : false;
}

// #endregion

// #region Node Comparison

CYAML_API int cyaml_node_cmp_str(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* str, size_t len)
{
    if (!doc || !node || !str)
        return node ? 1 : (str ? -1 : 0);
    if (node->type != CYAML_SCALAR)
        return 1;

    const char* src = cyaml_src(doc);
    if (!src)
        return 1;

    if (len == 0)
        len = strlen(str);

    if (node->style == CYAML_PLAIN) {
        if (node->span.len != len)
            return (int)node->span.len - (int)len;
        return memcmp(src + node->span.off, str, len);
    }

    char* val = cyaml_scalar_str(doc, node);
    if (!val)
        return 1;

    size_t vlen = strlen(val);
    int result;
    if (vlen != len)
        result = (int)vlen - (int)len;
    else
        result = memcmp(val, str, len);

    free(val);
    return result;
}

CYAML_API bool cyaml_node_eq_str(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* str)
{
    return cyaml_node_cmp_str(doc, node, str, 0) == 0;
}

// #endregion
