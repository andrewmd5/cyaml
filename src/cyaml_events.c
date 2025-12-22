#include "cyaml_internal.h"

// #region Event Buffer

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
    const cyaml_doc_t* doc;
    bool indent;
} event_buf_t;

static inline bool event_grow(event_buf_t* e, size_t need)
{
    if (e->len + need < e->cap)
        return true;
    size_t new_cap = e->cap ? e->cap * 2 : 256;
    while (new_cap < e->len + need)
        new_cap *= 2;
    char* new_buf = realloc(e->buf, new_cap);
    if (!new_buf)
        return false;
    e->buf = new_buf;
    e->cap = new_cap;
    return true;
}

static inline bool event_char(event_buf_t* e, char c)
{
    if (!event_grow(e, 1))
        return false;
    e->buf[e->len++] = c;
    return true;
}

static inline bool event_str(event_buf_t* e, const char* s)
{
    size_t slen = strlen(s);
    if (!event_grow(e, slen))
        return false;
    memcpy(e->buf + e->len, s, slen);
    e->len += slen;
    return true;
}

//! Output string with percent-decoding (e.g., %21 -> !)
static bool event_str_pct_decode(event_buf_t* e, const char* s, size_t len)
{
    if (!event_grow(e, len))
        return false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '%' && i + 2 < len) {
            int h1 = CYAML_HEX_VALUE(s[i + 1]);
            int h2 = CYAML_HEX_VALUE(s[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                e->buf[e->len++] = (char)((h1 << 4) | h2);
                i += 2;
                continue;
            }
        }
        e->buf[e->len++] = s[i];
    }
    return true;
}

static inline bool event_indent(event_buf_t* e, int depth)
{
    if (!e->indent)
        return true;
    for (int i = 0; i < depth; i++) {
        if (!event_char(e, C_SP))
            return false;
    }
    return true;
}

// #endregion

// #region Value Escaping

//! Escape special characters for event stream output
//! Note: yaml-test-suite format only escapes: \n \r \t \\ \0 and non-printables
//! (does NOT escape quotes like YAML double-quoted strings do)
static bool event_escape_value(event_buf_t* e, const char* s, size_t len)
{
    size_t pos = 0;

    while (pos < len) {
        cyaml_cp_t cp;
        int consumed = cyaml_utf8_decode(s + pos, len - pos, &cp);
        if (consumed <= 0 || cp == CYAML_CP_INVALID) {
            // Invalid UTF-8, output as \xNN
            if (!event_grow(e, 4))
                return false;
            static const char hex[] = "0123456789ABCDEF";
            unsigned char c = (unsigned char)s[pos];
            e->buf[e->len++] = C_BSLASH;
            e->buf[e->len++] = 'x';
            e->buf[e->len++] = hex[(c >> 4) & 0xF];
            e->buf[e->len++] = hex[c & 0xF];
            pos++;
            continue;
        }

        // Event format specific escapes (subset of YAML escapes)
        switch (cp) {
        case C_LF:
            if (!event_str(e, "\\n"))
                return false;
            break;
        case C_CR:
            if (!event_str(e, "\\r"))
                return false;
            break;
        case C_TAB:
            if (!event_str(e, "\\t"))
                return false;
            break;
        case C_BSLASH:
            if (!event_str(e, "\\\\"))
                return false;
            break;
        case C_NUL:
            if (!event_str(e, "\\0"))
                return false;
            break;
        default:
            if (cyaml_is_printable(cp) && cp >= 0x20) {
                // Output raw UTF-8 (including quotes - not escaped in event format)
                if (!event_grow(e, (size_t)consumed))
                    return false;
                memcpy(e->buf + e->len, s + pos, (size_t)consumed);
                e->len += (size_t)consumed;
            } else {
                // Non-printable: use cyaml_write_escape for hex/unicode
                char esc[12];
                int esc_len = cyaml_write_escape(cp, esc, sizeof(esc));
                if (esc_len > 0) {
                    if (!event_grow(e, (size_t)esc_len))
                        return false;
                    memcpy(e->buf + e->len, esc, (size_t)esc_len);
                    e->len += (size_t)esc_len;
                }
            }
            break;
        }

        pos += (size_t)consumed;
    }

    return true;
}

// #endregion

// #region Scalar Resolution

//! [7.4] Resolve flow scalar line folding for single/double quoted strings
//! Returns malloc'd resolved string (caller must free), or NULL on failure
static char* resolve_flow_scalar(const char* s, size_t len, cyaml_style_t style, size_t* out_len)
{
    // Allocate output buffer (may be smaller than input after folding)
    size_t cap = len + 1;
    char* out = malloc(cap);
    if (!out)
        return NULL;

    size_t pos = 0;
    size_t out_pos = 0;
    bool first_line = true;

    while (pos < len) {
        // Find end of current line
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < len && s[line_end] != C_LF && s[line_end] != C_CR) {
            line_end++;
        }

        // Content of this line (before any trailing whitespace trimming)
        size_t content_start = line_start;
        size_t content_end = line_end;

        // Trim leading whitespace on continuation lines (not first line)
        if (!first_line) {
            while (content_start < content_end && (s[content_start] == C_SP || s[content_start] == C_TAB)) {
                content_start++;
            }
        }

        // Trim trailing whitespace (but not on last segment before close quote)
        size_t next_pos = line_end;
        if (next_pos < len && s[next_pos] == C_CR)
            next_pos++;
        if (next_pos < len && s[next_pos] == C_LF)
            next_pos++;

        bool has_more = (next_pos < len);
        if (has_more) {
            // Trim trailing whitespace
            while (content_end > content_start && (s[content_end - 1] == C_SP || s[content_end - 1] == C_TAB)) {
                content_end--;
            }
        }

        // Copy line content
        size_t content_len = content_end - content_start;
        if (content_len > 0) {
            // Handle escaped quotes for single-quoted style
            if (style == CYAML_SINGLE) {
                for (size_t i = content_start; i < content_end; i++) {
                    if (s[i] == '\'' && i + 1 < content_end && s[i + 1] == '\'') {
                        out[out_pos++] = '\'';
                        i++; // Skip second quote
                    } else {
                        out[out_pos++] = s[i];
                    }
                }
            } else {
                memcpy(out + out_pos, s + content_start, content_len);
                out_pos += content_len;
            }
        }

        // Handle line break
        if (line_end < len) {
            // Skip CRLF or LF
            pos = line_end;
            if (pos < len && s[pos] == C_CR)
                pos++;
            if (pos < len && s[pos] == C_LF)
                pos++;

            // Count consecutive empty lines
            int empty_lines = 0;
            size_t scan = pos;
            while (scan < len) {
                // Skip leading whitespace
                while (scan < len && (s[scan] == C_SP || s[scan] == C_TAB))
                    scan++;

                // Check if this is an empty line
                if (scan < len && (s[scan] == C_LF || s[scan] == C_CR)) {
                    empty_lines++;
                    if (s[scan] == C_CR)
                        scan++;
                    if (scan < len && s[scan] == C_LF)
                        scan++;
                    pos = scan;
                } else {
                    // Non-empty line content found
                    break;
                }
            }

            if (empty_lines > 0) {
                // [6.5] b-l-trimmed: break + empty lines → produces exactly empty_lines newlines
                for (int i = 0; i < empty_lines; i++) {
                    out[out_pos++] = C_LF;
                }
            } else if (pos < len) {
                // Non-empty continuation → fold to space
                out[out_pos++] = C_SP;
            }
            // If pos >= len, don't add anything (end of content)

            first_line = false;
        } else {
            pos = line_end;
        }
    }

    out[out_pos] = C_NUL;
    if (out_len)
        *out_len = out_pos;
    return out;
}

// #endregion

// #region Node Events

#define EVENT_STACK_INIT_CAP 32

typedef enum {
    EFRAME_VALUE,
    EFRAME_SEQ_OPEN,
    EFRAME_SEQ_ITEM,
    EFRAME_SEQ_CLOSE,
    EFRAME_MAP_OPEN,
    EFRAME_MAP_PAIR,
    EFRAME_MAP_VAL,
    EFRAME_MAP_CLOSE
} event_frame_state_t;

typedef struct {
    const cyaml_node_t* node;
    uint32_t child_idx;
    event_frame_state_t state;
    int depth;
} event_frame_t;

#define EVENT_PUSH(stk, cnt, cap, nd, st, dp, on_fail)                     \
    do {                                                                   \
        if ((cnt) >= (cap)) {                                              \
            size_t new_cap = (cap) * 2;                                    \
            event_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk))); \
            if (!tmp) {                                                    \
                on_fail;                                                   \
            }                                                              \
            (stk) = tmp;                                                   \
            (cap) = new_cap;                                               \
        }                                                                  \
        (stk)[(cnt)] = (event_frame_t) { (nd), 0, (st), (dp) };            \
        (cnt)++;                                                           \
    } while (0)

#define EV_PUSH(nd, st, dp) \
    EVENT_PUSH(stack, stack_count, stack_cap, nd, st, dp, { result = false; goto cleanup; })

#define EV_FAIL(expr)       \
    do {                    \
        if (!(expr)) {      \
            result = false; \
            goto cleanup;   \
        }                   \
    } while (0)

//! Output anchor if present
static bool event_anchor(event_buf_t* e, const cyaml_node_t* n)
{
    if (n->anchor.len > 0) {
        if (!event_str(e, " &"))
            return false;
        const char* anchor = cyaml_src(e->doc) + n->anchor.off;
        if (!event_grow(e, n->anchor.len))
            return false;
        memcpy(e->buf + e->len, anchor, n->anchor.len);
        e->len += n->anchor.len;
    }
    return true;
}

//! Output tag if present, expanding shorthand tags per YAML spec
//! Expand tag using document's TAG directives
//! !! → tag:yaml.org,2002:  (default secondary tag handle)
//! !  → local tag (verbatim)
static bool event_tag(event_buf_t* e, const cyaml_node_t* n)
{
    if (n->tag.len > 0) {
        if (!event_str(e, " <"))
            return false;

        const char* tag = cyaml_src(e->doc) + n->tag.off;
        size_t tag_len = n->tag.len;

        // Verbatim tag: !<...> - output content without !< and >
        if (tag_len >= 3 && tag[0] == '!' && tag[1] == '<' && tag[tag_len - 1] == '>') {
            if (!event_grow(e, tag_len - 3))
                return false;
            memcpy(e->buf + e->len, tag + 2, tag_len - 3);
            e->len += tag_len - 3;
        } else if (tag[0] == '!') {
            // Tag with handle - try to expand using directives
            const char* src = cyaml_src(e->doc);
            bool expanded = false;

            // Find the handle (! or !! or !name!)
            size_t handle_len = 1; //! At least "!"
            if (tag_len >= 2 && tag[1] == '!') {
                handle_len = 2; //! "!!"
            } else {
                // Named handle: !name!
                for (size_t i = 1; i < tag_len; i++) {
                    if (tag[i] == '!') {
                        handle_len = i + 1;
                        break;
                    }
                }
            }

            // Look up handle in document's TAG directives
            for (uint8_t i = 0; i < e->doc->tag_count; i++) {
                const cyaml_tag_directive_t* td = &e->doc->tags[i];
                const char* h = src + td->handle.off;
                if (td->handle.len == handle_len && memcmp(h, tag, handle_len) == 0) {
                    // Found matching directive - output prefix + suffix
                    const char* prefix = src + td->prefix.off;
                    if (!event_grow(e, td->prefix.len))
                        return false;
                    memcpy(e->buf + e->len, prefix, td->prefix.len);
                    e->len += td->prefix.len;
                    // Output tag suffix (part after handle) with percent-decoding
                    if (tag_len > handle_len) {
                        if (!event_str_pct_decode(e, tag + handle_len, tag_len - handle_len))
                            return false;
                    }
                    expanded = true;
                    break;
                }
            }

            if (!expanded) {
                // No custom directive - use defaults
                if (handle_len == 2 && tag[1] == '!') {
                    // Default !! → tag:yaml.org,2002:
                    if (!event_str(e, "tag:yaml.org,2002:"))
                        return false;
                    if (tag_len > 2) {
                        if (!event_grow(e, tag_len - 2))
                            return false;
                        memcpy(e->buf + e->len, tag + 2, tag_len - 2);
                        e->len += tag_len - 2;
                    }
                } else {
                    // Primary handle or named - output as-is
                    if (!event_grow(e, tag_len))
                        return false;
                    memcpy(e->buf + e->len, tag, tag_len);
                    e->len += tag_len;
                }
            }
        } else {
            // No ! prefix - output as-is
            if (!event_grow(e, tag_len))
                return false;
            memcpy(e->buf + e->len, tag, tag_len);
            e->len += tag_len;
        }

        if (!event_char(e, '>'))
            return false;
    }
    return true;
}

static bool event_scalar(event_buf_t* e, const cyaml_node_t* n, int depth)
{
    if (!event_indent(e, depth))
        return false;
    if (!event_str(e, "=VAL"))
        return false;

    if (!event_anchor(e, n))
        return false;
    if (!event_tag(e, n))
        return false;

    if (!event_char(e, C_SP))
        return false;

    // Style indicator per yaml-test-suite format
    char style;
    switch (n->style) {
    case CYAML_SINGLE:
        style = '\'';
        break;
    case CYAML_DOUBLE:
        style = '"';
        break;
    case CYAML_LITERAL:
        style = '|';
        break;
    case CYAML_FOLDED:
        style = '>';
        break;
    case CYAML_PLAIN:
    default:
        style = ':';
        break;
    }
    if (!event_char(e, style))
        return false;

    // Value content - resolve based on scalar style
    const char* s = cyaml_src(e->doc) + n->span.off;
    size_t slen = n->span.len;

    if (n->style == CYAML_DOUBLE) {
        // Double-quoted scalars need escape sequence decoding
        // Use cyaml_scalar_str which handles this
        char* decoded = cyaml_scalar_str(e->doc, n);
        if (!decoded)
            return false;
        bool ok = event_escape_value(e, decoded, strlen(decoded));
        free(decoded);
        if (!ok)
            return false;
    } else if (n->style == CYAML_SINGLE || n->style == CYAML_PLAIN) {
        // [7.3.3] [7.4] Flow and plain scalars need line folding resolution
        size_t resolved_len;
        char* resolved = resolve_flow_scalar(s, slen, n->style, &resolved_len);
        if (!resolved)
            return false;

        bool ok = event_escape_value(e, resolved, resolved_len);
        free(resolved);
        if (!ok)
            return false;
    } else {
        // [8.1] Block scalars (literal, folded) - strip indentation, apply chomping
        // For empty scalars (no content), leading breaks are subject to chomping
        bool is_empty = (slen == 0);
        if (is_empty) {
            // Empty scalar: leading breaks follow chomping rules
            if (n->chomp == CYAML_KEEP) {
                for (uint8_t i = 0; i < n->leading_breaks; i++) {
                    if (!event_str(e, "\\n"))
                        return false;
                }
            }
            // strip and clip: output nothing for empty scalar
        } else {
            // Non-empty: output leading empty lines
            for (uint8_t i = 0; i < n->leading_breaks; i++) {
                if (!event_str(e, "\\n"))
                    return false;
            }
        }

        // Use block indent from parser (handles explicit indent indicators)
        size_t block_indent = n->indent;
        size_t pos = 0;

        // Process each line, stripping block_indent spaces
        // For folded scalars: single newlines become spaces, multiple become newlines
        pos = 0;
        while (pos < slen) {
            // Skip indent (up to block_indent spaces)
            size_t line_indent = 0;
            while (pos < slen && s[pos] == C_SP && line_indent < block_indent) {
                pos++;
                line_indent++;
            }

            // Check if this line is "more-indented" (starts with space/tab after block indent)
            bool current_is_spaced = (pos < slen && (s[pos] == C_SP || s[pos] == C_TAB));

            // Copy line content until break
            size_t line_start = pos;
            while (pos < slen && s[pos] != C_LF && s[pos] != C_CR) {
                pos++;
            }

            // Check if this is an empty line (no content after indent strip)
            bool empty_line = (pos == line_start);

            // Output line content
            if (pos > line_start) {
                if (!event_escape_value(e, s + line_start, pos - line_start))
                    return false;
            }

            // Handle line break
            if (pos < slen && (s[pos] == C_LF || s[pos] == C_CR)) {
                // Count consecutive breaks
                int break_count = 0;
                while (pos < slen && (s[pos] == C_LF || s[pos] == C_CR)) {
                    if (s[pos] == C_CR)
                        pos++;
                    if (pos < slen && s[pos] == C_LF)
                        pos++;
                    break_count++;
                    // Check if next line is empty (less than block_indent content)
                    size_t peek = pos;
                    size_t peek_indent = 0;
                    while (peek < slen && s[peek] == C_SP && peek_indent < block_indent) {
                        peek++;
                        peek_indent++;
                    }
                    if (peek >= slen || s[peek] == C_LF || s[peek] == C_CR) {
                        // Next line is empty, continue counting breaks
                        pos = peek;
                    } else {
                        break;
                    }
                }

                // For folded scalars: fold single break to space, multiple to newlines
                if (n->style == CYAML_FOLDED) {
                    // Check if next line starts with whitespace (spaced text)
                    size_t peek = pos;
                    size_t peek_indent = 0;
                    while (peek < slen && s[peek] == C_SP && peek_indent < block_indent) {
                        peek++;
                        peek_indent++;
                    }
                    bool next_is_spaced = (peek < slen && (s[peek] == C_SP || s[peek] == C_TAB));
                    bool next_has_content = (peek < slen && s[peek] != C_LF && s[peek] != C_CR);

                    // Folding rules per YAML spec:
                    // - Line breaks around more-indented (spaced) lines are NOT folded
                    // - Empty lines become line feeds
                    // - Single break between regular content lines becomes space
                    bool around_spaced = current_is_spaced || next_is_spaced;

                    if (break_count == 1 && !empty_line && !around_spaced && next_has_content) {
                        // Single break between regular content lines -> fold to space
                        if (!event_str(e, " "))
                            return false;
                    } else if (around_spaced) {
                        // Around spaced lines: preserve ALL line breaks
                        for (int i = 0; i < break_count; i++) {
                            if (!event_str(e, "\\n"))
                                return false;
                        }
                    } else {
                        // Multiple breaks or end of content: first break is consumed,
                        // remaining become newlines. For empty_line, output all.
                        int newlines = (empty_line || !next_has_content) ? break_count : break_count - 1;
                        for (int i = 0; i < newlines; i++) {
                            if (!event_str(e, "\\n"))
                                return false;
                        }
                    }
                } else {
                    // Literal scalar: preserve all newlines
                    for (int i = 0; i < break_count; i++) {
                        if (!event_str(e, "\\n"))
                            return false;
                    }
                }
            }
        }

        // Apply chomping for trailing newlines
        // Note: span excludes trailing breaks, trailing_breaks has the count
        if (n->chomp == CYAML_STRIP) {
            // Strip: no trailing newlines (already not in span)
        } else if (n->chomp == CYAML_KEEP) {
            // Keep: output all trailing newlines
            for (uint8_t i = 0; i < n->trailing_breaks; i++) {
                if (!event_str(e, "\\n"))
                    return false;
            }
        } else {
            // Clip (default): output exactly one trailing newline if there was content
            // Empty scalars with clip produce empty string (no trailing newline)
            if (n->trailing_breaks > 0 && slen > 0) {
                if (!event_str(e, "\\n"))
                    return false;
            }
        }
    }

    return event_char(e, C_LF);
}

static bool event_alias(event_buf_t* e, const cyaml_node_t* n, int depth)
{
    if (!event_indent(e, depth))
        return false;
    if (!event_str(e, "=ALI *"))
        return false;

    if (n->anchor.len > 0) {
        const char* name = cyaml_src(e->doc) + n->anchor.off;
        if (!event_grow(e, n->anchor.len))
            return false;
        memcpy(e->buf + e->len, name, n->anchor.len);
        e->len += n->anchor.len;
    }

    return event_char(e, C_LF);
}

static bool event_seq_open(event_buf_t* e, const cyaml_node_t* n, int depth)
{
    if (!event_indent(e, depth))
        return false;
    if (!event_str(e, "+SEQ"))
        return false;
    if (n->style == (cyaml_style_t)CYAML_FLOW) {
        if (!event_str(e, " []"))
            return false;
    }
    if (!event_anchor(e, n))
        return false;
    if (!event_tag(e, n))
        return false;
    return event_char(e, C_LF);
}

static bool event_seq_close(event_buf_t* e, int depth)
{
    if (!event_indent(e, depth))
        return false;
    return event_str(e, "-SEQ\n");
}

static bool event_map_open(event_buf_t* e, const cyaml_node_t* n, int depth)
{
    if (!event_indent(e, depth))
        return false;
    if (!event_str(e, "+MAP"))
        return false;
    if (n->style == (cyaml_style_t)CYAML_FLOW) {
        if (!event_str(e, " {}"))
            return false;
    }
    if (!event_anchor(e, n))
        return false;
    if (!event_tag(e, n))
        return false;
    return event_char(e, C_LF);
}

static bool event_map_close(event_buf_t* e, int depth)
{
    if (!event_indent(e, depth))
        return false;
    return event_str(e, "-MAP\n");
}

static bool event_node(event_buf_t* e, const cyaml_node_t* root, int start_depth)
{
    if (!root || root->type == CYAML_NONE) {
        if (!event_indent(e, start_depth))
            return false;
        return event_str(e, "=VAL :\n");
    }

    event_frame_t* stack = malloc(EVENT_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        return false;
    size_t stack_count = 0;
    size_t stack_cap = EVENT_STACK_INIT_CAP;
    bool result = true;

    EV_PUSH(root, EFRAME_VALUE, start_depth);

    while (stack_count > 0) {
        event_frame_t* f = &stack[stack_count - 1];
        const cyaml_node_t* n = f->node;

        switch (f->state) {
        case EFRAME_VALUE:
            stack_count--;
            if (!n || n->type == CYAML_NONE) {
                EV_FAIL(event_indent(e, f->depth));
                EV_FAIL(event_str(e, "=VAL :\n"));
            } else {
                switch (n->type) {
                case CYAML_NULL:
                    EV_FAIL(event_indent(e, f->depth));
                    EV_FAIL(event_str(e, "=VAL"));
                    EV_FAIL(event_anchor(e, n));
                    EV_FAIL(event_tag(e, n));
                    EV_FAIL(event_str(e, " :\n"));
                    break;
                case CYAML_SCALAR:
                    EV_FAIL(event_scalar(e, n, f->depth));
                    break;
                case CYAML_SEQ:
                    EV_PUSH(n, EFRAME_SEQ_OPEN, f->depth);
                    break;
                case CYAML_MAP:
                    EV_PUSH(n, EFRAME_MAP_OPEN, f->depth);
                    break;
                case CYAML_ALIAS:
                    EV_FAIL(event_alias(e, n, f->depth));
                    break;
                default:
                    break;
                }
            }
            break;

        case EFRAME_SEQ_OPEN:
            EV_FAIL(event_seq_open(e, n, f->depth));
            f->state = EFRAME_SEQ_ITEM;
            break;

        case EFRAME_SEQ_ITEM:
            if (f->child_idx >= n->seq.count)
                f->state = EFRAME_SEQ_CLOSE;
            else
                EV_PUSH(n->seq.items[f->child_idx++], EFRAME_VALUE, f->depth + 1);
            break;

        case EFRAME_SEQ_CLOSE:
            EV_FAIL(event_seq_close(e, f->depth));
            stack_count--;
            break;

        case EFRAME_MAP_OPEN:
            EV_FAIL(event_map_open(e, n, f->depth));
            f->state = EFRAME_MAP_PAIR;
            break;

        case EFRAME_MAP_PAIR:
            if (f->child_idx >= n->map.count) {
                f->state = EFRAME_MAP_CLOSE;
            } else {
                f->state = EFRAME_MAP_VAL;
                EV_PUSH(n->map.pairs[f->child_idx].key, EFRAME_VALUE, f->depth + 1);
            }
            break;

        case EFRAME_MAP_VAL:
            EV_PUSH(n->map.pairs[f->child_idx++].val, EFRAME_VALUE, f->depth + 1);
            f->state = EFRAME_MAP_PAIR;
            break;

        case EFRAME_MAP_CLOSE:
            EV_FAIL(event_map_close(e, f->depth));
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

//! Internal: emit events for a single document (without stream markers)
static bool event_document(event_buf_t* e, const cyaml_doc_t* doc)
{
    // +DOC - Document start (with --- if explicit marker was present)
    if (!event_indent(e, 1))
        return false;
    if (doc->flags & CYAML_DOC_START) {
        if (!event_str(e, "+DOC ---\n"))
            return false;
    } else {
        if (!event_str(e, "+DOC\n"))
            return false;
    }

    // Root node at depth 2 (emit empty value if no root)
    if (!doc->root) {
        if (!event_indent(e, 2))
            return false;
        if (!event_str(e, "=VAL :\n"))
            return false;
    } else {
        // Temporarily set doc for node event generation
        const cyaml_doc_t* saved = e->doc;
        e->doc = doc;
        bool ok = event_node(e, doc->root, 2);
        e->doc = saved;
        if (!ok)
            return false;
    }

    // -DOC - Document end (with ... if explicit marker was present)
    if (!event_indent(e, 1))
        return false;
    if (doc->flags & CYAML_DOC_END) {
        if (!event_str(e, "-DOC ...\n"))
            return false;
    } else {
        if (!event_str(e, "-DOC\n"))
            return false;
    }

    return true;
}

CYAML_API char* cyaml_events(const cyaml_doc_t* doc, bool indent, size_t* len)
{
    if (!doc)
        return NULL;

    event_buf_t e = {
        .buf = NULL,
        .len = 0,
        .cap = 0,
        .doc = doc,
        .indent = indent
    };

    // +STR - Stream start
    if (!event_str(&e, "+STR\n"))
        goto fail;

    // Single document
    if (!event_document(&e, doc))
        goto fail;

    // -STR - Stream end
    if (!event_str(&e, indent ? "-STR" : "-STR\n"))
        goto fail;

    // Null terminate
    if (!event_grow(&e, 1))
        goto fail;
    e.buf[e.len] = C_NUL;

    if (len)
        *len = e.len;
    return e.buf;

fail:
    free(e.buf);
    return NULL;
}

CYAML_API char* cyaml_stream_events(const cyaml_stream_t* stream, bool indent, size_t* len)
{
    if (!stream)
        return NULL;

    event_buf_t e = {
        .buf = NULL,
        .len = 0,
        .cap = 0,
        .doc = NULL,
        .indent = indent
    };

    // +STR - Stream start
    if (!event_str(&e, "+STR\n"))
        goto fail;

    // All documents
    for (uint32_t i = 0; i < stream->count; i++) {
        if (!event_document(&e, stream->docs[i]))
            goto fail;
    }

    // -STR - Stream end
    if (!event_str(&e, indent ? "-STR" : "-STR\n"))
        goto fail;

    // Null terminate
    if (!event_grow(&e, 1))
        goto fail;
    e.buf[e.len] = C_NUL;

    if (len)
        *len = e.len;
    return e.buf;

fail:
    free(e.buf);
    return NULL;
}

// #endregion
