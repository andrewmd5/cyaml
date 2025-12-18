#include "cyaml.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
} stats_t;

static char* read_file(const char* path, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

static bool nodes_match(const cyaml_doc_t* doc, const cyaml_node_t* actual,
    const cyaml_doc_t* expected_doc, const cyaml_node_t* expected)
{
    if (!actual && !expected)
        return true;
    if (!actual || !expected)
        return false;
    if (actual->type != expected->type)
        return false;

    const char* src = cyaml_src(doc);
    const char* exp_src = cyaml_src(expected_doc);

    switch (actual->type) {
    case CYAML_NULL:
        return true;
    case CYAML_SCALAR: {
        if (actual->span.len != expected->span.len)
            return false;
        return memcmp(src + actual->span.off, exp_src + expected->span.off,
                   actual->span.len)
            == 0;
    }
    case CYAML_SEQ:
        if (actual->seq.count != expected->seq.count)
            return false;
        for (uint32_t i = 0; i < actual->seq.count; i++) {
            if (!nodes_match(doc, actual->seq.items[i], expected_doc, expected->seq.items[i])) {
                return false;
            }
        }
        return true;
    case CYAML_MAP:
        if (actual->map.count != expected->map.count)
            return false;
        for (uint32_t i = 0; i < actual->map.count; i++) {
            if (!nodes_match(doc, actual->map.pairs[i].key, expected_doc, expected->map.pairs[i].key) || !nodes_match(doc, actual->map.pairs[i].val, expected_doc, expected->map.pairs[i].val)) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

static bool result_matches(const cyaml_doc_t* doc, const cyaml_path_result_t* actual,
    const cyaml_doc_t* test_doc, const cyaml_node_t* expected)
{
    if (!expected || expected->type != CYAML_SEQ)
        return false;

    if (actual->count != expected->seq.count)
        return false;

    for (uint32_t i = 0; i < actual->count; i++) {
        if (!nodes_match(doc, actual->nodes[i], test_doc, expected->seq.items[i])) {
            return false;
        }
    }
    return true;
}

static void run_test(const cyaml_doc_t* suite_doc, const cyaml_node_t* test,
    stats_t* stats, bool verbose)
{
    stats->total++;

    cyaml_node_t* id_node = cyaml_get(suite_doc, test, "id");
    cyaml_node_t* desc_node = cyaml_get(suite_doc, test, "description");
    cyaml_node_t* doc_node = cyaml_get(suite_doc, test, "document");
    cyaml_node_t* path_node = cyaml_get(suite_doc, test, "path");
    cyaml_node_t* result_node = cyaml_get(suite_doc, test, "result");
    cyaml_node_t* error_node = cyaml_get(suite_doc, test, "error");

    char id[64] = { 0 };
    char desc[256] = { 0 };
    char path[256] = { 0 };

    if (id_node && id_node->type == CYAML_SCALAR) {
        size_t len = id_node->span.len < 63 ? id_node->span.len : 63;
        memcpy(id, cyaml_src(suite_doc) + id_node->span.off, len);
    }
    if (desc_node && desc_node->type == CYAML_SCALAR) {
        size_t len = desc_node->span.len < 255 ? desc_node->span.len : 255;
        memcpy(desc, cyaml_src(suite_doc) + desc_node->span.off, len);
    }
    if (path_node && path_node->type == CYAML_SCALAR) {
        size_t len = path_node->span.len < 255 ? path_node->span.len : 255;
        memcpy(path, cyaml_src(suite_doc) + path_node->span.off, len);
    }

    if (!doc_node || !path_node) {
        printf("SKIP  %-20s %s (missing document or path)\n", id, desc);
        stats->skipped++;
        return;
    }

    size_t emit_len;
    char* doc_yaml = cyaml_emit_node(suite_doc, doc_node, NULL, &emit_len);
    if (!doc_yaml) {
        printf("SKIP  %-20s %s (emit failed)\n", id, desc);
        stats->skipped++;
        return;
    }

    cyaml_error_t err;
    cyaml_doc_t* test_doc = cyaml_parse(doc_yaml, emit_len, NULL, &err);
    if (!test_doc) {
        printf("SKIP  %-20s %s (parse failed: %s)\n", id, desc, cyaml_strerror(err.code));
        free(doc_yaml);
        stats->skipped++;
        return;
    }

    cyaml_path_result_t result = cyaml_path_query(test_doc, NULL, path);

    bool passed = false;

    if (error_node) {
        passed = (result.error != NULL);
        if (!passed && verbose) {
            printf("       Expected error but got success\n");
        }
    } else if (result_node) {
        if (result.error) {
            passed = false;
            if (verbose) {
                printf("       Unexpected error: %s at pos %u\n", result.error, result.error_pos);
            }
        } else {
            passed = result_matches(test_doc, &result, suite_doc, result_node);
            if (!passed && verbose) {
                printf("       Got %u nodes, expected %u\n",
                    result.count, result_node->seq.count);
            }
        }
    }

    if (passed) {
        if (verbose) {
            printf("PASS  %-20s %s\n", id, desc);
        }
        stats->passed++;
    } else {
        printf("FAIL  %-20s %s\n", id, desc);
        if (verbose) {
            printf("       Path: %s\n", path);
        }
        stats->failed++;
    }

    cyaml_path_result_free(&result);
    cyaml_free(test_doc);
    free(doc_yaml);
}

static void run_suite(const cyaml_doc_t* doc, const cyaml_node_t* suite,
    stats_t* stats, bool verbose)
{
    cyaml_node_t* name_node = cyaml_get(doc, suite, "suite");
    cyaml_node_t* desc_node = cyaml_get(doc, suite, "description");
    cyaml_node_t* tests_node = cyaml_get(doc, suite, "tests");

    char name[64] = { 0 };
    char desc[256] = { 0 };

    if (name_node && name_node->type == CYAML_SCALAR) {
        size_t len = name_node->span.len < 63 ? name_node->span.len : 63;
        memcpy(name, cyaml_src(doc) + name_node->span.off, len);
    }
    if (desc_node && desc_node->type == CYAML_SCALAR) {
        size_t len = desc_node->span.len < 255 ? desc_node->span.len : 255;
        memcpy(desc, cyaml_src(doc) + desc_node->span.off, len);
    }

    printf("\n=== Suite: %s ===\n", name);
    if (desc[0])
        printf("    %s\n\n", desc);

    if (!tests_node || tests_node->type != CYAML_SEQ) {
        printf("    (no tests)\n");
        return;
    }

    for (uint32_t i = 0; i < tests_node->seq.count; i++) {
        run_test(doc, tests_node->seq.items[i], stats, verbose);
    }
}

int main(int argc, char** argv)
{
    const char* test_file = "../refs/ypath/tests.yml";
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else {
            test_file = argv[i];
        }
    }

    printf("YPATH Test Runner\n");
    printf("=================\n");
    printf("Test file: %s\n", test_file);

    size_t len;
    char* yaml = read_file(test_file, &len);
    if (!yaml) {
        fprintf(stderr, "Failed to read test file: %s\n", test_file);
        return 1;
    }

    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, len, NULL, &err);
    if (!stream) {
        fprintf(stderr, "Failed to parse test file: %s\n", cyaml_strerror(err.code));
        free(yaml);
        return 1;
    }

    stats_t stats = { 0 };

    for (uint32_t i = 0; i < stream->count; i++) {
        run_suite(stream->docs[i], cyaml_root(stream->docs[i]), &stats, verbose);
    }

    printf("\n=================\n");
    printf("Results: %d passed, %d failed, %d skipped (of %d total)\n",
        stats.passed, stats.failed, stats.skipped, stats.total);

    cyaml_stream_free(stream);
    free(yaml);

    return stats.failed > 0 ? 1 : 0;
}
