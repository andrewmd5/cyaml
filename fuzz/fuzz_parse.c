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
        if (len == 0 || len > 1024 * 1024)
            continue;

        src = realloc(src, len);
        if (!src)
            continue;
        memcpy(src, buf, len);

        cyaml_error_t err;
        cyaml_doc_t *doc = cyaml_parse(src, len, NULL, &err);
        if (doc) {
            cyaml_node_t *root = cyaml_root(doc);
            if (root) {
                free(cyaml_emit(doc, NULL, NULL));
                free(cyaml_dump(doc, NULL));
                free(cyaml_json(doc, 0, NULL));
                free(cyaml_events(doc, false, NULL));
            }
            cyaml_free(doc);
        }

        cyaml_stream_t *stream = cyaml_parse_stream(src, len, NULL, &err);
        if (stream) {
            for (uint32_t i = 0; i < cyaml_stream_count(stream); i++) {
                cyaml_doc_t *d = cyaml_stream_doc(stream, i);
                if (d && cyaml_root(d))
                    free(cyaml_emit(d, NULL, NULL));
            }
            cyaml_stream_free(stream);
        }
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

    if (len == 0 || len > 1024 * 1024) {
        free(src);
        return 0;
    }

    src = realloc(src, len);

    cyaml_error_t err;
    cyaml_doc_t *doc = cyaml_parse(src, len, NULL, &err);
    if (doc) {
        cyaml_node_t *root = cyaml_root(doc);
        if (root) {
            free(cyaml_emit(doc, NULL, NULL));
            free(cyaml_dump(doc, NULL));
            free(cyaml_json(doc, 0, NULL));
            free(cyaml_events(doc, false, NULL));
        }
        cyaml_free(doc);
    }

    cyaml_stream_t *stream = cyaml_parse_stream(src, len, NULL, &err);
    if (stream) {
        for (uint32_t i = 0; i < cyaml_stream_count(stream); i++) {
            cyaml_doc_t *d = cyaml_stream_doc(stream, i);
            if (d && cyaml_root(d))
                free(cyaml_emit(d, NULL, NULL));
        }
        cyaml_stream_free(stream);
    }

    free(src);
#endif
    return 0;
}
