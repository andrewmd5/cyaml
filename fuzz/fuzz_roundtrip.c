// AFL fuzzing harness for parse -> emit -> parse roundtrip
#include "cyaml.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#ifdef __AFL_FUZZ_TESTCASE_LEN
__AFL_FUZZ_INIT();
#endif

int main(int argc, char **argv) {
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
        if (!f) return 1;
        fseek(f, 0, SEEK_END);
        len = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = malloc(len + 1);
        if (!buf) { fclose(f); return 1; }
        fread(buf, 1, len, f);
        buf[len] = 0;
        fclose(f);
    } else {
        return 1;
    }
    do {
#endif
        if (len == 0 || len > 64 * 1024) break;

        cyaml_error_t err;

        cyaml_doc_t *doc1 = cyaml_parse((const char *)buf, len, NULL, &err);
        if (!doc1) break;

        size_t emit_len;
        char *emitted = cyaml_emit(doc1, NULL, &emit_len);
        if (!emitted) {
            cyaml_free(doc1);
            break;
        }

        cyaml_doc_t *doc2 = cyaml_parse(emitted, emit_len, NULL, &err);
        if (doc2) {
            char *emitted2 = cyaml_emit(doc2, NULL, NULL);
            free(emitted2);
            cyaml_free(doc2);
        }

        free(emitted);

        char *dumped = cyaml_dump(doc1, NULL);
        if (dumped) {
            cyaml_doc_t *doc3 = cyaml_parse(dumped, strlen(dumped), NULL, &err);
            if (doc3) cyaml_free(doc3);
            free(dumped);
        }

        cyaml_free(doc1);
#ifdef __AFL_FUZZ_TESTCASE_LEN
    }
#else
    } while (0);
    free(buf);
#endif
    return 0;
}
