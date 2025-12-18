#ifndef CYAML_INTERNAL_H
#define CYAML_INTERNAL_H

#include "cyaml.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "cyaml_utf8.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region Assertions

//! Release-safe assertion macro - always active, crashes on failure
//! Use for invariants that indicate bugs if violated
#define CYAML_ASSERT(cond, msg)                                       \
    do {                                                              \
        if (!(cond)) {                                                \
            fprintf(stderr, "CYAML ASSERT FAILED: %s\n  %s:%d: %s\n", \
                (msg), __FILE__, __LINE__, #cond);                    \
            abort();                                                  \
        }                                                             \
    } while (0)

//! Unreachable code marker - crashes if reached
#define CYAML_UNREACHABLE(msg)                              \
    do {                                                    \
        fprintf(stderr, "CYAML UNREACHABLE: %s\n  %s:%d\n", \
            (msg), __FILE__, __LINE__);                     \
        abort();                                            \
    } while (0)

//! Debug tracing - define CYAML_TRACE to enable
#ifdef CYAML_TRACE
#define TRACE(...) fprintf(stderr, "TRACE: " __VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

// #endregion

// #region Constants

#ifndef CYAML_VERSION_STR
#define CYAML_VERSION_STR "0.0.0-dev"
#endif
#define SEQ_INIT_CAP 8
#define MAP_INIT_CAP 8
#define EMIT_INIT_CAP 256

// Character constants
#define C_NUL '\0'
#define C_LF '\n'
#define C_CR '\r'
#define C_TAB '\t'
#define C_SP ' '
#define C_HASH '#'
#define C_BSLASH '\\'
#define C_TILDE '~'

// YAML Core Schema string constants
#define S_NULL "null"
#define S_TRUE "true"
#define S_FALSE "false"
#define S_TILDE "~"
#define S_NAN ".nan"
#define S_INF ".inf"
#define S_PINF "+.inf"
#define S_NINF "-.inf"

#define L_NULL 4
#define L_TRUE 4
#define L_FALSE 5
#define L_TILDE 1
#define L_NAN 4
#define L_INF 4
#define L_PINF 5
#define L_NINF 5

// #endregion

// #region Pool Allocation

//! Allocate node from document pool
cyaml_node_t* cyaml_pool_alloc(cyaml_doc_t* doc);

//! Append string to document's source buffer (for building)
bool cyaml_doc_append(cyaml_doc_t* doc, const char* str, size_t len, uint32_t* off);

// #endregion

#ifdef __cplusplus
}
#endif

#endif // CYAML_INTERNAL_H
