#include "cyaml_internal.h"

// #region Helpers

//! Get line span of a node (end_line - start_line)
static uint32_t span_line_count(cyaml_span_t span)
{
    if (span.end_line == 0 || span.start_line == 0)
        return 0;
    return span.end_line - span.start_line;
}

// #endregion

// #region Emitter State

typedef enum {
    EMIT_DUMP = 1 << 0 //!< Canonical dump mode (yaml-test-suite format)
} emitter_flags_t;

typedef struct {
    char* buf; //!< Output buffer
    size_t len; //!< Current length
    size_t cap; //!< Capacity
    cyaml_emit_opts_t opts; //!< Emit options
    const cyaml_doc_t* doc; //!< Source document
    uint8_t flags; //!< Emitter flags
    uint32_t comment_idx; //!< Next comment index to emit
    uint32_t last_line; //!< Last line we emitted content for
} emitter_t;

#define EMIT_IS_DUMP(e) (((e)->flags & EMIT_DUMP) != 0)

// #endregion

// #region Buffer Management

static bool emit_grow(emitter_t* e, size_t need)
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

static bool emit_char(emitter_t* e, char c)
{
    if (!emit_grow(e, 1))
        return false;
    e->buf[e->len++] = c;
    return true;
}

static bool emit_str(emitter_t* e, const char* s, size_t len)
{
    if (!emit_grow(e, len))
        return false;
    memcpy(e->buf + e->len, s, len);
    e->len += len;
    return true;
}

static bool emit_cstr(emitter_t* e, const char* s)
{
    return emit_str(e, s, strlen(s));
}

static bool emit_indent(emitter_t* e, int depth)
{
    static const char spaces[] = "                                "; // 32 spaces
    int n = depth * e->opts.indent;
    while (n > 0) {
        int chunk = n > 32 ? 32 : n;
        if (!emit_str(e, spaces, (size_t)chunk))
            return false;
        n -= chunk;
    }
    return true;
}

// #endregion

// #region Emit Macros

//! Emit helpers - return false on failure
#define EMIT(e, c)            \
    do {                      \
        if (!emit_char(e, c)) \
            return false;     \
    } while (0)
#define EMIT_S(e, s)          \
    do {                      \
        if (!emit_cstr(e, s)) \
            return false;     \
    } while (0)
#define EMIT_N(e, s, n)         \
    do {                        \
        if (!emit_str(e, s, n)) \
            return false;       \
    } while (0)
#define INDENT(e, d)            \
    do {                        \
        if (!emit_indent(e, d)) \
            return false;       \
    } while (0)
#define NODE(e, n, d)            \
    do {                         \
        if (!emit_node(e, n, d)) \
            return false;        \
    } while (0)
#define DNODE(e, n, d)           \
    do {                         \
        if (!dump_node(e, n, d)) \
            return false;        \
    } while (0)
#define EMIT_PROPS(e, n)        \
    do {                        \
        if (!emit_anchor(e, n)) \
            return false;       \
        if (!emit_tag(e, n))    \
            return false;       \
    } while (0)
#define DUMP_PROPS(e, n)        \
    do {                        \
        if (!dump_anchor(e, n)) \
            return false;       \
        if (!dump_tag(e, n))    \
            return false;       \
    } while (0)

//! Buffer inspection
#define EMIT_LAST(e) ((e)->len > 0 ? (e)->buf[(e)->len - 1] : C_NUL)
#define EMIT_PREV(e) ((e)->len > 1 ? (e)->buf[(e)->len - 2] : C_NUL)
#define EMIT_TRIM_SPACE(e)        \
    do {                          \
        if (EMIT_LAST(e) == C_SP) \
            (e)->len--;           \
    } while (0)
#define NEEDS_NL(e) ((e)->len > 0 && (e)->buf[(e)->len - 1] != C_LF)

//! Check if node just emitted trailing breaks (KEEP chomp)
#define EMITTED_KEEP_BREAKS(n) \
    ((n) && ((n)->style == CYAML_LITERAL || (n)->style == CYAML_FOLDED) && (n)->chomp == CYAML_KEEP && (n)->trailing_breaks > 0)

//! For buffer-allocating functions: free and return NULL on failure
#define EMIT_OR(e, c)           \
    do {                        \
        if (!emit_char(e, c)) { \
            free((e)->buf);     \
            return NULL;        \
        }                       \
    } while (0)
#define EMIT_S_OR(e, s)         \
    do {                        \
        if (!emit_cstr(e, s)) { \
            free((e)->buf);     \
            return NULL;        \
        }                       \
    } while (0)
#define TRY_OR(e, expr)     \
    do {                    \
        if (!(expr)) {      \
            free((e)->buf); \
            return NULL;    \
        }                   \
    } while (0)

// #endregion

// #region Comment Emitters

static inline bool emit_comments_before_line(emitter_t* e, uint32_t line, int depth)
{
    if (!e->opts.comments || !e->doc || !e->doc->comments)
        return true;
    const char* src = cyaml_src(e->doc);
    if (!src)
        return true;
    while (e->comment_idx < e->doc->comments->count) {
        cyaml_span_t span = e->doc->comments->items[e->comment_idx];
        if (span.start_line >= line)
            break;
        if (NEEDS_NL(e))
            EMIT(e, C_LF);
        INDENT(e, depth);
        EMIT_N(e, src + span.off, span.len);
        e->comment_idx++;
        e->last_line = span.end_line;
    }
    return true;
}

static inline bool emit_inline_comment(emitter_t* e, uint32_t line)
{
    if (!e->opts.comments || !e->doc || !e->doc->comments)
        return true;
    const char* src = cyaml_src(e->doc);
    if (!src)
        return true;
    while (e->comment_idx < e->doc->comments->count) {
        cyaml_span_t span = e->doc->comments->items[e->comment_idx];
        if (span.start_line > line)
            break;
        if (span.start_line == line) {
            EMIT_S(e, "  ");
            EMIT_N(e, src + span.off, span.len);
            e->comment_idx++;
            e->last_line = span.end_line;
            return true;
        }
        e->comment_idx++;
    }
    return true;
}

static inline bool emit_remaining_comments(emitter_t* e, int depth)
{
    if (!e->opts.comments || !e->doc || !e->doc->comments)
        return true;
    const char* src = cyaml_src(e->doc);
    if (!src)
        return true;
    while (e->comment_idx < e->doc->comments->count) {
        cyaml_span_t span = e->doc->comments->items[e->comment_idx];
        if (NEEDS_NL(e))
            EMIT(e, C_LF);
        INDENT(e, depth);
        EMIT_N(e, src + span.off, span.len);
        e->comment_idx++;
    }
    return true;
}

// #endregion

// #region Scalar Style Helpers

//! [7.3.3] Check if plain scalar needs quoting
//! Plain scalars cannot contain c-indicator at start, or certain patterns
static bool needs_quoting_ex(const char* s, size_t len, size_t tag_len)
{
    if (len == 0)
        return true;
    char first = s[0];
    char second = len > 1 ? s[1] : C_NUL;

    // Flow indicators always need quoting at start
    if (CYAML_IS_FLOW(first))
        return true;

    // These indicators need quoting only if followed by whitespace
    // - : ? can start plain scalars if not followed by whitespace
    if ((first == '-' || first == ':' || first == '?') && CYAML_IS_WHITE(second))
        return true;

    // Document markers (--- or ...) need quoting - they look like doc start/end
    if (len >= 3) {
        if ((s[0] == '-' && s[1] == '-' && s[2] == '-') || (s[0] == '.' && s[1] == '.' && s[2] == '.')) {
            return true;
        }
    }

    // Other indicators at start always need quoting
    if (first == C_HASH || first == '&' || first == '*' || first == '!' || first == '|' || first == '>' || first == '\'' || first == '"' || first == '%' || first == '@' || first == '`')
        return true;

    // Ends with colon - ambiguous as mapping key
    if (s[len - 1] == ':')
        return true;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (CYAML_IS_BREAK(c))
            return true;
        if (c == C_HASH && i > 0 && CYAML_IS_WHITE(s[i - 1]))
            return true;
        if (c == ':' && i + 1 < len && CYAML_IS_WHITE(s[i + 1]))
            return true;
    }
    // Reserved words - skip check if type tag provides disambiguation
    if (tag_len == 0) {
        if (len == L_NULL && cyaml_memicmp(s, S_NULL, L_NULL) == 0)
            return true;
        if (len == L_TRUE && cyaml_memicmp(s, S_TRUE, L_TRUE) == 0)
            return true;
        if (len == L_FALSE && cyaml_memicmp(s, S_FALSE, L_FALSE) == 0)
            return true;
        if (len == L_TILDE && s[0] == C_TILDE)
            return true;
    }
    return false;
}

static bool needs_quoting(const char* s, size_t len)
{
    return needs_quoting_ex(s, len, 0);
}

// #endregion

// #region Scalar Emitters

//! [7.3.1] Emit double-quoted scalar with escape sequences
static bool emit_double_quoted(emitter_t* e, const char* s, size_t len)
{
    EMIT(e, '"');
    size_t pos = 0;
    while (pos < len) {
        cyaml_cp_t cp;
        int consumed = cyaml_utf8_decode(s + pos, len - pos, &cp);
        if (consumed <= 0 || cp == CYAML_CP_INVALID) {
            char esc[8];
            int n = cyaml_write_escape((unsigned char)s[pos], esc, sizeof(esc));
            if (n > 0)
                EMIT_N(e, esc, (size_t)n);
            pos++;
            continue;
        }
        if (cp == '"') {
            EMIT_S(e, "\\\"");
        } else {
            char esc[12];
            int esc_len = cyaml_write_escape(cp, esc, sizeof(esc));
            if (esc_len > 0) {
                EMIT_N(e, esc, (size_t)esc_len);
            } else if (EMIT_IS_DUMP(e) && cp > 0x7F) {
                // Dump mode: escape non-ASCII as \uXXXX or \UXXXXXXXX
                static const char hex[] = "0123456789ABCDEF";
                if (cp <= 0xFFFF) {
                    char buf[6] = { C_BSLASH, 'u',
                        hex[(cp >> 12) & 0xF], hex[(cp >> 8) & 0xF],
                        hex[(cp >> 4) & 0xF], hex[cp & 0xF] };
                    EMIT_N(e, buf, 6);
                } else {
                    char buf[10] = { C_BSLASH, 'U',
                        hex[(cp >> 28) & 0xF], hex[(cp >> 24) & 0xF],
                        hex[(cp >> 20) & 0xF], hex[(cp >> 16) & 0xF],
                        hex[(cp >> 12) & 0xF], hex[(cp >> 8) & 0xF],
                        hex[(cp >> 4) & 0xF], hex[cp & 0xF] };
                    EMIT_N(e, buf, 10);
                }
            } else {
                EMIT_N(e, s + pos, (size_t)consumed);
            }
        }
        pos += (size_t)consumed;
    }
    EMIT(e, '"');
    return true;
}

//! [7.3.2] Emit single-quoted scalar (escapes ' as '')
static bool emit_single_quoted(emitter_t* e, const char* s, size_t len, int depth)
{
    EMIT(e, '\'');
    size_t pos = 0;
    bool prev_was_newline = false;
    while (pos < len) {
        cyaml_cp_t cp;
        int consumed = cyaml_utf8_decode(s + pos, len - pos, &cp);
        CYAML_ASSERT(consumed > 0 && cp != CYAML_CP_INVALID, "invalid UTF-8");
        if (cp == '\'') {
            EMIT_S(e, "''");
            prev_was_newline = false;
        } else if (cp == C_LF) {
            // n content newlines -> n+1 output newlines
            if (prev_was_newline) {
                EMIT(e, C_LF);
            } else {
                EMIT_S(e, "\n\n");
            }
            size_t next_pos = pos + (size_t)consumed;
            if (next_pos >= len || s[next_pos] != C_LF) {
                INDENT(e, depth > 0 ? depth : 1);
            }
            prev_was_newline = true;
        } else {
            EMIT_N(e, s + pos, (size_t)consumed);
            prev_was_newline = false;
        }
        pos += (size_t)consumed;
    }
    EMIT(e, '\'');
    return true;
}

//! [8.1.1.1] Check if block scalar needs explicit indentation indicator
//! Required when first content char is space/tab
static bool block_needs_indicator(const emitter_t* e, const char* s, size_t len, uint8_t leading_breaks)
{
    if (len == 0)
        return false;

    // Skip any leading newlines to find first content
    size_t pos = 0;
    while (pos < len && (s[pos] == C_LF || s[pos] == C_CR))
        pos++;

    if (pos >= len) {
        // All newlines (empty scalar with only breaks) - no content to indent
        return false;
    }

    // If first content character is space, indicator is required
    // (tabs can't be used for YAML indentation, so leading tab is unambiguous)
    if (s[pos] == C_SP)
        return true;

    // If content has leading newlines but leading_breaks is 0, those newlines
    // came from whitespace-only lines that were folded. Indicator needed because
    // parser can't auto-detect indent from whitespace-only lines.
    if (pos > 0 && leading_breaks == 0)
        return true;

    // Canonical form only: with 2+ leading blank lines, use explicit indicator
    if (EMIT_IS_DUMP(e) && pos >= 2)
        return true;

    return false;
}

//! [8.1.2] Emit literal block scalar (|)
static bool emit_literal(emitter_t* e, const char* s, size_t len, const cyaml_node_t* n, int depth)
{
    // Check context BEFORE emitting (buffer state changes after each emit)
    bool in_map_value = (EMIT_PREV(e) == ':' && EMIT_LAST(e) == C_SP);

    EMIT(e, '|');

    // Count trailing newlines and check if content is empty
    size_t trailing = 0, content_len = len;
    while (content_len > 0 && (s[content_len - 1] == C_LF || s[content_len - 1] == C_CR)) {
        content_len--;
        trailing++;
    }
    bool empty_content = (content_len == 0);

    // [8.1.1.1] Indentation indicator
    // Need indicator when: first content char is space, OR empty scalar with KEEP in map value
    bool need_indicator = (len > 0 && block_needs_indicator(e, s, len, n->leading_breaks))
        || (empty_content && n->chomp == CYAML_KEEP && in_map_value);
    if (need_indicator)
        EMIT(e, (char)('0' + e->opts.indent));

    // Chomping: STRIP=-, KEEP=+ (when differs from CLIP), CLIP=default
    // KEEP differs from CLIP when: empty content with trailing, or 2+ trailing
    bool emitted_keep = false;
    if (n->chomp == CYAML_STRIP) {
        EMIT(e, '-');
    } else if (n->chomp == CYAML_KEEP && (empty_content || trailing >= 2)) {
        EMIT(e, '+');
        emitted_keep = true;
    }

    EMIT(e, C_LF);

    while (len > 0 && (s[len - 1] == C_LF || s[len - 1] == C_CR))
        len--;
    int content_depth = depth > 0 ? depth : 1;

    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && s[pos] != C_LF && s[pos] != C_CR)
            pos++;
        if (pos > line_start) {
            INDENT(e, content_depth);
            EMIT_N(e, s + line_start, pos - line_start);
        }
        if (pos < len) {
            EMIT(e, C_LF);
            if (s[pos] == C_CR && pos + 1 < len && s[pos + 1] == C_LF)
                pos += 2;
            else
                pos++;
        }
    }

    if (emitted_keep) {
        uint8_t breaks = (len == 0 && n->trailing_breaks == 0) ? n->leading_breaks : n->trailing_breaks;
        for (uint8_t i = 0; i < breaks; i++)
            EMIT(e, C_LF);
    }

    return true;
}

//! [8.1.3] Emit folded block scalar (>)
static bool emit_folded(emitter_t* e, const char* s, size_t len, const cyaml_node_t* n, int depth)
{
    EMIT(e, '>');

    // Count trailing newlines and check if content is empty
    size_t trailing = 0, content_len = len;
    while (content_len > 0 && (s[content_len - 1] == C_LF || s[content_len - 1] == C_CR)) {
        content_len--;
        trailing++;
    }
    bool empty_content = (content_len == 0);

    // [8.1.1.1] Indentation indicator
    if (block_needs_indicator(e, s, len, n->leading_breaks)) {
        EMIT(e, (char)('0' + e->opts.indent));
    }

    // Chomping: STRIP=-, KEEP=+ (when differs from CLIP), CLIP=default
    // KEEP differs from CLIP when: empty content with trailing, or 2+ trailing
    if (n->chomp == CYAML_STRIP) {
        EMIT(e, '-');
    } else if (n->chomp == CYAML_KEEP && (empty_content || trailing >= 2)) {
        EMIT(e, '+');
    }

    EMIT(e, C_LF);

    while (len > 0 && (s[len - 1] == C_LF || s[len - 1] == C_CR))
        len--;
    int content_depth = depth > 0 ? depth : 1;

    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && s[pos] != C_LF && s[pos] != C_CR)
            pos++;
        bool had_content = (pos > line_start);
        bool content_more_indented = (had_content && s[line_start] == C_SP);
        if (had_content) {
            INDENT(e, content_depth);
            EMIT_N(e, s + line_start, pos - line_start);
        }
        if (pos < len) {
            // Count consecutive line breaks
            size_t break_count = 0;
            while (pos < len && (s[pos] == C_LF || s[pos] == C_CR)) {
                if (s[pos] == C_CR && pos + 1 < len && s[pos + 1] == C_LF)
                    pos += 2;
                else
                    pos++;
                break_count++;
            }
            // Check if next content is "more indented" (starts with space)
            bool next_more_indented = (pos < len && s[pos] == C_SP);
            // In folded mode, breaks around more-indented content are preserved.
            // Only add +1 for line ending when going regular → regular.
            size_t newlines = break_count;
            if (had_content && !content_more_indented && !next_more_indented) {
                newlines++; // Regular → regular: add line ending
            }
            for (size_t i = 0; i < newlines; i++)
                EMIT(e, C_LF);
        }
    }

    // KEEP mode: emit trailing-1 newlines (first is implicit)
    if (n->chomp == CYAML_KEEP && trailing >= 2) {
        for (size_t i = 1; i < trailing; i++)
            EMIT(e, C_LF);
    }

    return true;
}

// #endregion

// #region Normal Emit Mode

static bool emit_node(emitter_t* e, const cyaml_node_t* n, int depth);

static bool can_be_single(const char* s, size_t len);
static bool has_trailing_whitespace(const char* s, size_t len);
static bool has_leading_space_tab(const char* s, size_t len);

static bool emit_anchor(emitter_t* e, const cyaml_node_t* n)
{
    if (n->anchor.len == 0)
        return true;
    EMIT(e, '&');
    EMIT_N(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len);
    EMIT(e, C_SP);
    return true;
}

static bool emit_tag(emitter_t* e, const cyaml_node_t* n)
{
    if (n->tag.len == 0)
        return true;
    EMIT_N(e, cyaml_src(e->doc) + n->tag.off, n->tag.len);
    EMIT(e, C_SP);
    return true;
}

static bool emit_scalar(emitter_t* e, const cyaml_node_t* n, int depth)
{
    char* s = cyaml_scalar_str(e->doc, n);
    if (!s)
        return emit_cstr(e, "\"\"");
    size_t len = strlen(s);

    cyaml_style_t style = n->style;

    switch (style) {
    case CYAML_PLAIN:
        if (needs_quoting_ex(s, len, n->tag.len)) {
            style = CYAML_DOUBLE;
        }
        break;
    case CYAML_SINGLE:
        if (!can_be_single(s, len))
            style = CYAML_DOUBLE;
        break;
    case CYAML_DOUBLE:
        break;
    case CYAML_LITERAL:
    case CYAML_FOLDED:
        if (len == 0) {
            style = CYAML_DOUBLE;
        } else if (n->chomp != CYAML_KEEP) {
            // Check for trailing whitespace before trailing newlines
            size_t check_len = len;
            while (check_len > 0 && s[check_len - 1] == C_LF)
                check_len--;
            if (check_len > 0 && (s[check_len - 1] == C_SP || s[check_len - 1] == C_TAB)) {
                // With CLIP/STRIP, trailing whitespace can't be preserved
                style = CYAML_DOUBLE;
            }
        }
        break;
    default:
        break;
    }

    bool result;
    switch (style) {
    case CYAML_DOUBLE:
        result = emit_double_quoted(e, s, len);
        break;
    case CYAML_SINGLE:
        result = emit_single_quoted(e, s, len, depth);
        break;
    case CYAML_LITERAL:
        result = emit_literal(e, s, len, n, depth);
        break;
    case CYAML_FOLDED:
        result = emit_folded(e, s, len, n, depth);
        break;
    default:
        result = emit_str(e, s, len);
        break;
    }

    free(s);
    return result;
}

static bool emit_block_seq(emitter_t* e, const cyaml_node_t* n, int depth);
static bool emit_block_map(emitter_t* e, const cyaml_node_t* n, int depth);
static bool emit_flow_seq(emitter_t* e, const cyaml_node_t* n, int depth);
static bool emit_flow_map(emitter_t* e, const cyaml_node_t* n, int depth);

static bool emit_node(emitter_t* e, const cyaml_node_t* n, int depth)
{
    if (!n || n->type == CYAML_NONE) {
        EMIT_S(e, S_NULL);
        return true;
    }

    switch (n->type) {
    case CYAML_NULL:
        EMIT_PROPS(e, n);
        EMIT_S(e, S_NULL);
        return true;

    case CYAML_SCALAR:
        EMIT_PROPS(e, n);
        return emit_scalar(e, n, depth);

    case CYAML_SEQ:
        if (!EMIT_IS_DUMP(e) && n->style == (cyaml_style_t)CYAML_FLOW && n->seq.count > 0) {
            EMIT_PROPS(e, n);
            return emit_block_seq(e, n, depth);
        }
        if (n->style == (cyaml_style_t)CYAML_FLOW || e->opts.coll == CYAML_FLOW) {
            EMIT_PROPS(e, n);
            return emit_flow_seq(e, n, depth);
        }
        EMIT_PROPS(e, n);
        return emit_block_seq(e, n, depth);

    case CYAML_MAP:
        if (!EMIT_IS_DUMP(e) && n->style == (cyaml_style_t)CYAML_FLOW && n->map.count > 0) {
            EMIT_PROPS(e, n);
            return emit_block_map(e, n, depth);
        }
        if (n->style == (cyaml_style_t)CYAML_FLOW || e->opts.coll == CYAML_FLOW) {
            EMIT_PROPS(e, n);
            return emit_flow_map(e, n, depth);
        }
        EMIT_PROPS(e, n);
        return emit_block_map(e, n, depth);

    case CYAML_ALIAS:
        EMIT(e, '*');
        return emit_str(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len);

    default:
        CYAML_UNREACHABLE("invalid node type");
    }
}

static bool emit_flow_seq(emitter_t* e, const cyaml_node_t* n, int depth)
{
    EMIT(e, '[');
    for (uint32_t i = 0; i < n->seq.count; i++) {
        if (i > 0)
            EMIT_S(e, ", ");
        NODE(e, n->seq.items[i], depth);
    }
    EMIT(e, ']');
    return true;
}

static bool emit_flow_map(emitter_t* e, const cyaml_node_t* n, int depth)
{
    EMIT(e, '{');
    for (uint32_t i = 0; i < n->map.count; i++) {
        if (i > 0)
            EMIT_S(e, ", ");
        NODE(e, n->map.pairs[i].key, depth);
        EMIT_S(e, ": ");
        NODE(e, n->map.pairs[i].val, depth);
    }
    EMIT(e, '}');
    return true;
}

static bool emit_block_seq(emitter_t* e, const cyaml_node_t* n, int depth)
{
    for (uint32_t i = 0; i < n->seq.count; i++) {
        cyaml_node_t* item = n->seq.items[i];
        if (item && e->opts.comments)
            if (!emit_comments_before_line(e, item->span.start_line, depth))
                return false;
        bool already_newline = (EMIT_LAST(e) == C_LF);
        if (i > 0 || (depth > 0 && !already_newline))
            EMIT(e, C_LF);
        INDENT(e, depth);
        EMIT_S(e, "- ");
        NODE(e, item, depth + 1);
        if (item && e->opts.comments && item->type == CYAML_SCALAR)
            if (!emit_inline_comment(e, item->span.end_line))
                return false;
    }
    return true;
}

//! Emit sequence in compact format (first item inline, rest indented)
static bool emit_compact_seq(emitter_t* e, const cyaml_node_t* n, int indent)
{
    for (uint32_t i = 0; i < n->seq.count; i++) {
        if (i > 0) {
            EMIT(e, C_LF);
            INDENT(e, indent);
        }
        EMIT_S(e, "- ");
        NODE(e, n->seq.items[i], indent + 1);
    }
    return true;
}

static bool emit_block_map(emitter_t* e, const cyaml_node_t* n, int depth)
{
    for (uint32_t i = 0; i < n->map.count; i++) {
        const cyaml_node_t* key = n->map.pairs[i].key;
        const cyaml_node_t* val = n->map.pairs[i].val;
        if (key && e->opts.comments)
            if (!emit_comments_before_line(e, key->span.start_line, depth))
                return false;
        bool just_after_dash = (EMIT_PREV(e) == '-' && EMIT_LAST(e) == C_SP);
        bool already_newline = (EMIT_LAST(e) == C_LF);
        if (i > 0 || (depth > 0 && !just_after_dash && !already_newline))
            EMIT(e, C_LF);
        if (i > 0 || (!just_after_dash && (depth > 0 || already_newline))) {
            INDENT(e, depth);
        }
        bool key_is_empty = !key || key->type == CYAML_NULL || key->type == CYAML_NONE;
        bool key_is_complex = key && (key->type == CYAML_MAP || key->type == CYAML_SEQ);

        if (key_is_complex) {
            EMIT_S(e, "? ");
            if (key->type == CYAML_SEQ) {
                if (!emit_compact_seq(e, key, depth + 1))
                    return false;
            } else {
                NODE(e, key, depth);
            }
            EMIT(e, C_LF);
            INDENT(e, depth);
        } else if (!key_is_empty) {
            NODE(e, key, depth);
        }

        bool val_is_empty = !val || val->type == CYAML_NULL || val->type == CYAML_NONE;
        if (!val_is_empty && val->type == CYAML_SCALAR && val->style == CYAML_PLAIN) {
            char* vs = cyaml_scalar_str(e->doc, val);
            if (vs && strlen(vs) == 0)
                val_is_empty = true;
            free(vs);
        }
        bool val_is_block = false;
        if (val && (val->type == CYAML_MAP || val->type == CYAML_SEQ)) {
            if (val->style != (cyaml_style_t)CYAML_FLOW) {
                val_is_block = true;
            } else if (!EMIT_IS_DUMP(e)) {
                uint32_t count = val->type == CYAML_MAP ? val->map.count : val->seq.count;
                if (count > 0)
                    val_is_block = true;
            }
        }

        if (val_is_empty) {
            EMIT(e, ':');
        } else if (key_is_complex && val->type == CYAML_SEQ) {
            EMIT_S(e, ": ");
            if (!emit_compact_seq(e, val, depth + 1))
                return false;
        } else if (val_is_block) {
            bool val_has_props = val->anchor.len > 0 || val->tag.len > 0;
            if (val_has_props) {
                // Emit props on same line as key, then newline
                EMIT_S(e, ": ");
                EMIT_PROPS(e, val);
                EMIT(e, C_LF);
                int val_depth = (val->type == CYAML_SEQ) ? depth : depth + 1;
                // Emit collection without props (already emitted)
                if (val->type == CYAML_SEQ) {
                    if (!emit_block_seq(e, val, val_depth))
                        return false;
                } else {
                    if (!emit_block_map(e, val, val_depth))
                        return false;
                }
            } else {
                EMIT_S(e, ":\n");
                int val_depth = (val->type == CYAML_SEQ) ? depth : depth + 1;
                NODE(e, val, val_depth);
            }
        } else {
            EMIT_S(e, ": ");
            NODE(e, val, depth + 1);
            if (e->opts.comments && val && val->type == CYAML_SCALAR)
                if (!emit_inline_comment(e, val->span.end_line))
                    return false;
        }
    }
    return true;
}

// #endregion

// #region Dump Mode (yaml-test-suite format)

static bool dump_node(emitter_t* e, const cyaml_node_t* n, int depth);

//! Get scalar content with chomping applied
static char* get_dump_scalar(const cyaml_doc_t* doc, const cyaml_node_t* n, size_t* out_len)
{
    char* str = cyaml_scalar_str(doc, n);
    if (!str) {
        *out_len = 0;
        return NULL;
    }

    size_t len = strlen(str);

    if (n->style == CYAML_LITERAL || n->style == CYAML_FOLDED) {
        while (len > 0 && str[len - 1] == C_LF)
            len--;
        str[len] = C_NUL;

        // Add trailing newlines based on chomping: KEEP=all, CLIP=1, STRIP=0
        uint8_t trailing = 0;
        if (n->chomp == CYAML_KEEP) {
            trailing = n->trailing_breaks;
        } else if (n->chomp == CYAML_CLIP && n->trailing_breaks > 0) {
            trailing = 1;
        }
        if (trailing > 0) {
            char* new_str = realloc(str, len + trailing + 1);
            if (new_str) {
                str = new_str;
                for (uint8_t i = 0; i < trailing; i++)
                    str[len + i] = C_LF;
                len += trailing;
                str[len] = C_NUL;
            }
        }
    }

    *out_len = len;
    return str;
}

//! Check if content has problematic trailing whitespace for block scalars
//! - Trailing space/tab before newlines (except tab-only lines) needs quoting
//! - Trailing space/tab at the very end is also problematic
//! - Lines consisting only of spaces could be confused with indentation
//! Note: Lines with only tabs are okay - tabs aren't valid YAML indentation
static bool has_trailing_whitespace(const char* s, size_t len)
{
    // Check trailing whitespace at the very end
    if (len > 0 && (s[len - 1] == C_SP || s[len - 1] == C_TAB)) {
        return true;
    }
    // Check each line
    size_t line_start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == C_LF) {
            size_t line_len = i - line_start;
            if (line_len > 0) {
                // Check if line ends with space/tab (trailing whitespace)
                char last_char = s[i - 1];
                if (last_char == C_SP || last_char == C_TAB) {
                    // Trailing whitespace - but check if line is ONLY tabs (allowed)
                    bool only_tabs = true;
                    for (size_t j = line_start; j < i; j++) {
                        if (s[j] != C_TAB) {
                            only_tabs = false;
                            break;
                        }
                    }
                    if (!only_tabs) {
                        return true; // Has trailing whitespace and not a tab-only line
                    }
                }
            }
            line_start = i + 1;
        }
    }
    return false;
}

//! Check if content has tab after spaces at line start (can't represent in block scalar)
static bool has_leading_space_tab(const char* s, size_t len)
{
    bool at_line_start = true;
    bool seen_space = false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == C_LF) {
            at_line_start = true;
            seen_space = false;
        } else if (at_line_start) {
            if (s[i] == C_SP) {
                seen_space = true;
            } else if (s[i] == C_TAB && seen_space) {
                return true; // Space followed by tab at line start
            } else {
                at_line_start = false;
                seen_space = false;
            }
        }
    }
    return false;
}

//! Check if content can be plain scalar
//! Check if content contains non-ASCII characters
static bool has_non_ascii(const char* s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)s[i] > 0x7F)
            return true;
    }
    return false;
}

static bool can_be_plain_ex(const char* s, size_t len, size_t tag_len)
{
    if (len == 0)
        return false;
    if (needs_quoting_ex(s, len, tag_len))
        return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == C_LF || c == C_CR)
            return false;
        if (c < 0x20 && c != C_TAB)
            return false;
    }
    return true;
}

static bool can_be_plain(const char* s, size_t len)
{
    return can_be_plain_ex(s, len, 0);
}

//! Check if content can be single-quoted
static bool can_be_single(const char* s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 && c != C_TAB && c != C_LF && c != C_CR)
            return false;
    }
    return true;
}

//! Check if a block scalar has tabs (preserved as content)
static bool has_block_scalar_tabs(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    if (!n)
        return false;
    if (n->type == CYAML_SCALAR && (n->style == CYAML_LITERAL || n->style == CYAML_FOLDED) && n->span.len > 0) {
        const char* src = cyaml_src(doc);
        if (src) {
            for (uint32_t i = n->span.off; i < n->span.off + n->span.len; i++) {
                if (src[i] == C_TAB)
                    return true;
            }
        }
    }
    if (n->type == CYAML_SEQ) {
        for (uint32_t i = 0; i < n->seq.count; i++) {
            if (has_block_scalar_tabs(doc, n->seq.items[i]))
                return true;
        }
    }
    if (n->type == CYAML_MAP) {
        for (uint32_t i = 0; i < n->map.count; i++) {
            if (has_block_scalar_tabs(doc, n->map.pairs[i].key))
                return true;
            if (has_block_scalar_tabs(doc, n->map.pairs[i].val))
                return true;
        }
    }
    return false;
}

//! Check if a double-quoted scalar has tabs in line-folding position that get normalized
//! Only checks within collections, not root scalars (which don't need --- marker).
static bool has_folding_tabs_in_collection(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    if (!n)
        return false;
    // Only check scalars that are inside collections
    if (n->type == CYAML_SCALAR && n->style == CYAML_DOUBLE && n->span.len > 0) {
        const char* src = cyaml_src(doc);
        if (src) {
            // Look for tabs in folding position (after newline, possibly with spaces)
            bool after_newline = false;
            for (uint32_t i = n->span.off; i < n->span.off + n->span.len; i++) {
                char c = src[i];
                if (c == C_LF || c == C_CR) {
                    after_newline = true;
                } else if (after_newline) {
                    if (c == C_SP) {
                        // Still in leading whitespace
                    } else if (c == C_TAB) {
                        // Tab in folding position - normalized to space
                        return true;
                    } else {
                        after_newline = false;
                    }
                }
            }
        }
    }
    if (n->type == CYAML_SEQ) {
        for (uint32_t i = 0; i < n->seq.count; i++) {
            if (has_folding_tabs_in_collection(doc, n->seq.items[i]))
                return true;
        }
    }
    if (n->type == CYAML_MAP) {
        for (uint32_t i = 0; i < n->map.count; i++) {
            if (has_folding_tabs_in_collection(doc, n->map.pairs[i].key))
                return true;
            if (has_folding_tabs_in_collection(doc, n->map.pairs[i].val))
                return true;
        }
    }
    return false;
}

//! Check if a block scalar will be converted due to trailing whitespace at the end
//! Internal blank lines with whitespace are OK (preserved in literal blocks).
//! But content ending with whitespace (no trailing newline) is problematic.
static bool block_scalar_needs_conversion(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    if (!n)
        return false;
    if (n->type == CYAML_SCALAR && (n->style == CYAML_LITERAL || n->style == CYAML_FOLDED)) {
        // With KEEP chomping, trailing whitespace is explicitly preserved
        // and will be correctly represented in the literal block
        if (n->chomp == CYAML_KEEP) {
            return false;
        }
        char* s = cyaml_scalar_str(doc, n);
        if (s) {
            size_t len = strlen(s);
            // Check if content ends with whitespace (possibly before trailing newlines)
            // This is problematic because it would appear as trailing whitespace
            // on the final content line in the literal block.
            // Skip trailing newlines first (from CLIP/KEEP chomping)
            while (len > 0 && s[len - 1] == C_LF)
                len--;
            if (len > 0 && (s[len - 1] == C_SP || s[len - 1] == C_TAB)) {
                free(s);
                return true;
            }
            free(s);
        }
    }
    if (n->type == CYAML_SEQ) {
        for (uint32_t i = 0; i < n->seq.count; i++) {
            if (block_scalar_needs_conversion(doc, n->seq.items[i]))
                return true;
        }
    }
    if (n->type == CYAML_MAP) {
        for (uint32_t i = 0; i < n->map.count; i++) {
            if (block_scalar_needs_conversion(doc, n->map.pairs[i].key))
                return true;
            if (block_scalar_needs_conversion(doc, n->map.pairs[i].val))
                return true;
        }
    }
    return false;
}

//! Dump scalar preserving original style when possible
static bool dump_scalar(emitter_t* e, const cyaml_node_t* n, int depth)
{
    size_t len;
    char* s = get_dump_scalar(e->doc, n, &len);
    if (!s)
        return emit_cstr(e, "\"\"");

    cyaml_style_t style = n->style;

    // In dump mode, non-ASCII content must be double-quoted (to escape as \uXXXX)
    if (has_non_ascii(s, len)) {
        style = CYAML_DOUBLE;
    }

    // Validate original style can represent the content
    switch (style) {
    case CYAML_PLAIN:
        // With a type tag, reserved words don't need quoting (tag disambiguates)
        if (!can_be_plain_ex(s, len, n->tag.len)) {
            // Prefer single-quoted if possible, else double
            style = can_be_single(s, len) ? CYAML_SINGLE : CYAML_DOUBLE;
        }
        break;
    case CYAML_SINGLE:
        if (!can_be_single(s, len))
            style = CYAML_DOUBLE;
        break;
    case CYAML_DOUBLE:
        break;
    case CYAML_LITERAL:
    case CYAML_FOLDED:
        if (len == 0) {
            // Empty block scalar: keep mode stays block, others become ""
            if (n->chomp != CYAML_KEEP) {
                style = CYAML_DOUBLE;
            }
        } else if (has_trailing_whitespace(s, len)) {
            style = CYAML_DOUBLE;
        } else if (has_leading_space_tab(s, len)) {
            // Tab after spaces at line start is ambiguous with indentation
            style = CYAML_DOUBLE;
        } else if (style == CYAML_FOLDED) {
            // For folded scalars, first line all-whitespace is problematic
            // (folding rules interact poorly with leading whitespace)
            // Literal scalars preserve exactly, so they're fine
            size_t first_newline = 0;
            while (first_newline < len && s[first_newline] != C_LF)
                first_newline++;
            if (first_newline > 0) {
                bool first_line_all_ws = true;
                for (size_t j = 0; j < first_newline; j++) {
                    if (s[j] != C_SP && s[j] != C_TAB) {
                        first_line_all_ws = false;
                        break;
                    }
                }
                if (first_line_all_ws)
                    style = CYAML_DOUBLE;
            }
        }
        break;
    default:
        style = can_be_plain(s, len) ? CYAML_PLAIN : can_be_single(s, len) ? CYAML_SINGLE
                                                                           : CYAML_DOUBLE;
        break;
    }

    bool result;
    switch (style) {
    case CYAML_DOUBLE:
        result = emit_double_quoted(e, s, len);
        break;
    case CYAML_SINGLE:
        result = emit_single_quoted(e, s, len, depth);
        break;
    case CYAML_LITERAL:
        result = emit_literal(e, s, len, n, depth);
        break;
    case CYAML_FOLDED:
        result = emit_folded(e, s, len, n, depth);
        break;
    default:
        result = emit_str(e, s, len);
        break;
    }

    free(s);
    return result;
}

//! Emit anchor with trailing space (&name )
static bool dump_anchor(emitter_t* e, const cyaml_node_t* n)
{
    if (n->anchor.len == 0)
        return true;
    EMIT(e, '&');
    EMIT_N(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len);
    EMIT(e, C_SP);
    return true;
}

//! Canonicalize tag URI: tag:yaml.org,2002:xxx -> !!xxx, !local -> !local, else !<uri>
static bool emit_canonical_tag(emitter_t* e, const char* uri, size_t uri_len)
{
    static const char* yaml_prefix = "tag:yaml.org,2002:";
    static const size_t yaml_prefix_len = 18;

    if (uri_len > yaml_prefix_len && memcmp(uri, yaml_prefix, yaml_prefix_len) == 0) {
        EMIT_S(e, "!!");
        return emit_str(e, uri + yaml_prefix_len, uri_len - yaml_prefix_len);
    }
    if (uri_len > 0 && uri[0] == '!') {
        return emit_str(e, uri, uri_len);
    }
    EMIT_S(e, "!<");
    EMIT_N(e, uri, uri_len);
    EMIT(e, '>');
    return true;
}

static bool emit_resolved_tag_raw(emitter_t* e, const cyaml_node_t* n)
{
    if (n->tag.len == 0)
        return true;

    const char* src = cyaml_src(e->doc);
    const char* tag = src + n->tag.off;
    size_t tag_len = n->tag.len;

    // Verbatim tag !<...> - extract URI and canonicalize
    if (tag_len >= 3 && tag[0] == '!' && tag[1] == '<' && tag[tag_len - 1] == '>') {
        const char* uri = tag + 2;
        size_t uri_len = tag_len - 3;
        return emit_canonical_tag(e, uri, uri_len);
    }

    // In dump mode, resolve tag handles to full URI then canonicalize
    if (EMIT_IS_DUMP(e) && e->doc) {
        // Try to match tag handles in order of specificity:
        // 1. Secondary handle !! (2 chars)
        // 2. Named handle !name! (ends with !)
        // 3. Primary handle ! (1 char)

        size_t handle_len = 0;
        const cyaml_tag_directive_t* matched_td = NULL;

        for (uint8_t i = 0; i < e->doc->tag_count; i++) {
            const cyaml_tag_directive_t* td = &e->doc->tags[i];
            const char* handle = src + td->handle.off;
            size_t h_len = td->handle.len;

            // Check if tag starts with this handle
            if (h_len <= tag_len && memcmp(handle, tag, h_len) == 0) {
                // Prefer longer (more specific) handles
                if (h_len > handle_len) {
                    handle_len = h_len;
                    matched_td = td;
                }
            }
        }

        if (matched_td) {
            // Found matching handle - build full URI and canonicalize
            const char* prefix = src + matched_td->prefix.off;
            size_t prefix_len = matched_td->prefix.len;
            const char* suffix = tag + handle_len;
            size_t suffix_len = tag_len - handle_len;

            // Build full URI in temp buffer
            size_t uri_len = prefix_len + suffix_len;
            char* uri = malloc(uri_len + 1);
            if (!uri)
                return false;
            memcpy(uri, prefix, prefix_len);
            memcpy(uri + prefix_len, suffix, suffix_len);
            uri[uri_len] = C_NUL;

            bool ok = emit_canonical_tag(e, uri, uri_len);
            free(uri);
            return ok;
        }
    }

    // No resolution needed or handle not found - emit as-is
    return emit_str(e, tag, tag_len);
}

//! Emit resolved tag with trailing space
static bool dump_tag(emitter_t* e, const cyaml_node_t* n)
{
    if (n->tag.len == 0)
        return true;
    if (!emit_resolved_tag_raw(e, n))
        return false;
    EMIT(e, C_SP);
    return true;
}

//! Check if value is empty (null, none, or empty scalar except KEEP blocks)
static bool is_empty_value(const cyaml_node_t* val)
{
    if (!val || val->type == CYAML_NONE || val->type == CYAML_NULL)
        return true;
    if (val->type == CYAML_SCALAR && val->span.len == 0) {
        // KEEP blocks with trailing breaks need to emit |+
        if ((val->style == CYAML_LITERAL || val->style == CYAML_FOLDED) && val->chomp == CYAML_KEEP)
            return false;
        return true;
    }
    return false;
}

//! Check if key needs space before colon (*alias, !tag, &anchor require "key :")
static bool key_needs_space(const cyaml_node_t* key)
{
    if (!key)
        return false;
    if (key->type == CYAML_ALIAS)
        return true;
    if (key->type == CYAML_SCALAR && key->tag.len > 0 && key->span.len == 0)
        return true;
    if (key->type == CYAML_NULL && (key->tag.len > 0 || key->anchor.len > 0))
        return true;
    return false;
}

// Forward declarations
static bool is_empty_key(const cyaml_node_t* key);

//! Check if key needs explicit form (? key\n: value)
//! Required for: collection keys, block scalars, or double-quoted with newline escapes
static bool key_needs_explicit(emitter_t* e, const cyaml_node_t* key)
{
    if (!key)
        return false;
    if (key->type == CYAML_MAP || key->type == CYAML_SEQ)
        return true;
    if (key->type != CYAML_SCALAR)
        return false;
    if (key->style == CYAML_LITERAL || key->style == CYAML_FOLDED)
        return true;
    if (key->style != CYAML_DOUBLE)
        return false;

    // Check for newline escape sequences in double-quoted key
    const char* src = cyaml_src(e->doc);
    const char* p = src + key->span.off;
    const char* end = p + key->span.len;
    while (p < end) {
        if (*p == C_BSLASH && p + 1 < end) {
            char next = *(p + 1);
            if (next == 'n' || next == 'r' || next == 'N' || next == 'L' || next == 'P')
                return true;
        }
        p++;
    }
    return false;
}

static bool dump_block_map(emitter_t* e, const cyaml_node_t* n, int depth);
static bool dump_block_seq_from(emitter_t* e, const cyaml_node_t* n, int depth, uint32_t start);

//! Emit a seq item inline, handling nested collections that need explicit keys
static bool emit_seq_item_inline(emitter_t* e, const cyaml_node_t* item, int depth)
{
    EMIT_S(e, "- ");

    if (item && item->type == CYAML_SEQ && item->seq.count > 0 && item->anchor.len == 0 && item->tag.len == 0) {
        cyaml_node_t* first = item->seq.items[0];
        if (first && first->type == CYAML_MAP && first->map.count > 0 && key_needs_explicit(e, first->map.pairs[0].key)) {
            EMIT_S(e, "- ? ");
            cyaml_node_t* k = first->map.pairs[0].key;
            if (k->type == CYAML_SEQ && k->seq.count > 0) {
                if (!emit_seq_item_inline(e, k->seq.items[0], depth + 2))
                    return false;
                for (uint32_t j = 1; j < k->seq.count; j++) {
                    EMIT(e, C_LF);
                    INDENT(e, depth + 4);
                    if (!emit_seq_item_inline(e, k->seq.items[j], depth + 4))
                        return false;
                }
            } else {
                if (!emit_seq_item_inline(e, k, depth + 2))
                    return false;
            }
            EMIT(e, C_LF);
            INDENT(e, depth + 2);
            EMIT_S(e, ": ");
            cyaml_node_t* v = first->map.pairs[0].val;
            if (!is_empty_value(v)) {
                DNODE(e, v, depth + 2);
            }
            for (uint32_t j = 1; j < first->map.count; j++) {
                EMIT(e, C_LF);
                INDENT(e, depth + 2);
                DNODE(e, first->map.pairs[j].key, depth + 2);
                EMIT_S(e, ": ");
                DNODE(e, first->map.pairs[j].val, depth + 2);
            }
            if (item->seq.count > 1) {
                if (!dump_block_seq_from(e, item, depth + 1, 1))
                    return false;
            }
            return true;
        }
        if (!emit_seq_item_inline(e, first, depth + 2))
            return false;
        if (item->seq.count > 1) {
            if (!dump_block_seq_from(e, item, depth + 2, 1))
                return false;
        }
        return true;
    }

    if (item && item->type == CYAML_MAP && item->map.count > 0 && item->anchor.len == 0 && item->tag.len == 0 && key_needs_explicit(e, item->map.pairs[0].key)) {
        EMIT_S(e, "? ");
        cyaml_node_t* k = item->map.pairs[0].key;
        if (k->type == CYAML_SEQ && k->seq.count > 0) {
            if (!emit_seq_item_inline(e, k->seq.items[0], depth + 2))
                return false;
            for (uint32_t j = 1; j < k->seq.count; j++) {
                EMIT(e, C_LF);
                INDENT(e, depth + 2);
                if (!emit_seq_item_inline(e, k->seq.items[j], depth + 2))
                    return false;
            }
        } else {
            DNODE(e, k, depth + 2);
        }
        EMIT(e, C_LF);
        INDENT(e, depth);
        EMIT_S(e, ": ");
        cyaml_node_t* v = item->map.pairs[0].val;
        if (!is_empty_value(v)) {
            DNODE(e, v, depth + 1);
        }
        return true;
    }

    if (!item) {
        EMIT_S(e, S_NULL);
        return true;
    }
    if (item->type == CYAML_SCALAR)
        return dump_scalar(e, item, depth);
    DNODE(e, item, depth);
    return true;
}

static bool dump_block_seq(emitter_t* e, const cyaml_node_t* n, int depth)
{
    return dump_block_seq_from(e, n, depth, 0);
}

static bool dump_block_seq_from(emitter_t* e, const cyaml_node_t* n, int depth, uint32_t start)
{
    for (uint32_t i = start; i < n->seq.count; i++) {
        bool has_trailing_nl = (EMIT_LAST(e) == C_LF && EMIT_PREV(e) == C_LF);
        if (!has_trailing_nl && (i > start || depth > 0))
            EMIT(e, C_LF);
        INDENT(e, depth);

        cyaml_node_t* item = n->seq.items[i];

        if (item && item->type == CYAML_MAP && item->map.count > 0) {
            bool item_has_props = item->anchor.len > 0 || item->tag.len > 0;
            if (item_has_props) {
                EMIT_S(e, "- ");
                DUMP_PROPS(e, item);
                EMIT_TRIM_SPACE(e);
                if (!dump_block_map(e, item, depth + 1))
                    return false;
                continue;
            }
            cyaml_node_t* first_key = item->map.pairs[0].key;
            cyaml_node_t* first_val = item->map.pairs[0].val;

            if (key_needs_explicit(e, first_key)) {
                EMIT_S(e, "- ? ");
                if (first_key->type == CYAML_SEQ && first_key->seq.count > 0) {
                    if (!emit_seq_item_inline(e, first_key->seq.items[0], depth + 2))
                        return false;
                    for (uint32_t j = 1; j < first_key->seq.count; j++) {
                        EMIT(e, C_LF);
                        INDENT(e, depth + 2);
                        if (!emit_seq_item_inline(e, first_key->seq.items[j], depth + 2))
                            return false;
                    }
                } else if (first_key->type == CYAML_MAP && first_key->map.count > 0) {
                    cyaml_node_t* k0 = first_key->map.pairs[0].key;
                    cyaml_node_t* v0 = first_key->map.pairs[0].val;
                    if (!is_empty_key(k0)) {
                        DNODE(e, k0, depth);
                    }
                    EMIT_S(e, ": ");
                    DNODE(e, v0, depth);
                    for (uint32_t j = 1; j < first_key->map.count; j++) {
                        EMIT(e, C_LF);
                        INDENT(e, depth + 2);
                        cyaml_node_t* kj = first_key->map.pairs[j].key;
                        if (!is_empty_key(kj)) {
                            DNODE(e, kj, depth);
                        }
                        EMIT_S(e, ": ");
                        DNODE(e, first_key->map.pairs[j].val, depth);
                    }
                } else {
                    DNODE(e, first_key, depth + 1);
                }
                EMIT(e, C_LF);
                INDENT(e, depth + 1);
                EMIT(e, ':');
                if (!is_empty_value(first_val)) {
                    EMIT(e, C_SP);
                    DNODE(e, first_val, depth + 2);
                }
                for (uint32_t j = 1; j < item->map.count; j++) {
                    EMIT(e, C_LF);
                    INDENT(e, depth + 1);
                    cyaml_node_t* k = item->map.pairs[j].key;
                    cyaml_node_t* v = item->map.pairs[j].val;
                    if (key_needs_explicit(e, k)) {
                        EMIT_S(e, "? ");
                        DNODE(e, k, depth + 1);
                        EMIT(e, C_LF);
                        INDENT(e, depth + 1);
                        EMIT(e, ':');
                    } else {
                        DNODE(e, k, depth + 1);
                        EMIT(e, ':');
                    }
                    if (!is_empty_value(v)) {
                        EMIT(e, C_SP);
                        DNODE(e, v, depth + 2);
                    }
                }
                continue;
            }

            EMIT_S(e, "- ");
            DNODE(e, first_key, depth);
            if (key_needs_space(first_key)) {
                EMIT_S(e, " :");
            } else {
                EMIT(e, ':');
            }
            if (is_empty_value(first_val)) {
                if (first_val && (first_val->anchor.len > 0 || first_val->tag.len > 0)) {
                    EMIT(e, C_SP);
                    DUMP_PROPS(e, first_val);
                    EMIT_TRIM_SPACE(e);
                }
            } else if (first_val->type == CYAML_SEQ || first_val->type == CYAML_MAP) {
                DNODE(e, first_val, depth + 1);
            } else {
                EMIT(e, C_SP);
                DNODE(e, first_val, depth + 2);
            }
            for (uint32_t j = 1; j < item->map.count; j++) {
                EMIT(e, C_LF);
                INDENT(e, depth + 1);
                cyaml_node_t* key = item->map.pairs[j].key;
                DNODE(e, key, depth + 1);
                if (key && key->type == CYAML_ALIAS) {
                    EMIT_S(e, " :");
                } else {
                    EMIT(e, ':');
                }
                cyaml_node_t* val = item->map.pairs[j].val;
                if (is_empty_value(val)) {
                    if (val && (val->anchor.len > 0 || val->tag.len > 0)) {
                        EMIT(e, C_SP);
                        DUMP_PROPS(e, val);
                        EMIT_TRIM_SPACE(e);
                    }
                } else if (val->type == CYAML_SEQ || val->type == CYAML_MAP) {
                    DNODE(e, val, depth + 2);
                } else {
                    EMIT(e, C_SP);
                    DNODE(e, val, depth + 2);
                }
            }
        }
        // Compact nested sequences: - - item
        else if (item && item->type == CYAML_SEQ && item->seq.count > 0) {
            bool item_has_props = item->anchor.len > 0 || item->tag.len > 0;
            if (item_has_props) {
                EMIT_S(e, "- ");
                DUMP_PROPS(e, item);
                EMIT_TRIM_SPACE(e);
                if (!dump_block_seq(e, item, depth + 1))
                    return false;
                continue;
            }
            EMIT_S(e, "- ");
            cyaml_node_t* cur = item;
            int extra_depth = 0;
            while (cur->seq.count == 1 && cur->seq.items[0] && cur->seq.items[0]->type == CYAML_SEQ && cur->seq.items[0]->seq.count > 0 && cur->seq.items[0]->anchor.len == 0 && cur->seq.items[0]->tag.len == 0) {
                EMIT_S(e, "- ");
                cur = cur->seq.items[0];
                extra_depth++;
            }
            if (cur->seq.count > 0) {
                cyaml_node_t* first = cur->seq.items[0];
                if (first && first->type == CYAML_MAP && first->map.count > 0 && first->anchor.len == 0 && first->tag.len == 0) {
                    EMIT_S(e, "- ");
                    cyaml_node_t* k = first->map.pairs[0].key;
                    cyaml_node_t* v = first->map.pairs[0].val;
                    if (key_needs_explicit(e, k)) {
                        EMIT_S(e, "? ");
                        if (k->type == CYAML_MAP && k->map.count > 0) {
                            cyaml_node_t* k0 = k->map.pairs[0].key;
                            cyaml_node_t* v0 = k->map.pairs[0].val;
                            DNODE(e, k0, depth);
                            EMIT_S(e, ": ");
                            DNODE(e, v0, depth);
                        } else {
                            DNODE(e, k, depth + extra_depth + 1);
                        }
                        EMIT(e, C_LF);
                        INDENT(e, depth + extra_depth + 2);
                        EMIT(e, ':');
                        if (!is_empty_value(v)) {
                            EMIT(e, C_SP);
                            DNODE(e, v, depth + extra_depth + 1);
                        }
                    } else {
                        if (!is_empty_key(k)) {
                            DNODE(e, k, depth);
                            if (key_needs_space(k)) {
                                EMIT_S(e, " :");
                            } else {
                                EMIT(e, ':');
                            }
                        } else {
                            EMIT(e, ':');
                        }
                        if (!is_empty_value(v)) {
                            EMIT(e, C_SP);
                            DNODE(e, v, depth + extra_depth + 2);
                        }
                    }
                    for (uint32_t m = 1; m < first->map.count; m++) {
                        EMIT(e, C_LF);
                        INDENT(e, depth + extra_depth + 1);
                        DNODE(e, first->map.pairs[m].key, depth);
                        EMIT_S(e, ": ");
                        DNODE(e, first->map.pairs[m].val, depth + extra_depth + 2);
                    }
                } else {
                    EMIT_S(e, "- ");
                    DNODE(e, first, depth + extra_depth + 1);
                }
                for (uint32_t j = 1; j < cur->seq.count; j++) {
                    EMIT(e, C_LF);
                    INDENT(e, depth + extra_depth + 1);
                    EMIT_S(e, "- ");
                    DNODE(e, cur->seq.items[j], depth + extra_depth + 2);
                }
            }
        }
        // Empty/null item: emit "-" with anchor/tag if present
        else if (!item || item->type == CYAML_NULL || item->type == CYAML_NONE || (item->type == CYAML_SCALAR && item->span.len == 0 && !((item->style == CYAML_LITERAL || item->style == CYAML_FOLDED) && item->chomp == CYAML_KEEP))) {
            if (item && (item->anchor.len > 0 || item->tag.len > 0)) {
                EMIT_S(e, "- ");
                DUMP_PROPS(e, item);
                EMIT_TRIM_SPACE(e);
            } else {
                EMIT(e, '-');
            }
        } else {
            EMIT_S(e, "- ");
            DNODE(e, item, depth + 1);
        }
    }
    return true;
}

//! Check if key is empty/null (no content, no anchor, no tag)
static bool is_empty_key(const cyaml_node_t* key)
{
    if (!key || key->type == CYAML_NONE)
        return true;
    if (key->type == CYAML_NULL && key->anchor.len == 0 && key->tag.len == 0)
        return true;
    if (key->type == CYAML_SCALAR && key->span.len == 0 && key->anchor.len == 0 && key->tag.len == 0)
        return true;
    return false;
}

//! Dump block mapping
static bool dump_block_map(emitter_t* e, const cyaml_node_t* n, int depth)
{
    for (uint32_t i = 0; i < n->map.count; i++) {
        bool has_trailing_nl = (EMIT_LAST(e) == C_LF && EMIT_PREV(e) == C_LF);
        if (!has_trailing_nl && (i > 0 || depth > 0))
            EMIT(e, C_LF);
        INDENT(e, depth);

        cyaml_node_t* key = n->map.pairs[i].key;
        cyaml_node_t* val = n->map.pairs[i].val;

        if (is_empty_key(key)) {
            EMIT(e, ':');
        } else {
            bool explicit_key = key_needs_explicit(e, key);

            if (explicit_key) {
                EMIT_S(e, "? ");
                if (key->type == CYAML_MAP && key->map.count > 0) {
                    cyaml_node_t* k0 = key->map.pairs[0].key;
                    cyaml_node_t* v0 = key->map.pairs[0].val;
                    if (!is_empty_key(k0)) {
                        DNODE(e, k0, depth);
                    }
                    EMIT_S(e, ": ");
                    DNODE(e, v0, depth);
                    for (uint32_t j = 1; j < key->map.count; j++) {
                        EMIT(e, C_LF);
                        INDENT(e, depth + 1);
                        cyaml_node_t* kj = key->map.pairs[j].key;
                        if (!is_empty_key(kj)) {
                            DNODE(e, kj, depth);
                        }
                        EMIT_S(e, ": ");
                        DNODE(e, key->map.pairs[j].val, depth);
                    }
                } else if (key->type == CYAML_SEQ && key->seq.count > 0) {
                    bool key_has_props = key->anchor.len > 0 || key->tag.len > 0;
                    if (key_has_props) {
                        DUMP_PROPS(e, key);
                        EMIT_TRIM_SPACE(e);
                        for (uint32_t j = 0; j < key->seq.count; j++) {
                            EMIT(e, C_LF);
                            INDENT(e, depth);
                            EMIT_S(e, "- ");
                            DNODE(e, key->seq.items[j], depth);
                        }
                    } else {
                        EMIT_S(e, "- ");
                        DNODE(e, key->seq.items[0], depth);
                        for (uint32_t j = 1; j < key->seq.count; j++) {
                            EMIT(e, C_LF);
                            INDENT(e, depth + 1);
                            EMIT_S(e, "- ");
                            DNODE(e, key->seq.items[j], depth);
                        }
                    }
                } else {
                    int key_depth = (key->type == CYAML_SCALAR && (key->style == CYAML_LITERAL || key->style == CYAML_FOLDED))
                        ? depth + 1
                        : depth;
                    DNODE(e, key, key_depth);
                }
                EMIT(e, C_LF);
                INDENT(e, depth);
                if (val && val->type == CYAML_MAP && val->map.count > 0 && !is_empty_value(val)) {
                    EMIT_S(e, ": ");
                    cyaml_node_t* vk0 = val->map.pairs[0].key;
                    cyaml_node_t* vv0 = val->map.pairs[0].val;
                    DNODE(e, vk0, depth);
                    EMIT_S(e, ": ");
                    DNODE(e, vv0, depth);
                    for (uint32_t j = 1; j < val->map.count; j++) {
                        EMIT(e, C_LF);
                        INDENT(e, depth + 1);
                        DNODE(e, val->map.pairs[j].key, depth);
                        EMIT_S(e, ": ");
                        DNODE(e, val->map.pairs[j].val, depth);
                    }
                    continue;
                }
                if (val && val->type == CYAML_SEQ && val->seq.count > 0 && !is_empty_value(val)) {
                    EMIT_S(e, ": - ");
                    DNODE(e, val->seq.items[0], depth);
                    for (uint32_t j = 1; j < val->seq.count; j++) {
                        EMIT(e, C_LF);
                        INDENT(e, depth + 1);
                        EMIT_S(e, "- ");
                        DNODE(e, val->seq.items[j], depth);
                    }
                    continue;
                }
                EMIT_S(e, ":");
            } else {
                DNODE(e, key, depth);
                if (key_needs_space(key)) {
                    EMIT_S(e, " :");
                } else {
                    EMIT(e, ':');
                }
            }
        }

        bool val_has_props = val && (val->anchor.len > 0 || val->tag.len > 0);
        bool explicit_key = !is_empty_key(key) && key_needs_explicit(e, key);
        bool from_flow = (n->style == (cyaml_style_t)CYAML_FLOW);
        if (is_empty_value(val)) {
            if (val_has_props) {
                EMIT(e, C_SP);
                DUMP_PROPS(e, val);
                EMIT_TRIM_SPACE(e);
            } else if (!explicit_key && from_flow) {
                // Implicit null from flow mapping (explicit empty stays empty)
                if (val && val->type == CYAML_NULL)
                    EMIT_S(e, " null");
            } else if (val && val->type == CYAML_SCALAR && (val->style == CYAML_LITERAL || val->style == CYAML_FOLDED) && val->chomp != CYAML_KEEP) {
                EMIT_S(e, " \"\"");
            }
        } else if (val->type == CYAML_SEQ) {
            if (val->style == (cyaml_style_t)CYAML_FLOW && val->seq.count == 0) {
                EMIT(e, C_SP);
                DNODE(e, val, depth + 1);
            } else if (val_has_props) {
                EMIT(e, C_SP);
                DUMP_PROPS(e, val);
                EMIT_TRIM_SPACE(e);
                if (depth == 0)
                    EMIT(e, C_LF);
                if (!dump_block_seq(e, val, depth))
                    return false;
            } else {
                if (depth == 0)
                    EMIT(e, C_LF);
                if (!dump_block_seq(e, val, depth))
                    return false;
            }
        } else if (val->type == CYAML_MAP) {
            if (val->style == (cyaml_style_t)CYAML_FLOW && val->map.count == 0) {
                EMIT(e, C_SP);
                DNODE(e, val, depth + 1);
            } else if (val_has_props) {
                EMIT(e, C_SP);
                DUMP_PROPS(e, val);
                EMIT_TRIM_SPACE(e);
                if (!dump_block_map(e, val, depth + 1))
                    return false;
            } else {
                DNODE(e, val, depth + 1);
            }
        } else {
            EMIT(e, C_SP);
            DNODE(e, val, depth + 1);
        }
    }
    return true;
}

//! Dump any node
static bool dump_node(emitter_t* e, const cyaml_node_t* n, int depth)
{
    if (!n || n->type == CYAML_NONE) {
        EMIT_S(e, S_NULL);
        return true;
    }

    switch (n->type) {
    case CYAML_NULL:
        if (!dump_anchor(e, n))
            return false;
        if (n->tag.len > 0)
            return emit_resolved_tag_raw(e, n);
        if (n->anchor.len > 0) {
            EMIT_TRIM_SPACE(e);
            return true;
        }
        if (!dump_tag(e, n))
            return false;
        EMIT_S(e, S_NULL);
        return true;

    case CYAML_SCALAR:
        if (!dump_anchor(e, n))
            return false;
        if (n->tag.len > 0 && n->span.len == 0)
            return emit_resolved_tag_raw(e, n);
        if (!dump_tag(e, n))
            return false;
        return dump_scalar(e, n, depth);

    case CYAML_SEQ:
        if (n->seq.count == 0) {
            DUMP_PROPS(e, n);
            EMIT_S(e, "[]");
            return true;
        }
        if (n->anchor.len > 0 || n->tag.len > 0) {
            DUMP_PROPS(e, n);
            EMIT_TRIM_SPACE(e);
            EMIT(e, C_LF);
        }
        return dump_block_seq(e, n, depth);

    case CYAML_MAP:
        if (n->map.count == 0) {
            DUMP_PROPS(e, n);
            EMIT_S(e, "{}");
            return true;
        }
        if (n->anchor.len > 0 || n->tag.len > 0) {
            DUMP_PROPS(e, n);
            EMIT_TRIM_SPACE(e);
            EMIT(e, C_LF);
        }
        return dump_block_map(e, n, depth);

    case CYAML_ALIAS:
        EMIT(e, '*');
        return emit_str(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len);

    default:
        CYAML_UNREACHABLE("invalid node type");
    }
}

//! Dump single document
//! For single-doc mode: stream=NULL. For stream mode: pass stream and index.
static bool dump_document(emitter_t* e, const cyaml_doc_t* doc,
    const cyaml_stream_t* stream, uint32_t index)
{
    const cyaml_doc_t* saved = e->doc;
    e->doc = doc;

    // Derive iteration context from stream position
    bool is_last = !stream || index == stream->count - 1;
    bool prev_had_content = stream && index > 0 && stream->docs[index - 1]->root && (stream->docs[index - 1]->root->type == CYAML_SEQ || stream->docs[index - 1]->root->type == CYAML_MAP);

    // Check document type
    bool is_collection = doc->root && (doc->root->type == CYAML_SEQ || doc->root->type == CYAML_MAP);
    bool is_empty = !doc->root || doc->root->type == CYAML_NULL || doc->root->type == CYAML_NONE;
    bool is_scalar = doc->root && doc->root->type == CYAML_SCALAR;

    // Emit --- for: collections, empty documents, and scalars with explicit doc start
    bool root_has_props = doc->root && (doc->root->anchor.len > 0 || doc->root->tag.len > 0);

    // Check if map would start with ambiguous content (needs --- for clarity)
    // Only for plain scalar keys starting with ? when converting from flow to block
    bool needs_doc_marker = false;
    if (doc->root && doc->root->type == CYAML_MAP && doc->root->map.count > 0 && doc->root->style == (cyaml_style_t)CYAML_FLOW) {
        cyaml_node_t* first_key = doc->root->map.pairs[0].key;
        // Plain scalar starting with ? looks like explicit key indicator
        if (first_key && first_key->type == CYAML_SCALAR && first_key->span.len > 0) {
            const char* src = cyaml_src(doc);
            if (src[first_key->span.off] == '?') {
                needs_doc_marker = true;
            }
        }
    }

    // Check if document has block scalars or flow collections that become block (need --- for clarity)
    bool has_block_scalars = false;

    // In emit mode: add --- for flow collections at root (may have unusual formatting)
    // or if document has directives (will be stripped in output)
    bool emit_needs_doc_start = false;
    if (!EMIT_IS_DUMP(e)) {
        bool has_directives = (doc->flags & CYAML_HAS_DIRECTIVE) || (doc->tag_count > 0);
        if (has_directives) {
            emit_needs_doc_start = true;
        }
        // Root-level flow collection that will become block needs ---
        if (is_collection && doc->root->style == (cyaml_style_t)CYAML_FLOW) {
            emit_needs_doc_start = true;
        }
    }

    if (EMIT_IS_DUMP(e) && is_collection) {
        // Check for block scalars or unusual flow formatting in the tree
        if (doc->root->type == CYAML_SEQ) {
            for (uint32_t i = 0; i < doc->root->seq.count && !has_block_scalars; i++) {
                cyaml_node_t* item = doc->root->seq.items[i];
                if (item && item->type == CYAML_MAP) {
                    for (uint32_t j = 0; j < item->map.count; j++) {
                        cyaml_node_t* v = item->map.pairs[j].val;
                        if (v && v->type == CYAML_SCALAR && (v->style == CYAML_LITERAL || v->style == CYAML_FOLDED)) {
                            has_block_scalars = true;
                            break;
                        }
                    }
                }
            }
        } else if (doc->root->type == CYAML_MAP && doc->root->style != (cyaml_style_t)CYAML_FLOW) {
            // Block map: check for flow collections with unusual multi-line formatting
            // (more lines than items indicates weird formatting like VJP3)
            for (uint32_t i = 0; i < doc->root->map.count && !has_block_scalars; i++) {
                cyaml_node_t* v = doc->root->map.pairs[i].val;
                if (v && v->style == (cyaml_style_t)CYAML_FLOW) {
                    uint32_t lines = span_line_count(v->span);
                    uint32_t items = (v->type == CYAML_MAP) ? v->map.count : (v->type == CYAML_SEQ) ? v->seq.count
                                                                                                    : 0;
                    // If lines > items + 1, the flow collection has unusual formatting
                    // (allows for closing bracket on separate line)
                    if (lines > items + 1) {
                        has_block_scalars = true;
                    }
                }
            }
        }
    }

    if ((doc->flags & CYAML_DOC_START) || needs_doc_marker || has_block_scalars || emit_needs_doc_start) {
        if (is_collection || is_empty) {
            if (!emit_cstr(e, "---")) {
                e->doc = saved;
                return false;
            }
            // For empty document at end of stream after content, add newline and ... to terminate
            // But only if DOC_END flag is not set (DOC_END will emit ... itself)
            if (is_empty && is_last && prev_had_content && !(doc->flags & CYAML_DOC_END)) {
                if (!emit_char(e, C_LF)) {
                    e->doc = saved;
                    return false;
                }
                if (!emit_cstr(e, "...")) {
                    e->doc = saved;
                    return false;
                }
            } else if (is_collection && root_has_props) {
                // Collection with anchor/tag: emit space, anchor/tag goes on same line
                if (!emit_char(e, C_SP)) {
                    e->doc = saved;
                    return false;
                }
            } else if (!is_empty || !is_last) {
                // Collections and non-final empty docs get newline
                if (!emit_char(e, C_LF)) {
                    e->doc = saved;
                    return false;
                }
            }
        } else if (is_scalar) {
            // For single-doc scalar without properties/directives:
            // - Block scalars (| or >) always need ---
            // - If scalar needs quoting, skip --- (quoted scalar is unambiguous)
            // - If scalar is plain, emit --- (plain scalar could be confused with directives)
            bool has_directives = (doc->flags & CYAML_HAS_DIRECTIVE) || (doc->tag_count > 0);
            bool needs_doc_start = root_has_props || has_directives || prev_had_content || !is_last;

            // Check if the scalar will be emitted as a block scalar
            // (Block scalars can get converted to double-quoted if they have trailing whitespace)
            char* str = cyaml_scalar_str(doc, doc->root);
            if (str) {
                size_t len = strlen(str);
                cyaml_style_t orig_style = doc->root->style;
                bool will_be_block = (orig_style == CYAML_LITERAL || orig_style == CYAML_FOLDED) && len > 0 && !has_trailing_whitespace(str, len) && str[0] != C_TAB;

                if (will_be_block) {
                    // Block scalars are unambiguous (start with | or >), only need ---
                    // if: zero indent, DOC_END set, or fewer than 2 trailing breaks
                    if (doc->root->indent == 0 || (doc->flags & CYAML_DOC_END) || doc->root->trailing_breaks < 2) {
                        needs_doc_start = true;
                    }
                } else if (!needs_doc_start) {
                    // Check if scalar will be plain (needs ---) or quoted (no --- needed)
                    if (!needs_quoting(str, len)) {
                        // Plain scalar needs --- to avoid ambiguity
                        needs_doc_start = true;
                    }
                }
                free(str);
            }
            if (needs_doc_start) {
                if (!emit_cstr(e, "--- ")) {
                    e->doc = saved;
                    return false;
                }
            }
        }
    }

    if (doc->root && doc->root->type != CYAML_NULL && doc->root->type != CYAML_NONE) {
        if (!dump_node(e, doc->root, 0)) {
            e->doc = saved;
            return false;
        }
    }

    // Emit ... for documents with explicit end marker
    if (doc->flags & CYAML_DOC_END) {
        // Ensure newline before ...
        if (e->len == 0 || e->buf[e->len - 1] != C_LF) {
            if (!emit_char(e, C_LF)) {
                e->doc = saved;
                return false;
            }
        }
        if (!emit_cstr(e, "...\n")) {
            e->doc = saved;
            return false;
        }
    }

    // Documents ending with 2+ trailing newlines (from keep block scalars) need ...
    // to mark the end unambiguously
    if (!(doc->flags & CYAML_DOC_END) && e->len >= 2 && e->buf[e->len - 1] == C_LF && e->buf[e->len - 2] == C_LF) {
        if (!emit_cstr(e, "...\n")) {
            e->doc = saved;
            return false;
        }
    }

    e->doc = saved;
    return true;
}

// #endregion

// #region Public API

CYAML_API char* cyaml_emit(const cyaml_doc_t* doc, const cyaml_emit_opts_t* opts, size_t* len)
{
    if (!doc)
        return NULL;
    static const cyaml_emit_opts_t default_opts = { 2, 80, false, false, false, CYAML_PLAIN, CYAML_BLOCK };

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = opts ? *opts : default_opts, .doc = doc, .flags = 0, .comment_idx = 0, .last_line = 0
    };

    if (e.opts.comments && doc->root)
        emit_comments_before_line(&e, doc->root->span.start_line, 0);
    if (e.opts.doc_start)
        emit_cstr(&e, "---\n");
    if (doc->root)
        emit_node(&e, doc->root, 0);
    if (e.opts.comments)
        emit_remaining_comments(&e, 0);
    emit_char(&e, C_LF);
    if (e.opts.doc_end)
        emit_cstr(&e, "...\n");

    emit_char(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

CYAML_API char* cyaml_emit_node(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const cyaml_emit_opts_t* opts, size_t* len)
{
    if (!doc || !node)
        return NULL;
    static const cyaml_emit_opts_t default_opts = { 2, 80, false, false, false, CYAML_PLAIN, CYAML_BLOCK };

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = opts ? *opts : default_opts, .doc = doc, .flags = 0, .comment_idx = 0, .last_line = 0
    };

    emit_node(&e, node, 0);
    emit_char(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

CYAML_API char* cyaml_dump(const cyaml_doc_t* doc, size_t* len)
{
    if (!doc)
        return NULL;
    static const cyaml_emit_opts_t dump_opts = { 2, 0, false, false, false, CYAML_PLAIN, CYAML_BLOCK };

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = dump_opts, .doc = doc, .flags = EMIT_DUMP, .comment_idx = 0, .last_line = 0
    };

    TRY_OR(&e, dump_document(&e, doc, NULL, 0));
    EMIT_OR(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

CYAML_API char* cyaml_stream_dump(const cyaml_stream_t* stream, size_t* len)
{
    if (!stream)
        return NULL;
    static const cyaml_emit_opts_t dump_opts = { 2, 0, false, false, false, CYAML_PLAIN, CYAML_BLOCK };

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = dump_opts, .doc = NULL, .flags = EMIT_DUMP, .comment_idx = 0, .last_line = 0
    };

    for (uint32_t i = 0; i < stream->count; i++) {
        bool is_last = (i == stream->count - 1);
        TRY_OR(&e, dump_document(&e, stream->docs[i], stream, i));
        // Ensure newline between documents in multi-doc streams
        const cyaml_doc_t* doc = stream->docs[i];
        if (!is_last && NEEDS_NL(&e))
            EMIT_OR(&e, C_LF);
        // For last document: add \n... if plain scalar could be ambiguous
        // Cases: has anchor, or content has % at word boundary (looks like directive)
        if (is_last && doc->root && doc->root->type == CYAML_SCALAR && doc->root->style == CYAML_PLAIN && (doc->flags & CYAML_DOC_START) && !(doc->flags & CYAML_DOC_END) && doc->root->tag.len == 0) {
            bool needs_end = false;
            // Scalars with anchors need termination
            if (doc->root->anchor.len > 0)
                needs_end = true;
            // Tab separator after --- needs termination (K54U)
            if (doc->flags & CYAML_DOC_TAB_SEP)
                needs_end = true;
            // Check for directive-like content
            if (!needs_end) {
                char* content = cyaml_scalar_str((cyaml_doc_t*)doc, doc->root);
                if (content) {
                    for (const char* p = content; *p; p++) {
                        if (*p == '%' && (p == content || *(p - 1) == C_SP || *(p - 1) == C_LF)) {
                            needs_end = true;
                            break;
                        }
                    }
                    free(content);
                }
            }
            if (needs_end) {
                if (NEEDS_NL(&e))
                    EMIT_OR(&e, C_LF);
                EMIT_S_OR(&e, "...\n");
            }
        }
    }

    // Ensure trailing newline (yaml-test-suite out.yaml format)
    if (NEEDS_NL(&e))
        EMIT_OR(&e, C_LF);

    EMIT_OR(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

CYAML_API char* cyaml_stream_emit(const cyaml_stream_t* stream, size_t* len)
{
    if (!stream)
        return NULL;
    static const cyaml_emit_opts_t emit_opts = { 2, 80, false, false, false, CYAML_PLAIN, CYAML_BLOCK };

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = emit_opts, .doc = NULL, .flags = 0, .comment_idx = 0, .last_line = 0
    };

    for (uint32_t i = 0; i < stream->count; i++) {
        const cyaml_doc_t* doc = stream->docs[i];
        e.doc = doc;

        // Emit %YAML directive if original had one
        // Skip if there's unusual whitespace (tabs) between directive and content
        bool skip_directive = false;
        if ((doc->flags & CYAML_HAS_DIRECTIVE) && doc->version.major > 0) {
            const char* src = cyaml_src(doc);
            uint32_t src_len = cyaml_src_len(doc);
            if (src && src_len > 0) {
                // Check for tabs in source before document content
                for (uint32_t j = 0; j < src_len; j++) {
                    if (src[j] == C_TAB) {
                        skip_directive = true;
                        break;
                    }
                    if (src[j] == '-' && j + 2 < src_len && src[j + 1] == '-' && src[j + 2] == '-') {
                        break; // Reached ---, stop checking
                    }
                }
            }
            if (!skip_directive) {
                char ver[32];
                snprintf(ver, sizeof(ver), "%%YAML %u.%u\n", doc->version.major, doc->version.minor);
                EMIT_S_OR(&e, ver);
            }
        }

        // Determine if we need document start marker
        bool needs_doc_start = (doc->flags & CYAML_DOC_START) != 0;

        // Add --- for collections that have formatting that will be normalized:
        // 1. Flow collections at root with leading whitespace
        // 2. Any document with tabs (tabs get normalized to spaces)
        if (!needs_doc_start && doc->root) {
            bool is_flow_coll = (doc->root->type == CYAML_SEQ || doc->root->type == CYAML_MAP) && doc->root->style == (cyaml_style_t)CYAML_FLOW;
            // Check if root starts at non-zero column (has leading whitespace)
            if (is_flow_coll && doc->root->span.start_col > 1) {
                needs_doc_start = true;
            }

            // Check for tabs in structural positions (will be normalized)
            // For collections, check if source has tabs in line-leading positions
            // but not inside quoted strings or block scalars (where tabs are content)
            bool is_collection = doc->root->type == CYAML_SEQ || doc->root->type == CYAML_MAP;
            if (!needs_doc_start && is_collection) {
                const char* src_ptr = cyaml_src(doc);
                uint32_t src_len = cyaml_src_len(doc);
                if (src_ptr && src_len > 0) {
                    bool in_leading_ws = true;
                    bool in_block_scalar = false;
                    bool in_quoted = false;
                    char quote_char = 0;
                    uint32_t block_indent = 0;
                    uint32_t cur_indent = 0;

                    for (uint32_t k = 0; k < src_len && !needs_doc_start; k++) {
                        char c = src_ptr[k];

                        // Handle quoted strings - track open/close
                        if (!in_block_scalar) {
                            if (!in_quoted && (c == '"' || c == '\'')) {
                                in_quoted = true;
                                quote_char = c;
                                in_leading_ws = false;
                                continue;
                            } else if (in_quoted && c == quote_char) {
                                // Check for escape (only in double-quoted)
                                if (quote_char == '"' && k > 0 && src_ptr[k - 1] == C_BSLASH) {
                                    // Escaped quote, still in string
                                    continue;
                                }
                                in_quoted = false;
                                continue;
                            }
                        }

                        if (in_quoted) {
                            // Inside quoted string - tabs are content, not structure
                            if (c == C_LF) {
                                in_leading_ws = true;
                                cur_indent = 0;
                            } else {
                                in_leading_ws = false;
                            }
                            continue;
                        }

                        if (c == C_LF) {
                            in_leading_ws = true;
                            cur_indent = 0;
                            continue;
                        }

                        if (in_leading_ws) {
                            if (c == C_SP) {
                                cur_indent++;
                            } else if (c == C_TAB) {
                                // Tab in leading whitespace (structural position)
                                if (!in_block_scalar || cur_indent < block_indent) {
                                    needs_doc_start = true;
                                }
                                cur_indent++;
                            } else {
                                if (in_block_scalar && cur_indent < block_indent) {
                                    in_block_scalar = false;
                                }
                                in_leading_ws = false;
                            }
                        } else {
                            // Check for block scalar indicators
                            if ((c == '|' || c == '>') && k + 1 < src_len) {
                                for (uint32_t j = k + 1; j < src_len; j++) {
                                    if (src_ptr[j] == C_LF) {
                                        in_block_scalar = true;
                                        block_indent = 0;
                                        for (uint32_t m = j + 1; m < src_len; m++) {
                                            if (src_ptr[m] == C_SP)
                                                block_indent++;
                                            else if (src_ptr[m] == C_LF) {
                                                block_indent = 0;
                                            } else
                                                break;
                                        }
                                        break;
                                    } else if (src_ptr[j] != C_SP && src_ptr[j] != '+' && src_ptr[j] != '-' && (src_ptr[j] < '0' || src_ptr[j] > '9')) {
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Check for block scalars with trailing whitespace that will be converted
            if (!needs_doc_start) {
                needs_doc_start = block_scalar_needs_conversion(doc, doc->root);
            }

            // Check for tabs in folding position that get normalized (only in collections)
            // Skip if there are block scalars with tabs - those preserve tabs, so the
            // document already has visible tabs and no --- is needed for clarity.
            if (!needs_doc_start && is_collection && !has_block_scalar_tabs(doc, doc->root)) {
                needs_doc_start = has_folding_tabs_in_collection(doc, doc->root);
            }
        }

        // Add --- if document has directives (will be stripped)
        if (!needs_doc_start && (doc->flags & CYAML_HAS_DIRECTIVE)) {
            needs_doc_start = true;
        }

        // Emit document start marker
        if (needs_doc_start) {
            EMIT_S_OR(&e, "---");
            if (doc->root && doc->root->type != CYAML_NULL && doc->root->type != CYAML_NONE) {
                // Scalars get space; collections get newline
                // Exception: empty flow collections stay as flow and get space
                bool use_space = (doc->root->type == CYAML_SCALAR);
                if (!use_space && doc->root->style == (cyaml_style_t)CYAML_FLOW) {
                    // Empty flow collections stay as flow
                    uint32_t count = (doc->root->type == CYAML_SEQ) ? doc->root->seq.count : (doc->root->type == CYAML_MAP) ? doc->root->map.count
                                                                                                                            : 0;
                    if (count == 0)
                        use_space = true;
                }
                EMIT_OR(&e, use_space ? C_SP : C_LF);
            } else if (skip_directive) {
                EMIT_S_OR(&e, " null\n");
            } else {
                EMIT_OR(&e, C_LF);
            }
        }

        // Emit document content (skip empty/null roots in emit mode)
        if (doc->root && doc->root->type != CYAML_NULL && doc->root->type != CYAML_NONE) {
            TRY_OR(&e, emit_node(&e, doc->root, 0));
        }

        // Ensure trailing newline
        if (NEEDS_NL(&e))
            EMIT_OR(&e, C_LF);

        // Emit document end marker if needed
        if (doc->flags & CYAML_DOC_END)
            EMIT_S_OR(&e, "...\n");
    }

    // Ensure trailing newline (yaml-test-suite emit.yaml format)
    if (NEEDS_NL(&e))
        EMIT_OR(&e, C_LF);

    EMIT_OR(&e, C_NUL);
    e.len--;

    if (len)
        *len = e.len;
    return e.buf;
}

// #endregion
