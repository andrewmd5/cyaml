# cyaml

A portable, fully compliant YAML 1.2 parser, emitter, and document manipulation library written in C11.

**[Documentation](https://cyaml.org)** | **[API Reference](https://cyaml.org/api/)** | **[YPATH Spec](https://cyaml.org/ypath/)**

## Rationale

The state of YAML parsing in C is not ideal. libyaml, the reference implementation used as a foundation for bindings in many languages, does not fully pass the official YAML test suite and has curious arbitrary limitations such as a 1024-character restriction on implicit keys. On the opposite end, libraries like libfyaml achieve better conformance but carry significant portability constraints, limiting the platforms they can target without substantial porting effort.

cyaml was written to support [Dawn](https://github.com/andrewmd5/dawn), which needs to parse YAML frontmatter and must remain portable to any platform. This library provides that portability while maintaining strict conformance to the YAML 1.2 specification.

## Features

- Full YAML 1.2 specification compliance
- Passes the complete [yaml-test-suite](https://github.com/yaml/yaml-test-suite) for parsing, JSON conversion, canonical dump, and emission
- Zero-copy parsing with span-based value access
- Document builder API for programmatic construction
- Path-based queries via [YPATH](#ypath), a query language for YAML
- JSON output
- Canonical dump format compatible with yaml-test-suite
- Event stream output
- Anchor and alias support with cycle detection
- scanf/printf-style extraction and building
- Path-based modification (insert, delete, append)
- No dependencies beyond the C standard library
- Builds on Windows, macOS, Linux, and BSD

## Building

```bash
mkdir build && cd build
cmake ..
make
```

Options:

| Option | Default | Description |
|--------|---------|-------------|
| `CYAML_BUILD_TESTS` | ON | Build test suite and tools |
| `CYAML_BUILD_SHARED` | ON | Build shared library |
| `CYAML_BUILD_STATIC` | ON | Build static library |
| `CYAML_ENABLE_LTO` | ON | Enable link-time optimization |

## Usage

### Parsing

```c
#include "cyaml.h"

const char *yaml = "name: cyaml\nversion: 1.0\n";
cyaml_error_t err;
cyaml_doc_t *doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);

if (!doc) {
    fprintf(stderr, "Parse error at line %u: %s\n", err.span.start_line, err.msg);
    return 1;
}

cyaml_node_t *root = cyaml_root(doc);
cyaml_node_t *name = cyaml_get(doc, root, "name");

char *value = cyaml_scalar_str(doc, name);
printf("name: %s\n", value);
free(value);

cyaml_free(doc);
```

### Building Documents

```c
cyaml_doc_t *doc = cyaml_doc_new();
cyaml_node_t *root = cyaml_new_map(doc);
cyaml_set_root(doc, root);

cyaml_map_set(doc, root, "host", cyaml_new_cstr(doc, "localhost"));
cyaml_map_set(doc, root, "port", cyaml_new_int(doc, 8080));

cyaml_node_t *features = cyaml_new_seq(doc);
cyaml_seq_push(features, cyaml_new_cstr(doc, "parsing"));
cyaml_seq_push(features, cyaml_new_cstr(doc, "emitting"));
cyaml_map_set(doc, root, "features", features);

char *output = cyaml_emit(doc, NULL, NULL);
printf("%s", output);
free(output);
cyaml_free(doc);
```

Output:

```yaml
host: localhost
port: 8080
features:
  - parsing
  - emitting
```

### scanf/printf Style

Extract values directly with path-based scanf:

```c
unsigned int port;
char host[256];
cyaml_scanf(doc, "/server/port %u /server/host %255s", &port, host);
```

Build nodes with printf syntax:

```c
cyaml_node_t *server = cyaml_buildf(doc, "host: %s\nport: %d", "localhost", 8080);
```

### JSON Conversion

```c
char *json = cyaml_json(doc, 2, NULL);  // 2-space indent
printf("%s\n", json);
free(json);
```

## YPATH

YPATH is a query language for traversing YAML documents, similar to JSONPath or XPath. The full specification is in [refs/ypath/spec.md](refs/ypath/spec.md).

### Quick Reference

| Syntax | Description |
|--------|-------------|
| `/` | Document root |
| `/foo` | Child named "foo" |
| `/foo/bar` | Nested child |
| `*` | All children |
| `**` | Recursive descent |
| `[0]` | First element |
| `[-1]` | Last element |
| `[0:3]` | Slice (first three) |
| `[?@.active]` | Filter (where active is truthy) |
| `[?@.price < 20]` | Filter with comparison |

### Examples

```c
// Simple path
cyaml_node_t *title = cyaml_path(doc, "/store/books[0]/title");

// Query with multiple results
cyaml_path_result_t result = cyaml_path_query(doc, NULL, "/users[?@.active]/name");
for (uint32_t i = 0; i < result.count; i++) {
    cyaml_node_t *name = cyaml_path_get(&result, i);
    // ...
}
cyaml_path_result_free(&result);

// Recursive search
cyaml_path_result_t prices = cyaml_path_query(doc, NULL, "/**/price");
```

## Performance

Speed is not the primary goal of cyaml, but it is by no means slow. Expect upwards of 220 MB/s when processing large documents, with minimal memory pressure.

## Testing

The test suite is self-validating: the test harness uses cyaml itself to parse the yaml-test-suite metadata. This provides a strong bootstrap guarantee.

```bash
cd build
./cyaml_suite ../refs/yaml-test-suite/data
```

The suite validates:
- **Parsing**: Documents parse without error for valid inputs, reject invalid inputs
- **JSON**: Output matches expected JSON conversion
- **Dump**: Canonical output matches expected format
- **Emit**: Re-parsing emitted output produces equivalent documents

Additional testing:
- `./ypath_test ../refs/ypath/tests.yml` validates the YPATH implementation
- `./test_api` runs API-level unit tests
- AFL fuzzing harnesses in `fuzz/` for parser, ypath, and roundtrip testing

## License

MIT
