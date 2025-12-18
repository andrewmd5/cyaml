
#include "cyaml_internal.h"
#include "cyaml_utf8.h"

// #region JSON Emitter State

#define JSON_MAX_DEPTH 64 //!< Max nesting depth

typedef struct {
    char* buf; //!< Output buffer
    size_t len; //!< Current length
    size_t cap; //!< Capacity
    int indent; //!< Spaces per indent level (0 = compact)
    const cyaml_doc_t* doc; //!< Source document
    const cyaml_node_t* stack[JSON_MAX_DEPTH]; //!< Visited nodes for cycle detection
    int stack_depth; //!< Current stack depth
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

static bool json_char(json_emitter_t* e, char c)
{
    if (!json_grow(e, 1))
        return false;
    e->buf[e->len++] = c;
    return true;
}

static bool json_str(json_emitter_t* e, const char* s, size_t len)
{
    if (!json_grow(e, len))
        return false;
    memcpy(e->buf + e->len, s, len);
    e->len += len;
    return true;
}

static bool json_cstr(json_emitter_t* e, const char* s)
{
    return json_str(e, s, strlen(s));
}

static bool json_indent(json_emitter_t* e, int depth)
{
    if (e->indent <= 0)
        return true;
    static const char spaces[] = "                                "; // 32 spaces
    int n = depth * e->indent;
    while (n > 0) {
        int chunk = n > 32 ? 32 : n;
        if (!json_str(e, spaces, (size_t)chunk))
            return false;
        n -= chunk;
    }
    return true;
}

static bool json_newline(json_emitter_t* e)
{
    if (e->indent <= 0)
        return true;
    return json_char(e, C_LF);
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

//! For buffer-allocating functions: free and return NULL on failure
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

// #region String Escaping

//! Emit a JSON-escaped string (with surrounding quotes)
static bool json_quoted_string(json_emitter_t* e, const char* s, size_t len)
{
    J(e, '"');

    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];

        // Handle escape sequences
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

        // Control characters (0x00-0x1F) must be escaped as \uXXXX
        if (c < 0x20) {
            char hex[7];
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            JS(e, hex);
            i++;
            continue;
        }

        // ASCII printable - emit directly
        if (CYAML_IS_ASCII(c)) {
            J(e, (char)c);
            i++;
            continue;
        }

        // UTF-8 multibyte - decode and validate
        cyaml_cp_t cp;
        int bytes = cyaml_utf8_decode(s + i, len - i, &cp);
        if (bytes <= 0) {
            // Invalid UTF-8 - emit replacement character
            JS(e, "\xEF\xBF\xBD");
            i++;
            continue;
        }

        // Valid UTF-8 - emit bytes directly (JSON allows UTF-8)
        JN(e, s + i, (size_t)bytes);
        i += (size_t)bytes;
    }

    J(e, '"');
    return true;
}

// #endregion

// #region Value Detection

//! Check if node has !!str tag (for empty string handling)
static bool has_str_tag(const json_emitter_t* e, const cyaml_node_t* n)
{
    if (!e->doc || !n || n->tag.len == 0)
        return false;
    const char* src = cyaml_src(e->doc);
    if (!src)
        return false;
    const char* tag = src + n->tag.off;
    // Check for !!str (the standard YAML string tag shorthand)
    return (n->tag.len == 5 && memcmp(tag, "!!str", 5) == 0);
}

//! Check if node has non-specific tag (!) which forces string type
static bool has_nonspecific_tag(const json_emitter_t* e, const cyaml_node_t* n)
{
    if (!e->doc || !n || n->tag.len == 0)
        return false;
    const char* src = cyaml_src(e->doc);
    if (!src)
        return false;
    const char* tag = src + n->tag.off;
    // Non-specific tag is just "!"
    return (n->tag.len == 1 && tag[0] == '!');
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

//! Check if scalar is a YAML hex/octal integer and convert to decimal
//! Returns allocated string with decimal representation, or NULL if not hex/octal
static char* convert_yaml_int(const char* s, size_t len)
{
    if (len < 3)
        return NULL;

    // Skip optional sign
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+')
        i++;

    // Must be 0x or 0o prefix
    if (i + 2 >= len || s[i] != '0')
        return NULL;
    char prefix = s[i + 1];
    if (prefix != 'x' && prefix != 'X' && prefix != 'o' && prefix != 'O')
        return NULL;

    const char* end;
    int64_t val;
    if (!cyaml_str_to_i64(s, &end, &val))
        return NULL;
    if ((size_t)(end - s) != len)
        return NULL; // Must consume entire string

    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRId64, val);
    return cyaml_strdup(buf);
}

//! Check if scalar represents a JSON number
//! JSON numbers: optional minus, digits, optional decimal, optional exponent
static bool is_json_number(const char* s, size_t len)
{
    if (len == 0)
        return false;

    size_t i = 0;

    // Optional minus
    if (s[i] == '-')
        i++;
    if (i >= len)
        return false;

    // Integer part (no leading zeros allowed in JSON)
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < len && CYAML_IS_DIGIT(s[i]))
            i++;
    } else {
        return false;
    }

    // Optional decimal part
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

static bool json_node(json_emitter_t* e, const cyaml_node_t* n, int depth);

//! Check if node is already on the visited stack
static bool json_is_cyclic(json_emitter_t* e, const cyaml_node_t* n)
{
    for (int i = 0; i < e->stack_depth; i++) {
        if (e->stack[i] == n)
            return true;
    }
    return false;
}

//! Emit JSON array
static bool json_array(json_emitter_t* e, const cyaml_node_t* n, int depth)
{
    J(e, '[');

    if (n->seq.count == 0) {
        J(e, ']');
        return true;
    }

    JNEWLINE(e);

    for (uint32_t i = 0; i < n->seq.count; i++) {
        JINDENT(e, depth + 1);

        cyaml_node_t* item = n->seq.items[i];
        if (!json_node(e, item, depth + 1))
            return false;

        if (i + 1 < n->seq.count)
            J(e, ',');
        JNEWLINE(e);
    }

    JINDENT(e, depth);
    J(e, ']');
    return true;
}

//! Emit JSON object
static bool json_object(json_emitter_t* e, const cyaml_node_t* n, int depth)
{
    J(e, '{');

    if (n->map.count == 0) {
        J(e, '}');
        return true;
    }

    JNEWLINE(e);

    for (uint32_t i = 0; i < n->map.count; i++) {
        JINDENT(e, depth + 1);

        // Key - must be a string in JSON
        // Resolve alias if key is an alias reference
        cyaml_node_t* key = n->map.pairs[i].key;
        if (key && key->type == CYAML_ALIAS && key->alias.target) {
            key = key->alias.target;
        }
        char* key_str = cyaml_scalar_str((cyaml_doc_t*)e->doc, key);
        if (!key_str)
            key_str = cyaml_strdup("");

        if (!json_quoted_string(e, key_str, strlen(key_str))) {
            free(key_str);
            return false;
        }
        free(key_str);

        // Separator
        J(e, ':');
        if (e->indent > 0)
            J(e, C_SP);

        // Value
        cyaml_node_t* val = n->map.pairs[i].val;
        if (!json_node(e, val, depth + 1))
            return false;

        if (i + 1 < n->map.count)
            J(e, ',');
        JNEWLINE(e);
    }

    JINDENT(e, depth);
    J(e, '}');
    return true;
}

//! Emit JSON scalar value
static bool json_scalar(json_emitter_t* e, const cyaml_node_t* n)
{
    char* str = cyaml_scalar_str((cyaml_doc_t*)e->doc, n);
    if (!str) {
        JS(e, S_NULL);
        return true;
    }

    size_t len = strlen(str);
    bool result = true;

    // Check for special JSON values (only for plain/unquoted scalars without non-specific tag)
    // Non-specific tag (!) forces string type in JSON schema
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

        // YAML hex/octal integer -> convert to decimal
        char* decimal = convert_yaml_int(str, len);
        if (decimal) {
            result = json_cstr(e, decimal);
            free(decimal);
            goto done;
        }

        // Number - normalize trailing .00 to integer
        if (is_json_number(str, len)) {
            // Check for trailing .00, .0, etc. that can be removed
            size_t out_len = len;
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

    // Default: quoted string
    result = json_quoted_string(e, str, len);

done:
    free(str);
    return result;
}

//! Emit any node as JSON
static bool json_node(json_emitter_t* e, const cyaml_node_t* n, int depth)
{
    if (!n || depth >= JSON_MAX_DEPTH) {
        JS(e, S_NULL);
        return true;
    }

    switch (n->type) {
    case CYAML_NONE:
    case CYAML_NULL:
        if (has_str_tag(e, n)) {
            JS(e, "\"\"");
        } else {
            JS(e, S_NULL);
        }
        return true;

    case CYAML_SCALAR:
        return json_scalar(e, n);

    case CYAML_SEQ:
        return json_array(e, n, depth);

    case CYAML_MAP:
        return json_object(e, n, depth);

    case CYAML_ALIAS:
        if (!n->alias.target || e->stack_depth >= JSON_MAX_DEPTH || json_is_cyclic(e, n->alias.target)) {
            JS(e, S_NULL); // unresolved, too deep, or cyclic
            return true;
        }
        e->stack[e->stack_depth++] = n->alias.target;
        bool ok = json_node(e, n->alias.target, depth);
        e->stack_depth--;
        return ok;

    default:
        CYAML_UNREACHABLE("invalid node type");
    }
}

// #endregion

// #region Public API

CYAML_API char* cyaml_json(const cyaml_doc_t* doc, int indent, size_t* len)
{
    if (!doc)
        return NULL;

    json_emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .indent = indent > 0 ? indent : 0, .doc = doc, .stack_depth = 0
    };

    TRY_J(&e, json_node(&e, doc->root, 0));

    // Add trailing newline for pretty-printed output
    if (e.indent > 0)
        J_OR(&e, C_LF);

    // Null terminate
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

    // Empty stream produces empty array
    if (stream->count == 0) {
        char* result = cyaml_strdup("[]");
        if (len)
            *len = 2;
        return result;
    }

    // Single document: output directly (not wrapped in array)
    if (stream->count == 1) {
        return cyaml_json(stream->docs[0], indent, len);
    }

    // Multi-document: output as JSON array
    json_emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .indent = indent > 0 ? indent : 0, .doc = NULL, .stack_depth = 0
    };

    J_OR(&e, '[');
    if (e.indent > 0)
        J_OR(&e, C_LF);

    for (uint32_t i = 0; i < stream->count; i++) {
        if (e.indent > 0)
            TRY_J(&e, json_indent(&e, 1));

        e.doc = stream->docs[i];
        TRY_J(&e, json_node(&e, stream->docs[i]->root, 1));

        if (i + 1 < stream->count)
            J_OR(&e, ',');
        if (e.indent > 0)
            J_OR(&e, C_LF);
    }

    J_OR(&e, ']');
    if (e.indent > 0)
        J_OR(&e, C_LF);

    // Null terminate
    J_OR(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

// #endregion
