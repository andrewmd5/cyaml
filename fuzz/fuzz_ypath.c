// AFL fuzzing harness for YPATH queries
#include "cyaml.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#ifdef __AFL_FUZZ_TESTCASE_LEN
__AFL_FUZZ_INIT();
#endif

// Sample YAML document for path queries
static const char *sample_yaml =
    "users:\n"
    "  - name: alice\n"
    "    age: 30\n"
    "    active: true\n"
    "    tags: [admin, user]\n"
    "  - name: bob\n"
    "    age: 25\n"
    "    active: false\n"
    "    tags: [user]\n"
    "config:\n"
    "  server:\n"
    "    host: localhost\n"
    "    port: 8080\n"
    "  debug: true\n"
    "items: [1, 2, 3, 4, 5]\n"
    "matrix:\n"
    "  - [1, 2, 3]\n"
    "  - [4, 5, 6]\n";

int main(int argc, char **argv) {
    cyaml_error_t err;
    cyaml_doc_t *doc = cyaml_parse(sample_yaml, strlen(sample_yaml), NULL, &err);
    if (!doc) return 1;

#ifdef __AFL_FUZZ_TESTCASE_LEN
    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
#else
    unsigned char *buf = NULL;
    size_t len = 0;

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { cyaml_free(doc); return 1; }
        fseek(f, 0, SEEK_END);
        len = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = malloc(len + 1);
        if (!buf) { fclose(f); cyaml_free(doc); return 1; }
        fread(buf, 1, len, f);
        buf[len] = 0;
        fclose(f);
    } else {
        cyaml_free(doc);
        return 1;
    }
    do {
#endif
        if (len == 0 || len > 4096) break;

        char *path = malloc(len + 1);
        if (!path) break;
        memcpy(path, buf, len);
        path[len] = '\0';

        cyaml_node_t *node = cyaml_path(doc, path);
        (void)node;

        node = cyaml_path_first(doc, cyaml_root(doc), path);
        (void)node;

        cyaml_path_result_t result = cyaml_path_query(doc, NULL, path);
        if (result.count > 0) {
            for (uint32_t i = 0; i < result.count; i++) {
                cyaml_node_t *n = cyaml_path_get(&result, i);
                (void)n;
            }
        }
        cyaml_path_result_free(&result);

        free(path);
#ifdef __AFL_FUZZ_TESTCASE_LEN
    }
#else
    } while (0);
    free(buf);
#endif
    cyaml_free(doc);
    return 0;
}
