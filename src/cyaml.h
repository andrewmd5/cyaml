#ifndef CYAML_H
#define CYAML_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// #region Platform

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef CYAML_BUILDING_DLL
#define CYAML_API __declspec(dllexport)
#elif defined(CYAML_DLL)
#define CYAML_API __declspec(dllimport)
#else
#define CYAML_API
#endif
#else
#define CYAML_API __attribute__((visibility("default")))
#endif

// #endregion

// #region Types

//! Node types
typedef enum {
    CYAML_NONE = 0, //!< Invalid/uninitialized
    CYAML_NULL, //!< Null value (~, null)
    CYAML_SCALAR, //!< Scalar value
    CYAML_SEQ, //!< Sequence (array)
    CYAML_MAP, //!< Mapping (object)
    CYAML_ALIAS //!< Alias (*anchor)
} cyaml_type_t;

//! Scalar styles
typedef enum {
    CYAML_PLAIN = 0, //!< Plain (unquoted)
    CYAML_SINGLE, //!< Single-quoted
    CYAML_DOUBLE, //!< Double-quoted
    CYAML_LITERAL, //!< Literal block (|)
    CYAML_FOLDED //!< Folded block (>)
} cyaml_style_t;

//! Collection styles
typedef enum {
    CYAML_BLOCK = 0, //!< Block style (indented)
    CYAML_FLOW //!< Flow style ({}, [])
} cyaml_coll_t;

//! Chomping for block scalars
typedef enum {
    CYAML_CLIP = 0, //!< Default: single trailing newline
    CYAML_STRIP, //!< Strip all trailing newlines (-)
    CYAML_KEEP //!< Keep all trailing newlines (+)
} cyaml_chomp_t;

//! YAML specification version
typedef enum {
    CYAML_SPEC_AUTO = 0, //!< Auto-detect from %YAML directive (default: 1.2)
    CYAML_SPEC_1_1, //!< YAML 1.1
    CYAML_SPEC_1_2, //!< YAML 1.2
    CYAML_SPEC_1_3 //!< YAML 1.3
} cyaml_spec_t;

//! Scalar flags (bitmask)
typedef enum {
    CYAML_SCALAR_NONE = 0,
    CYAML_SCALAR_EXPLICIT_EMPTY = 1 << 0 //!< Empty value that should NOT become null
} cyaml_scalar_flags_t;

//! Inferred scalar value kind (YAML Core Schema)
typedef enum {
    CYAML_KIND_NULL, //!< null, ~, Null, NULL, or empty
    CYAML_KIND_BOOL, //!< true, false (case-insensitive)
    CYAML_KIND_INT, //!< Integer (decimal, 0x hex, 0o octal)
    CYAML_KIND_FLOAT, //!< Float (includes .inf, .nan)
    CYAML_KIND_STRING //!< Unrecognized (treat as string)
} cyaml_scalar_kind_t;

// #endregion

// #region Span (Atom)

//! Span - a reference into source data with location info
//! This is the core building block: no string copying during parse
typedef struct {
    uint32_t off; //!< Byte offset into source
    uint32_t len; //!< Length in bytes
    uint32_t start_line; //!< 1-based start line
    uint32_t start_col; //!< 1-based start column
    uint32_t end_line; //!< 1-based end line
    uint32_t end_col; //!< 1-based end column
} cyaml_span_t;

// #endregion

// #region Node

typedef struct cyaml_node cyaml_node_t;
typedef struct cyaml_doc cyaml_doc_t;

//! Key-value pair for mappings
typedef struct {
    cyaml_node_t* key;
    cyaml_node_t* val;
} cyaml_pair_t;

//! Node - minimal metadata, content via spans
struct cyaml_node {
    cyaml_type_t type;
    cyaml_style_t style; //!< Scalar style (or collection style for seq/map)
    cyaml_chomp_t chomp; //!< Block scalar chomping
    uint8_t indent; //!< Block scalar explicit indent
    uint8_t leading_breaks; //!< Leading empty lines for block scalars
    uint8_t trailing_breaks; //!< Trailing line breaks for block scalars

    cyaml_span_t span; //!< Node's span in source
    cyaml_span_t tag; //!< Tag span (0,0 if none)
    cyaml_span_t anchor; //!< Anchor span (0,0 if none)

    union {
        //! Scalar: just the span (in node.span)
        struct {
            uint8_t flags; //!< Scalar flags (CYAML_SCALAR_EXPLICIT_EMPTY, etc.)
        } scalar;

        //! Sequence
        struct {
            cyaml_node_t** items;
            uint32_t count;
            uint32_t cap;
        } seq;

        //! Mapping
        struct {
            cyaml_pair_t* pairs;
            uint32_t count;
            uint32_t cap;
        } map;

        //! Alias
        struct {
            cyaml_node_t* target; //!< Resolved target (NULL until resolved)
        } alias;
    };
};

// #endregion

// #region Document

//! Memory pool for node allocation (chunked to avoid pointer invalidation)
#define CYAML_POOL_CHUNK_SIZE 64

typedef struct {
    cyaml_node_t** chunks; //!< Array of chunk pointers (never moved once allocated)
    uint32_t chunk_count; //!< Number of allocated chunks
    uint32_t chunk_cap; //!< Capacity of chunks array
    uint32_t count; //!< Total nodes allocated
} cyaml_pool_t;

//! Document flags
typedef enum {
    CYAML_DOC_START = 1 << 0, //!< Had explicit '---' marker
    CYAML_DOC_END = 1 << 1, //!< Had explicit '...' marker
    CYAML_HAS_DIRECTIVE = 1 << 2, //!< Had any directive (%YAML, %TAG, or reserved)
    CYAML_DOC_TAB_SEP = 1 << 3 //!< Tab separator after '---' marker
} cyaml_doc_flags_t;

//! Document mode
typedef enum {
    CYAML_PARSING, //!< Parsed document (source borrowed)
    CYAML_BUILDING //!< Built document (source owned)
} cyaml_mode_t;

//! TAG directive - maps handle to prefix
//! e.g., %TAG !yaml! tag:yaml.org,2002:
#define CYAML_MAX_TAG_DIRECTIVES 8
typedef struct {
    cyaml_span_t handle; //!< Tag handle (e.g., "!" or "!!" or "!yaml!")
    cyaml_span_t prefix; //!< Tag prefix (e.g., "tag:yaml.org,2002:")
} cyaml_tag_directive_t;

//! Document - source is borrowed for parsing, owned for building
struct cyaml_doc {
    cyaml_mode_t mode; //!< Document mode

    union {
        //! Borrowed source (parsed documents)
        struct {
            const char* ptr;
            uint32_t len;
        } borrow;

        //! Owned buffer (built documents)
        struct {
            char* ptr;
            uint32_t len;
            uint32_t cap;
        } owned;
    } src;

    cyaml_node_t* root; //!< Root node

    cyaml_pool_t pool; //!< Node pool

    //! Version from %YAML directive
    struct {
        uint8_t major;
        uint8_t minor;
    } version;

    //! TAG directives for this document
    cyaml_tag_directive_t tags[CYAML_MAX_TAG_DIRECTIVES];
    uint8_t tag_count;

    uint8_t flags; //!< Document flags (cyaml_doc_flags_t)

    struct {
        cyaml_span_t* items;
        uint32_t count;
        uint32_t cap;
    }* comments;
};

// #endregion

// #region Stream

//! [9.2] Stream - contains one or more documents
//! l-yaml-stream ::= l-document-prefix* l-any-document? ...
//! Source is always borrowed - caller manages lifetime
typedef struct {
    const char* src; //!< Source buffer (borrowed, must outlive stream)
    uint32_t src_len; //!< Source length

    cyaml_doc_t** docs; //!< Array of documents
    uint32_t count; //!< Number of documents
    uint32_t cap; //!< Capacity
} cyaml_stream_t;

// #endregion

// #region Error

typedef enum {
    CYAML_OK = 0,
    CYAML_ERR_NOMEM,
    CYAML_ERR_SYNTAX,
    CYAML_ERR_EOF,
    CYAML_ERR_INDENT,
    CYAML_ERR_ESCAPE,
    CYAML_ERR_ANCHOR,
    CYAML_ERR_ALIAS,
    CYAML_ERR_TAG,
    CYAML_ERR_DUP_KEY,
    CYAML_ERR_IO
} cyaml_err_t;

typedef struct {
    cyaml_err_t code;
    cyaml_span_t span; //!< Error location
    char msg[128];
} cyaml_error_t;

// #endregion

// #region Parse Options

typedef struct {
    bool dup_keys; //!< Allow duplicate keys (default: false)
    bool preserve_comments; //!< Preserve comments (default: false)
    uint32_t max_depth; //!< Max nesting depth (0 = unlimited)
    uint32_t max_size; //!< Max source size (0 = unlimited)
    cyaml_spec_t spec; //!< YAML spec version (default: auto-detect)
} cyaml_opts_t;

//! Default options
#define CYAML_OPTS_DEFAULT ((cyaml_opts_t) { false, false, 1000, 0, CYAML_SPEC_AUTO })

// #endregion

// #region Emit Options

typedef struct {
    uint8_t indent; //!< Indentation (default: 2)
    uint8_t width; //!< Line width, 0 = no wrap (default: 80)
    bool doc_start; //!< Emit --- (default: false)
    bool doc_end; //!< Emit ... (default: false)
    bool preserve_comments; //!< Emit comments if available (default: false)
    bool preserve_style; //!< Preserve original node styles (default: false = convert flow to block)
    cyaml_style_t style; //!< Default scalar style
    cyaml_coll_t coll; //!< Default collection style
} cyaml_emit_opts_t;

#define CYAML_EMIT_DEFAULT ((cyaml_emit_opts_t) { 2, 80, false, false, false, false, CYAML_PLAIN, CYAML_BLOCK })
#define CYAML_DUMP_DEFAULT ((cyaml_emit_opts_t) { 2, 0, false, false, false, false, CYAML_PLAIN, CYAML_BLOCK })

// #endregion

// #region Parsing

//! Parse first document from YAML string
//! @param src   Source buffer (must outlive returned doc)
//! @param len   Source length
//! @param opts  Parse options (NULL for defaults)
//! @param err   Error output (NULL to ignore)
//! @return Document or NULL on error
CYAML_API cyaml_doc_t* cyaml_parse(const char* src, size_t len,
    const cyaml_opts_t* opts, cyaml_error_t* err);

//! Free document and all nodes (does not free source buffer)
//! @param doc  Document to free (NULL safe)
CYAML_API void cyaml_free(cyaml_doc_t* doc);

// #endregion

// #region Stream Parsing

//! Parse YAML stream containing multiple documents
//! @param src   Source buffer (must outlive returned stream)
//! @param len   Source length
//! @param opts  Parse options (NULL for defaults)
//! @param err   Error output (NULL to ignore)
//! @return Stream or NULL on error
CYAML_API cyaml_stream_t* cyaml_parse_stream(const char* src, size_t len,
    const cyaml_opts_t* opts, cyaml_error_t* err);

//! Free stream and all documents (does not free source buffer)
//! @param stream  Stream to free (NULL safe)
CYAML_API void cyaml_stream_free(cyaml_stream_t* stream);

//! Get document count
static inline uint32_t cyaml_stream_count(const cyaml_stream_t* s)
{
    return s ? s->count : 0;
}

//! Get document at index
static inline cyaml_doc_t* cyaml_stream_doc(const cyaml_stream_t* s, uint32_t i)
{
    return (s && i < s->count) ? s->docs[i] : NULL;
}

// #endregion

// #region Document Access

//! Get root node
static inline cyaml_node_t* cyaml_root(const cyaml_doc_t* doc)
{
    return doc ? doc->root : NULL;
}

//! Get source buffer (works for both parsed and built docs)
static inline const char* cyaml_src(const cyaml_doc_t* doc)
{
    if (!doc)
        return NULL;
    return (doc->mode == CYAML_PARSING) ? doc->src.borrow.ptr : doc->src.owned.ptr;
}

//! Get source length
static inline uint32_t cyaml_src_len(const cyaml_doc_t* doc)
{
    if (!doc)
        return 0;
    return (doc->mode == CYAML_PARSING) ? doc->src.borrow.len : doc->src.owned.len;
}

// #endregion

// #region Span Utilities

//! Get pointer to span data
static inline const char* cyaml_span_ptr(const cyaml_doc_t* doc, cyaml_span_t s)
{
    const char* src = cyaml_src(doc);
    return (src && s.len) ? src + s.off : NULL;
}

//! Duplicate span as null-terminated string
//! @param doc  Document containing the span
//! @param s    Span to duplicate
//! @return Allocated string (caller must free) or NULL
CYAML_API char* cyaml_span_dup(const cyaml_doc_t* doc, cyaml_span_t s);

//! Get scalar value as string with all YAML processing applied
//! Handles escape sequences, line folding, and block scalar indent stripping.
//! @param doc  Document containing the node
//! @param n    Scalar node
//! @return Allocated string (caller must free) or NULL
CYAML_API char* cyaml_scalar_str(const cyaml_doc_t* doc, const cyaml_node_t* n);

//! Compare span to string (case-sensitive)
//! @param doc  Document containing the span
//! @param s    Span to compare
//! @param str  String to compare against
//! @return true if equal
CYAML_API bool cyaml_span_eq(const cyaml_doc_t* doc, cyaml_span_t s, const char* str);

//! Compare span to string (case-insensitive)
//! @param doc  Document containing the span
//! @param s    Span to compare
//! @param str  String to compare against
//! @return true if equal (ignoring case)
CYAML_API bool cyaml_span_ieq(const cyaml_doc_t* doc, cyaml_span_t s, const char* str);

//! Compare two spans for equality
//! @param doc  Document containing the spans
//! @param a    First span
//! @param b    Second span
//! @return true if equal
CYAML_API bool cyaml_span_cmp(const cyaml_doc_t* doc, cyaml_span_t a, cyaml_span_t b);

// #endregion

// #region Node Type Checks

static inline bool cyaml_is_null(const cyaml_node_t* n)
{
    return !n || n->type == CYAML_NULL || n->type == CYAML_NONE;
}
static inline bool cyaml_is_scalar(const cyaml_node_t* n)
{
    return n && n->type == CYAML_SCALAR;
}
static inline bool cyaml_is_seq(const cyaml_node_t* n)
{
    return n && n->type == CYAML_SEQ;
}
static inline bool cyaml_is_map(const cyaml_node_t* n)
{
    return n && n->type == CYAML_MAP;
}
static inline bool cyaml_is_alias(const cyaml_node_t* n)
{
    return n && n->type == CYAML_ALIAS;
}

// #endregion

// #region Scalar Value Extraction

//! Get scalar value span
static inline cyaml_span_t cyaml_val(const cyaml_node_t* n)
{
    return (n && n->type == CYAML_SCALAR) ? n->span : (cyaml_span_t) { 0 };
}

//! Get scalar as string pointer (not null-terminated!)
static inline const char* cyaml_str(const cyaml_doc_t* doc, const cyaml_node_t* n)
{
    return (n && n->type == CYAML_SCALAR) ? cyaml_span_ptr(doc, n->span) : NULL;
}

//! Get scalar length
static inline uint32_t cyaml_len(const cyaml_node_t* n)
{
    return (n && n->type == CYAML_SCALAR) ? n->span.len : 0;
}

//! Parse scalar as signed integer
//! Supports decimal, hex (0x), and octal (0o) formats.
//! @param doc  Document containing the node
//! @param n    Scalar node to parse
//! @param out  Output value
//! @return true on success
CYAML_API bool cyaml_as_int(const cyaml_doc_t* doc, const cyaml_node_t* n, int64_t* out);

//! Parse scalar as unsigned integer
//! Supports decimal, hex (0x), and octal (0o) formats.
//! @param doc  Document containing the node
//! @param n    Scalar node to parse
//! @param out  Output value
//! @return true on success
CYAML_API bool cyaml_as_uint(const cyaml_doc_t* doc, const cyaml_node_t* n, uint64_t* out);

//! Parse scalar as floating point
//! Supports .inf, -.inf, and .nan special values.
//! @param doc  Document containing the node
//! @param n    Scalar node to parse
//! @param out  Output value
//! @return true on success
CYAML_API bool cyaml_as_float(const cyaml_doc_t* doc, const cyaml_node_t* n, double* out);

//! Parse scalar as boolean
//! Recognizes true/false (case-insensitive).
//! @param doc  Document containing the node
//! @param n    Scalar node to parse
//! @param out  Output value
//! @return true on success
CYAML_API bool cyaml_as_bool(const cyaml_doc_t* doc, const cyaml_node_t* n, bool* out);

//! Check if scalar represents null
//! Recognizes ~, null, Null, NULL, or empty value.
//! @param doc  Document containing the node
//! @param n    Node to check
//! @return true if null
CYAML_API bool cyaml_is_null_val(const cyaml_doc_t* doc, const cyaml_node_t* n);

//! Infer scalar value kind using YAML Core Schema rules
//! For plain scalars, determines if value is null, bool, int, float, or string.
//! Quoted scalars are always treated as strings.
//! @param doc  Document containing the node
//! @param n    Scalar node to analyze
//! @return Inferred kind (CYAML_KIND_STRING if not scalar or quoted)
CYAML_API cyaml_scalar_kind_t cyaml_scalar_kind(const cyaml_doc_t* doc, const cyaml_node_t* n);

// #endregion

// #region Sequence Access

//! Get sequence length
static inline uint32_t cyaml_seq_len(const cyaml_node_t* n)
{
    return (n && n->type == CYAML_SEQ) ? n->seq.count : 0;
}

//! Get sequence item at index
static inline cyaml_node_t* cyaml_seq_get(const cyaml_node_t* n, uint32_t i)
{
    return (n && n->type == CYAML_SEQ && i < n->seq.count) ? n->seq.items[i] : NULL;
}

// #endregion

// #region Mapping Access

//! Get mapping size
static inline uint32_t cyaml_map_len(const cyaml_node_t* n)
{
    return (n && n->type == CYAML_MAP) ? n->map.count : 0;
}

//! Get mapping pair at index
static inline cyaml_pair_t* cyaml_map_at(const cyaml_node_t* n, uint32_t i)
{
    return (n && n->type == CYAML_MAP && i < n->map.count) ? &n->map.pairs[i] : NULL;
}

//! Get mapping value by string key
//! @param doc  Document containing the node
//! @param n    Mapping node
//! @param key  Key to look up
//! @return Value node or NULL if not found
CYAML_API cyaml_node_t* cyaml_get(const cyaml_doc_t* doc, const cyaml_node_t* n, const char* key);

//! Check if mapping contains key
//! @param doc  Document containing the node
//! @param n    Mapping node
//! @param key  Key to check
//! @return true if key exists
CYAML_API bool cyaml_has(const cyaml_doc_t* doc, const cyaml_node_t* n, const char* key);

// #endregion

// #region Path Access

//! Navigate to node at YPATH
//! Uses YPATH syntax: /store/books[0]/title, /users/*/name, etc.
//! Absolute paths (starting with /) evaluate from document root.
//! @param doc   Document to query
//! @param path  YPATH expression (e.g., "/users[0]/name", "/config/db/host")
//! @return First matching node or NULL if not found
CYAML_API cyaml_node_t* cyaml_path(const cyaml_doc_t* doc, const char* path);

// #endregion

// #region Iteration

//! Iterate sequence: for (uint32_t i = 0; i < cyaml_seq_len(n); i++) { cyaml_seq_get(n, i) }
//! Iterate mapping: for (uint32_t i = 0; i < cyaml_map_len(n); i++) { cyaml_map_at(n, i) }

// Iteration macros
#define CYAML_EACH_SEQ(n, item, i) \
    for (uint32_t i = 0; i < cyaml_seq_len(n) && ((item) = cyaml_seq_get(n, i), 1); i++)

#define CYAML_EACH_MAP(n, pair, i) \
    for (uint32_t i = 0; i < cyaml_map_len(n) && ((pair) = cyaml_map_at(n, i), 1); i++)

// #endregion

// #region Building

//! Create empty document for building YAML programmatically
//! @return New document or NULL on allocation failure
CYAML_API cyaml_doc_t* cyaml_doc_new(void);

//! Allocate node of specified type from document pool
//! @param doc   Document to allocate from
//! @param type  Node type
//! @return New node or NULL on failure
CYAML_API cyaml_node_t* cyaml_node_new(cyaml_doc_t* doc, cyaml_type_t type);

//! Create null node
//! @param doc  Document to allocate from
//! @return New null node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_null(cyaml_doc_t* doc);

//! Create scalar node from string with explicit length
//! @param doc  Document to allocate from
//! @param str  String data (copied)
//! @param len  String length
//! @return New scalar node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_str(cyaml_doc_t* doc, const char* str, size_t len);

//! Create scalar node from null-terminated string
//! @param doc  Document to allocate from
//! @param str  Null-terminated string (copied)
//! @return New scalar node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_cstr(cyaml_doc_t* doc, const char* str);

//! Create scalar node from signed integer
//! @param doc  Document to allocate from
//! @param val  Integer value
//! @return New scalar node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_int(cyaml_doc_t* doc, int64_t val);

//! Create scalar node from unsigned integer
//! @param doc  Document to allocate from
//! @param val  Unsigned value
//! @return New scalar node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_uint(cyaml_doc_t* doc, uint64_t val);

//! Create scalar node from floating point
//! @param doc  Document to allocate from
//! @param val  Float value
//! @return New scalar node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_float(cyaml_doc_t* doc, double val);

//! Create scalar node from boolean
//! @param doc  Document to allocate from
//! @param val  Boolean value
//! @return New scalar node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_bool(cyaml_doc_t* doc, bool val);

//! Create empty sequence node
//! @param doc  Document to allocate from
//! @return New sequence node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_seq(cyaml_doc_t* doc);

//! Create empty mapping node
//! @param doc  Document to allocate from
//! @return New mapping node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_map(cyaml_doc_t* doc);

//! Append item to sequence
//! @param seq   Sequence node
//! @param item  Item to append
//! @return true on success
CYAML_API bool cyaml_seq_push(cyaml_node_t* seq, cyaml_node_t* item);

//! Set or update mapping key-value pair
//! @param doc  Document containing the map
//! @param map  Mapping node
//! @param key  Key string (creates new scalar key)
//! @param val  Value node
//! @return true on success
CYAML_API bool cyaml_map_set(cyaml_doc_t* doc, cyaml_node_t* map,
    const char* key, cyaml_node_t* val);

//! Set document root
static inline void cyaml_set_root(cyaml_doc_t* doc, cyaml_node_t* root)
{
    if (doc)
        doc->root = root;
}

// #endregion

// #region Emitting

//! Emit document to newly allocated YAML string
//! @param doc   Document to emit
//! @param opts  Emit options (NULL for defaults: 2-space indent, 80 char width)
//! @param len   Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_emit(const cyaml_doc_t* doc, const cyaml_emit_opts_t* opts, size_t* len);

//! Emit single node to newly allocated YAML string
//! @param doc   Document containing the node
//! @param node  Node to emit
//! @param opts  Emit options (NULL for defaults)
//! @param len   Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_emit_node(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const cyaml_emit_opts_t* opts, size_t* len);

//! Emit stream to YAML preserving original formatting
//! @param stream  Stream to emit
//! @param len     Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_stream_emit(const cyaml_stream_t* stream, size_t* len);

// #endregion

// #region Dump (Canonical Output)

//! Dump document to canonical YAML format
//! Uses yaml-test-suite out.yaml format conventions.
//! @param doc  Document to dump
//! @param len  Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_dump(const cyaml_doc_t* doc, size_t* len);

//! Dump stream to canonical YAML format (all documents)
//! @param stream  Stream to dump
//! @param len     Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_stream_dump(const cyaml_stream_t* stream, size_t* len);

// #endregion

// #region Event Stream

//! Emit event stream for single document
//! Format: +STR, +DOC, +SEQ, +MAP, =VAL, -MAP, -SEQ, -DOC, -STR
//! @param doc     Document to emit events for
//! @param indent  Add visual indentation (false for yaml-test-suite format)
//! @param len     Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_events(const cyaml_doc_t* doc, bool indent, size_t* len);

//! Emit event stream for full stream (all documents)
//! @param stream  Stream to emit events for
//! @param indent  Add visual indentation
//! @param len     Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_stream_events(const cyaml_stream_t* stream, bool indent, size_t* len);

// #endregion

// #region JSON Output

//! Convert document to JSON
//! @param doc     Document to convert
//! @param indent  Spaces per indent level (0 = compact)
//! @param len     Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_json(const cyaml_doc_t* doc, int indent, size_t* len);

//! Convert stream to JSON
//! Single document streams output the JSON value directly.
//! Multi-document streams output as a JSON array.
//! @param stream  Stream to convert
//! @param indent  Spaces per indent level (0 = compact)
//! @param len     Output length (NULL to ignore)
//! @return Allocated string (caller must free) or NULL on error
CYAML_API char* cyaml_stream_json(const cyaml_stream_t* stream, int indent, size_t* len);

// #endregion

// #region Anchor and Alias Management

//! Get anchor name from node
//! @param doc   Document containing the node
//! @param node  Node to check
//! @return Anchor name pointer (not null-terminated) or NULL if none
static inline const char* cyaml_anchor(const cyaml_doc_t* doc, const cyaml_node_t* node)
{
    return (node && node->anchor.len) ? cyaml_span_ptr(doc, node->anchor) : NULL;
}

//! Get anchor name length
//! @param node  Node to check
//! @return Anchor length or 0 if none
static inline uint32_t cyaml_anchor_len(const cyaml_node_t* node)
{
    return node ? node->anchor.len : 0;
}

//! Set anchor name on a node
//! @param doc     Document containing the node (must be in BUILDING mode)
//! @param node    Node to anchor
//! @param anchor  Anchor name (without &), NULL to clear
//! @return true on success
CYAML_API bool cyaml_set_anchor(cyaml_doc_t* doc, cyaml_node_t* node, const char* anchor);

//! Create alias node pointing to an anchored node
//! @param doc     Document to allocate in
//! @param target  Target node (must have anchor set)
//! @return New alias node or NULL on failure
CYAML_API cyaml_node_t* cyaml_new_alias(cyaml_doc_t* doc, cyaml_node_t* target);

//! Find node by anchor name (searches from root)
//! @param doc     Document to search
//! @param anchor  Anchor name to find
//! @return Node with anchor or NULL if not found
CYAML_API cyaml_node_t* cyaml_find_anchor(const cyaml_doc_t* doc, const char* anchor);

//! Resolve all aliases in document (replace with deep copies of targets)
//! @param doc  Document to resolve
//! @return true on success, false if unresolved aliases remain
CYAML_API bool cyaml_resolve_aliases(cyaml_doc_t* doc);

// #endregion

// #region Node Copy and Merge

//! Deep copy a node into a document
//! Copies node and all children. Scalar content is copied into target doc.
//! @param doc   Target document to allocate into
//! @param src   Source document containing the node
//! @param node  Source node to copy
//! @return New node or NULL on failure
CYAML_API cyaml_node_t* cyaml_node_copy(cyaml_doc_t* doc, const cyaml_doc_t* src, const cyaml_node_t* node);

//! Deep merge src map into dst map
//! For conflicting keys: scalars replaced, maps merged recursively, seqs replaced.
//! @param doc  Document containing both nodes
//! @param dst  Destination map (modified in place)
//! @param src  Source map to merge from
//! @return true on success
CYAML_API bool cyaml_map_merge(cyaml_doc_t* doc, cyaml_node_t* dst, const cyaml_node_t* src);

// #endregion

// #region Key Sorting

//! Key comparison function type
//! @param doc  Document containing nodes
//! @param a    First key node
//! @param b    Second key node
//! @return <0 if a<b, 0 if equal, >0 if a>b
typedef int (*cyaml_key_cmp_t)(const cyaml_doc_t* doc,
    const cyaml_node_t* a,
    const cyaml_node_t* b);

//! Sort mapping keys
//! @param doc  Document containing the map
//! @param map  Mapping node to sort
//! @param cmp  Comparison function (NULL for default alphabetical)
//! @return true on success
CYAML_API bool cyaml_map_sort(const cyaml_doc_t* doc, cyaml_node_t* map, cyaml_key_cmp_t cmp);

//! Sort mapping keys recursively (all nested maps)
//! @param doc   Document containing the node
//! @param node  Starting node (will recurse into children)
//! @param cmp   Comparison function (NULL for default alphabetical)
//! @return true on success
CYAML_API bool cyaml_map_sort_recursive(const cyaml_doc_t* doc, cyaml_node_t* node, cyaml_key_cmp_t cmp);

// #endregion

// #region Comment Access

//! Get number of comments (0 if opts.comments was false)
static inline uint32_t cyaml_comment_count(const cyaml_doc_t* doc)
{
    return (doc && doc->comments) ? doc->comments->count : 0;
}

//! Get comment span at index
static inline cyaml_span_t cyaml_comment_at(const cyaml_doc_t* doc, uint32_t i)
{
    if (doc && doc->comments && i < doc->comments->count)
        return doc->comments->items[i];
    return (cyaml_span_t) { 0 };
}

// #endregion

// #region Path-based Scanf/Printf API

//! Extract values using path-based scanf
//!
//! Format: `"/path1 %fmt /path2 %fmt ..."` where paths and formats alternate.
//! Paths use YPATH syntax.
//!
//! **Format specifiers:**
//! - `%d`, `%i` - int
//! - `%u` - unsigned int
//! - `%ld`, `%li` - long
//! - `%lu` - unsigned long
//! - `%lld`, `%lli` - long long (int64_t)
//! - `%llu` - unsigned long long (uint64_t)
//! - `%f` - float
//! - `%lf` - double
//! - `%Ns` - string (N = buffer size, e.g. `%255s`)
//! - `%b` - bool
//! - `%n` - cyaml_node_t**
//!
//! @param doc     Document to query
//! @param format  Path+format string (space-separated)
//! @param ...     Output pointers for each format specifier
//! @return Number of successfully extracted values, -1 on format error
//!
//! **Example:**
//! ```c
//! unsigned int port;
//! char host[256];
//! cyaml_scanf(doc, "/server/port %u /server/host %255s", &port, host);
//! ```
CYAML_API int cyaml_scanf(const cyaml_doc_t* doc, const char* format, ...);

//! Extract values using path-based scanf (va_list version)
//! @param doc     Document to query
//! @param format  Path+format string (space-separated)
//! @param ap      Variable argument list
//! @return Number of successfully extracted values, -1 on format error
CYAML_API int cyaml_vscanf(const cyaml_doc_t* doc, const char* format, va_list ap);

//! Extract values starting from a specific node
//! Relative paths start from node, absolute paths (/) still use root.
//! @param doc     Document containing the node
//! @param node    Starting node for relative paths (NULL = root)
//! @param format  Path+format string
//! @param ...     Output pointers
//! @return Number of successfully extracted values
CYAML_API int cyaml_node_scanf(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* format, ...);

//! Extract values starting from a specific node (va_list version)
//! @param doc     Document containing the node
//! @param node    Starting node for relative paths (NULL = root)
//! @param format  Path+format string
//! @param ap      Variable argument list
//! @return Number of successfully extracted values
CYAML_API int cyaml_node_vscanf(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* format, va_list ap);

//! Build node from printf-style format string
//! Creates nodes by substituting values into a YAML template.
//! Formats: %d, %u, %ld, %lu, %lld, %llu, %f, %lf, %s, %b
//! @param doc     Document to allocate in
//! @param format  YAML with format specifiers
//! @param ...     Values to substitute
//! @return New node or NULL on error
//! @example cyaml_buildf(doc, "port: %d", 8080)         // -> {port: 8080}
//! @example cyaml_buildf(doc, "- %s\n- %s", "a", "b")   // -> [a, b]
//! @example cyaml_buildf(doc, "%s", "hello")            // -> "hello"
CYAML_API cyaml_node_t* cyaml_buildf(cyaml_doc_t* doc, const char* format, ...);

//! Build node from printf-style format string (va_list version)
//! @param doc     Document to allocate in
//! @param format  YAML with format specifiers
//! @param ap      Variable argument list
//! @return New node or NULL on error
CYAML_API cyaml_node_t* cyaml_vbuildf(cyaml_doc_t* doc, const char* format, va_list ap);

// #endregion

// #region Path-based Modification

//! Insert or update node at path
//! Creates intermediate maps as needed. For existing keys, replaces value.
//! Path must resolve to a valid insertion point.
//! @param doc   Document to modify (must be CYAML_BUILDING mode or parsed)
//! @param path  Target path (e.g., "/server/port", "/users/0/name")
//! @param val   Value node to insert (must be from same doc)
//! @return true on success
//! @example cyaml_insert_at(doc, "/server/timeout", cyaml_new_int(doc, 30));
CYAML_API bool cyaml_insert_at(cyaml_doc_t* doc, const char* path, cyaml_node_t* val);

//! Insert printf-built node at path (convenience)
//! Equivalent to: cyaml_insert_at(doc, path, cyaml_buildf(doc, format, ...))
//! @param doc     Document to modify
//! @param path    Target path
//! @param format  YAML format string
//! @param ...     Values to substitute
//! @return true on success
//! @example cyaml_insertf(doc, "/server", "timeout: %d", 30);
CYAML_API bool cyaml_insertf(cyaml_doc_t* doc, const char* path, const char* format, ...);

//! Delete node at path
//! Removes the node from its parent container.
//! @param doc   Document to modify
//! @param path  Path to node to delete
//! @return true if deleted, false if not found
CYAML_API bool cyaml_delete_at(cyaml_doc_t* doc, const char* path);

//! Append to sequence at path
//! Path must resolve to a sequence node.
//! @param doc   Document to modify
//! @param path  Path to sequence
//! @param val   Value to append
//! @return true on success
CYAML_API bool cyaml_append_at(cyaml_doc_t* doc, const char* path, cyaml_node_t* val);

//! Append printf-built node to sequence at path
//! Path must resolve to a sequence node.
//! @param doc     Document to modify
//! @param path    Path to sequence
//! @param format  YAML format string for new item
//! @param ...     Values to substitute
//! @return true on success
CYAML_API bool cyaml_appendf(cyaml_doc_t* doc, const char* path, const char* format, ...);

// #endregion

// #region Node Comparison

//! Compare scalar node value to string (like strcmp)
//! @param doc   Document containing node
//! @param node  Node to compare (must be scalar)
//! @param str   String to compare against
//! @param len   String length (0 = use strlen)
//! @return 0 if equal, <0 if node < str, >0 if node > str
CYAML_API int cyaml_node_cmp_str(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* str, size_t len);

//! Check if scalar node equals string
//! @param doc   Document containing node
//! @param node  Node to compare
//! @param str   String to compare (null-terminated)
//! @return true if node value equals str
CYAML_API bool cyaml_node_eq_str(const cyaml_doc_t* doc, const cyaml_node_t* node,
    const char* str);

// #endregion

// #region Utility

//! Get human-readable error message
//! @param err  Error code
//! @return Static error string
CYAML_API const char* cyaml_strerror(cyaml_err_t err);

//! Get library version string
//! @return Static version string (e.g., "0.1.0")
CYAML_API const char* cyaml_version(void);

// #endregion

// #region YPATH Query API

//! Query result containing matched nodes or error info
typedef struct {
    cyaml_node_t** nodes; //!< Array of pointers to matched nodes (owned by doc)
    uint32_t count; //!< Number of matched nodes
    const char* error; //!< Error message (NULL on success)
    uint32_t error_pos; //!< Position in path where error occurred
} cyaml_path_result_t;

//! Execute a YPATH query and return all matching nodes
//! Nodes returned are pointers into the document - no data is copied.
//! @param doc      Document to query
//! @param context  Context node for relative paths (NULL = use root)
//! @param path     YPATH expression (e.g., "/users[0]/name", "/items[?@.active]")
//! @return Result with matched nodes or error info
CYAML_API cyaml_path_result_t cyaml_path_query(const cyaml_doc_t* doc,
    const cyaml_node_t* context,
    const char* path);

//! Free query result
//! @param result  Result to free (NULL safe)
CYAML_API void cyaml_path_result_free(cyaml_path_result_t* result);

//! Get first matching node (convenience wrapper)
//! Equivalent to cyaml_path_query + taking first result + free.
//! @param doc      Document to query
//! @param context  Context node for relative paths (NULL = use root)
//! @param path     YPATH expression
//! @return First matching node or NULL on error/no match
CYAML_API cyaml_node_t* cyaml_path_first(const cyaml_doc_t* doc,
    const cyaml_node_t* context,
    const char* path);

//! Get result count
static inline uint32_t cyaml_path_count(const cyaml_path_result_t* r)
{
    return r ? r->count : 0;
}

//! Get result node at index
static inline cyaml_node_t* cyaml_path_get(const cyaml_path_result_t* r, uint32_t i)
{
    return (r && i < r->count) ? r->nodes[i] : NULL;
}

//! Debug: dump tokens and parsed AST to stdout
//! @param path  YPATH expression to analyze
CYAML_API void cyaml_path_debug(const char* path);

// #endregion

#ifdef __cplusplus
}
#endif

#endif // CYAML_H
