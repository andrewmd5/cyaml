
#include "cyaml_internal.h"
#include "cyaml_utf8.h"

// #region JSON Emitter State

#define JSON_STACK_INIT_CAP 32
#define JSON_VISITED_INIT_CAP 32

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
    int indent;
    const cyaml_doc_t* doc;
    const cyaml_node_t** visited;
    size_t visited_count;
    size_t visited_cap;
} json_emitter_t;

// #endregion

// #region Buffer Management

static bool json_grow(json_emitter_t* e, size_t need)
{
    if (e->len + need < e->cap)
        return true;
    size_t new_cap = e->cap ? e->cap * 2 : EMIT_INIT_CAP;
    while (new_cap < e->len + need)
        new_cap *= 2;
    char* new_buf = realloc(e->buf, new_cap);
    if (!new_buf)
        return false;
    e->buf = new_buf;
    e->cap = new_cap;
    return true;
}

static inline bool json_char(json_emitter_t* e, char c)
{
    if (!json_grow(e, 1))
        return false;
    e->buf[e->len++] = c;
    return true;
}

static inline bool json_str(json_emitter_t* e, const char* s, size_t len)
{
    if (!json_grow(e, len))
        return false;
    memcpy(e->buf + e->len, s, len);
    e->len += len;
    return true;
}

static inline bool json_cstr(json_emitter_t* e, const char* s)
{
    return json_str(e, s, strlen(s));
}

static bool json_indent(json_emitter_t* e, int depth)
{
    if (e->indent <= 0)
        return true;
    static const char spaces[] = "                                ";
    int n = depth * e->indent;
    while (n > 0) {
        int chunk = n > 32 ? 32 : n;
        if (!json_str(e, spaces, (size_t)chunk))
            return false;
        n -= chunk;
    }
    return true;
}

static inline bool json_newline(json_emitter_t* e)
{
    return e->indent <= 0 || json_char(e, C_LF);
}

// #endregion

// #region Macros

#define J(e, c)               \
    do {                      \
        if (!json_char(e, c)) \
            return false;     \
    } while (0)
#define JS(e, s)              \
    do {                      \
        if (!json_cstr(e, s)) \
            return false;     \
    } while (0)
#define JN(e, s, n)             \
    do {                        \
        if (!json_str(e, s, n)) \
            return false;       \
    } while (0)
#define JNEWLINE(e)           \
    do {                      \
        if (!json_newline(e)) \
            return false;     \
    } while (0)
#define JINDENT(e, d)           \
    do {                        \
        if (!json_indent(e, d)) \
            return false;       \
    } while (0)

//! Fail macro for stack-based functions
#define JFAIL(expr)         \
    do {                    \
        if (!(expr)) {      \
            result = false; \
            goto cleanup;   \
        }                   \
    } while (0)

//! Free buffer and return NULL on failure
#define J_OR(e, c)              \
    do {                        \
        if (!json_char(e, c)) { \
            free((e)->buf);     \
            return NULL;        \
        }                       \
    } while (0)
#define JS_OR(e, s)             \
    do {                        \
        if (!json_cstr(e, s)) { \
            free((e)->buf);     \
            return NULL;        \
        }                       \
    } while (0)
#define TRY_J(e, expr)      \
    do {                    \
        if (!(expr)) {      \
            free((e)->buf); \
            return NULL;    \
        }                   \
    } while (0)

// #endregion

// #region Frame Types

typedef enum {
    JFRAME_VALUE,
    JFRAME_SEQ_OPEN,
    JFRAME_SEQ_ITEM,
    JFRAME_SEQ_CLOSE,
    JFRAME_MAP_OPEN,
    JFRAME_MAP_KEY,
    JFRAME_MAP_VAL,
    JFRAME_MAP_CLOSE
} json_frame_state_t;

typedef struct {
    const cyaml_node_t* node;
    uint32_t child_idx;
    json_frame_state_t state;
    int depth;
} json_frame_t;

#define JSON_PUSH(stk, cnt, cap, nd, st, dp, on_fail)                     \
    do {                                                                  \
        if ((cnt) >= (cap)) {                                             \
            size_t new_cap = (cap) * 2;                                   \
            json_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk))); \
            if (!tmp) {                                                   \
                on_fail;                                                  \
            }                                                             \
            (stk) = tmp;                                                  \
            (cap) = new_cap;                                              \
        }                                                                 \
        (stk)[(cnt)].node = (nd);                                         \
        (stk)[(cnt)].child_idx = 0;                                       \
        (stk)[(cnt)].state = (st);                                        \
        (stk)[(cnt)].depth = (dp);                                        \
        (cnt)++;                                                          \
    } while (0)

#define JPUSH(nd, st, dp) \
    JSON_PUSH(stack, stack_count, stack_cap, nd, st, dp, { result = false; goto cleanup; })

// #endregion

// #region String Escaping

//! Emit JSON-escaped string with surrounding quotes
static bool json_quoted_string(json_emitter_t* e, const char* s, size_t len)
{
    J(e, '"');

    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];

        switch (c) {
        case '"':
            JS(e, "\\\"");
            i++;
            continue;
        case C_BSLASH:
            JS(e, "\\\\");
            i++;
            continue;
        case '\b':
            JS(e, "\\b");
            i++;
            continue;
        case '\f':
            JS(e, "\\f");
            i++;
            continue;
        case C_LF:
            JS(e, "\\n");
            i++;
            continue;
        case C_CR:
            JS(e, "\\r");
            i++;
            continue;
        case C_TAB:
            JS(e, "\\t");
            i++;
            continue;
        }

        if (c < 0x20) {
            // Control characters require \uXXXX encoding
            char hex[7];
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            JS(e, hex);
            i++;
            continue;
        }

        if (CYAML_IS_ASCII(c)) {
            J(e, (char)c);
            i++;
            continue;
        }

        // UTF-8 multibyte: decode and validate
        cyaml_cp_t cp;
        int bytes = cyaml_utf8_decode(s + i, len - i, &cp);
        if (bytes <= 0) {
            // Invalid UTF-8: emit replacement character
            JS(e, "\xEF\xBF\xBD");
            i++;
            continue;
        }

        JN(e, s + i, (size_t)bytes);
        i += (size_t)bytes;
    }

    J(e, '"');
    return true;
}

// #endregion

// #region Value Detection

//! Check if node has a specific tag
static bool has_tag(const json_emitter_t* e, const cyaml_node_t* n,
    const char* tag, size_t tag_len)
{
    if (!e->doc || !n || n->tag.len != tag_len)
        return false;
    const char* src = cyaml_src(e->doc);
    return src && memcmp(src + n->tag.off, tag, tag_len) == 0;
}

//! Check for !!str tag (forces string type)
static inline bool has_str_tag(const json_emitter_t* e, const cyaml_node_t* n)
{
    return has_tag(e, n, "!!str", 5);
}

//! Check for non-specific tag (!) which forces string type
static inline bool has_nonspecific_tag(const json_emitter_t* e, const cyaml_node_t* n)
{
    return has_tag(e, n, "!", 1);
}

//! Check if scalar represents JSON null
static bool is_json_null(const char* s, size_t len)
{
    if (len == 0)
        return true;
    if (len == L_TILDE && s[0] == C_TILDE)
        return true;
    if (len == L_NULL && cyaml_memicmp(s, S_NULL, L_NULL) == 0)
        return true;
    return false;
}

//! Check if scalar represents JSON boolean
static bool is_json_bool(const char* s, size_t len, bool* value)
{
    if (len == L_TRUE && cyaml_memicmp(s, S_TRUE, L_TRUE) == 0) {
        *value = true;
        return true;
    }
    if (len == L_FALSE && cyaml_memicmp(s, S_FALSE, L_FALSE) == 0) {
        *value = false;
        return true;
    }
    return false;
}

//! Convert YAML hex/octal integer to decimal string
//! @return Allocated string or NULL if not hex/octal
static char* convert_yaml_int(const char* s, size_t len)
{
    if (len < 3)
        return NULL;

    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i + 2 >= len || s[i] != '0')
        return NULL;

    char prefix = s[i + 1];
    if (prefix != 'x' && prefix != 'X' && prefix != 'o' && prefix != 'O')
        return NULL;

    const char* end;
    int64_t val;
    if (!cyaml_str_to_i64(s, &end, &val) || (size_t)(end - s) != len)
        return NULL;

    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRId64, val);
    return cyaml_strdup(buf);
}

//! Check if scalar is valid JSON number
static bool is_json_number(const char* s, size_t len)
{
    if (len == 0)
        return false;

    size_t i = 0;

    if (s[i] == '-')
        i++;
    if (i >= len)
        return false;

    // Integer part: no leading zeros except standalone 0
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < len && CYAML_IS_DIGIT(s[i]))
            i++;
    } else {
        return false;
    }

    // Optional fraction
    if (i < len && s[i] == '.') {
        i++;
        if (i >= len || !CYAML_IS_DIGIT(s[i]))
            return false;
        while (i < len && CYAML_IS_DIGIT(s[i]))
            i++;
    }

    // Optional exponent
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-'))
            i++;
        if (i >= len || !CYAML_IS_DIGIT(s[i]))
            return false;
        while (i < len && CYAML_IS_DIGIT(s[i]))
            i++;
    }

    return i == len;
}

// #endregion

// #region Node Emission

static bool json_scalar(json_emitter_t* e, const cyaml_node_t* n)
{
    char* str = cyaml_scalar_str((cyaml_doc_t*)e->doc, n);
    if (!str) {
        JS(e, S_NULL);
        return true;
    }

    size_t len = strlen(str);
    bool result = true;

    if (n->style == CYAML_PLAIN && !has_nonspecific_tag(e, n)) {
        if (is_json_null(str, len)) {
            result = json_cstr(e, S_NULL);
            goto done;
        }

        bool bool_val;
        if (is_json_bool(str, len, &bool_val)) {
            result = json_cstr(e, bool_val ? S_TRUE : S_FALSE);
            goto done;
        }

        char* decimal = convert_yaml_int(str, len);
        if (decimal) {
            result = json_cstr(e, decimal);
            free(decimal);
            goto done;
        }

        if (is_json_number(str, len)) {
            size_t out_len = len;
            // Strip trailing .0 fraction
            if (len > 2) {
                const char* dot = memchr(str, '.', len);
                if (dot) {
                    size_t frac_start = (size_t)(dot - str) + 1;
                    bool all_zeros = true;
                    for (size_t j = frac_start; j < len; j++) {
                        if (str[j] != '0') {
                            all_zeros = false;
                            break;
                        }
                    }
                    if (all_zeros)
                        out_len = (size_t)(dot - str);
                }
            }
            result = json_str(e, str, out_len);
            goto done;
        }
    }

    result = json_quoted_string(e, str, len);

done:
    free(str);
    return result;
}

static bool json_is_cyclic(json_emitter_t* e, const cyaml_node_t* n)
{
    for (size_t i = 0; i < e->visited_count; i++) {
        if (e->visited[i] == n)
            return true;
    }
    return false;
}

static bool json_push_visited(json_emitter_t* e, const cyaml_node_t* n)
{
    if (e->visited_count >= e->visited_cap) {
        size_t new_cap = e->visited_cap ? e->visited_cap * 2 : JSON_VISITED_INIT_CAP;
        const cyaml_node_t** new_visited = realloc(e->visited, new_cap * sizeof(*new_visited));
        if (!new_visited)
            return false;
        e->visited = new_visited;
        e->visited_cap = new_cap;
    }
    e->visited[e->visited_count++] = n;
    return true;
}

//! Emit JSON for a node tree (iterative)
static bool json_node(json_emitter_t* e, const cyaml_node_t* root, int start_depth)
{
    if (!root) {
        JS(e, S_NULL);
        return true;
    }

    json_frame_t* stack = malloc(JSON_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        return false;
    size_t stack_count = 0;
    size_t stack_cap = JSON_STACK_INIT_CAP;
    bool result = true;

    JPUSH(root, JFRAME_VALUE, start_depth);

    while (stack_count > 0) {
        json_frame_t* f = &stack[stack_count - 1];
        const cyaml_node_t* n = f->node;

        switch (f->state) {
        case JFRAME_VALUE:
            stack_count--;
            if (!n) {
                JFAIL(json_cstr(e, S_NULL));
                break;
            }
            switch (n->type) {
            case CYAML_NONE:
            case CYAML_NULL:
                if (has_str_tag(e, n))
                    JFAIL(json_cstr(e, "\"\""));
                else
                    JFAIL(json_cstr(e, S_NULL));
                break;
            case CYAML_SCALAR:
                JFAIL(json_scalar(e, n));
                break;
            case CYAML_SEQ:
                JPUSH(n, JFRAME_SEQ_OPEN, f->depth);
                break;
            case CYAML_MAP:
                JPUSH(n, JFRAME_MAP_OPEN, f->depth);
                break;
            case CYAML_ALIAS:
                if (!n->alias.target || json_is_cyclic(e, n->alias.target)) {
                    JFAIL(json_cstr(e, S_NULL));
                } else {
                    JFAIL(json_push_visited(e, n->alias.target));
                    JPUSH(n->alias.target, JFRAME_VALUE, f->depth);
                }
                break;
            default:
                break;
            }
            break;

        case JFRAME_SEQ_OPEN:
            JFAIL(json_char(e, '['));
            if (n->seq.count == 0) {
                JFAIL(json_char(e, ']'));
                stack_count--;
            } else {
                JFAIL(json_newline(e));
                f->state = JFRAME_SEQ_ITEM;
            }
            break;

        case JFRAME_SEQ_ITEM:
            if (f->child_idx >= n->seq.count) {
                f->state = JFRAME_SEQ_CLOSE;
            } else {
                JFAIL(json_indent(e, f->depth + 1));
                uint32_t idx = f->child_idx++;
                JPUSH(n->seq.items[idx], JFRAME_VALUE, f->depth + 1);
            }
            break;

        case JFRAME_SEQ_CLOSE:
            if (f->child_idx > 0 && f->child_idx < n->seq.count)
                JFAIL(json_char(e, ','));
            JFAIL(json_newline(e));
            JFAIL(json_indent(e, f->depth));
            JFAIL(json_char(e, ']'));
            stack_count--;
            break;

        case JFRAME_MAP_OPEN:
            JFAIL(json_char(e, '{'));
            if (n->map.count == 0) {
                JFAIL(json_char(e, '}'));
                stack_count--;
            } else {
                JFAIL(json_newline(e));
                f->state = JFRAME_MAP_KEY;
            }
            break;

        case JFRAME_MAP_KEY:
            if (f->child_idx >= n->map.count) {
                f->state = JFRAME_MAP_CLOSE;
            } else {
                JFAIL(json_indent(e, f->depth + 1));
                cyaml_node_t* key = n->map.pairs[f->child_idx].key;
                if (key && key->type == CYAML_ALIAS && key->alias.target)
                    key = key->alias.target;
                char* key_str = cyaml_scalar_str((cyaml_doc_t*)e->doc, key);
                if (!key_str)
                    key_str = cyaml_strdup("");
                bool ok = json_quoted_string(e, key_str, strlen(key_str));
                free(key_str);
                JFAIL(ok);
                JFAIL(json_char(e, ':'));
                if (e->indent > 0)
                    JFAIL(json_char(e, C_SP));
                f->state = JFRAME_MAP_VAL;
                JPUSH(n->map.pairs[f->child_idx].val, JFRAME_VALUE, f->depth + 1);
            }
            break;

        case JFRAME_MAP_VAL:
            if (f->child_idx + 1 < n->map.count)
                JFAIL(json_char(e, ','));
            JFAIL(json_newline(e));
            f->child_idx++;
            f->state = JFRAME_MAP_KEY;
            break;

        case JFRAME_MAP_CLOSE:
            JFAIL(json_indent(e, f->depth));
            JFAIL(json_char(e, '}'));
            stack_count--;
            break;
        }
    }

cleanup:
    free(stack);
    return result;
}

// #endregion

// #region Public API

CYAML_API char* cyaml_json(const cyaml_doc_t* doc, int indent, size_t* len)
{
    if (!doc)
        return NULL;

    json_emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .indent = indent > 0 ? indent : 0, .doc = doc, .visited = NULL, .visited_count = 0, .visited_cap = 0
    };

    if (!json_node(&e, doc->root, 0)) {
        free(e.buf);
        free(e.visited);
        return NULL;
    }
    free(e.visited);

    if (e.indent > 0)
        J_OR(&e, C_LF);
    J_OR(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

CYAML_API char* cyaml_stream_json(const cyaml_stream_t* stream, int indent, size_t* len)
{
    if (!stream)
        return NULL;

    if (stream->count == 0) {
        char* result = cyaml_strdup("[]");
        if (len)
            *len = 2;
        return result;
    }

    if (stream->count == 1)
        return cyaml_json(stream->docs[0], indent, len);

    json_emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .indent = indent > 0 ? indent : 0, .doc = NULL, .visited = NULL, .visited_count = 0, .visited_cap = 0
    };

    J_OR(&e, '[');
    if (e.indent > 0)
        J_OR(&e, C_LF);

    for (uint32_t i = 0; i < stream->count; i++) {
        if (e.indent > 0)
            TRY_J(&e, json_indent(&e, 1));

        e.doc = stream->docs[i];
        e.visited_count = 0;
        TRY_J(&e, json_node(&e, stream->docs[i]->root, 1));

        if (i + 1 < stream->count)
            J_OR(&e, ',');
        if (e.indent > 0)
            J_OR(&e, C_LF);
    }

    free(e.visited);

    J_OR(&e, ']');
    if (e.indent > 0)
        J_OR(&e, C_LF);
    J_OR(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

// #endregion
