
#include "cyaml_internal.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>

// #region Pool Allocation

//! Chunked pool allocation - nodes never move once allocated
//! This prevents pointer invalidation when pool grows
cyaml_node_t* cyaml_pool_alloc(cyaml_doc_t* doc)
{
    if (!doc)
        return NULL;

    uint32_t chunk_idx = doc->pool.count / CYAML_POOL_CHUNK_SIZE;
    uint32_t node_idx = doc->pool.count % CYAML_POOL_CHUNK_SIZE;

    if (chunk_idx >= doc->pool.chunk_count) {
        if (chunk_idx >= doc->pool.chunk_cap) {
            uint32_t new_cap = doc->pool.chunk_cap ? doc->pool.chunk_cap * 2 : 4;
            cyaml_node_t** new_chunks = realloc(doc->pool.chunks,
                (size_t)new_cap * sizeof(cyaml_node_t*));
            if (!new_chunks)
                return NULL;
            doc->pool.chunks = new_chunks;
            doc->pool.chunk_cap = new_cap;
        }

        cyaml_node_t* new_chunk = calloc(CYAML_POOL_CHUNK_SIZE, sizeof(cyaml_node_t));
        if (!new_chunk)
            return NULL;
        doc->pool.chunks[chunk_idx] = new_chunk;
        doc->pool.chunk_count++;
    }

    cyaml_node_t* n = &doc->pool.chunks[chunk_idx][node_idx];
    doc->pool.count++;
    return n;
}

// #endregion

// #region Span Utilities

CYAML_API char* cyaml_span_dup(const cyaml_doc_t* doc, cyaml_span_t s)
{
    const char* src = cyaml_src(doc);
    if (!src || !s.len)
        return NULL;
    return cyaml_strndup(src + s.off, s.len);
}

CYAML_API char* cyaml_scalar_str(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    if (!doc || !n || n->type != CYAML_SCALAR)
        return NULL;

    const char* src = cyaml_src(doc);
    if (!src)
        return NULL;

    const char* p = src + n->span.off;
    const char* end = p + n->span.len;

    if (n->style == CYAML_LITERAL || n->style == CYAML_FOLDED) {
        char* str = malloc(n->span.len + n->leading_breaks + n->trailing_breaks + 2);
        if (!str)
            return NULL;

        char* out = str;
        for (uint8_t i = 0; i < n->leading_breaks; i++)
            *out++ = C_LF;

        uint32_t indent = n->indent;
        bool is_folded = (n->style == CYAML_FOLDED);
        bool prev_more_indented = false;
        bool skip_indent = false;

        while (p < end) {
            if (!skip_indent) {
                for (uint32_t i = 0; i < indent && p < end && *p == C_SP; i++)
                    p++;
            }
            skip_indent = false;

            bool more_indented = (p < end && (*p == C_SP || *p == C_TAB));
            const char* line_start = out;
            while (p < end && *p != C_LF)
                *out++ = *p++;
            size_t line_len = (size_t)(out - line_start);

            if (p < end && *p == C_LF) {
                p++;
                if (!is_folded) {
                    *out++ = C_LF;
                } else {
                    int empty_lines = 0;
                    const char* look = p;
                    while (look < end) {
                        for (uint32_t i = 0; i < indent && look < end && *look == C_SP; i++)
                            look++;
                        if (look < end && *look == C_LF) {
                            empty_lines++;
                            look++;
                        } else
                            break;
                    }
                    bool next_more_indented = (look < end && (*look == C_SP || *look == C_TAB));

                    if (empty_lines > 0) {
                        int newlines = empty_lines + (more_indented || next_more_indented ? 1 : 0);
                        for (int i = 0; i < newlines; i++)
                            *out++ = C_LF;
                        p = look;
                        skip_indent = true;
                        prev_more_indented = false;
                    } else if (more_indented || next_more_indented || prev_more_indented || line_len == 0) {
                        *out++ = C_LF;
                        prev_more_indented = more_indented;
                    } else {
                        *out++ = C_SP;
                        prev_more_indented = more_indented;
                    }
                }
            }
        }

        if (n->chomp == CYAML_CLIP && out > str)
            *out++ = C_LF;
        else if (n->chomp == CYAML_KEEP) {
            for (uint8_t i = 0; i < n->trailing_breaks; i++)
                *out++ = C_LF;
        }
        *out = C_NUL;
        return str;
    }

    char* str = malloc(n->span.len + 16);
    if (!str)
        return NULL;

    char* out = str;
    while (p < end) {
        if (n->style == CYAML_SINGLE && *p == '\'' && p + 1 < end && p[1] == '\'') {
            *out++ = '\'';
            p += 2;
            continue;
        }

        if (n->style == CYAML_DOUBLE && *p == C_BSLASH && p + 1 < end) {
            p++;
            if (*p == C_LF || *p == C_CR) {
                if (*p == C_CR)
                    p++;
                if (p < end && *p == C_LF)
                    p++;
                while (p < end && (*p == C_SP || *p == C_TAB))
                    p++;
            } else {
                int consumed;
                cyaml_cp_t cp = cyaml_parse_escape(p, (size_t)(end - p), &consumed);
                if (consumed > 0) {
                    out += cyaml_utf8_encode(cp, out);
                    p += consumed;
                } else {
                    *out++ = *p++;
                }
            }
            continue;
        }

        if (*p == C_SP || *p == C_TAB) {
            const char* look = p;
            while (look < end && (*look == C_SP || *look == C_TAB))
                look++;
            if (look < end && (*look == C_LF || *look == C_CR)) {
                p = look;
                continue;
            }
            *out++ = *p++;
            continue;
        }

        if (*p == C_LF || *p == C_CR) {
            if (*p == C_CR)
                p++;
            if (p < end && *p == C_LF)
                p++;

            int empty_lines = 0;
            while (p < end) {
                while (p < end && (*p == C_SP || *p == C_TAB))
                    p++;
                if (p < end && (*p == C_LF || *p == C_CR)) {
                    empty_lines++;
                    if (*p == C_CR)
                        p++;
                    if (p < end && *p == C_LF)
                        p++;
                } else
                    break;
            }

            if (empty_lines > 0) {
                for (int i = 0; i < empty_lines; i++)
                    *out++ = C_LF;
            } else {
                *out++ = C_SP;
            }
            continue;
        }

        *out++ = *p++;
    }

    *out = C_NUL;
    return str;
}

CYAML_API bool cyaml_span_eq(const cyaml_doc_t* doc, cyaml_span_t s, const char* str)
{
    const char* src = cyaml_src(doc);
    if (!src || !str)
        return false;
    size_t len = strlen(str);
    if (s.len != len)
        return false;
    return memcmp(src + s.off, str, len) == 0;
}

CYAML_API bool cyaml_span_ieq(const cyaml_doc_t* doc, cyaml_span_t s, const char* str)
{
    const char* src = cyaml_src(doc);
    if (!src || !str)
        return false;
    size_t len = strlen(str);
    if (s.len != len)
        return false;
    return cyaml_memicmp(src + s.off, str, len) == 0;
}

CYAML_API bool cyaml_span_cmp(const cyaml_doc_t* doc, cyaml_span_t a, cyaml_span_t b)
{
    const char* src = cyaml_src(doc);
    if (!src)
        return false;
    if (a.len != b.len)
        return false;
    return memcmp(src + a.off, src + b.off, a.len) == 0;
}

// #endregion

// #region Value Extraction (YAML 1.2 Spec Chapter 10 - Recommended Schemas)

//! [10.2.1.1] Null
//! Matches: null | Null | NULL | ~ | (empty)
CYAML_API bool cyaml_is_null_val(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    if (!n)
        return true;
    if (n->type == CYAML_NULL || n->type == CYAML_NONE)
        return true;
    if (n->type != CYAML_SCALAR)
        return false;

    cyaml_span_t s = n->span;
    if (s.len == 0)
        return true;

    const char* src = cyaml_src(doc);
    if (!src)
        return false;
    const char* p = src + s.off;
    if (s.len == L_TILDE && p[0] == C_TILDE)
        return true;
    if (s.len == L_NULL && cyaml_memicmp(p, S_NULL, L_NULL) == 0)
        return true;
    return false;
}

//! [10.2.1.2] Boolean
//! True: true | True | TRUE
//! False: false | False | FALSE
CYAML_API bool cyaml_as_bool(const cyaml_doc_t* doc, const cyaml_node_t* n, bool* out)
{
    if (!doc || !n || n->type != CYAML_SCALAR || !out)
        return false;

    const char* src = cyaml_src(doc);
    if (!src)
        return false;

    cyaml_span_t s = n->span;
    const char* p = src + s.off;

    if (s.len == L_TRUE && cyaml_memicmp(p, S_TRUE, L_TRUE) == 0) {
        *out = true;
        return true;
    }
    if (s.len == L_FALSE && cyaml_memicmp(p, S_FALSE, L_FALSE) == 0) {
        *out = false;
        return true;
    }

    return false;
}

//! [10.2.1.3] Integer
//! Decimal: [-+]?[0-9]+
//! Octal: 0o[0-7]+
//! Hex: 0x[0-9a-fA-F]+
CYAML_API bool cyaml_as_int(const cyaml_doc_t* doc, const cyaml_node_t* n, int64_t* out)
{
    if (!doc || !n || n->type != CYAML_SCALAR || !out)
        return false;

    char* str = cyaml_span_dup(doc, n->span);
    if (!str)
        return false;

    const char* end;
    bool ok = cyaml_str_to_i64(str, &end, out);
    ok = ok && (*end == C_NUL);
    free(str);
    return ok;
}

CYAML_API bool cyaml_as_uint(const cyaml_doc_t* doc, const cyaml_node_t* n, uint64_t* out)
{
    if (!doc || !n || n->type != CYAML_SCALAR || !out)
        return false;

    char* str = cyaml_span_dup(doc, n->span);
    if (!str)
        return false;

    const char* end;
    bool ok = cyaml_str_to_u64(str, &end, out);
    ok = ok && (*end == C_NUL);
    free(str);
    return ok;
}

//! [10.2.1.4] Floating Point
//! Decimal: [-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?
//! Infinity: [-+]?(\.inf|\.Inf|\.INF)
//! Not-a-Number: \.nan|\.NaN|\.NAN
CYAML_API bool cyaml_as_float(const cyaml_doc_t* doc, const cyaml_node_t* n, double* out)
{
    if (!doc || !n || n->type != CYAML_SCALAR || !out)
        return false;

    const char* src = cyaml_src(doc);
    if (!src)
        return false;

    cyaml_span_t s = n->span;
    const char* p = src + s.off;

    if (s.len == L_NAN && cyaml_memicmp(p, S_NAN, L_NAN) == 0) {
        *out = NAN;
        return true;
    }
    if (s.len == L_INF && cyaml_memicmp(p, S_INF, L_INF) == 0) {
        *out = INFINITY;
        return true;
    }
    if (s.len == L_PINF && cyaml_memicmp(p, S_PINF, L_PINF) == 0) {
        *out = INFINITY;
        return true;
    }
    if (s.len == L_NINF && cyaml_memicmp(p, S_NINF, L_NINF) == 0) {
        *out = -INFINITY;
        return true;
    }

    char* str = cyaml_span_dup(doc, s);
    if (!str)
        return false;

    const char* end;
    double val;
    bool ok = cyaml_str_to_f64(str, &end, &val) && *end == C_NUL;
    free(str);

    if (ok)
        *out = val;
    return ok;
}

CYAML_API cyaml_scalar_kind_t cyaml_scalar_kind(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    if (!n || n->type == CYAML_NULL || n->type == CYAML_NONE)
        return CYAML_KIND_NULL;
    if (n->type != CYAML_SCALAR)
        return CYAML_KIND_STRING;

    if (n->style == CYAML_SINGLE || n->style == CYAML_DOUBLE)
        return CYAML_KIND_STRING;

    cyaml_span_t s = n->span;
    if (s.len == 0)
        return CYAML_KIND_NULL;

    const char* src = cyaml_src(doc);
    if (!src)
        return CYAML_KIND_STRING;

    const char* p = src + s.off;

    if (s.len == L_TILDE && p[0] == C_TILDE)
        return CYAML_KIND_NULL;
    if (s.len == L_NULL && cyaml_memicmp(p, S_NULL, L_NULL) == 0)
        return CYAML_KIND_NULL;

    if (s.len == L_TRUE && cyaml_memicmp(p, S_TRUE, L_TRUE) == 0)
        return CYAML_KIND_BOOL;
    if (s.len == L_FALSE && cyaml_memicmp(p, S_FALSE, L_FALSE) == 0)
        return CYAML_KIND_BOOL;

    if (s.len == L_NAN && cyaml_memicmp(p, S_NAN, L_NAN) == 0)
        return CYAML_KIND_FLOAT;
    if (s.len == L_INF && cyaml_memicmp(p, S_INF, L_INF) == 0)
        return CYAML_KIND_FLOAT;
    if (s.len == L_PINF && cyaml_memicmp(p, S_PINF, L_PINF) == 0)
        return CYAML_KIND_FLOAT;
    if (s.len == L_NINF && cyaml_memicmp(p, S_NINF, L_NINF) == 0)
        return CYAML_KIND_FLOAT;

    if (cyaml_scan_int(p, s.len))
        return CYAML_KIND_INT;
    if (cyaml_scan_float(p, s.len))
        return CYAML_KIND_FLOAT;

    return CYAML_KIND_STRING;
}

// #endregion

// #region Mapping/Sequence Access

CYAML_API cyaml_node_t* cyaml_get(const cyaml_doc_t* doc, const cyaml_node_t* n, const char* key)
{
    if (!doc || !n || n->type != CYAML_MAP || !key)
        return NULL;

    const char* src = cyaml_src(doc);
    if (!src)
        return NULL;

    size_t key_len = strlen(key);
    for (uint32_t i = 0; i < n->map.count; i++) {
        cyaml_node_t* k = n->map.pairs[i].key;
        if (k && k->type == CYAML_SCALAR && k->span.len == key_len && memcmp(src + k->span.off, key, key_len) == 0) {
            return n->map.pairs[i].val;
        }
    }
    return NULL;
}

CYAML_API bool cyaml_has(const cyaml_doc_t* doc, const cyaml_node_t* n, const char* key)
{
    return cyaml_get(doc, n, key) != NULL;
}

// #endregion

// #region Path Access

CYAML_API cyaml_node_t* cyaml_path(const cyaml_doc_t* doc, const char* path)
{
    return cyaml_path_first(doc, NULL, path);
}

// #endregion

// #region Document Management

CYAML_API cyaml_doc_t* cyaml_doc_new(void)
{
    cyaml_doc_t* doc = calloc(1, sizeof(cyaml_doc_t));
    if (doc) {
        doc->mode = CYAML_BUILDING;
        doc->version.major = 1;
        doc->version.minor = 2;
    }
    return doc;
}

CYAML_API void cyaml_free(cyaml_doc_t* doc)
{
    if (!doc)
        return;

    uint32_t remaining = doc->pool.count;
    for (uint32_t c = 0; c < doc->pool.chunk_count; c++) {
        cyaml_node_t* chunk = doc->pool.chunks[c];
        uint32_t nodes_in_chunk = (remaining > CYAML_POOL_CHUNK_SIZE)
            ? CYAML_POOL_CHUNK_SIZE
            : remaining;

        for (uint32_t i = 0; i < nodes_in_chunk; i++) {
            cyaml_node_t* n = &chunk[i];
            if (n->type == CYAML_SEQ) {
                free(n->seq.items);
            } else if (n->type == CYAML_MAP) {
                free(n->map.pairs);
            }
        }
        free(chunk);
        remaining -= nodes_in_chunk;
    }

    free(doc->pool.chunks);

    if (doc->mode == CYAML_BUILDING) {
        free(doc->src.owned.ptr);
    }

    if (doc->comments) {
        free(doc->comments->items);
        free(doc->comments);
    }

    free(doc);
}

//! Append data to builder buffer, returns offset
bool cyaml_doc_append(cyaml_doc_t* doc, const char* data, size_t len, uint32_t* out_off)
{
    if (!doc || doc->mode != CYAML_BUILDING || !data)
        return false;

    uint32_t needed = doc->src.owned.len + (uint32_t)len;
    if (needed > doc->src.owned.cap) {
        uint32_t new_cap = doc->src.owned.cap ? doc->src.owned.cap * 2 : 256;
        while (new_cap < needed)
            new_cap *= 2;
        char* new_buf = realloc(doc->src.owned.ptr, new_cap);
        if (!new_buf)
            return false;
        doc->src.owned.ptr = new_buf;
        doc->src.owned.cap = new_cap;
    }

    if (out_off)
        *out_off = doc->src.owned.len;
    memcpy(doc->src.owned.ptr + doc->src.owned.len, data, len);
    doc->src.owned.len += (uint32_t)len;
    return true;
}

CYAML_API void cyaml_stream_free(cyaml_stream_t* stream)
{
    if (!stream)
        return;

    for (uint32_t i = 0; i < stream->count; i++) {
        cyaml_free(stream->docs[i]);
    }
    free(stream->docs);
    // Source is borrowed - caller manages lifetime
    free(stream);
}

// #endregion

// #region Node Creation (Builder API)

CYAML_API cyaml_node_t* cyaml_node_new(cyaml_doc_t* doc, cyaml_type_t type)
{
    cyaml_node_t* n = cyaml_pool_alloc(doc);
    if (n)
        n->type = type;
    return n;
}

CYAML_API cyaml_node_t* cyaml_new_null(cyaml_doc_t* doc)
{
    return cyaml_node_new(doc, CYAML_NULL);
}

CYAML_API cyaml_node_t* cyaml_new_str(cyaml_doc_t* doc, const char* str, size_t len)
{
    if (!doc || !str)
        return NULL;

    cyaml_node_t* n = cyaml_node_new(doc, CYAML_SCALAR);
    if (!n)
        return NULL;

    uint32_t off;
    if (!cyaml_doc_append(doc, str, len, &off)) {
        return NULL;
    }

    n->span.off = off;
    n->span.len = (uint32_t)len;
    return n;
}

CYAML_API cyaml_node_t* cyaml_new_cstr(cyaml_doc_t* doc, const char* str)
{
    return cyaml_new_str(doc, str, str ? strlen(str) : 0);
}

CYAML_API cyaml_node_t* cyaml_new_int(cyaml_doc_t* doc, int64_t val)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%" PRId64, val);
    return cyaml_new_str(doc, buf, (size_t)len);
}

CYAML_API cyaml_node_t* cyaml_new_uint(cyaml_doc_t* doc, uint64_t val)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%" PRIu64, val);
    return cyaml_new_str(doc, buf, (size_t)len);
}

CYAML_API cyaml_node_t* cyaml_new_float(cyaml_doc_t* doc, double val)
{
    if (isnan(val))
        return cyaml_new_str(doc, S_NAN, L_NAN);
    if (isinf(val))
        return cyaml_new_str(doc, val > 0 ? S_INF : S_NINF, val > 0 ? L_INF : L_NINF);
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", val);
    return cyaml_new_str(doc, buf, (size_t)len);
}

CYAML_API cyaml_node_t* cyaml_new_bool(cyaml_doc_t* doc, bool val)
{
    return cyaml_new_str(doc, val ? S_TRUE : S_FALSE, val ? L_TRUE : L_FALSE);
}

CYAML_API cyaml_node_t* cyaml_new_seq(cyaml_doc_t* doc)
{
    return cyaml_node_new(doc, CYAML_SEQ);
}

CYAML_API cyaml_node_t* cyaml_new_map(cyaml_doc_t* doc)
{
    return cyaml_node_new(doc, CYAML_MAP);
}

CYAML_API bool cyaml_seq_push(cyaml_node_t* seq, cyaml_node_t* item)
{
    if (!seq || seq->type != CYAML_SEQ || !item)
        return false;

    if (seq->seq.count >= seq->seq.cap) {
        uint32_t new_cap = seq->seq.cap ? seq->seq.cap * 2 : SEQ_INIT_CAP;
        cyaml_node_t** new_items = realloc(seq->seq.items, (size_t)new_cap * sizeof(cyaml_node_t*));
        if (!new_items)
            return false;
        seq->seq.items = new_items;
        seq->seq.cap = new_cap;
    }

    seq->seq.items[seq->seq.count++] = item;
    return true;
}

CYAML_API bool cyaml_map_set(cyaml_doc_t* doc, cyaml_node_t* map,
    const char* key, cyaml_node_t* val)
{
    if (!doc || !map || map->type != CYAML_MAP || !key || !val)
        return false;

    const char* src = cyaml_src(doc);
    size_t key_len = strlen(key);
    for (uint32_t i = 0; i < map->map.count; i++) {
        cyaml_node_t* k = map->map.pairs[i].key;
        if (k && k->type == CYAML_SCALAR && k->span.len == key_len && src && memcmp(src + k->span.off, key, key_len) == 0) {
            map->map.pairs[i].val = val;
            return true;
        }
    }

    cyaml_node_t* key_node = cyaml_new_cstr(doc, key);
    if (!key_node)
        return false;

    if (map->map.count >= map->map.cap) {
        uint32_t new_cap = map->map.cap ? map->map.cap * 2 : MAP_INIT_CAP;
        cyaml_pair_t* new_pairs = realloc(map->map.pairs, (size_t)new_cap * sizeof(cyaml_pair_t));
        if (!new_pairs)
            return false;
        map->map.pairs = new_pairs;
        map->map.cap = new_cap;
    }

    map->map.pairs[map->map.count].key = key_node;
    map->map.pairs[map->map.count].val = val;
    map->map.count++;
    return true;
}

// #endregion

// #region Anchor and Alias Management

CYAML_API bool cyaml_set_anchor(cyaml_doc_t* doc, cyaml_node_t* node, const char* anchor)
{
    if (!doc || !node)
        return false;

    if (!anchor) {
        node->anchor = (cyaml_span_t) { 0 };
        return true;
    }

    size_t len = strlen(anchor);
    uint32_t off;
    if (!cyaml_doc_append(doc, anchor, len, &off))
        return false;

    node->anchor.off = off;
    node->anchor.len = (uint32_t)len;
    return true;
}

CYAML_API cyaml_node_t* cyaml_new_alias(cyaml_doc_t* doc, cyaml_node_t* target)
{
    if (!doc || !target || !target->anchor.len)
        return NULL;

    cyaml_node_t* node = cyaml_node_new(doc, CYAML_ALIAS);
    if (!node)
        return NULL;

    node->alias.target = target;
    node->span = target->anchor;
    return node;
}

static cyaml_node_t* find_anchor_in_node(const cyaml_doc_t* doc, cyaml_node_t* node,
    const char* anchor, size_t anchor_len)
{
    if (!node)
        return NULL;

    if (node->anchor.len == anchor_len) {
        const char* src = cyaml_src(doc);
        if (src && memcmp(src + node->anchor.off, anchor, anchor_len) == 0) {
            return node;
        }
    }

    switch (node->type) {
    case CYAML_SEQ:
        for (uint32_t i = 0; i < node->seq.count; i++) {
            cyaml_node_t* found = find_anchor_in_node(doc, node->seq.items[i], anchor, anchor_len);
            if (found)
                return found;
        }
        break;
    case CYAML_MAP:
        for (uint32_t i = 0; i < node->map.count; i++) {
            cyaml_node_t* found = find_anchor_in_node(doc, node->map.pairs[i].key, anchor, anchor_len);
            if (found)
                return found;
            found = find_anchor_in_node(doc, node->map.pairs[i].val, anchor, anchor_len);
            if (found)
                return found;
        }
        break;
    default:
        break;
    }
    return NULL;
}

CYAML_API cyaml_node_t* cyaml_find_anchor(const cyaml_doc_t* doc, const char* anchor)
{
    if (!doc || !anchor || !doc->root)
        return NULL;
    return find_anchor_in_node(doc, doc->root, anchor, strlen(anchor));
}

// #endregion

// #region Node Copy and Merge

static bool copy_span_to_doc(cyaml_doc_t* dst, const cyaml_doc_t* src_doc,
    cyaml_span_t src_span, cyaml_span_t* out)
{
    if (!src_span.len) {
        *out = (cyaml_span_t) { 0 };
        return true;
    }

    if (src_doc == dst) {
        *out = src_span;
        return true;
    }

    const char* src = cyaml_src(src_doc);
    if (!src)
        return false;

    uint32_t off;
    if (!cyaml_doc_append(dst, src + src_span.off, src_span.len, &off))
        return false;

    *out = src_span;
    out->off = off;
    return true;
}

CYAML_API cyaml_node_t* cyaml_node_copy(cyaml_doc_t* doc, const cyaml_doc_t* src_doc,
    const cyaml_node_t* node)
{
    if (!doc || !node)
        return NULL;
    if (!src_doc)
        src_doc = doc;

    cyaml_node_t* copy = cyaml_node_new(doc, node->type);
    if (!copy)
        return NULL;

    copy->style = node->style;
    copy->chomp = node->chomp;
    copy->indent = node->indent;
    copy->leading_breaks = node->leading_breaks;
    copy->trailing_breaks = node->trailing_breaks;

    if (!copy_span_to_doc(doc, src_doc, node->span, &copy->span))
        return NULL;
    if (!copy_span_to_doc(doc, src_doc, node->tag, &copy->tag))
        return NULL;
    if (!copy_span_to_doc(doc, src_doc, node->anchor, &copy->anchor))
        return NULL;

    switch (node->type) {
    case CYAML_SCALAR:
        copy->scalar.flags = node->scalar.flags;
        break;

    case CYAML_SEQ:
        for (uint32_t i = 0; i < node->seq.count; i++) {
            cyaml_node_t* child = cyaml_node_copy(doc, src_doc, node->seq.items[i]);
            if (!child)
                return NULL;
            if (!cyaml_seq_push(copy, child))
                return NULL;
        }
        break;

    case CYAML_MAP:
        for (uint32_t i = 0; i < node->map.count; i++) {
            cyaml_node_t* key = cyaml_node_copy(doc, src_doc, node->map.pairs[i].key);
            cyaml_node_t* val = cyaml_node_copy(doc, src_doc, node->map.pairs[i].val);
            if (!key || !val)
                return NULL;

            // Add pair directly (not using cyaml_map_set to preserve key node)
            if (copy->map.count >= copy->map.cap) {
                uint32_t new_cap = copy->map.cap ? copy->map.cap * 2 : MAP_INIT_CAP;
                cyaml_pair_t* new_pairs = realloc(copy->map.pairs,
                    (size_t)new_cap * sizeof(cyaml_pair_t));
                if (!new_pairs)
                    return NULL;
                copy->map.pairs = new_pairs;
                copy->map.cap = new_cap;
            }
            copy->map.pairs[copy->map.count].key = key;
            copy->map.pairs[copy->map.count].val = val;
            copy->map.count++;
        }
        break;

    case CYAML_ALIAS:
        // For aliases, copy the target reference
        // Note: The target pointer will be invalid if copying to different doc
        // Caller should use cyaml_resolve_aliases after copying if needed
        copy->alias.target = node->alias.target;
        break;

    default:
        break;
    }

    return copy;
}

static int32_t find_key_index(const cyaml_doc_t* doc, const cyaml_node_t* map,
    const cyaml_node_t* key)
{
    if (!map || map->type != CYAML_MAP || !key)
        return -1;

    const char* src = cyaml_src(doc);
    if (!src)
        return -1;

    for (uint32_t i = 0; i < map->map.count; i++) {
        cyaml_node_t* k = map->map.pairs[i].key;
        if (k && k->type == CYAML_SCALAR && key->type == CYAML_SCALAR && k->span.len == key->span.len && memcmp(src + k->span.off, src + key->span.off, k->span.len) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

CYAML_API bool cyaml_map_merge(cyaml_doc_t* doc, cyaml_node_t* dst, const cyaml_node_t* src)
{
    if (!doc || !dst || dst->type != CYAML_MAP || !src || src->type != CYAML_MAP)
        return false;

    for (uint32_t i = 0; i < src->map.count; i++) {
        cyaml_node_t* src_key = src->map.pairs[i].key;
        cyaml_node_t* src_val = src->map.pairs[i].val;

        int32_t idx = find_key_index(doc, dst, src_key);

        if (idx < 0) {
            cyaml_node_t* key_copy = cyaml_node_copy(doc, doc, src_key);
            cyaml_node_t* val_copy = cyaml_node_copy(doc, doc, src_val);
            if (!key_copy || !val_copy)
                return false;

            if (dst->map.count >= dst->map.cap) {
                uint32_t new_cap = dst->map.cap ? dst->map.cap * 2 : MAP_INIT_CAP;
                cyaml_pair_t* new_pairs = realloc(dst->map.pairs,
                    (size_t)new_cap * sizeof(cyaml_pair_t));
                if (!new_pairs)
                    return false;
                dst->map.pairs = new_pairs;
                dst->map.cap = new_cap;
            }
            dst->map.pairs[dst->map.count].key = key_copy;
            dst->map.pairs[dst->map.count].val = val_copy;
            dst->map.count++;
        } else {
            cyaml_node_t* dst_val = dst->map.pairs[idx].val;
            if (dst_val->type == CYAML_MAP && src_val->type == CYAML_MAP) {
                if (!cyaml_map_merge(doc, dst_val, src_val))
                    return false;
            } else {
                cyaml_node_t* val_copy = cyaml_node_copy(doc, doc, src_val);
                if (!val_copy)
                    return false;
                dst->map.pairs[idx].val = val_copy;
            }
        }
    }

    return true;
}

#define RESOLVE_STACK_INIT_CAP 32
#define RESOLVE_VISITED_INIT_CAP 32

typedef enum {
    RFRAME_NODE,
    RFRAME_SEQ,
    RFRAME_MAP_KEY,
    RFRAME_MAP_VAL
} resolve_frame_state_t;

typedef struct {
    cyaml_node_t** node_ptr;
    uint32_t child_idx;
    resolve_frame_state_t state;
} resolve_frame_t;

#define RESOLVE_PUSH(stk, cnt, cap, ptr, st, on_fail)                        \
    do {                                                                     \
        if ((cnt) >= (cap)) {                                                \
            size_t new_cap = (cap) * 2;                                      \
            resolve_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk))); \
            if (!tmp) {                                                      \
                on_fail;                                                     \
            }                                                                \
            (stk) = tmp;                                                     \
            (cap) = new_cap;                                                 \
        }                                                                    \
        (stk)[(cnt)].node_ptr = (ptr);                                       \
        (stk)[(cnt)].child_idx = 0;                                          \
        (stk)[(cnt)].state = (st);                                           \
        (cnt)++;                                                             \
    } while (0)

#define VISITED_PUSH(arr, cnt, cap, nd, on_fail)                                 \
    do {                                                                         \
        if ((cnt) >= (cap)) {                                                    \
            size_t new_cap = (cap) * 2;                                          \
            const cyaml_node_t** tmp = realloc((arr), new_cap * sizeof(*(arr))); \
            if (!tmp) {                                                          \
                on_fail;                                                         \
            }                                                                    \
            (arr) = tmp;                                                         \
            (cap) = new_cap;                                                     \
        }                                                                        \
        (arr)[(cnt)++] = (nd);                                                   \
    } while (0)

CYAML_API bool cyaml_resolve_aliases(cyaml_doc_t* doc)
{
    if (!doc || !doc->root)
        return doc != NULL;

    resolve_frame_t* stack = malloc(RESOLVE_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        return false;
    size_t stack_count = 0;
    size_t stack_cap = RESOLVE_STACK_INIT_CAP;

    const cyaml_node_t** visited = malloc(RESOLVE_VISITED_INIT_CAP * sizeof(*visited));
    if (!visited) {
        free(stack);
        return false;
    }
    size_t visited_count = 0;
    size_t visited_cap = RESOLVE_VISITED_INIT_CAP;

    bool result = true;

#define RPUSH(ptr, st) RESOLVE_PUSH(stack, stack_count, stack_cap, ptr, st, { result = false; goto cleanup; })
#define VPUSH(nd) VISITED_PUSH(visited, visited_count, visited_cap, nd, { result = false; goto cleanup; })

    RPUSH(&doc->root, RFRAME_NODE);

    while (stack_count > 0) {
        resolve_frame_t* f = &stack[stack_count - 1];

        switch (f->state) {
        case RFRAME_NODE: {
            if (!f->node_ptr || !*f->node_ptr) {
                stack_count--;
                break;
            }
            cyaml_node_t* node = *f->node_ptr;

            if (node->type == CYAML_ALIAS) {
                if (!node->alias.target) {
                    result = false;
                    goto cleanup;
                }
                bool is_cyclic = false;
                for (size_t i = 0; i < visited_count; i++) {
                    if (visited[i] == node->alias.target) {
                        is_cyclic = true;
                        break;
                    }
                }
                if (is_cyclic) {
                    result = false;
                    goto cleanup;
                }
                VPUSH(node->alias.target);
                cyaml_node_t* copy = cyaml_node_copy(doc, doc, node->alias.target);
                visited_count--;
                if (!copy) {
                    result = false;
                    goto cleanup;
                }
                *f->node_ptr = copy;
                break;
            }

            switch (node->type) {
            case CYAML_SEQ:
                if (node->seq.count > 0) {
                    f->state = RFRAME_SEQ;
                    f->child_idx = 0;
                } else {
                    stack_count--;
                }
                break;
            case CYAML_MAP:
                if (node->map.count > 0) {
                    f->state = RFRAME_MAP_KEY;
                    f->child_idx = 0;
                } else {
                    stack_count--;
                }
                break;
            default:
                stack_count--;
                break;
            }
            break;
        }

        case RFRAME_SEQ: {
            cyaml_node_t* node = *f->node_ptr;
            if (f->child_idx >= node->seq.count) {
                stack_count--;
            } else {
                uint32_t idx = f->child_idx++;
                RPUSH(&node->seq.items[idx], RFRAME_NODE);
            }
            break;
        }

        case RFRAME_MAP_KEY: {
            cyaml_node_t* node = *f->node_ptr;
            if (f->child_idx >= node->map.count) {
                stack_count--;
            } else {
                f->state = RFRAME_MAP_VAL;
                RPUSH(&node->map.pairs[f->child_idx].key, RFRAME_NODE);
            }
            break;
        }

        case RFRAME_MAP_VAL: {
            cyaml_node_t* node = *f->node_ptr;
            f->state = RFRAME_MAP_KEY;
            uint32_t idx = f->child_idx++;
            RPUSH(&node->map.pairs[idx].val, RFRAME_NODE);
            break;
        }
        }
    }

#undef RPUSH
#undef VPUSH

cleanup:
    free(stack);
    free(visited);
    return result;
}

// #endregion

// #region Key Sorting

static int default_key_cmp(const cyaml_doc_t* doc, const cyaml_node_t* a, const cyaml_node_t* b)
{
    if (!a || !b)
        return a ? 1 : (b ? -1 : 0);
    if (a->type != CYAML_SCALAR || b->type != CYAML_SCALAR) {
        if (a->type != b->type)
            return (int)a->type - (int)b->type;
        return (int)(a->span.off - b->span.off);
    }

    const char* src = cyaml_src(doc);
    if (!src)
        return 0;

    const char* sa = src + a->span.off;
    const char* sb = src + b->span.off;
    size_t la = a->span.len;
    size_t lb = b->span.len;
    size_t min_len = la < lb ? la : lb;

    int cmp = memcmp(sa, sb, min_len);
    if (cmp != 0)
        return cmp;
    return (la > lb) - (la < lb);
}

CYAML_API bool cyaml_map_sort(const cyaml_doc_t* doc, cyaml_node_t* map, cyaml_key_cmp_t cmp)
{
    if (!map || map->type != CYAML_MAP || map->map.count < 2)
        return true;
    if (!cmp)
        cmp = default_key_cmp;

    cyaml_pair_t* pairs = map->map.pairs;
    uint32_t n = map->map.count;

    for (uint32_t i = 1; i < n; i++) {
        cyaml_pair_t tmp = pairs[i];
        uint32_t j = i;
        while (j > 0 && cmp(doc, pairs[j - 1].key, tmp.key) > 0) {
            pairs[j] = pairs[j - 1];
            j--;
        }
        pairs[j] = tmp;
    }
    return true;
}

#define SORT_STACK_INIT_CAP 32

typedef struct {
    cyaml_node_t* node;
    uint32_t child_idx;
    bool sorted;
} sort_frame_t;

#define SORT_PUSH(stk, cnt, cap, nd, on_fail)                             \
    do {                                                                  \
        if ((cnt) >= (cap)) {                                             \
            size_t new_cap = (cap) * 2;                                   \
            sort_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk))); \
            if (!tmp) {                                                   \
                on_fail;                                                  \
            }                                                             \
            (stk) = tmp;                                                  \
            (cap) = new_cap;                                              \
        }                                                                 \
        (stk)[(cnt)].node = (nd);                                         \
        (stk)[(cnt)].child_idx = 0;                                       \
        (stk)[(cnt)].sorted = false;                                      \
        (cnt)++;                                                          \
    } while (0)

CYAML_API bool cyaml_map_sort_recursive(const cyaml_doc_t* doc, cyaml_node_t* node,
    cyaml_key_cmp_t cmp)
{
    if (!node)
        return true;

    sort_frame_t* stack = malloc(SORT_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        return false;
    size_t stack_count = 0;
    size_t stack_cap = SORT_STACK_INIT_CAP;
    bool result = true;

#define SPUSH(nd) SORT_PUSH(stack, stack_count, stack_cap, nd, { result = false; goto cleanup; })

    SPUSH(node);

    while (stack_count > 0) {
        sort_frame_t* f = &stack[stack_count - 1];
        cyaml_node_t* n = f->node;

        if (!n) {
            stack_count--;
            continue;
        }

        switch (n->type) {
        case CYAML_MAP:
            if (!f->sorted) {
                if (!cyaml_map_sort(doc, n, cmp)) {
                    result = false;
                    goto cleanup;
                }
                f->sorted = true;
            }
            if (f->child_idx >= n->map.count) {
                stack_count--;
            } else {
                uint32_t idx = f->child_idx++;
                SPUSH(n->map.pairs[idx].val);
            }
            break;
        case CYAML_SEQ:
            if (f->child_idx >= n->seq.count) {
                stack_count--;
            } else {
                uint32_t idx = f->child_idx++;
                SPUSH(n->seq.items[idx]);
            }
            break;
        default:
            stack_count--;
            break;
        }
    }

#undef SPUSH

cleanup:
    free(stack);
    return result;
}

// #endregion

// #region Utility

CYAML_API const char* cyaml_strerror(cyaml_err_t err)
{
    switch (err) {
    case CYAML_OK:
        return "Success";
    case CYAML_ERR_NOMEM:
        return "Out of memory";
    case CYAML_ERR_SYNTAX:
        return "Syntax error";
    case CYAML_ERR_EOF:
        return "Unexpected end of input";
    case CYAML_ERR_INDENT:
        return "Invalid indentation";
    case CYAML_ERR_ESCAPE:
        return "Invalid escape sequence";
    case CYAML_ERR_ANCHOR:
        return "Invalid anchor";
    case CYAML_ERR_ALIAS:
        return "Undefined alias";
    case CYAML_ERR_TAG:
        return "Invalid tag";
    case CYAML_ERR_DUP_KEY:
        return "Duplicate key";
    case CYAML_ERR_IO:
        return "I/O error";
    default:
        return "Unknown error";
    }
}

CYAML_API const char* cyaml_version(void)
{
    return CYAML_VERSION_STR;
}

// #endregion
