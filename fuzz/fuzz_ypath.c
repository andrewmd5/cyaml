#include "cyaml.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __AFL_FUZZ_TESTCASE_LEN
__AFL_FUZZ_INIT();
#endif

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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    cyaml_error_t err;
    cyaml_doc_t *doc = cyaml_parse(sample_yaml, strlen(sample_yaml), NULL, &err);
    if (!doc)
        return 1;

#ifdef __AFL_FUZZ_TESTCASE_LEN
    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    char *path = NULL;

    while (__AFL_LOOP(10000)) {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
        if (len == 0 || len > 4096)
            continue;

        path = realloc(path, len + 1);
        if (!path)
            continue;
        memcpy(path, buf, len);
        path[len] = '\0';

        cyaml_path(doc, path);
        cyaml_path_first(doc, cyaml_root(doc), path);

        cyaml_path_result_t result = cyaml_path_query(doc, NULL, path);
        for (uint32_t i = 0; i < result.count; i++)
            cyaml_path_get(&result, i);
        cyaml_path_result_free(&result);
    }
    free(path);
#else
    char *path = NULL;
    size_t len = 0;
    size_t cap = 0;
    char tmp[4096];
    ssize_t n;

    while ((n = read(STDIN_FILENO, tmp, sizeof(tmp))) > 0) {
        if (len + (size_t)n > cap) {
            cap = cap ? cap * 2 : 4096;
            if (cap < len + (size_t)n)
                cap = len + (size_t)n;
            path = realloc(path, cap);
            if (!path) {
                cyaml_free(doc);
                return 1;
            }
        }
        memcpy(path + len, tmp, (size_t)n);
        len += (size_t)n;
    }

    if (len == 0 || len > 4096) {
        free(path);
        cyaml_free(doc);
        return 0;
    }

    path = realloc(path, len + 1);
    path[len] = '\0';

    cyaml_path(doc, path);
    cyaml_path_first(doc, cyaml_root(doc), path);

    cyaml_path_result_t result = cyaml_path_query(doc, NULL, path);
    for (uint32_t i = 0; i < result.count; i++)
        cyaml_path_get(&result, i);
    cyaml_path_result_free(&result);

    free(path);
#endif
    cyaml_free(doc);
    return 0;
}
