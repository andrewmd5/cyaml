#ifndef TEST_RESULTS_H
#define TEST_RESULTS_H

#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "cyaml_utf8.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Portable localtime wrapper
static inline struct tm* tr_localtime(const time_t* timer, struct tm* result)
{
#ifdef _WIN32
    return localtime_s(result, timer) == 0 ? result : NULL;
#else
    return localtime_r(timer, result);
#endif
}

#define TR_MAX_TESTS 1024
#define TR_MAX_CONTENT 8192

typedef enum {
    TR_NONE = 0,
    TR_PASS = 1,
    TR_FAIL = 2,
    TR_SKIP = 3
} tr_result_t;

typedef struct {
    char id[64];
    char name[256];
    tr_result_t tree;
    tr_result_t dump;
    tr_result_t emit;
    tr_result_t json;
    tr_result_t prev_tree;
    tr_result_t prev_dump;
    tr_result_t prev_emit;
    tr_result_t prev_json;
    char tree_regressed[32];
    char dump_regressed[32];
    char emit_regressed[32];
    char json_regressed[32];
    char fail_type[8];
    char* input;
    char* expected;
    char* got;
    char* error;
} tr_test_t;

typedef struct {
    char id[64];
    char type[8];
} tr_change_t;

typedef struct {
    char version[16];
    char date[32];
    char suite[256];
    tr_test_t tests[TR_MAX_TESTS];
    int test_count;
    int total;
    int tree_pass, tree_fail;
    int dump_pass, dump_fail, dump_skip;
    int emit_pass, emit_fail, emit_skip;
    int json_pass, json_fail, json_skip;
    tr_change_t regressions[TR_MAX_TESTS];
    int reg_count;
    tr_change_t improvements[TR_MAX_TESTS];
    int imp_count;
} tr_results_t;

const char* tr_result_str(tr_result_t r);
tr_result_t tr_parse_result(const char* s);
void tr_free_test(tr_test_t* t);
void tr_free_results(tr_results_t* r);
bool tr_load(const char* path, tr_results_t* r);
bool tr_save(const char* path, tr_results_t* r);
tr_test_t* tr_find_test(tr_results_t* r, const char* id);
void tr_compute_changes(tr_results_t* curr, tr_results_t* prev);

#endif // TEST_RESULTS_H

#ifdef TEST_RESULTS_IMPL

#include "cyaml.h"

const char* tr_result_str(tr_result_t r)
{
    switch (r) {
    case TR_PASS:
        return "pass";
    case TR_FAIL:
        return "fail";
    case TR_SKIP:
        return "skip";
    default:
        return "none";
    }
}

tr_result_t tr_parse_result(const char* s)
{
    if (!s)
        return TR_NONE;
    if (strcmp(s, "pass") == 0)
        return TR_PASS;
    if (strcmp(s, "fail") == 0)
        return TR_FAIL;
    if (strcmp(s, "skip") == 0)
        return TR_SKIP;
    return TR_NONE;
}

void tr_free_test(tr_test_t* t)
{
    free(t->input);
    t->input = NULL;
    free(t->expected);
    t->expected = NULL;
    free(t->got);
    t->got = NULL;
    free(t->error);
    t->error = NULL;
}

void tr_free_results(tr_results_t* r)
{
    for (int i = 0; i < r->test_count; i++) {
        tr_free_test(&r->tests[i]);
    }
    memset(r, 0, sizeof(*r));
}

tr_test_t* tr_find_test(tr_results_t* r, const char* id)
{
    for (int i = 0; i < r->test_count; i++) {
        if (strcmp(r->tests[i].id, id) == 0)
            return &r->tests[i];
    }
    return NULL;
}

//! Get string value from map key
static char* yaml_map_str(cyaml_doc_t* doc, cyaml_node_t* map, const char* key)
{
    if (!map || map->type != CYAML_MAP)
        return NULL;
    for (uint32_t i = 0; i < map->map.count; i++) {
        char* k = cyaml_scalar_str(doc, map->map.pairs[i].key);
        if (k && strcmp(k, key) == 0) {
            free(k);
            return cyaml_scalar_str(doc, map->map.pairs[i].val);
        }
        free(k);
    }
    return NULL;
}

//! Get node from map key
static cyaml_node_t* yaml_map_get(cyaml_doc_t* doc, cyaml_node_t* map, const char* key)
{
    if (!map || map->type != CYAML_MAP)
        return NULL;
    for (uint32_t i = 0; i < map->map.count; i++) {
        char* k = cyaml_scalar_str(doc, map->map.pairs[i].key);
        if (k && strcmp(k, key) == 0) {
            free(k);
            return map->map.pairs[i].val;
        }
        free(k);
    }
    return NULL;
}

//! Get int from map key
static int yaml_map_int(cyaml_doc_t* doc, cyaml_node_t* map, const char* key)
{
    char* s = yaml_map_str(doc, map, key);
    int v = s ? atoi(s) : 0;
    free(s);
    return v;
}

//! Load test from YAML map node
static void tr_load_test(cyaml_doc_t* doc, cyaml_node_t* node, tr_test_t* t)
{
    memset(t, 0, sizeof(*t));

    char* s;
    if ((s = yaml_map_str(doc, node, "id"))) {
        cyaml_strlcpy(t->id, s, sizeof(t->id));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "name"))) {
        cyaml_strlcpy(t->name, s, sizeof(t->name));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "tree"))) {
        t->tree = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "dump"))) {
        t->dump = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "emit"))) {
        t->emit = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "json"))) {
        t->json = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "prev_tree"))) {
        t->prev_tree = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "prev_dump"))) {
        t->prev_dump = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "prev_emit"))) {
        t->prev_emit = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "prev_json"))) {
        t->prev_json = tr_parse_result(s);
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "tree_regressed"))) {
        cyaml_strlcpy(t->tree_regressed, s, sizeof(t->tree_regressed));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "dump_regressed"))) {
        cyaml_strlcpy(t->dump_regressed, s, sizeof(t->dump_regressed));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "emit_regressed"))) {
        cyaml_strlcpy(t->emit_regressed, s, sizeof(t->emit_regressed));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "json_regressed"))) {
        cyaml_strlcpy(t->json_regressed, s, sizeof(t->json_regressed));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "fail_type"))) {
        cyaml_strlcpy(t->fail_type, s, sizeof(t->fail_type));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "error"))) {
        t->error = s;
    }
    if ((s = yaml_map_str(doc, node, "input"))) {
        t->input = s;
    }
    if ((s = yaml_map_str(doc, node, "expected"))) {
        t->expected = s;
    }
    if ((s = yaml_map_str(doc, node, "got"))) {
        t->got = s;
    }
}

//! Load change from YAML map node
static void tr_load_change(cyaml_doc_t* doc, cyaml_node_t* node, tr_change_t* c)
{
    memset(c, 0, sizeof(*c));
    char* s;
    if ((s = yaml_map_str(doc, node, "id"))) {
        cyaml_strlcpy(c->id, s, sizeof(c->id));
        free(s);
    }
    if ((s = yaml_map_str(doc, node, "type"))) {
        cyaml_strlcpy(c->type, s, sizeof(c->type));
        free(s);
    }
}

bool tr_load(const char* path, tr_results_t* r)
{
    memset(r, 0, sizeof(*r));

    FILE* f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc((size_t)size + 1);
    if (!content) {
        fclose(f);
        return false;
    }

    size_t n = fread(content, 1, (size_t)size, f);
    fclose(f);
    content[n] = '\0';

    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(content, n, NULL, &err);

    if (!stream || stream->count == 0) {
        cyaml_stream_free(stream);
        free(content);
        return false;
    }

    cyaml_doc_t* doc = stream->docs[0];
    cyaml_node_t* root = doc->root;

    char* s;
    if ((s = yaml_map_str(doc, root, "version"))) {
        cyaml_strlcpy(r->version, s, sizeof(r->version));
        free(s);
    }
    if ((s = yaml_map_str(doc, root, "date"))) {
        cyaml_strlcpy(r->date, s, sizeof(r->date));
        free(s);
    }
    if ((s = yaml_map_str(doc, root, "suite"))) {
        cyaml_strlcpy(r->suite, s, sizeof(r->suite));
        free(s);
    }

    cyaml_node_t* tests = yaml_map_get(doc, root, "tests");
    if (tests && tests->type == CYAML_SEQ) {
        for (uint32_t i = 0; i < tests->seq.count && r->test_count < TR_MAX_TESTS; i++) {
            tr_load_test(doc, tests->seq.items[i], &r->tests[r->test_count++]);
        }
    }

    cyaml_node_t* regs = yaml_map_get(doc, root, "regressions");
    if (regs && regs->type == CYAML_SEQ) {
        for (uint32_t i = 0; i < regs->seq.count && r->reg_count < TR_MAX_TESTS; i++) {
            tr_load_change(doc, regs->seq.items[i], &r->regressions[r->reg_count++]);
        }
    }

    cyaml_node_t* imps = yaml_map_get(doc, root, "improvements");
    if (imps && imps->type == CYAML_SEQ) {
        for (uint32_t i = 0; i < imps->seq.count && r->imp_count < TR_MAX_TESTS; i++) {
            tr_load_change(doc, imps->seq.items[i], &r->improvements[r->imp_count++]);
        }
    }

    cyaml_node_t* summary = yaml_map_get(doc, root, "summary");
    if (summary) {
        r->total = yaml_map_int(doc, summary, "total");
        r->tree_pass = yaml_map_int(doc, summary, "tree_pass");
        r->tree_fail = yaml_map_int(doc, summary, "tree_fail");
        r->dump_pass = yaml_map_int(doc, summary, "dump_pass");
        r->dump_fail = yaml_map_int(doc, summary, "dump_fail");
        r->dump_skip = yaml_map_int(doc, summary, "dump_skip");
        r->emit_pass = yaml_map_int(doc, summary, "emit_pass");
        r->emit_fail = yaml_map_int(doc, summary, "emit_fail");
        r->emit_skip = yaml_map_int(doc, summary, "emit_skip");
        r->json_pass = yaml_map_int(doc, summary, "json_pass");
        r->json_fail = yaml_map_int(doc, summary, "json_fail");
        r->json_skip = yaml_map_int(doc, summary, "json_skip");
    }

    cyaml_stream_free(stream);
    free(content);
    return true;
}

void tr_compute_changes(tr_results_t* curr, tr_results_t* prev)
{
    curr->reg_count = 0;
    curr->imp_count = 0;

    time_t now = time(NULL);
    struct tm tm_buf;
    char datebuf[32];
    strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", tr_localtime(&now, &tm_buf));

    for (int i = 0; i < curr->test_count; i++) {
        tr_test_t* ct = &curr->tests[i];
        tr_test_t* pt = tr_find_test(prev, ct->id);
        if (!pt)
            continue;

        ct->prev_tree = pt->tree;
        ct->prev_dump = pt->dump;
        ct->prev_emit = pt->emit;
        ct->prev_json = pt->json;

        if (pt->tree == TR_PASS && ct->tree == TR_FAIL) {
            cyaml_strlcpy(ct->tree_regressed, datebuf, sizeof(ct->tree_regressed));
            cyaml_strlcpy(curr->regressions[curr->reg_count].id, ct->id, sizeof(curr->regressions[0].id));
            cyaml_strlcpy(curr->regressions[curr->reg_count].type, "tree", sizeof(curr->regressions[0].type));
            curr->reg_count++;
        } else if (ct->tree == TR_FAIL && pt->tree_regressed[0]) {
            cyaml_strlcpy(ct->tree_regressed, pt->tree_regressed, sizeof(ct->tree_regressed));
        } else if (pt->tree == TR_FAIL && ct->tree == TR_PASS) {
            cyaml_strlcpy(curr->improvements[curr->imp_count].id, ct->id, sizeof(curr->improvements[0].id));
            cyaml_strlcpy(curr->improvements[curr->imp_count].type, "tree", sizeof(curr->improvements[0].type));
            curr->imp_count++;
        }

        if (pt->dump == TR_PASS && ct->dump == TR_FAIL) {
            cyaml_strlcpy(ct->dump_regressed, datebuf, sizeof(ct->dump_regressed));
            cyaml_strlcpy(curr->regressions[curr->reg_count].id, ct->id, sizeof(curr->regressions[0].id));
            cyaml_strlcpy(curr->regressions[curr->reg_count].type, "dump", sizeof(curr->regressions[0].type));
            curr->reg_count++;
        } else if (ct->dump == TR_FAIL && pt->dump_regressed[0]) {
            cyaml_strlcpy(ct->dump_regressed, pt->dump_regressed, sizeof(ct->dump_regressed));
        } else if (pt->dump == TR_FAIL && ct->dump == TR_PASS) {
            cyaml_strlcpy(curr->improvements[curr->imp_count].id, ct->id, sizeof(curr->improvements[0].id));
            cyaml_strlcpy(curr->improvements[curr->imp_count].type, "dump", sizeof(curr->improvements[0].type));
            curr->imp_count++;
        }

        if (pt->emit == TR_PASS && ct->emit == TR_FAIL) {
            cyaml_strlcpy(ct->emit_regressed, datebuf, sizeof(ct->emit_regressed));
            cyaml_strlcpy(curr->regressions[curr->reg_count].id, ct->id, sizeof(curr->regressions[0].id));
            cyaml_strlcpy(curr->regressions[curr->reg_count].type, "emit", sizeof(curr->regressions[0].type));
            curr->reg_count++;
        } else if (ct->emit == TR_FAIL && pt->emit_regressed[0]) {
            cyaml_strlcpy(ct->emit_regressed, pt->emit_regressed, sizeof(ct->emit_regressed));
        } else if (pt->emit == TR_FAIL && ct->emit == TR_PASS) {
            cyaml_strlcpy(curr->improvements[curr->imp_count].id, ct->id, sizeof(curr->improvements[0].id));
            cyaml_strlcpy(curr->improvements[curr->imp_count].type, "emit", sizeof(curr->improvements[0].type));
            curr->imp_count++;
        }

        if (pt->json == TR_PASS && ct->json == TR_FAIL) {
            cyaml_strlcpy(ct->json_regressed, datebuf, sizeof(ct->json_regressed));
            cyaml_strlcpy(curr->regressions[curr->reg_count].id, ct->id, sizeof(curr->regressions[0].id));
            cyaml_strlcpy(curr->regressions[curr->reg_count].type, "json", sizeof(curr->regressions[0].type));
            curr->reg_count++;
        } else if (ct->json == TR_FAIL && pt->json_regressed[0]) {
            cyaml_strlcpy(ct->json_regressed, pt->json_regressed, sizeof(ct->json_regressed));
        } else if (pt->json == TR_FAIL && ct->json == TR_PASS) {
            cyaml_strlcpy(curr->improvements[curr->imp_count].id, ct->id, sizeof(curr->improvements[0].id));
            cyaml_strlcpy(curr->improvements[curr->imp_count].type, "json", sizeof(curr->improvements[0].type));
            curr->imp_count++;
        }
    }
}

bool tr_save(const char* path, tr_results_t* r)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    if (!doc)
        return false;

    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);

    time_t now = time(NULL);
    struct tm tm_buf2;
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", tr_localtime(&now, &tm_buf2));

    cyaml_map_set(doc, root, "version", cyaml_new_cstr(doc, "2"));
    cyaml_map_set(doc, root, "date", cyaml_new_cstr(doc, timebuf));
    cyaml_map_set(doc, root, "suite", cyaml_new_cstr(doc, r->suite));

    if (r->test_count > 0) {
        cyaml_node_t* tests = cyaml_new_seq(doc);
        for (int i = 0; i < r->test_count; i++) {
            tr_test_t* t = &r->tests[i];
            cyaml_node_t* test = cyaml_new_map(doc);

            cyaml_map_set(doc, test, "id", cyaml_new_cstr(doc, t->id));
            if (t->name[0])
                cyaml_map_set(doc, test, "name", cyaml_new_cstr(doc, t->name));
            cyaml_map_set(doc, test, "tree", cyaml_new_cstr(doc, tr_result_str(t->tree)));
            cyaml_map_set(doc, test, "dump", cyaml_new_cstr(doc, tr_result_str(t->dump)));
            cyaml_map_set(doc, test, "emit", cyaml_new_cstr(doc, tr_result_str(t->emit)));
            cyaml_map_set(doc, test, "json", cyaml_new_cstr(doc, tr_result_str(t->json)));

            if (t->prev_tree != TR_NONE && t->prev_tree != t->tree)
                cyaml_map_set(doc, test, "prev_tree", cyaml_new_cstr(doc, tr_result_str(t->prev_tree)));
            if (t->prev_dump != TR_NONE && t->prev_dump != t->dump)
                cyaml_map_set(doc, test, "prev_dump", cyaml_new_cstr(doc, tr_result_str(t->prev_dump)));
            if (t->prev_emit != TR_NONE && t->prev_emit != t->emit)
                cyaml_map_set(doc, test, "prev_emit", cyaml_new_cstr(doc, tr_result_str(t->prev_emit)));
            if (t->prev_json != TR_NONE && t->prev_json != t->json)
                cyaml_map_set(doc, test, "prev_json", cyaml_new_cstr(doc, tr_result_str(t->prev_json)));

            if (t->tree_regressed[0])
                cyaml_map_set(doc, test, "tree_regressed", cyaml_new_cstr(doc, t->tree_regressed));
            if (t->dump_regressed[0])
                cyaml_map_set(doc, test, "dump_regressed", cyaml_new_cstr(doc, t->dump_regressed));
            if (t->emit_regressed[0])
                cyaml_map_set(doc, test, "emit_regressed", cyaml_new_cstr(doc, t->emit_regressed));
            if (t->json_regressed[0])
                cyaml_map_set(doc, test, "json_regressed", cyaml_new_cstr(doc, t->json_regressed));

            if (t->fail_type[0])
                cyaml_map_set(doc, test, "fail_type", cyaml_new_cstr(doc, t->fail_type));
            if (t->error)
                cyaml_map_set(doc, test, "error", cyaml_new_cstr(doc, t->error));
            if (t->input)
                cyaml_map_set(doc, test, "input", cyaml_new_cstr(doc, t->input));
            if (t->expected)
                cyaml_map_set(doc, test, "expected", cyaml_new_cstr(doc, t->expected));
            if (t->got)
                cyaml_map_set(doc, test, "got", cyaml_new_cstr(doc, t->got));

            cyaml_seq_push(tests, test);
        }
        cyaml_map_set(doc, root, "tests", tests);
    }

    if (r->reg_count > 0) {
        cyaml_node_t* regs = cyaml_new_seq(doc);
        for (int i = 0; i < r->reg_count; i++) {
            cyaml_node_t* reg = cyaml_new_map(doc);
            cyaml_map_set(doc, reg, "id", cyaml_new_cstr(doc, r->regressions[i].id));
            cyaml_map_set(doc, reg, "type", cyaml_new_cstr(doc, r->regressions[i].type));
            cyaml_seq_push(regs, reg);
        }
        cyaml_map_set(doc, root, "regressions", regs);
    }

    if (r->imp_count > 0) {
        cyaml_node_t* imps = cyaml_new_seq(doc);
        for (int i = 0; i < r->imp_count; i++) {
            cyaml_node_t* imp = cyaml_new_map(doc);
            cyaml_map_set(doc, imp, "id", cyaml_new_cstr(doc, r->improvements[i].id));
            cyaml_map_set(doc, imp, "type", cyaml_new_cstr(doc, r->improvements[i].type));
            cyaml_seq_push(imps, imp);
        }
        cyaml_map_set(doc, root, "improvements", imps);
    }

    cyaml_node_t* summary = cyaml_new_map(doc);
    cyaml_map_set(doc, summary, "total", cyaml_new_int(doc, r->total));
    cyaml_map_set(doc, summary, "tree_pass", cyaml_new_int(doc, r->tree_pass));
    cyaml_map_set(doc, summary, "tree_fail", cyaml_new_int(doc, r->tree_fail));
    cyaml_map_set(doc, summary, "dump_pass", cyaml_new_int(doc, r->dump_pass));
    cyaml_map_set(doc, summary, "dump_fail", cyaml_new_int(doc, r->dump_fail));
    cyaml_map_set(doc, summary, "dump_skip", cyaml_new_int(doc, r->dump_skip));
    cyaml_map_set(doc, summary, "emit_pass", cyaml_new_int(doc, r->emit_pass));
    cyaml_map_set(doc, summary, "emit_fail", cyaml_new_int(doc, r->emit_fail));
    cyaml_map_set(doc, summary, "emit_skip", cyaml_new_int(doc, r->emit_skip));
    cyaml_map_set(doc, summary, "json_pass", cyaml_new_int(doc, r->json_pass));
    cyaml_map_set(doc, summary, "json_fail", cyaml_new_int(doc, r->json_fail));
    cyaml_map_set(doc, summary, "json_skip", cyaml_new_int(doc, r->json_skip));
    cyaml_map_set(doc, root, "summary", summary);

    size_t len;
    char* yaml = cyaml_emit(doc, NULL, &len);
    cyaml_free(doc);
    if (!yaml)
        return false;

    FILE* f = fopen(path, "wb");
    if (!f) {
        free(yaml);
        return false;
    }
    size_t written = fwrite(yaml, 1, len, f);
    fclose(f);
    free(yaml);
    return written == len;
}

#endif // TEST_RESULTS_IMPL
