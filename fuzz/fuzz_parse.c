#define _GNU_SOURCE
#include <unistd.h>
#include "cyaml.c"
#include "cyaml_emitter.c"
#include "cyaml_events.c"
#include "cyaml_json.c"
#include "cyaml_modify.c"
#include "cyaml_parser.c"
#include "cyaml_path.c"
#include "cyaml_utf8.c"

__AFL_FUZZ_INIT();

int main(void)
{
    __AFL_INIT();
    char *src = 0;
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        src = realloc(src, len);
        memcpy(src, buf, len);
        cyaml_parse(src, len, 0, 0);
    }
}
