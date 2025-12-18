// AFL fuzzing harness for cyaml parser
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
        if (len == 0 || len > 1024 * 1024) break;

        cyaml_error_t err;

        cyaml_doc_t *doc = cyaml_parse((const char *)buf, len, NULL, &err);
        if (doc) {
            cyaml_node_t *root = cyaml_root(doc);
            if (root) {
                char *yaml = cyaml_emit(doc, NULL, NULL);
                free(yaml);

                char *dump = cyaml_dump(doc, NULL);
                free(dump);

                char *json = cyaml_json(doc, 0, NULL);
                free(json);

                char *events = cyaml_events(doc, false, NULL);
                free(events);
            }
            cyaml_free(doc);
        }

        cyaml_stream_t *stream = cyaml_parse_stream((const char *)buf, len, NULL, &err);
        if (stream) {
            for (uint32_t i = 0; i < cyaml_stream_count(stream); i++) {
                cyaml_doc_t *d = cyaml_stream_doc(stream, i);
                if (d && cyaml_root(d)) {
                    char *yaml = cyaml_emit(d, NULL, NULL);
                    free(yaml);
                }
            }
            cyaml_stream_free(stream);
        }
#ifdef __AFL_FUZZ_TESTCASE_LEN
    }
#else
    } while (0);
    free(buf);
#endif
    return 0;
}
