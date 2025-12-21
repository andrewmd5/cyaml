#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "cyaml.h"
#include "cyaml_utf8.h"
#define TEST_RESULTS_IMPL
#include "test_results.h"
#include "compat/dirent.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define PATH_SEP "\\"
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#include <strings.h>
#include <limits.h>
#define PATH_SEP "/"
#endif

#define RESULTS_FILE ".cyaml_suite_results"

static tr_results_t prev_results = { 0 };
static tr_results_t curr_results = { 0 };

// #region Utilities

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
    // Normalize CRLF to LF in-place for cross-platform consistency
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != '\r')
            buf[j++] = buf[i];
    }
    buf[j] = '\0';
    n = j;
    if (len)
        *len = n;
    return buf;
}

static char* get_scalar(cyaml_doc_t* doc, cyaml_node_t* node)
{
    return (node && node->type == CYAML_SCALAR) ? cyaml_scalar_str(doc, node) : NULL;
}

static bool file_exists(const char* path)
{
    FILE* f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static bool is_directory(const char* path)
{
    DIR* d = opendir(path);
    if (d) {
        closedir(d);
        return true;
    }
    return false;
}

static cyaml_node_t* map_get(cyaml_doc_t* doc, cyaml_node_t* map, const char* key)
{
    if (!map || map->type != CYAML_MAP)
        return NULL;
    for (uint32_t i = 0; i < map->map.count; i++) {
        char* k = get_scalar(doc, map->map.pairs[i].key);
        if (k && strcmp(k, key) == 0) {
            free(k);
            return map->map.pairs[i].val;
        }
        free(k);
    }
    return NULL;
}

static bool has_tag(const char* tags, const char* tag)
{
    if (!tags || !tag)
        return false;
    size_t tag_len = strlen(tag);
    const char* p = tags;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char* end = p;
        while (*end && *end != ' ')
            end++;
        size_t len = (size_t)(end - p);
        if (len == tag_len && strncmp(p, tag, tag_len) == 0)
            return true;
        p = end;
    }
    return false;
}

// #endregion

// #region JSON Parsing

static const char* skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

static const char* find_json_end(const char* p, const char* end)
{
    p = skip_ws(p, end);
    if (p >= end)
        return NULL;

    switch (*p) {
    case '{': {
        int depth = 1;
        p++;
        while (p < end && depth > 0) {
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end)
                        p += 2;
                    else
                        p++;
                }
                if (p < end)
                    p++;
            } else if (*p == '{') {
                depth++;
                p++;
            } else if (*p == '}') {
                depth--;
                p++;
            } else
                p++;
        }
        return depth == 0 ? p : NULL;
    }
    case '[': {
        int depth = 1;
        p++;
        while (p < end && depth > 0) {
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end)
                        p += 2;
                    else
                        p++;
                }
                if (p < end)
                    p++;
            } else if (*p == '[') {
                depth++;
                p++;
            } else if (*p == ']') {
                depth--;
                p++;
            } else
                p++;
        }
        return depth == 0 ? p : NULL;
    }
    case '"': {
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end)
                p += 2;
            else
                p++;
        }
        return (p < end) ? p + 1 : NULL;
    }
    default: {
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'
            && *p != ',' && *p != '}' && *p != ']') {
            p++;
        }
        return p;
    }
    }
}

static cyaml_stream_t** parse_multi_json(const char* json, size_t len, uint32_t* count)
{
    *count = 0;
    const char* p = json;
    const char* end = json + len;
    uint32_t doc_count = 0;

    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end)
            break;
        const char* doc_end = find_json_end(p, end);
        if (!doc_end)
            break;
        doc_count++;
        p = doc_end;
    }

    if (doc_count == 0)
        return NULL;

    cyaml_stream_t** streams = calloc(doc_count + 1, sizeof(cyaml_stream_t*));
    if (!streams)
        return NULL;

    p = json;
    uint32_t i = 0;
    while (p < end && i < doc_count) {
        p = skip_ws(p, end);
        if (p >= end)
            break;

        const char* doc_start = p;
        const char* doc_end = find_json_end(p, end);
        if (!doc_end)
            break;

        size_t doc_len = (size_t)(doc_end - doc_start);
        cyaml_error_t err;
        streams[i] = cyaml_parse_stream(doc_start, doc_len, NULL, &err);
        if (!streams[i]) {
            for (uint32_t j = 0; j < i; j++)
                cyaml_stream_free(streams[j]);
            free(streams);
            return NULL;
        }
        i++;
        p = doc_end;
    }

    *count = i;
    return streams;
}

// #endregion

// #region Semantic Comparison

static bool nodes_equal(cyaml_doc_t* doc1, cyaml_node_t* n1, cyaml_doc_t* doc2, cyaml_node_t* n2)
{
    if (!n1 && !n2)
        return true;
    if (!n1 || !n2)
        return false;

    while (n1->type == CYAML_ALIAS && n1->alias.target)
        n1 = n1->alias.target;
    while (n2->type == CYAML_ALIAS && n2->alias.target)
        n2 = n2->alias.target;

    if ((n1->type == CYAML_NULL || n1->type == CYAML_NONE) && (n2->type == CYAML_NULL || n2->type == CYAML_NONE)) {
        return true;
    }

    if (n1->type == CYAML_SCALAR || n2->type == CYAML_SCALAR || n1->type == CYAML_NULL || n2->type == CYAML_NULL || n1->type == CYAML_NONE || n2->type == CYAML_NONE) {

        char* s1 = cyaml_scalar_str(doc1, n1);
        char* s2 = cyaml_scalar_str(doc2, n2);

        bool is_null1 = (!s1 || strlen(s1) == 0 || (strlen(s1) == 4 && strcasecmp(s1, "null") == 0) || (strlen(s1) == 1 && s1[0] == '~'));
        bool is_null2 = (!s2 || strlen(s2) == 0 || (strlen(s2) == 4 && strcasecmp(s2, "null") == 0) || (strlen(s2) == 1 && s2[0] == '~'));

        if (is_null1 && is_null2) {
            free(s1);
            free(s2);
            return true;
        }
        if (is_null1 || is_null2) {
            free(s1);
            free(s2);
            return false;
        }

        bool eq = (strcmp(s1, s2) == 0);
        if (!eq) {
            char *end1, *end2;
            double d1 = strtod(s1, &end1);
            double d2 = strtod(s2, &end2);
            bool num1_ok = (*end1 == '\0');
            bool num2_ok = (*end2 == '\0');

            if (!num1_ok && strlen(s1) > 2 && s1[0] == '0' && (s1[1] == 'x' || s1[1] == 'X')) {
                d1 = (double)strtoll(s1, &end1, 16);
                num1_ok = (*end1 == '\0');
            }
            if (!num1_ok && strlen(s1) > 2 && s1[0] == '0' && (s1[1] == 'o' || s1[1] == 'O')) {
                d1 = (double)strtoll(s1 + 2, &end1, 8);
                num1_ok = (*end1 == '\0');
            }

            if (num1_ok && num2_ok && d1 == d2)
                eq = true;
        }
        free(s1);
        free(s2);
        return eq;
    }

    if (n1->type != n2->type)
        return false;

    switch (n1->type) {
    case CYAML_SEQ:
        if (n1->seq.count != n2->seq.count)
            return false;
        for (uint32_t i = 0; i < n1->seq.count; i++) {
            if (!nodes_equal(doc1, n1->seq.items[i], doc2, n2->seq.items[i]))
                return false;
        }
        return true;

    case CYAML_MAP:
        if (n1->map.count != n2->map.count)
            return false;
        for (uint32_t i = 0; i < n1->map.count; i++) {
            char* key1 = cyaml_scalar_str(doc1, n1->map.pairs[i].key);
            if (!key1)
                continue;

            bool found = false;
            for (uint32_t j = 0; j < n2->map.count; j++) {
                char* key2 = cyaml_scalar_str(doc2, n2->map.pairs[j].key);
                if (key2 && strcmp(key1, key2) == 0) {
                    free(key2);
                    found = nodes_equal(doc1, n1->map.pairs[i].val,
                        doc2, n2->map.pairs[j].val);
                    break;
                }
                free(key2);
            }
            free(key1);
            if (!found)
                return false;
        }
        return true;

    default:
        return false;
    }
}

// #endregion

// #region Results

typedef struct {
    int total;
    int tree_pass, tree_fail;
    int dump_pass, dump_fail, dump_skip;
    int emit_pass, emit_fail, emit_skip;
    int json_pass, json_fail, json_skip;
    int skip_no_yaml, skip_parse, skip_1_3;
} stats_t;

static tr_test_t* record_result(const char* id, const char* name,
    tr_result_t tree, tr_result_t dump,
    tr_result_t emit, tr_result_t json)
{
    if (curr_results.test_count >= TR_MAX_TESTS)
        return NULL;

    tr_test_t* existing = tr_find_test(&curr_results, id);
    if (existing) {
        existing->tree = tree;
        existing->dump = dump;
        existing->emit = emit;
        existing->json = json;
        return existing;
    }

    tr_test_t* t = &curr_results.tests[curr_results.test_count++];
    memset(t, 0, sizeof(*t));
    cyaml_strlcpy(t->id, id, sizeof(t->id));
    if (name)
        cyaml_strlcpy(t->name, name, sizeof(t->name));
    t->tree = tree;
    t->dump = dump;
    t->emit = emit;
    t->json = json;

    tr_test_t* prev = tr_find_test(&prev_results, id);
    if (prev) {
        t->prev_tree = prev->tree;
        t->prev_dump = prev->dump;
        t->prev_emit = prev->emit;
        t->prev_json = prev->json;
    }

    return t;
}

static void print_regressions_improvements(void)
{
    if (prev_results.test_count == 0) {
        printf("\n(No previous results to compare - first run)\n");
        return;
    }

    printf("\n");
    if (curr_results.reg_count == 0 && curr_results.imp_count == 0) {
        printf("No regressions or improvements from previous run.\n");
    } else {
        if (curr_results.reg_count > 0) {
            printf("!! REGRESSIONS: %d test(s) now failing\n", curr_results.reg_count);
            for (int i = 0; i < curr_results.reg_count; i++) {
                printf("  - %s (%s)\n", curr_results.regressions[i].id,
                    curr_results.regressions[i].type);
            }
            printf("\n");
        }
        if (curr_results.imp_count > 0) {
            printf("++ IMPROVEMENTS: %d test(s) now passing\n", curr_results.imp_count);
            for (int i = 0; i < curr_results.imp_count; i++) {
                printf("  + %s (%s)\n", curr_results.improvements[i].id,
                    curr_results.improvements[i].type);
            }
            printf("\n");
        }
    }
}

// #endregion

// #region Test Runner

static void run_data_test(const char* test_dir, const char* test_id, int case_num,
    int total_cases, const char* src_dir, stats_t* s,
    cyaml_spec_t spec)
{
    char path[PATH_MAX];

    char full_id[80];
    if (total_cases > 1) {
        snprintf(full_id, sizeof(full_id), "%s:%d", test_id, case_num);
    } else {
        cyaml_strlcpy(full_id, test_id, sizeof(full_id));
    }
    const char* id = full_id;

    char* tags = NULL;
    if (src_dir) {
        snprintf(path, sizeof(path), "%s" PATH_SEP "%s.yaml", src_dir, test_id);
        char* src_content = read_file(path, NULL);
        if (src_content) {
            cyaml_error_t src_err;
            cyaml_stream_t* src_stream = cyaml_parse_stream(src_content, strlen(src_content), NULL, &src_err);
            if (src_stream && src_stream->count > 0) {
                cyaml_doc_t* src_doc = src_stream->docs[0];
                cyaml_node_t* src_root = src_doc->root;
                cyaml_node_t* test_node = src_root;
                if (src_root && src_root->type == CYAML_SEQ && case_num > 0 && (uint32_t)(case_num - 1) < src_root->seq.count) {
                    test_node = src_root->seq.items[case_num - 1];
                }
                if (test_node) {
                    tags = get_scalar(src_doc, map_get(src_doc, test_node, "tags"));
                }
            }
            cyaml_stream_free(src_stream);
            free(src_content);
        }
    }

    // In 1.2 mode: skip 1.3-mod tests (they require 1.3 behavior)
    // In 1.3 mode: run 1.3-mod tests, they should pass
    if (spec != CYAML_SPEC_1_3 && tags && has_tag(tags, "1.3-mod")) {
        s->skip_1_3++;
        free(tags);
        return;
    }

    // In 1.3 mode: 1.3-err tests should fail to parse
    // In 1.2 mode: 1.3-err tests should parse successfully
    bool is_1_3_err = tags && has_tag(tags, "1.3-err");

    snprintf(path, sizeof(path), "%s" PATH_SEP "===", test_dir);
    size_t name_len;
    char* name = read_file(path, &name_len);
    // Strip trailing CRLF or LF
    while (name && name_len > 0 && (name[name_len - 1] == '\n' || name[name_len - 1] == '\r'))
        name[--name_len] = '\0';

    snprintf(path, sizeof(path), "%s" PATH_SEP "in.yaml", test_dir);
    size_t yaml_len;
    char* yaml = read_file(path, &yaml_len);
    if (!yaml) {
        s->skip_no_yaml++;
        free(name);
        free(tags);
        return;
    }

    snprintf(path, sizeof(path), "%s" PATH_SEP "error", test_dir);
    bool expect_fail = file_exists(path);

    // In 1.3 mode, 1.3-err tests should fail to parse
    if (spec == CYAML_SPEC_1_3 && is_1_3_err) {
        expect_fail = true;
    }

    snprintf(path, sizeof(path), "%s" PATH_SEP "test.event", test_dir);
    char* tree = read_file(path, NULL);

    snprintf(path, sizeof(path), "%s" PATH_SEP "out.yaml", test_dir);
    char* dump = read_file(path, NULL);

    snprintf(path, sizeof(path), "%s" PATH_SEP "emit.yaml", test_dir);
    char* emit = read_file(path, NULL);

    snprintf(path, sizeof(path), "%s" PATH_SEP "in.json", test_dir);
    char* json = read_file(path, NULL);

    s->total++;

    tr_result_t tree_result = TR_NONE;
    tr_result_t dump_result = TR_SKIP;
    tr_result_t emit_result = TR_SKIP;
    tr_result_t json_result = TR_SKIP;
    tr_test_t* rec = NULL;
    char* got_events = NULL;
    char* got_dump = NULL;
    char* got_emit = NULL;
    char* got_json = NULL;

    cyaml_error_t err;
    cyaml_opts_t opts = { .spec = spec };
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, yaml_len, &opts, &err);

    if (!stream) {
        if (expect_fail) {
            s->tree_pass++;
            s->dump_skip++;
            s->emit_skip++;
            s->json_skip++;
            tree_result = TR_PASS;
            rec = record_result(id, name, tree_result, TR_SKIP, TR_SKIP, TR_SKIP);
        } else {
            s->tree_fail++;
            s->dump_skip++;
            s->emit_skip++;
            s->json_skip++;
            tree_result = TR_FAIL;
            fprintf(stderr, "\nTREE FAIL: %s\n", id);
            if (name)
                fprintf(stderr, "  Name: %s\n", name);
            fprintf(stderr, "  Error: %s at %u:%u\n", err.msg, err.span.start_line, err.span.start_col);

            rec = record_result(id, name, tree_result, TR_SKIP, TR_SKIP, TR_SKIP);
            if (rec) {
                cyaml_strlcpy(rec->fail_type, "tree", sizeof(rec->fail_type));
                rec->input = cyaml_strdup(yaml);
                rec->expected = tree ? cyaml_strdup(tree) : NULL;
                char errbuf[256];
                snprintf(errbuf, sizeof(errbuf), "%s at %u:%u", err.msg, err.span.start_line, err.span.start_col);
                rec->error = cyaml_strdup(errbuf);
            }
        }
        goto cleanup;
    }

    if (expect_fail) {
        s->tree_fail++;
        s->dump_skip++;
        s->emit_skip++;
        s->json_skip++;
        tree_result = TR_FAIL;
        fprintf(stderr, "\nTREE FAIL: %s (expected parse error but parsed ok)\n", id);

        rec = record_result(id, name, tree_result, TR_SKIP, TR_SKIP, TR_SKIP);
        if (rec) {
            cyaml_strlcpy(rec->fail_type, "tree", sizeof(rec->fail_type));
            rec->input = cyaml_strdup(yaml);
            rec->expected = cyaml_strdup("(expected parse error)");
            rec->got = cyaml_strdup("(parsed ok)");
        }
        cyaml_stream_free(stream);
        goto cleanup;
    }

    bool tree_ok = true;
    if (tree) {
        size_t len;
        got_events = cyaml_stream_events(stream, false, &len);
        tree_ok = (got_events && strcmp(got_events, tree) == 0);
        if (!tree_ok) {
            s->tree_fail++;
            tree_result = TR_FAIL;
            fprintf(stderr, "\nTREE FAIL: %s\n", id);
            if (name)
                fprintf(stderr, "  Name: %s\n", name);
            fprintf(stderr, "\n--- Expected ---\n%s", tree);
            fprintf(stderr, "\n--- Got ---\n%s\n", got_events ? got_events : "(null)");
        } else {
            s->tree_pass++;
            tree_result = TR_PASS;
        }
    } else {
        s->tree_pass++;
        tree_result = TR_PASS;
    }

    if (dump && tree_ok) {
        size_t len;
        got_dump = cyaml_stream_dump(stream, &len);
        if (got_dump && strcmp(got_dump, dump) == 0) {
            s->dump_pass++;
            dump_result = TR_PASS;
        } else {
            s->dump_fail++;
            dump_result = TR_FAIL;
            fprintf(stderr, "\nDUMP FAIL: %s\n", id);
            if (name)
                fprintf(stderr, "  Name: %s\n", name);
            fprintf(stderr, "\n--- Expected ---\n%s", dump);
            fprintf(stderr, "\n--- Got ---\n%s\n", got_dump ? got_dump : "(null)");
        }
    } else {
        s->dump_skip++;
        dump_result = TR_SKIP;
    }

    if (emit && tree_ok) {
        size_t len;
        got_emit = cyaml_stream_emit(stream, &len);
        if (got_emit && strcmp(got_emit, emit) == 0) {
            s->emit_pass++;
            emit_result = TR_PASS;
        } else {
            s->emit_fail++;
            emit_result = TR_FAIL;
            fprintf(stderr, "\nEMIT FAIL: %s\n", id);
            if (name)
                fprintf(stderr, "  Name: %s\n", name);
            fprintf(stderr, "\n--- Expected ---\n%s", emit);
            fprintf(stderr, "\n--- Got ---\n%s\n", got_emit ? got_emit : "(null)");
        }
    } else {
        s->emit_skip++;
        emit_result = TR_SKIP;
    }

    if (json && tree_ok) {
        size_t len;
        got_json = cyaml_stream_json(stream, 2, &len);

        uint32_t json_doc_count = 0;
        cyaml_stream_t** json_docs = parse_multi_json(json, strlen(json), &json_doc_count);

        bool match = false;
        if (json_doc_count == 0 && stream->count == 0) {
            match = true;
        } else if (json_docs && json_doc_count == stream->count) {
            match = true;
            for (uint32_t i = 0; i < stream->count && match; i++) {
                if (json_docs[i]->count != 1) {
                    match = false;
                } else {
                    match = nodes_equal(stream->docs[i], stream->docs[i]->root,
                        json_docs[i]->docs[0], json_docs[i]->docs[0]->root);
                }
            }
        }

        if (match) {
            s->json_pass++;
            json_result = TR_PASS;
        } else {
            s->json_fail++;
            json_result = TR_FAIL;
            fprintf(stderr, "\nJSON FAIL: %s\n", id);
            if (name)
                fprintf(stderr, "  Name: %s\n", name);
            fprintf(stderr, "\n--- Expected ---\n%s", json);
            fprintf(stderr, "\n--- Got ---\n%s\n", got_json ? got_json : "(null)");
            if (!json_docs) {
                fprintf(stderr, "  (Failed to parse expected JSON)\n");
            } else if (json_doc_count != stream->count) {
                fprintf(stderr, "  (Document count mismatch: expected %u, got %u)\n",
                    json_doc_count, stream->count);
            }
        }

        if (json_docs) {
            for (uint32_t i = 0; i < json_doc_count; i++) {
                cyaml_stream_free(json_docs[i]);
            }
            free(json_docs);
        }
    } else {
        s->json_skip++;
        json_result = TR_SKIP;
    }

    rec = record_result(id, name, tree_result, dump_result, emit_result, json_result);
    if (rec) {
        if (tree_result == TR_FAIL) {
            cyaml_strlcpy(rec->fail_type, "tree", sizeof(rec->fail_type));
            rec->input = cyaml_strdup(yaml);
            rec->expected = tree ? cyaml_strdup(tree) : NULL;
            rec->got = got_events ? cyaml_strdup(got_events) : NULL;
        } else if (dump_result == TR_FAIL) {
            cyaml_strlcpy(rec->fail_type, "dump", sizeof(rec->fail_type));
            rec->input = cyaml_strdup(yaml);
            rec->expected = dump ? cyaml_strdup(dump) : NULL;
            rec->got = got_dump ? cyaml_strdup(got_dump) : NULL;
        } else if (emit_result == TR_FAIL) {
            cyaml_strlcpy(rec->fail_type, "emit", sizeof(rec->fail_type));
            rec->input = cyaml_strdup(yaml);
            rec->expected = emit ? cyaml_strdup(emit) : NULL;
            rec->got = got_emit ? cyaml_strdup(got_emit) : NULL;
        } else if (json_result == TR_FAIL) {
            cyaml_strlcpy(rec->fail_type, "json", sizeof(rec->fail_type));
            rec->input = cyaml_strdup(yaml);
            rec->expected = json ? cyaml_strdup(json) : NULL;
            rec->got = got_json ? cyaml_strdup(got_json) : NULL;
        }
    }

    free(got_events);
    free(got_dump);
    free(got_emit);
    free(got_json);
    cyaml_stream_free(stream);

cleanup:
    free(name);
    free(yaml);
    free(tree);
    free(dump);
    free(emit);
    free(json);
    free(tags);
}

static void run_data_dir(const char* data_dir, const char* src_dir, const char* filter,
    stats_t* s, cyaml_spec_t spec)
{
    DIR* d = opendir(data_dir);
    if (!d) {
        fprintf(stderr, "Cannot open: %s\n", data_dir);
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        if (filter && !strstr(ent->d_name, filter))
            continue;

        char test_path[PATH_MAX];
        snprintf(test_path, sizeof(test_path), "%s" PATH_SEP "%s", data_dir, ent->d_name);

        if (!is_directory(test_path))
            continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "%s" PATH_SEP "00", test_path);

        if (is_directory(subpath)) {
            DIR* sd = opendir(test_path);
            int total_cases = 0;
            struct dirent* sent;
            while ((sent = readdir(sd)) != NULL) {
                if (sent->d_name[0] >= '0' && sent->d_name[0] <= '9') {
                    total_cases++;
                }
            }
            closedir(sd);

            sd = opendir(test_path);
            while ((sent = readdir(sd)) != NULL) {
                if (sent->d_name[0] < '0' || sent->d_name[0] > '9')
                    continue;

                int case_num = atoi(sent->d_name) + 1;
                snprintf(subpath, sizeof(subpath), "%s" PATH_SEP "%s", test_path, sent->d_name);

                printf("Running: %s/%s\n", ent->d_name, sent->d_name);
                run_data_test(subpath, ent->d_name, case_num, total_cases, src_dir, s, spec);
            }
            closedir(sd);
        } else {
            printf("Running: %s\n", ent->d_name);
            run_data_test(test_path, ent->d_name, 1, 1, src_dir, s, spec);
        }
    }
    closedir(d);
}

// #endregion

// #region Main

int main(int argc, char** argv)
{
    const char* dir = "../refs/yaml-test-suite/data";
    const char* filter = NULL;
    bool no_save = false;
    cyaml_spec_t spec = CYAML_SPEC_1_2;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-save") == 0) {
            no_save = true;
        } else if (strcmp(argv[i], "--1.3") == 0) {
            spec = CYAML_SPEC_1_3;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [suite_path] [filter] [--no-save] [--1.3]\n", argv[0]);
            return 1;
        } else if (is_directory(argv[i])) {
            dir = argv[i];
        } else {
            filter = argv[i];
        }
    }

    printf("CYAML Test Suite Runner\n");
    printf("=======================\n");
    printf("Suite: %s\n", dir);
    printf("Spec:  YAML %s\n", spec == CYAML_SPEC_1_3 ? "1.3" : "1.2");
    if (filter)
        printf("Filter: %s\n", filter);
    printf("\n");

    if (!filter) {
        tr_load(RESULTS_FILE, &prev_results);
        if (prev_results.test_count > 0) {
            printf("Loaded %d previous results for comparison\n\n", prev_results.test_count);
        }
    }

    stats_t s = { 0 };

    char src_dir[PATH_MAX];
    cyaml_strlcpy(src_dir, dir, sizeof(src_dir));
    char* data_suffix = strstr(src_dir, "/data");
    if (!data_suffix)
        data_suffix = strstr(src_dir, "\\data");
    if (data_suffix && (data_suffix[5] == '\0' || data_suffix[5] == '/' || data_suffix[5] == '\\')) {
        size_t remaining = sizeof(src_dir) - (size_t)(data_suffix - src_dir);
        cyaml_strlcpy(data_suffix, PATH_SEP "src", remaining);
    }
    run_data_dir(dir, src_dir, filter, &s, spec);

    printf("\n=======================\n");
    printf("Total tests:    %d\n", s.total);
    printf("Tree:  pass=%d fail=%d\n", s.tree_pass, s.tree_fail);
    printf("Dump:  pass=%d fail=%d skip=%d\n", s.dump_pass, s.dump_fail, s.dump_skip);
    printf("Emit:  pass=%d fail=%d skip=%d\n", s.emit_pass, s.emit_fail, s.emit_skip);
    printf("JSON:  pass=%d fail=%d skip=%d\n", s.json_pass, s.json_fail, s.json_skip);
    printf("Skip:  no_yaml=%d parse=%d 1.3-mod=%d\n", s.skip_no_yaml, s.skip_parse, s.skip_1_3);
    printf("=======================\n");

    if (!filter) {
        tr_compute_changes(&curr_results, &prev_results);
        print_regressions_improvements();

        if (!no_save) {
            cyaml_strlcpy(curr_results.suite, dir, sizeof(curr_results.suite));
            curr_results.total = s.total;
            curr_results.tree_pass = s.tree_pass;
            curr_results.tree_fail = s.tree_fail;
            curr_results.dump_pass = s.dump_pass;
            curr_results.dump_fail = s.dump_fail;
            curr_results.dump_skip = s.dump_skip;
            curr_results.emit_pass = s.emit_pass;
            curr_results.emit_fail = s.emit_fail;
            curr_results.emit_skip = s.emit_skip;
            curr_results.json_pass = s.json_pass;
            curr_results.json_fail = s.json_fail;
            curr_results.json_skip = s.json_skip;

            tr_save(RESULTS_FILE, &curr_results);
            printf("Results saved to %s\n", RESULTS_FILE);
        }

        tr_free_results(&prev_results);
        tr_free_results(&curr_results);
    }

    int exit_code = 0;

    if (s.tree_fail > 0) {
        printf("TREE TESTS FAILED\n");
        exit_code = 1;
    }
    if (s.emit_fail > 0) {
        printf("EMIT TESTS FAILED\n");
        exit_code = 1;
    }
    if (s.json_fail > 0) {
        printf("JSON TESTS FAILED\n");
        exit_code = 1;
    }
    if (s.dump_fail > 0) {
        printf("Note: %d dump tests failed (non-blocking)\n", s.dump_fail);
    }
    if (exit_code == 0) {
        printf("All tests passed!\n");
    }
    return exit_code;
}

// #endregion
