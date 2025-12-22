#include "cyaml_internal.h"

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
    if (!e->opts.preserve_comments || !e->doc || !e->doc->comments)
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
    if (!e->opts.preserve_comments || !e->doc || !e->doc->comments)
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
    if (!e->opts.preserve_comments || !e->doc || !e->doc->comments)
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

#define EMIT_STACK_INIT_CAP 32

typedef enum {
    EMITF_VALUE,
    EMITF_FLOW_SEQ,
    EMITF_FLOW_SEQ_CLOSE,
    EMITF_FLOW_MAP,
    EMITF_FLOW_MAP_VAL,
    EMITF_FLOW_MAP_CLOSE,
    EMITF_BLOCK_SEQ,
    EMITF_BLOCK_SEQ_COMMENT,
    EMITF_BLOCK_MAP,
    EMITF_BLOCK_MAP_KEY,
    EMITF_BLOCK_MAP_VAL,
    EMITF_BLOCK_MAP_COMMENT,
    EMITF_COMPACT_SEQ
} emit_frame_state_t;

typedef struct {
    const cyaml_node_t* node;
    uint32_t child_idx;
    emit_frame_state_t state;
    int depth;
    uint8_t flags;
} emit_frame_t;

#define EMITF_SKIP_PROPS 1

#define DUMP_STACK_INIT_CAP 32

#define EMIT_PUSH(stk, cnt, cap, nd, st, dp, fl, on_fail)                 \
    do {                                                                  \
        if ((cnt) >= (cap)) {                                             \
            size_t new_cap = (cap) * 2;                                   \
            emit_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk))); \
            if (!tmp) {                                                   \
                on_fail;                                                  \
            }                                                             \
            (stk) = tmp;                                                  \
            (cap) = new_cap;                                              \
        }                                                                 \
        (stk)[(cnt)] = (emit_frame_t) { (nd), 0, (st), (dp), (fl) };      \
        (cnt)++;                                                          \
    } while (0)

#define EM_PUSH(nd, st, dp, fl) \
    EMIT_PUSH(stack, stack_count, stack_cap, nd, st, dp, fl, { result = false; goto cleanup; })

#define EM_FAIL()       \
    do {                \
        result = false; \
        goto cleanup;   \
    } while (0)

static bool emit_node(emitter_t* e, const cyaml_node_t* root, int start_depth)
{
    if (!root || root->type == CYAML_NONE) {
        EMIT_S(e, S_NULL);
        return true;
    }

    emit_frame_t* stack = malloc(EMIT_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        return false;
    size_t stack_count = 0;
    size_t stack_cap = EMIT_STACK_INIT_CAP;
    bool result = true;

    EM_PUSH(root, EMITF_VALUE, start_depth, 0);

    while (stack_count > 0) {
        emit_frame_t* f = &stack[stack_count - 1];
        const cyaml_node_t* n = f->node;

        switch (f->state) {
        case EMITF_VALUE: {
            stack_count--;
            if (!n || n->type == CYAML_NONE) {
                if (!emit_cstr(e, S_NULL))
                    EM_FAIL();
                break;
            }
            bool skip_props = (f->flags & EMITF_SKIP_PROPS);
            switch (n->type) {
            case CYAML_NULL:
                if (!skip_props) {
                    if (!emit_anchor(e, n))
                        EM_FAIL();
                    if (!emit_tag(e, n))
                        EM_FAIL();
                }
                if (!emit_cstr(e, S_NULL))
                    EM_FAIL();
                break;
            case CYAML_SCALAR:
                if (!skip_props) {
                    if (!emit_anchor(e, n))
                        EM_FAIL();
                    if (!emit_tag(e, n))
                        EM_FAIL();
                }
                if (!emit_scalar(e, n, f->depth))
                    EM_FAIL();
                break;
            case CYAML_SEQ: {
                if (!skip_props) {
                    if (!emit_anchor(e, n))
                        EM_FAIL();
                    if (!emit_tag(e, n))
                        EM_FAIL();
                }
                bool use_flow = (n->style == (cyaml_style_t)CYAML_FLOW || e->opts.coll == CYAML_FLOW);
                bool convert_to_block = (!e->opts.preserve_style && !EMIT_IS_DUMP(e) && n->style == (cyaml_style_t)CYAML_FLOW && n->seq.count > 0);
                if (convert_to_block || !use_flow) {
                    EM_PUSH(n, EMITF_BLOCK_SEQ, f->depth, 0);
                } else {
                    if (!emit_char(e, '['))
                        EM_FAIL();
                    if (n->seq.count > 0) {
                        EM_PUSH(n, EMITF_FLOW_SEQ, f->depth, 0);
                    } else {
                        if (!emit_char(e, ']'))
                            EM_FAIL();
                    }
                }
                break;
            }
            case CYAML_MAP: {
                if (!skip_props) {
                    if (!emit_anchor(e, n))
                        EM_FAIL();
                    if (!emit_tag(e, n))
                        EM_FAIL();
                }
                bool use_flow = (n->style == (cyaml_style_t)CYAML_FLOW || e->opts.coll == CYAML_FLOW);
                bool convert_to_block = (!e->opts.preserve_style && !EMIT_IS_DUMP(e) && n->style == (cyaml_style_t)CYAML_FLOW && n->map.count > 0);
                if (convert_to_block || !use_flow) {
                    EM_PUSH(n, EMITF_BLOCK_MAP, f->depth, 0);
                } else {
                    if (!emit_char(e, '{'))
                        EM_FAIL();
                    if (n->map.count > 0) {
                        EM_PUSH(n, EMITF_FLOW_MAP, f->depth, 0);
                    } else {
                        if (!emit_char(e, '}'))
                            EM_FAIL();
                    }
                }
                break;
            }
            case CYAML_ALIAS:
                if (!emit_char(e, '*'))
                    EM_FAIL();
                if (!emit_str(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len))
                    EM_FAIL();
                break;
            default:
                break;
            }
            break;
        }

        case EMITF_FLOW_SEQ:
            if (f->child_idx >= n->seq.count) {
                f->state = EMITF_FLOW_SEQ_CLOSE;
            } else {
                if (f->child_idx > 0)
                    if (!emit_cstr(e, ", "))
                        EM_FAIL();
                uint32_t idx = f->child_idx++;
                EM_PUSH(n->seq.items[idx], EMITF_VALUE, f->depth, 0);
            }
            break;

        case EMITF_FLOW_SEQ_CLOSE:
            if (!emit_char(e, ']'))
                EM_FAIL();
            stack_count--;
            break;

        case EMITF_FLOW_MAP:
            if (f->child_idx >= n->map.count) {
                f->state = EMITF_FLOW_MAP_CLOSE;
            } else {
                if (f->child_idx > 0)
                    if (!emit_cstr(e, ", "))
                        EM_FAIL();
                f->state = EMITF_FLOW_MAP_VAL;
                EM_PUSH(n->map.pairs[f->child_idx].key, EMITF_VALUE, f->depth, 0);
            }
            break;

        case EMITF_FLOW_MAP_VAL:
            if (!emit_cstr(e, ": "))
                EM_FAIL();
            f->state = EMITF_FLOW_MAP;
            EM_PUSH(n->map.pairs[f->child_idx++].val, EMITF_VALUE, f->depth, 0);
            break;

        case EMITF_FLOW_MAP_CLOSE:
            if (!emit_char(e, '}'))
                EM_FAIL();
            stack_count--;
            break;

        case EMITF_BLOCK_SEQ: {
            if (f->child_idx >= n->seq.count) {
                stack_count--;
                break;
            }
            cyaml_node_t* item = n->seq.items[f->child_idx];
            if (item && e->opts.preserve_comments)
                if (!emit_comments_before_line(e, item->span.start_line, f->depth))
                    EM_FAIL();
            bool already_newline = (EMIT_LAST(e) == C_LF);
            if (f->child_idx > 0 || (f->depth > 0 && !already_newline))
                if (!emit_char(e, C_LF))
                    EM_FAIL();
            if (!emit_indent(e, f->depth))
                EM_FAIL();
            if (!emit_cstr(e, "- "))
                EM_FAIL();
            f->state = EMITF_BLOCK_SEQ_COMMENT;
            EM_PUSH(item, EMITF_VALUE, f->depth + 1, 0);
            break;
        }

        case EMITF_BLOCK_SEQ_COMMENT: {
            cyaml_node_t* item = n->seq.items[f->child_idx++];
            if (item && e->opts.preserve_comments && item->type == CYAML_SCALAR)
                if (!emit_inline_comment(e, item->span.end_line))
                    EM_FAIL();
            f->state = EMITF_BLOCK_SEQ;
            break;
        }

        case EMITF_BLOCK_MAP: {
            if (f->child_idx >= n->map.count) {
                stack_count--;
                break;
            }
            const cyaml_node_t* key = n->map.pairs[f->child_idx].key;
            if (key && e->opts.preserve_comments)
                if (!emit_comments_before_line(e, key->span.start_line, f->depth))
                    EM_FAIL();
            bool just_after_dash = (EMIT_PREV(e) == '-' && EMIT_LAST(e) == C_SP);
            bool already_newline = (EMIT_LAST(e) == C_LF);
            if (f->child_idx > 0 || (f->depth > 0 && !just_after_dash && !already_newline))
                if (!emit_char(e, C_LF))
                    EM_FAIL();
            if (f->child_idx > 0 || (!just_after_dash && (f->depth > 0 || already_newline)))
                if (!emit_indent(e, f->depth))
                    EM_FAIL();
            bool key_is_empty = !key || key->type == CYAML_NULL || key->type == CYAML_NONE;
            bool key_is_complex = key && (key->type == CYAML_MAP || key->type == CYAML_SEQ);
            if (key_is_complex) {
                if (!emit_cstr(e, "? "))
                    EM_FAIL();
                if (key->type == CYAML_SEQ) {
                    f->state = EMITF_BLOCK_MAP_KEY;
                    EM_PUSH(key, EMITF_COMPACT_SEQ, f->depth + 1, 0);
                } else {
                    f->state = EMITF_BLOCK_MAP_KEY;
                    EM_PUSH(key, EMITF_VALUE, f->depth, 0);
                }
            } else {
                if (key_is_empty) {
                    f->state = EMITF_BLOCK_MAP_VAL;
                } else {
                    f->state = EMITF_BLOCK_MAP_VAL;
                    EM_PUSH(key, EMITF_VALUE, f->depth, 0);
                }
            }
            break;
        }

        case EMITF_BLOCK_MAP_KEY: {
            if (!emit_char(e, C_LF))
                EM_FAIL();
            if (!emit_indent(e, f->depth))
                EM_FAIL();
            f->state = EMITF_BLOCK_MAP_VAL;
            break;
        }

        case EMITF_BLOCK_MAP_VAL: {
            const cyaml_node_t* key = n->map.pairs[f->child_idx].key;
            const cyaml_node_t* val = n->map.pairs[f->child_idx].val;
            bool key_is_complex = key && (key->type == CYAML_MAP || key->type == CYAML_SEQ);
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
                } else if (!e->opts.preserve_style && !EMIT_IS_DUMP(e)) {
                    uint32_t count = val->type == CYAML_MAP ? val->map.count : val->seq.count;
                    if (count > 0)
                        val_is_block = true;
                }
            }
            if (val_is_empty) {
                if (!emit_char(e, ':'))
                    EM_FAIL();
                f->child_idx++;
                f->state = EMITF_BLOCK_MAP;
            } else if (key_is_complex && val->type == CYAML_SEQ) {
                if (!emit_cstr(e, ": "))
                    EM_FAIL();
                f->state = EMITF_BLOCK_MAP_COMMENT;
                EM_PUSH(val, EMITF_COMPACT_SEQ, f->depth + 1, 0);
            } else if (val_is_block) {
                bool val_has_props = val->anchor.len > 0 || val->tag.len > 0;
                int val_depth = (val->type == CYAML_SEQ) ? f->depth : f->depth + 1;
                if (val_has_props) {
                    if (!emit_cstr(e, ": "))
                        EM_FAIL();
                    if (!emit_anchor(e, val))
                        EM_FAIL();
                    if (!emit_tag(e, val))
                        EM_FAIL();
                    if (!emit_char(e, C_LF))
                        EM_FAIL();
                    f->state = EMITF_BLOCK_MAP_COMMENT;
                    if (val->type == CYAML_SEQ) {
                        EM_PUSH(val, EMITF_BLOCK_SEQ, val_depth, EMITF_SKIP_PROPS);
                    } else {
                        EM_PUSH(val, EMITF_BLOCK_MAP, val_depth, EMITF_SKIP_PROPS);
                    }
                } else {
                    if (!emit_cstr(e, ":\n"))
                        EM_FAIL();
                    f->state = EMITF_BLOCK_MAP_COMMENT;
                    EM_PUSH(val, EMITF_VALUE, val_depth, 0);
                }
            } else {
                if (!emit_cstr(e, ": "))
                    EM_FAIL();
                f->state = EMITF_BLOCK_MAP_COMMENT;
                EM_PUSH(val, EMITF_VALUE, f->depth + 1, 0);
            }
            break;
        }

        case EMITF_BLOCK_MAP_COMMENT: {
            const cyaml_node_t* val = n->map.pairs[f->child_idx++].val;
            if (e->opts.preserve_comments && val && val->type == CYAML_SCALAR)
                if (!emit_inline_comment(e, val->span.end_line))
                    EM_FAIL();
            f->state = EMITF_BLOCK_MAP;
            break;
        }

        case EMITF_COMPACT_SEQ: {
            if (f->child_idx >= n->seq.count) {
                stack_count--;
                break;
            }
            if (f->child_idx > 0) {
                if (!emit_char(e, C_LF))
                    EM_FAIL();
                if (!emit_indent(e, f->depth))
                    EM_FAIL();
            }
            if (!emit_cstr(e, "- "))
                EM_FAIL();
            uint32_t idx = f->child_idx++;
            EM_PUSH(n->seq.items[idx], EMITF_VALUE, f->depth + 1, 0);
            break;
        }
        }
    }

cleanup:
    free(stack);
    return result;
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

#define TREE_CHECK_STACK_CAP 64

typedef struct {
    const cyaml_node_t* node;
    uint32_t child_idx;
} tree_check_frame_t;

#define TREE_CHECK_PUSH(stk, cnt, cap, nd) \
    do {                                   \
        const cyaml_node_t* _nd = (nd);    \
        if (_nd && (cnt) < (cap)) {        \
            (stk)[(cnt)].node = _nd;       \
            (stk)[(cnt)++].child_idx = 0;  \
        }                                  \
    } while (0)

static bool has_block_scalar_tabs(const cyaml_doc_t* doc, const cyaml_node_t* root)
{
    if (!root)
        return false;

    tree_check_frame_t stack[TREE_CHECK_STACK_CAP];
    size_t sp = 0;
    const char* src = cyaml_src(doc);
    if (!src)
        return false;

    TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, root);

    while (sp > 0) {
        tree_check_frame_t* f = &stack[sp - 1];
        const cyaml_node_t* n = f->node;

        if (f->child_idx == 0) {
            if (n->type == CYAML_SCALAR && (n->style == CYAML_LITERAL || n->style == CYAML_FOLDED) && n->span.len > 0) {
                for (uint32_t i = n->span.off; i < n->span.off + n->span.len; i++) {
                    if (src[i] == C_TAB)
                        return true;
                }
            }
        }

        if (n->type == CYAML_SEQ && f->child_idx < n->seq.count) {
            TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, n->seq.items[f->child_idx++]);
        } else if (n->type == CYAML_MAP && f->child_idx < n->map.count * 2) {
            uint32_t pair_idx = f->child_idx / 2;
            cyaml_node_t* child = (f->child_idx % 2 == 0) ? n->map.pairs[pair_idx].key : n->map.pairs[pair_idx].val;
            f->child_idx++;
            TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, child);
        } else {
            sp--;
        }
    }
    return false;
}

static bool has_folding_tabs_in_collection(const cyaml_doc_t* doc, const cyaml_node_t* root)
{
    if (!root)
        return false;

    tree_check_frame_t stack[TREE_CHECK_STACK_CAP];
    size_t sp = 0;
    const char* src = cyaml_src(doc);
    if (!src)
        return false;

    TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, root);

    while (sp > 0) {
        tree_check_frame_t* f = &stack[sp - 1];
        const cyaml_node_t* n = f->node;

        if (f->child_idx == 0) {
            if (n->type == CYAML_SCALAR && n->style == CYAML_DOUBLE && n->span.len > 0) {
                bool after_newline = false;
                for (uint32_t i = n->span.off; i < n->span.off + n->span.len; i++) {
                    char c = src[i];
                    if (c == C_LF || c == C_CR) {
                        after_newline = true;
                    } else if (after_newline) {
                        if (c == C_SP)
                            continue; // still in leading whitespace
                        if (c == C_TAB)
                            return true;
                        after_newline = false;
                    }
                }
            }
        }

        if (n->type == CYAML_SEQ && f->child_idx < n->seq.count) {
            TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, n->seq.items[f->child_idx++]);
        } else if (n->type == CYAML_MAP && f->child_idx < n->map.count * 2) {
            uint32_t pair_idx = f->child_idx / 2;
            cyaml_node_t* child = (f->child_idx % 2 == 0) ? n->map.pairs[pair_idx].key : n->map.pairs[pair_idx].val;
            f->child_idx++;
            TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, child);
        } else {
            sp--;
        }
    }
    return false;
}

static bool block_scalar_needs_conversion(const cyaml_doc_t* doc, const cyaml_node_t* root)
{
    if (!root)
        return false;

    tree_check_frame_t stack[TREE_CHECK_STACK_CAP];
    size_t sp = 0;

    TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, root);

    while (sp > 0) {
        tree_check_frame_t* f = &stack[sp - 1];
        const cyaml_node_t* n = f->node;

        if (f->child_idx == 0) {
            if (n->type == CYAML_SCALAR && (n->style == CYAML_LITERAL || n->style == CYAML_FOLDED)) {
                if (n->chomp != CYAML_KEEP) {
                    char* s = cyaml_scalar_str(doc, n);
                    if (s) {
                        size_t len = strlen(s);
                        while (len > 0 && s[len - 1] == C_LF)
                            len--;
                        bool needs_conv = (len > 0 && (s[len - 1] == C_SP || s[len - 1] == C_TAB));
                        free(s);
                        if (needs_conv)
                            return true;
                    }
                }
            }
        }

        if (n->type == CYAML_SEQ && f->child_idx < n->seq.count) {
            TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, n->seq.items[f->child_idx++]);
        } else if (n->type == CYAML_MAP && f->child_idx < n->map.count * 2) {
            uint32_t pair_idx = f->child_idx / 2;
            cyaml_node_t* child = (f->child_idx % 2 == 0) ? n->map.pairs[pair_idx].key : n->map.pairs[pair_idx].val;
            f->child_idx++;
            TREE_CHECK_PUSH(stack, sp, TREE_CHECK_STACK_CAP, child);
        } else {
            sp--;
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
        style = can_be_plain_ex(s, len, 0) ? CYAML_PLAIN : can_be_single(s, len) ? CYAML_SINGLE
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
//! Skips ALIAS nodes since their anchor field is a reference, not a definition
static bool dump_anchor(emitter_t* e, const cyaml_node_t* n)
{
    if (!n || n->type == CYAML_ALIAS || n->anchor.len == 0)
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

//! Dump frame states for iterative traversal
typedef enum {
    DUMPF_VALUE,
    DUMPF_SEQ,
    DUMPF_SEQ_MAP_REST,
    DUMPF_SEQ_KEY_REST,
    DUMPF_MAP,
    DUMPF_MAP_VAL,
    DUMPF_DASH_ITEM, // Emit "- " then process item inline
    DUMPF_DASH_SEQ_MAP, // After inline key: handle rest of key seq, then colon+value, rest of map, rest of outer seq
    DUMPF_DASH_SEQ, // After inline first: handle rest of seq items
    DUMPF_DASH_MAP, // After inline map key: handle colon+value
    DUMPF_SEQ_KEYSEQ // Handle key SEQ iteration for explicit key in SEQ context (node=MAP)
} dump_state_t;

//! Stack frame for iterative dump
typedef struct {
    const cyaml_node_t* node;
    uint32_t idx;
    uint32_t sub_idx;
    uint32_t start_idx;
    dump_state_t state;
    int depth;
    int extra_depth;
    uint8_t flags;
} dump_frame_t;

#define DUMPF_SKIP_PROPS 0x01
#define DUMPF_FROM_FLOW 0x02
#define DUMPF_INLINE 0x04

#define DUMP_PUSH(stk, cnt, cap, nd, st, dp, on_fail)                                 \
    do {                                                                              \
        if ((cnt) >= (cap)) {                                                         \
            size_t new_cap = (cap) * 2;                                               \
            dump_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk)));             \
            if (!tmp) {                                                               \
                on_fail;                                                              \
            }                                                                         \
            (stk) = tmp;                                                              \
            (cap) = new_cap;                                                          \
        }                                                                             \
        (stk)[(cnt)] = (dump_frame_t) { .node = (nd), .state = (st), .depth = (dp) }; \
        (cnt)++;                                                                      \
    } while (0)

#define DM_PUSH(nd, st, dp) \
    DUMP_PUSH(stack, stack_count, stack_cap, nd, st, dp, { result = false; goto cleanup; })

#define DM_FAIL()       \
    do {                \
        result = false; \
        goto cleanup;   \
    } while (0)

static bool dump_node(emitter_t* e, const cyaml_node_t* root, int start_depth)
{
    if (!root || root->type == CYAML_NONE)
        return emit_cstr(e, S_NULL);

    dump_frame_t* stack = malloc(DUMP_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        return false;
    size_t stack_count = 0;
    size_t stack_cap = DUMP_STACK_INIT_CAP;
    bool result = true;

    DM_PUSH(root, DUMPF_VALUE, start_depth);

    while (stack_count > 0) {
        dump_frame_t* f = &stack[stack_count - 1];
        const cyaml_node_t* n = f->node;

        switch (f->state) {
        case DUMPF_VALUE: {
            stack_count--;
            if (!n || n->type == CYAML_NONE) {
                if (!emit_cstr(e, S_NULL))
                    DM_FAIL();
                break;
            }
            switch (n->type) {
            case CYAML_NULL:
                if (!dump_anchor(e, n))
                    DM_FAIL();
                if (n->tag.len > 0) {
                    if (!emit_resolved_tag_raw(e, n))
                        DM_FAIL();
                    break;
                }
                if (n->anchor.len > 0) {
                    EMIT_TRIM_SPACE(e);
                    break;
                }
                if (!dump_tag(e, n))
                    DM_FAIL();
                if (!emit_cstr(e, S_NULL))
                    DM_FAIL();
                break;
            case CYAML_SCALAR:
                if (!dump_anchor(e, n))
                    DM_FAIL();
                if (n->tag.len > 0 && n->span.len == 0) {
                    if (!emit_resolved_tag_raw(e, n))
                        DM_FAIL();
                    break;
                }
                if (!dump_tag(e, n))
                    DM_FAIL();
                if (!dump_scalar(e, n, f->depth))
                    DM_FAIL();
                break;
            case CYAML_SEQ:
                if (n->seq.count == 0) {
                    if (!dump_anchor(e, n))
                        DM_FAIL();
                    if (!dump_tag(e, n))
                        DM_FAIL();
                    if (!emit_cstr(e, "[]"))
                        DM_FAIL();
                    break;
                }
                if (n->anchor.len > 0 || n->tag.len > 0) {
                    if (!dump_anchor(e, n))
                        DM_FAIL();
                    if (!dump_tag(e, n))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                }
                DM_PUSH(n, DUMPF_SEQ, f->depth);
                break;
            case CYAML_MAP:
                if (n->map.count == 0) {
                    if (!dump_anchor(e, n))
                        DM_FAIL();
                    if (!dump_tag(e, n))
                        DM_FAIL();
                    if (!emit_cstr(e, "{}"))
                        DM_FAIL();
                    break;
                }
                if (n->anchor.len > 0 || n->tag.len > 0) {
                    if (!dump_anchor(e, n))
                        DM_FAIL();
                    if (!dump_tag(e, n))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                }
                DM_PUSH(n, DUMPF_MAP, f->depth);
                break;
            case CYAML_ALIAS:
                if (!emit_char(e, '*'))
                    DM_FAIL();
                if (!emit_str(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len))
                    DM_FAIL();
                break;
            default:
                break;
            }
            break;
        }

        case DUMPF_SEQ: {
            uint32_t start = f->start_idx;
            if (f->idx >= n->seq.count) {
                stack_count--;
                break;
            }
            bool is_after_dash = (EMIT_LAST(e) == C_SP && EMIT_PREV(e) == '-');
            if (!is_after_dash) {
                bool has_trailing_nl = (EMIT_LAST(e) == C_LF && EMIT_PREV(e) == C_LF);
                if (!has_trailing_nl && (f->idx > start || f->depth > 0))
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                if (!emit_indent(e, f->depth))
                    DM_FAIL();
            }

            cyaml_node_t* item = n->seq.items[f->idx];

            if (item && item->type == CYAML_MAP && item->map.count > 0) {
                bool item_has_props = item->anchor.len > 0 || item->tag.len > 0;
                if (item_has_props) {
                    if (!emit_cstr(e, "- "))
                        DM_FAIL();
                    if (!dump_anchor(e, item))
                        DM_FAIL();
                    if (!dump_tag(e, item))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                    f->idx++;
                    DM_PUSH(item, DUMPF_MAP, f->depth + 1);
                    break;
                }
                cyaml_node_t* first_key = item->map.pairs[0].key;
                cyaml_node_t* first_val = item->map.pairs[0].val;

                if (key_needs_explicit(e, first_key)) {
                    if (!emit_cstr(e, "- ? "))
                        DM_FAIL();
                    if (first_key->type == CYAML_SEQ && first_key->seq.count > 0) {
                        // Push frame for key seq handling (node=MAP to access key and value)
                        // Increment idx now so outer SEQ continues with next item when we return
                        f->idx++;
                        DM_PUSH(item, DUMPF_SEQ_KEYSEQ, f->depth + 1);
                        stack[stack_count - 1].idx = 0; // key item index
                        stack[stack_count - 1].sub_idx = 0; // phase tracking
                        // Push first key item for inline emission
                        DM_PUSH(first_key->seq.items[0], DUMPF_DASH_ITEM, f->depth + 2);
                        break;
                    } else if (first_key->type == CYAML_MAP && first_key->map.count > 0) {
                        cyaml_node_t* k0 = first_key->map.pairs[0].key;
                        cyaml_node_t* v0 = first_key->map.pairs[0].val;
                        if (!is_empty_key(k0)) {
                            if (!dump_node(e, k0, f->depth))
                                DM_FAIL();
                        }
                        if (!emit_cstr(e, ": "))
                            DM_FAIL();
                        if (!dump_node(e, v0, f->depth))
                            DM_FAIL();
                        for (uint32_t j = 1; j < first_key->map.count; j++) {
                            if (!emit_char(e, C_LF))
                                DM_FAIL();
                            if (!emit_indent(e, f->depth + 2))
                                DM_FAIL();
                            cyaml_node_t* kj = first_key->map.pairs[j].key;
                            if (!is_empty_key(kj)) {
                                if (!dump_node(e, kj, f->depth))
                                    DM_FAIL();
                            }
                            if (!emit_cstr(e, ": "))
                                DM_FAIL();
                            if (!dump_node(e, first_key->map.pairs[j].val, f->depth))
                                DM_FAIL();
                        }
                    } else {
                        if (!dump_node(e, first_key, f->depth + 1))
                            DM_FAIL();
                    }
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth + 1))
                        DM_FAIL();
                    if (!emit_char(e, ':'))
                        DM_FAIL();
                    if (!is_empty_value(first_val)) {
                        if (!emit_char(e, C_SP))
                            DM_FAIL();
                        f->sub_idx = 1;
                        f->state = DUMPF_SEQ_MAP_REST;
                        DM_PUSH(first_val, DUMPF_VALUE, f->depth + 2);
                        break;
                    }
                    f->sub_idx = 1;
                    f->state = DUMPF_SEQ_MAP_REST;
                    break;
                }

                if (!emit_cstr(e, "- "))
                    DM_FAIL();
                if (!dump_anchor(e, first_key))
                    DM_FAIL();
                if (!dump_tag(e, first_key))
                    DM_FAIL();
                if (first_key && first_key->type == CYAML_SCALAR) {
                    if (!dump_scalar(e, first_key, f->depth))
                        DM_FAIL();
                } else if (first_key && first_key->type == CYAML_ALIAS) {
                    if (!emit_char(e, '*'))
                        DM_FAIL();
                    if (!emit_str(e, cyaml_src(e->doc) + first_key->anchor.off, first_key->anchor.len))
                        DM_FAIL();
                } else if (first_key && first_key->type == CYAML_NULL && (first_key->anchor.len > 0 || first_key->tag.len > 0)) {
                    EMIT_TRIM_SPACE(e);
                }
                if (key_needs_space(first_key)) {
                    if (!emit_cstr(e, " :"))
                        DM_FAIL();
                } else {
                    if (!emit_char(e, ':'))
                        DM_FAIL();
                }
                if (is_empty_value(first_val)) {
                    if (first_val && (first_val->anchor.len > 0 || first_val->tag.len > 0)) {
                        if (!emit_char(e, C_SP))
                            DM_FAIL();
                        if (!dump_anchor(e, first_val))
                            DM_FAIL();
                        if (!dump_tag(e, first_val))
                            DM_FAIL();
                        EMIT_TRIM_SPACE(e);
                    }
                    f->sub_idx = 1;
                    f->state = DUMPF_SEQ_MAP_REST;
                    break;
                }
                if (first_val->type == CYAML_SEQ || first_val->type == CYAML_MAP) {
                    f->sub_idx = 1;
                    f->state = DUMPF_SEQ_MAP_REST;
                    DM_PUSH(first_val, DUMPF_VALUE, f->depth + 1);
                    break;
                }
                if (!emit_char(e, C_SP))
                    DM_FAIL();
                f->sub_idx = 1;
                f->state = DUMPF_SEQ_MAP_REST;
                DM_PUSH(first_val, DUMPF_VALUE, f->depth + 2);
                break;
            }

            if (item && item->type == CYAML_SEQ && item->seq.count > 0) {
                bool item_has_props = item->anchor.len > 0 || item->tag.len > 0;
                if (item_has_props) {
                    if (!emit_cstr(e, "- "))
                        DM_FAIL();
                    if (!dump_anchor(e, item))
                        DM_FAIL();
                    if (!dump_tag(e, item))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                    f->idx++;
                    DM_PUSH(item, DUMPF_SEQ, f->depth + 1);
                    break;
                }
                if (!emit_cstr(e, "- "))
                    DM_FAIL();
                cyaml_node_t* cur = item;
                int extra_depth = 0;
                while (cur->seq.count == 1 && cur->seq.items[0] && cur->seq.items[0]->type == CYAML_SEQ && cur->seq.items[0]->seq.count > 0 && cur->seq.items[0]->anchor.len == 0 && cur->seq.items[0]->tag.len == 0) {
                    if (!emit_cstr(e, "- "))
                        DM_FAIL();
                    cur = cur->seq.items[0];
                    extra_depth++;
                }
                f->extra_depth = extra_depth;
                if (cur->seq.count > 0) {
                    cyaml_node_t* first = cur->seq.items[0];
                    if (first && first->type == CYAML_MAP && first->map.count > 0 && first->anchor.len == 0 && first->tag.len == 0) {
                        if (!emit_cstr(e, "- "))
                            DM_FAIL();
                        cyaml_node_t* k = first->map.pairs[0].key;
                        cyaml_node_t* v = first->map.pairs[0].val;
                        if (key_needs_explicit(e, k)) {
                            if (!emit_cstr(e, "? "))
                                DM_FAIL();
                            if (k->type == CYAML_MAP && k->map.count > 0) {
                                cyaml_node_t* k0 = k->map.pairs[0].key;
                                cyaml_node_t* v0 = k->map.pairs[0].val;
                                if (k0 && k0->type == CYAML_SCALAR) {
                                    if (!dump_anchor(e, k0))
                                        DM_FAIL();
                                    if (!dump_tag(e, k0))
                                        DM_FAIL();
                                    if (!dump_scalar(e, k0, f->depth))
                                        DM_FAIL();
                                }
                                if (!emit_cstr(e, ": "))
                                    DM_FAIL();
                                if (v0 && v0->type == CYAML_SCALAR) {
                                    if (!dump_anchor(e, v0))
                                        DM_FAIL();
                                    if (!dump_tag(e, v0))
                                        DM_FAIL();
                                    if (!dump_scalar(e, v0, f->depth))
                                        DM_FAIL();
                                }
                            } else if (k->type == CYAML_SCALAR) {
                                if (!dump_anchor(e, k))
                                    DM_FAIL();
                                if (!dump_tag(e, k))
                                    DM_FAIL();
                                if (!dump_scalar(e, k, f->depth + extra_depth + 1))
                                    DM_FAIL();
                            }
                            if (!emit_char(e, C_LF))
                                DM_FAIL();
                            if (!emit_indent(e, f->depth + extra_depth + 2))
                                DM_FAIL();
                            if (!emit_char(e, ':'))
                                DM_FAIL();
                            if (!is_empty_value(v)) {
                                if (!emit_char(e, C_SP))
                                    DM_FAIL();
                                f->idx++;
                                if (cur->seq.count > 1) {
                                    DM_PUSH(cur, DUMPF_SEQ, f->depth + extra_depth + 1);
                                    stack[stack_count - 1].idx = 1;
                                    stack[stack_count - 1].start_idx = 1;
                                }
                                DM_PUSH(v, DUMPF_VALUE, f->depth + extra_depth + 1);
                                break;
                            }
                        } else {
                            if (!is_empty_key(k)) {
                                if (k->type == CYAML_SCALAR) {
                                    if (!dump_anchor(e, k))
                                        DM_FAIL();
                                    if (!dump_tag(e, k))
                                        DM_FAIL();
                                    if (!dump_scalar(e, k, f->depth))
                                        DM_FAIL();
                                } else if (k->type == CYAML_ALIAS) {
                                    if (!emit_char(e, '*'))
                                        DM_FAIL();
                                    if (!emit_str(e, cyaml_src(e->doc) + k->anchor.off, k->anchor.len))
                                        DM_FAIL();
                                }
                                if (key_needs_space(k)) {
                                    if (!emit_cstr(e, " :"))
                                        DM_FAIL();
                                } else {
                                    if (!emit_char(e, ':'))
                                        DM_FAIL();
                                }
                            } else {
                                if (!emit_char(e, ':'))
                                    DM_FAIL();
                            }
                            if (!is_empty_value(v)) {
                                if (!emit_char(e, C_SP))
                                    DM_FAIL();
                                f->idx++;
                                if (cur->seq.count > 1) {
                                    DM_PUSH(cur, DUMPF_SEQ, f->depth + extra_depth + 1);
                                    stack[stack_count - 1].idx = 1;
                                    stack[stack_count - 1].start_idx = 1;
                                }
                                DM_PUSH(v, DUMPF_VALUE, f->depth + extra_depth + 2);
                                break;
                            }
                        }
                        f->idx++;
                        if (cur->seq.count > 1) {
                            DM_PUSH(cur, DUMPF_SEQ, f->depth + extra_depth + 1);
                            stack[stack_count - 1].idx = 1;
                            stack[stack_count - 1].start_idx = 1;
                        }
                        break;
                    }
                    if (!emit_cstr(e, "- "))
                        DM_FAIL();
                    f->idx++;
                    if (cur->seq.count > 1) {
                        DM_PUSH(cur, DUMPF_SEQ, f->depth + extra_depth + 1);
                        stack[stack_count - 1].idx = 1;
                        stack[stack_count - 1].start_idx = 1;
                    }
                    DM_PUSH(first, DUMPF_VALUE, f->depth + extra_depth + 1);
                    break;
                }
                f->idx++;
                break;
            }

            if (!item || item->type == CYAML_NULL || item->type == CYAML_NONE || (item->type == CYAML_SCALAR && item->span.len == 0 && !((item->style == CYAML_LITERAL || item->style == CYAML_FOLDED) && item->chomp == CYAML_KEEP))) {
                if (item && (item->anchor.len > 0 || item->tag.len > 0)) {
                    if (!emit_cstr(e, "- "))
                        DM_FAIL();
                    if (!dump_anchor(e, item))
                        DM_FAIL();
                    if (!dump_tag(e, item))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                } else {
                    if (!emit_char(e, '-'))
                        DM_FAIL();
                }
                f->idx++;
                break;
            }

            if (!emit_cstr(e, "- "))
                DM_FAIL();
            f->idx++;
            DM_PUSH(item, DUMPF_VALUE, f->depth + 1);
            break;
        }

        case DUMPF_SEQ_MAP_REST: {
            cyaml_node_t* item = n->seq.items[f->idx];
            if (f->sub_idx >= item->map.count) {
                f->idx++;
                f->sub_idx = 0;
                f->state = DUMPF_SEQ;
                break;
            }
            if (!emit_char(e, C_LF))
                DM_FAIL();
            if (!emit_indent(e, f->depth + 1))
                DM_FAIL();
            cyaml_node_t* key = item->map.pairs[f->sub_idx].key;
            cyaml_node_t* val = item->map.pairs[f->sub_idx].val;

            if (key_needs_explicit(e, key)) {
                if (!emit_cstr(e, "? "))
                    DM_FAIL();
                if (key->type == CYAML_SCALAR) {
                    if (!dump_anchor(e, key))
                        DM_FAIL();
                    if (!dump_tag(e, key))
                        DM_FAIL();
                    if (!dump_scalar(e, key, f->depth + 1))
                        DM_FAIL();
                }
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth + 1))
                    DM_FAIL();
                if (!emit_char(e, ':'))
                    DM_FAIL();
            } else {
                if (!dump_anchor(e, key))
                    DM_FAIL();
                if (!dump_tag(e, key))
                    DM_FAIL();
                if (key && key->type == CYAML_SCALAR)
                    if (!dump_scalar(e, key, f->depth + 1))
                        DM_FAIL();
                if (key && key->type == CYAML_ALIAS) {
                    if (!emit_char(e, '*'))
                        DM_FAIL();
                    if (!emit_str(e, cyaml_src(e->doc) + key->anchor.off, key->anchor.len))
                        DM_FAIL();
                }
                if (key && key->type == CYAML_ALIAS) {
                    if (!emit_cstr(e, " :"))
                        DM_FAIL();
                } else {
                    if (!emit_char(e, ':'))
                        DM_FAIL();
                }
            }
            if (is_empty_value(val)) {
                if (val && (val->anchor.len > 0 || val->tag.len > 0)) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    if (!dump_anchor(e, val))
                        DM_FAIL();
                    if (!dump_tag(e, val))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                }
                f->sub_idx++;
                break;
            }
            if (val->type == CYAML_SEQ || val->type == CYAML_MAP) {
                f->sub_idx++;
                DM_PUSH(val, DUMPF_VALUE, f->depth + 2);
                break;
            }
            if (!emit_char(e, C_SP))
                DM_FAIL();
            f->sub_idx++;
            DM_PUSH(val, DUMPF_VALUE, f->depth + 2);
            break;
        }

        case DUMPF_SEQ_KEY_REST: {
            cyaml_node_t* item = n->seq.items[f->idx];
            cyaml_node_t* first_key = item->map.pairs[0].key;
            cyaml_node_t* first_val = item->map.pairs[0].val;
            uint32_t j = f->sub_idx;
            while (j < first_key->seq.count) {
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth + 2))
                    DM_FAIL();
                if (!emit_cstr(e, "- "))
                    DM_FAIL();
                cyaml_node_t* sitem = first_key->seq.items[j];
                if (!sitem) {
                    if (!emit_cstr(e, S_NULL))
                        DM_FAIL();
                } else if (sitem->type == CYAML_SCALAR) {
                    if (!dump_scalar(e, sitem, f->depth + 2))
                        DM_FAIL();
                } else if (sitem->type == CYAML_ALIAS) {
                    if (!emit_char(e, '*'))
                        DM_FAIL();
                    if (!emit_str(e, cyaml_src(e->doc) + sitem->anchor.off, sitem->anchor.len))
                        DM_FAIL();
                } else if (sitem->type == CYAML_SEQ || sitem->type == CYAML_MAP) {
                    f->sub_idx = j + 1;
                    DM_PUSH(sitem, DUMPF_VALUE, f->depth + 2);
                    break;
                } else {
                    if (!dump_anchor(e, sitem))
                        DM_FAIL();
                    if (!dump_tag(e, sitem))
                        DM_FAIL();
                }
                j++;
            }
            if (j >= first_key->seq.count) {
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth + 1))
                    DM_FAIL();
                if (!emit_char(e, ':'))
                    DM_FAIL();
                if (!is_empty_value(first_val)) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    f->sub_idx = 1;
                    f->state = DUMPF_SEQ_MAP_REST;
                    DM_PUSH(first_val, DUMPF_VALUE, f->depth + 2);
                    break;
                }
                f->sub_idx = 1;
                f->state = DUMPF_SEQ_MAP_REST;
            }
            break;
        }

        case DUMPF_MAP: {
            if (f->idx >= n->map.count) {
                stack_count--;
                break;
            }
            bool has_trailing_nl = (EMIT_LAST(e) == C_LF && EMIT_PREV(e) == C_LF);
            if (!has_trailing_nl && (f->idx > 0 || f->depth > 0))
                if (!emit_char(e, C_LF))
                    DM_FAIL();
            if (!emit_indent(e, f->depth))
                DM_FAIL();

            cyaml_node_t* key = n->map.pairs[f->idx].key;
            cyaml_node_t* val = n->map.pairs[f->idx].val;

            if (is_empty_key(key)) {
                if (!emit_char(e, ':'))
                    DM_FAIL();
                f->state = DUMPF_MAP_VAL;
                break;
            }

            bool explicit_key = key_needs_explicit(e, key);

            if (explicit_key) {
                if (!emit_cstr(e, "? "))
                    DM_FAIL();
                if (key->type == CYAML_MAP && key->map.count > 0) {
                    for (uint32_t j = 0; j < key->map.count; j++) {
                        if (j > 0) {
                            if (!emit_char(e, C_LF))
                                DM_FAIL();
                            if (!emit_indent(e, f->depth + 1))
                                DM_FAIL();
                        }
                        cyaml_node_t* kj = key->map.pairs[j].key;
                        if (!is_empty_key(kj)) {
                            if (!dump_anchor(e, kj))
                                DM_FAIL();
                            if (!dump_tag(e, kj))
                                DM_FAIL();
                            if (kj->type == CYAML_SCALAR) {
                                if (!dump_scalar(e, kj, f->depth))
                                    DM_FAIL();
                            } else if (kj->type == CYAML_SEQ && kj->seq.count == 0) {
                                if (!emit_cstr(e, "[]"))
                                    DM_FAIL();
                            } else if (kj->type == CYAML_MAP && kj->map.count == 0) {
                                if (!emit_cstr(e, "{}"))
                                    DM_FAIL();
                            }
                        }
                        if (!emit_cstr(e, ": "))
                            DM_FAIL();
                        cyaml_node_t* vj = key->map.pairs[j].val;
                        if (vj) {
                            if (!dump_anchor(e, vj))
                                DM_FAIL();
                            if (!dump_tag(e, vj))
                                DM_FAIL();
                            if (vj->type == CYAML_SCALAR) {
                                if (!dump_scalar(e, vj, f->depth))
                                    DM_FAIL();
                            } else if (vj->type == CYAML_SEQ && vj->seq.count == 0) {
                                if (!emit_cstr(e, "[]"))
                                    DM_FAIL();
                            } else if (vj->type == CYAML_MAP && vj->map.count == 0) {
                                if (!emit_cstr(e, "{}"))
                                    DM_FAIL();
                            }
                        }
                    }
                } else if (key->type == CYAML_SEQ && key->seq.count > 0) {
                    bool key_has_props = key->anchor.len > 0 || key->tag.len > 0;
                    if (key_has_props) {
                        if (!dump_anchor(e, key))
                            DM_FAIL();
                        if (!dump_tag(e, key))
                            DM_FAIL();
                        EMIT_TRIM_SPACE(e);
                        for (uint32_t j = 0; j < key->seq.count; j++) {
                            if (!emit_char(e, C_LF))
                                DM_FAIL();
                            if (!emit_indent(e, f->depth))
                                DM_FAIL();
                            if (!emit_cstr(e, "- "))
                                DM_FAIL();
                            cyaml_node_t* sitem = key->seq.items[j];
                            if (sitem && sitem->type == CYAML_SCALAR) {
                                if (!dump_anchor(e, sitem))
                                    DM_FAIL();
                                if (!dump_tag(e, sitem))
                                    DM_FAIL();
                                if (!dump_scalar(e, sitem, f->depth))
                                    DM_FAIL();
                            }
                        }
                    } else {
                        if (!emit_cstr(e, "- "))
                            DM_FAIL();
                        cyaml_node_t* sitem = key->seq.items[0];
                        if (sitem && sitem->type == CYAML_SCALAR) {
                            if (!dump_anchor(e, sitem))
                                DM_FAIL();
                            if (!dump_tag(e, sitem))
                                DM_FAIL();
                            if (!dump_scalar(e, sitem, f->depth))
                                DM_FAIL();
                        }
                        for (uint32_t j = 1; j < key->seq.count; j++) {
                            if (!emit_char(e, C_LF))
                                DM_FAIL();
                            if (!emit_indent(e, f->depth + 1))
                                DM_FAIL();
                            if (!emit_cstr(e, "- "))
                                DM_FAIL();
                            sitem = key->seq.items[j];
                            if (sitem && sitem->type == CYAML_SCALAR) {
                                if (!dump_anchor(e, sitem))
                                    DM_FAIL();
                                if (!dump_tag(e, sitem))
                                    DM_FAIL();
                                if (!dump_scalar(e, sitem, f->depth))
                                    DM_FAIL();
                            }
                        }
                    }
                } else {
                    int key_depth = (key->type == CYAML_SCALAR && (key->style == CYAML_LITERAL || key->style == CYAML_FOLDED))
                        ? f->depth + 1
                        : f->depth;
                    if (!dump_anchor(e, key))
                        DM_FAIL();
                    if (!dump_tag(e, key))
                        DM_FAIL();
                    if (key->type == CYAML_SCALAR)
                        if (!dump_scalar(e, key, key_depth))
                            DM_FAIL();
                }
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth))
                    DM_FAIL();

                if (val && val->type == CYAML_MAP && val->map.count > 0 && !is_empty_value(val)) {
                    if (!emit_cstr(e, ": "))
                        DM_FAIL();
                    for (uint32_t j = 0; j < val->map.count; j++) {
                        if (j > 0) {
                            if (!emit_char(e, C_LF))
                                DM_FAIL();
                            if (!emit_indent(e, f->depth + 1))
                                DM_FAIL();
                        }
                        cyaml_node_t* vkj = val->map.pairs[j].key;
                        if (vkj) {
                            if (!dump_anchor(e, vkj))
                                DM_FAIL();
                            if (!dump_tag(e, vkj))
                                DM_FAIL();
                            if (vkj->type == CYAML_SCALAR)
                                if (!dump_scalar(e, vkj, f->depth))
                                    DM_FAIL();
                        }
                        if (!emit_cstr(e, ": "))
                            DM_FAIL();
                        cyaml_node_t* vvj = val->map.pairs[j].val;
                        if (vvj) {
                            if (!dump_anchor(e, vvj))
                                DM_FAIL();
                            if (!dump_tag(e, vvj))
                                DM_FAIL();
                            if (vvj->type == CYAML_SCALAR)
                                if (!dump_scalar(e, vvj, f->depth))
                                    DM_FAIL();
                        }
                    }
                    f->idx++;
                    break;
                }
                if (val && val->type == CYAML_SEQ && val->seq.count > 0 && !is_empty_value(val)) {
                    if (!emit_cstr(e, ": - "))
                        DM_FAIL();
                    cyaml_node_t* sitem = val->seq.items[0];
                    if (sitem) {
                        if (!dump_anchor(e, sitem))
                            DM_FAIL();
                        if (!dump_tag(e, sitem))
                            DM_FAIL();
                        if (sitem->type == CYAML_SCALAR)
                            if (!dump_scalar(e, sitem, f->depth))
                                DM_FAIL();
                    }
                    for (uint32_t j = 1; j < val->seq.count; j++) {
                        if (!emit_char(e, C_LF))
                            DM_FAIL();
                        if (!emit_indent(e, f->depth + 1))
                            DM_FAIL();
                        if (!emit_cstr(e, "- "))
                            DM_FAIL();
                        sitem = val->seq.items[j];
                        if (sitem) {
                            if (!dump_anchor(e, sitem))
                                DM_FAIL();
                            if (!dump_tag(e, sitem))
                                DM_FAIL();
                            if (sitem->type == CYAML_SCALAR)
                                if (!dump_scalar(e, sitem, f->depth))
                                    DM_FAIL();
                        }
                    }
                    f->idx++;
                    break;
                }
                if (!emit_char(e, ':'))
                    DM_FAIL();
                f->state = DUMPF_MAP_VAL;
                break;
            }

            if (!dump_anchor(e, key))
                DM_FAIL();
            if (!dump_tag(e, key))
                DM_FAIL();
            if (key->type == CYAML_SCALAR) {
                if (!dump_scalar(e, key, f->depth))
                    DM_FAIL();
            } else if (key->type == CYAML_ALIAS) {
                if (!emit_char(e, '*'))
                    DM_FAIL();
                if (!emit_str(e, cyaml_src(e->doc) + key->anchor.off, key->anchor.len))
                    DM_FAIL();
            } else if (key->type == CYAML_NULL && (key->anchor.len > 0 || key->tag.len > 0)) {
                EMIT_TRIM_SPACE(e);
            }
            if (key_needs_space(key)) {
                if (!emit_cstr(e, " :"))
                    DM_FAIL();
            } else {
                if (!emit_char(e, ':'))
                    DM_FAIL();
            }
            f->state = DUMPF_MAP_VAL;
            break;
        }

        case DUMPF_MAP_VAL: {
            cyaml_node_t* key = n->map.pairs[f->idx].key;
            cyaml_node_t* val = n->map.pairs[f->idx].val;
            bool val_has_props = val && (val->anchor.len > 0 || val->tag.len > 0);
            bool explicit_key = !is_empty_key(key) && key_needs_explicit(e, key);
            bool from_flow = (n->style == (cyaml_style_t)CYAML_FLOW);

            if (is_empty_value(val)) {
                if (val_has_props) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    if (!dump_anchor(e, val))
                        DM_FAIL();
                    if (!dump_tag(e, val))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                } else if (!explicit_key && from_flow) {
                    if (val && val->type == CYAML_NULL)
                        if (!emit_cstr(e, " null"))
                            DM_FAIL();
                } else if (val && val->type == CYAML_SCALAR && (val->style == CYAML_LITERAL || val->style == CYAML_FOLDED) && val->chomp != CYAML_KEEP) {
                    if (!emit_cstr(e, " \"\""))
                        DM_FAIL();
                }
                f->idx++;
                f->state = DUMPF_MAP;
                break;
            }

            if (val->type == CYAML_SEQ) {
                if (val->style == (cyaml_style_t)CYAML_FLOW && val->seq.count == 0) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    f->idx++;
                    f->state = DUMPF_MAP;
                    DM_PUSH(val, DUMPF_VALUE, f->depth + 1);
                    break;
                }
                if (val_has_props) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    if (!dump_anchor(e, val))
                        DM_FAIL();
                    if (!dump_tag(e, val))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                    if (f->depth == 0)
                        if (!emit_char(e, C_LF))
                            DM_FAIL();
                    f->idx++;
                    f->state = DUMPF_MAP;
                    DM_PUSH(val, DUMPF_SEQ, f->depth);
                    break;
                }
                if (f->depth == 0)
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                f->idx++;
                f->state = DUMPF_MAP;
                DM_PUSH(val, DUMPF_SEQ, f->depth);
                break;
            }

            if (val->type == CYAML_MAP) {
                if (val->style == (cyaml_style_t)CYAML_FLOW && val->map.count == 0) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    f->idx++;
                    f->state = DUMPF_MAP;
                    DM_PUSH(val, DUMPF_VALUE, f->depth + 1);
                    break;
                }
                if (val_has_props) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    if (!dump_anchor(e, val))
                        DM_FAIL();
                    if (!dump_tag(e, val))
                        DM_FAIL();
                    EMIT_TRIM_SPACE(e);
                    f->idx++;
                    f->state = DUMPF_MAP;
                    DM_PUSH(val, DUMPF_MAP, f->depth + 1);
                    break;
                }
                f->idx++;
                f->state = DUMPF_MAP;
                DM_PUSH(val, DUMPF_VALUE, f->depth + 1);
                break;
            }

            if (!emit_char(e, C_SP))
                DM_FAIL();
            f->idx++;
            f->state = DUMPF_MAP;
            DM_PUSH(val, DUMPF_VALUE, f->depth + 1);
            break;
        }

        case DUMPF_DASH_ITEM: {
            // Emit "- " then analyze item and handle appropriately
            if (!emit_cstr(e, "- "))
                DM_FAIL();

            if (!n) {
                if (!emit_cstr(e, S_NULL))
                    DM_FAIL();
                stack_count--;
                break;
            }

            // Case 1: SEQ with first item being MAP with explicit key
            if (n->type == CYAML_SEQ && n->seq.count > 0 && n->anchor.len == 0 && n->tag.len == 0) {
                cyaml_node_t* first = n->seq.items[0];
                if (first && first->type == CYAML_MAP && first->map.count > 0 && first->anchor.len == 0 && first->tag.len == 0 && key_needs_explicit(e, first->map.pairs[0].key)) {
                    // Emit "- ? " for the nested structure
                    if (!emit_cstr(e, "- ? "))
                        DM_FAIL();
                    cyaml_node_t* k = first->map.pairs[0].key;

                    // Set up this frame as continuation for after key processing
                    f->state = DUMPF_DASH_SEQ_MAP;
                    f->idx = 0; // key seq item index (if key is SEQ)
                    f->sub_idx = 0; // phase tracking

                    if (k->type == CYAML_SEQ && k->seq.count > 0) {
                        // Key is a SEQ - inline the first item
                        DM_PUSH(k->seq.items[0], DUMPF_DASH_ITEM, f->depth + 2);
                    } else {
                        // Key is not SEQ - inline it directly
                        DM_PUSH(k, DUMPF_DASH_ITEM, f->depth + 2);
                    }
                    break;
                }
                // Simple SEQ case: inline first item, then handle rest
                f->state = DUMPF_DASH_SEQ;
                f->idx = 1; // next item to process
                DM_PUSH(first, DUMPF_DASH_ITEM, f->depth + 2);
                break;
            }

            // Case 2: MAP with explicit key (not nested in SEQ)
            if (n->type == CYAML_MAP && n->map.count > 0 && n->anchor.len == 0 && n->tag.len == 0 && key_needs_explicit(e, n->map.pairs[0].key)) {
                if (!emit_cstr(e, "? "))
                    DM_FAIL();
                cyaml_node_t* k = n->map.pairs[0].key;

                f->state = DUMPF_DASH_MAP;
                f->idx = 0;

                if (k->type == CYAML_SEQ && k->seq.count > 0) {
                    // Key is SEQ - inline first item
                    DM_PUSH(k->seq.items[0], DUMPF_DASH_ITEM, f->depth + 2);
                } else {
                    // Key is not SEQ - dump it directly
                    f->sub_idx = 1; // mark that we're past key handling
                    DM_PUSH(k, DUMPF_VALUE, f->depth + 2);
                }
                break;
            }

            // Case 3: simple item - dump directly
            if (n->type == CYAML_SCALAR) {
                if (!dump_anchor(e, n))
                    DM_FAIL();
                if (!dump_tag(e, n))
                    DM_FAIL();
                if (!dump_scalar(e, n, f->depth))
                    DM_FAIL();
                stack_count--;
                break;
            }
            if (n->type == CYAML_ALIAS) {
                if (!emit_char(e, '*'))
                    DM_FAIL();
                if (!emit_str(e, cyaml_src(e->doc) + n->anchor.off, n->anchor.len))
                    DM_FAIL();
                stack_count--;
                break;
            }
            // Other types: delegate to VALUE
            stack_count--;
            DM_PUSH(n, DUMPF_VALUE, f->depth);
            break;
        }

        case DUMPF_DASH_SEQ_MAP: {
            // After inline processing of key item(s) for a SEQ containing MAP with explicit key
            // n is the outer SEQ (e.g., [ [[b,c]]: d, e ])
            cyaml_node_t* first = n->seq.items[0]; // The MAP with explicit key
            cyaml_node_t* k = first->map.pairs[0].key;
            cyaml_node_t* v = first->map.pairs[0].val;

            // Phase 0: check if key is SEQ and we have more key items to process
            if (f->sub_idx == 0 && k->type == CYAML_SEQ) {
                f->idx++;
                if (f->idx < k->seq.count) {
                    // More key items - emit newline/indent and continue inline
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth + 4))
                        DM_FAIL();
                    DM_PUSH(k->seq.items[f->idx], DUMPF_DASH_ITEM, f->depth + 4);
                    break;
                }
                // Done with key items, move to value phase
                f->sub_idx = 1;
            }

            // Phase 1: emit colon and handle value
            if (f->sub_idx == 1) {
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth + 2))
                    DM_FAIL();
                if (!emit_char(e, ':'))
                    DM_FAIL();
                if (!is_empty_value(v)) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    f->sub_idx = 2;
                    DM_PUSH(v, DUMPF_VALUE, f->depth + 2);
                    break;
                }
                f->sub_idx = 2;
            }

            // Phase 2: handle rest of map pairs
            if (f->sub_idx == 2) {
                f->idx = 1; // reuse idx for map pair iteration
                f->sub_idx = 3;
            }
            if (f->sub_idx == 3) {
                if (f->idx < first->map.count) {
                    cyaml_node_t* mk = first->map.pairs[f->idx].key;
                    cyaml_node_t* mv = first->map.pairs[f->idx].val;
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth + 2))
                        DM_FAIL();
                    if (!is_empty_key(mk)) {
                        if (mk->type == CYAML_SCALAR) {
                            if (!dump_anchor(e, mk))
                                DM_FAIL();
                            if (!dump_tag(e, mk))
                                DM_FAIL();
                            if (!dump_scalar(e, mk, f->depth + 2))
                                DM_FAIL();
                        } else if (mk->type == CYAML_ALIAS) {
                            if (!emit_char(e, '*'))
                                DM_FAIL();
                            if (!emit_str(e, cyaml_src(e->doc) + mk->anchor.off, mk->anchor.len))
                                DM_FAIL();
                        }
                    }
                    if (!emit_cstr(e, ": "))
                        DM_FAIL();
                    if (!is_empty_value(mv)) {
                        f->idx++;
                        DM_PUSH(mv, DUMPF_VALUE, f->depth + 2);
                        break;
                    }
                    f->idx++;
                    break;
                }
                f->sub_idx = 4;
                f->idx = 1; // reset for seq iteration
            }

            // Phase 4: handle rest of outer seq items
            if (f->sub_idx == 4) {
                if (f->idx < n->seq.count) {
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth + 1))
                        DM_FAIL();
                    if (!emit_cstr(e, "- "))
                        DM_FAIL();
                    f->idx++;
                    DM_PUSH(n->seq.items[f->idx - 1], DUMPF_VALUE, f->depth + 2);
                    break;
                }
            }
            stack_count--;
            break;
        }

        case DUMPF_DASH_SEQ: {
            // After inline first item of simple SEQ, handle rest with newlines
            if (f->idx >= n->seq.count) {
                stack_count--;
                break;
            }
            if (!emit_char(e, C_LF))
                DM_FAIL();
            if (!emit_indent(e, f->depth + 2))
                DM_FAIL();
            if (!emit_cstr(e, "- "))
                DM_FAIL();
            cyaml_node_t* item = n->seq.items[f->idx];
            f->idx++;
            DM_PUSH(item, DUMPF_VALUE, f->depth + 3);
            break;
        }

        case DUMPF_DASH_MAP: {
            // After inline key handling for MAP with explicit key
            cyaml_node_t* k = n->map.pairs[0].key;
            cyaml_node_t* v = n->map.pairs[0].val;

            // Phase 0: if key is SEQ, iterate remaining key items
            if (f->sub_idx == 0 && k->type == CYAML_SEQ) {
                f->idx++;
                if (f->idx < k->seq.count) {
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth + 2))
                        DM_FAIL();
                    DM_PUSH(k->seq.items[f->idx], DUMPF_DASH_ITEM, f->depth + 2);
                    break;
                }
                f->sub_idx = 1;
            }

            // Phase 1: emit colon and handle value
            if (f->sub_idx <= 1) {
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth))
                    DM_FAIL();
                if (!emit_char(e, ':'))
                    DM_FAIL();
                if (!is_empty_value(v)) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    stack_count--;
                    DM_PUSH(v, DUMPF_VALUE, f->depth + 1);
                    break;
                }
            }
            stack_count--;
            break;
        }

        case DUMPF_SEQ_KEYSEQ: {
            // Handle key SEQ iteration for explicit key in SEQ context
            // n is the MAP, we access n->map.pairs[0].key for the key SEQ
            cyaml_node_t* key = n->map.pairs[0].key;
            cyaml_node_t* val = n->map.pairs[0].val;

            // Phase 0: iterate remaining key items
            if (f->sub_idx == 0) {
                f->idx++;
                if (f->idx < key->seq.count) {
                    // More key items - emit newline/indent and push inline
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth + 1))
                        DM_FAIL();
                    DM_PUSH(key->seq.items[f->idx], DUMPF_DASH_ITEM, f->depth + 1);
                    break;
                }
                // Done with key items, move to value phase
                f->sub_idx = 1;
            }

            // Phase 1: emit colon and handle value
            if (f->sub_idx == 1) {
                if (!emit_char(e, C_LF))
                    DM_FAIL();
                if (!emit_indent(e, f->depth))
                    DM_FAIL();
                if (!emit_char(e, ':'))
                    DM_FAIL();
                if (!is_empty_value(val)) {
                    if (!emit_char(e, C_SP))
                        DM_FAIL();
                    f->sub_idx = 2;
                    f->idx = 1; // for map pair iteration
                    DM_PUSH(val, DUMPF_VALUE, f->depth + 1);
                    break;
                }
                f->sub_idx = 2;
                f->idx = 1;
            }

            // Phase 2: handle rest of map pairs
            if (f->sub_idx == 2) {
                if (f->idx < n->map.count) {
                    cyaml_node_t* mk = n->map.pairs[f->idx].key;
                    cyaml_node_t* mv = n->map.pairs[f->idx].val;
                    if (!emit_char(e, C_LF))
                        DM_FAIL();
                    if (!emit_indent(e, f->depth))
                        DM_FAIL();
                    if (!is_empty_key(mk)) {
                        if (mk->type == CYAML_SCALAR) {
                            if (!dump_anchor(e, mk))
                                DM_FAIL();
                            if (!dump_tag(e, mk))
                                DM_FAIL();
                            if (!dump_scalar(e, mk, f->depth))
                                DM_FAIL();
                        } else if (mk->type == CYAML_ALIAS) {
                            if (!emit_char(e, '*'))
                                DM_FAIL();
                            if (!emit_str(e, cyaml_src(e->doc) + mk->anchor.off, mk->anchor.len))
                                DM_FAIL();
                        }
                    }
                    if (!emit_cstr(e, ": "))
                        DM_FAIL();
                    if (!is_empty_value(mv)) {
                        f->idx++;
                        DM_PUSH(mv, DUMPF_VALUE, f->depth + 1);
                        break;
                    }
                    f->idx++;
                    break;
                }
            }
            stack_count--;
            break;
        }
        }
    }

cleanup:
    free(stack);
    return result;
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
                    uint32_t lines = (v->span.end_line && v->span.start_line) ? v->span.end_line - v->span.start_line : 0;
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
                    if (!needs_quoting_ex(str, len, 0)) {
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

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = opts ? *opts : CYAML_EMIT_DEFAULT, .doc = doc, .flags = 0, .comment_idx = 0, .last_line = 0
    };

    if (e.opts.preserve_comments && doc->root)
        emit_comments_before_line(&e, doc->root->span.start_line, 0);
    if (e.opts.doc_start)
        emit_cstr(&e, "---\n");
    if (doc->root)
        emit_node(&e, doc->root, 0);
    if (e.opts.preserve_comments)
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

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = opts ? *opts : CYAML_EMIT_DEFAULT, .doc = doc, .flags = 0, .comment_idx = 0, .last_line = 0
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

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = CYAML_DUMP_DEFAULT, .doc = doc, .flags = EMIT_DUMP, .comment_idx = 0, .last_line = 0
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

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = CYAML_DUMP_DEFAULT, .doc = NULL, .flags = EMIT_DUMP, .comment_idx = 0, .last_line = 0
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

    emitter_t e = {
        .buf = NULL, .len = 0, .cap = 0, .opts = CYAML_EMIT_DEFAULT, .doc = NULL, .flags = 0, .comment_idx = 0, .last_line = 0
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
