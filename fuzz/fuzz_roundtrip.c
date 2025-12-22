#include "cyaml.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __AFL_FUZZ_TESTCASE_LEN
__AFL_FUZZ_INIT();
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef __AFL_FUZZ_TESTCASE_LEN
    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    char *src = NULL;

    while (__AFL_LOOP(10000)) {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
        if (len == 0 || len > 64 * 1024)
            continue;

        src = realloc(src, len);
        if (!src)
            continue;
        memcpy(src, buf, len);

        cyaml_error_t err;
        cyaml_doc_t *doc1 = cyaml_parse(src, len, NULL, &err);
        if (!doc1)
            continue;

        size_t emit_len;
        char *emitted = cyaml_emit(doc1, NULL, &emit_len);
        if (emitted) {
            cyaml_doc_t *doc2 = cyaml_parse(emitted, emit_len, NULL, &err);
            if (doc2) {
                free(cyaml_emit(doc2, NULL, NULL));
                cyaml_free(doc2);
            }
            free(emitted);
        }

        char *dumped = cyaml_dump(doc1, NULL);
        if (dumped) {
            cyaml_doc_t *doc3 = cyaml_parse(dumped, strlen(dumped), NULL, &err);
            if (doc3)
                cyaml_free(doc3);
            free(dumped);
        }

        cyaml_free(doc1);
    }
    free(src);
#else
    char *src = NULL;
    size_t len = 0;
    size_t cap = 0;
    char tmp[4096];
    ssize_t n;

    while ((n = read(STDIN_FILENO, tmp, sizeof(tmp))) > 0) {
        if (len + (size_t)n > cap) {
            cap = cap ? cap * 2 : 4096;
            if (cap < len + (size_t)n)
                cap = len + (size_t)n;
            src = realloc(src, cap);
            if (!src)
                return 1;
        }
        memcpy(src + len, tmp, (size_t)n);
        len += (size_t)n;
    }

    if (len == 0 || len > 64 * 1024) {
        free(src);
        return 0;
    }

    src = realloc(src, len);

    cyaml_error_t err;
    cyaml_doc_t *doc1 = cyaml_parse(src, len, NULL, &err);
    if (doc1) {
        size_t emit_len;
        char *emitted = cyaml_emit(doc1, NULL, &emit_len);
        if (emitted) {
            cyaml_doc_t *doc2 = cyaml_parse(emitted, emit_len, NULL, &err);
            if (doc2) {
                free(cyaml_emit(doc2, NULL, NULL));
                cyaml_free(doc2);
            }
            free(emitted);
        }

        char *dumped = cyaml_dump(doc1, NULL);
        if (dumped) {
            cyaml_doc_t *doc3 = cyaml_parse(dumped, strlen(dumped), NULL, &err);
            if (doc3)
                cyaml_free(doc3);
            free(dumped);
        }

        cyaml_free(doc1);
    }

    free(src);
#endif
    return 0;
}
