//! Usage: json_debug <suite_path> <test_id> [case_num]
//!
//! Compares YAML-to-JSON conversion against expected in.json

#include "debug_utils.h"

static void run_debug(dbg_test_files_t* t)
{
    cyaml_doc_t* out = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(out);
    cyaml_set_root(out, root);

    cyaml_map_set(out, root, "test", cyaml_new_cstr(out, t->full_id));
    if (t->name)
        cyaml_map_set(out, root, "name", cyaml_new_cstr(out, t->name));

    dbg_add_raw_info(out, root, "input", t->yaml, t->yaml_len);

    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(t->yaml, t->yaml_len, NULL, &err);

    if (!stream) {
        cyaml_node_t* parse_err = cyaml_new_map(out);
        cyaml_map_set(out, parse_err, "message", cyaml_new_cstr(out, err.msg));
        char loc[64];
        snprintf(loc, sizeof(loc), "%u:%u", err.span.start_line, err.span.start_col);
        cyaml_map_set(out, parse_err, "location", cyaml_new_cstr(out, loc));
        cyaml_map_set(out, root, "parse_error", parse_err);
        goto emit_output;
    }

    cyaml_node_t* docs = cyaml_new_seq(out);
    for (uint32_t i = 0; i < stream->count; i++) {
        cyaml_doc_t* doc = stream->docs[i];
        cyaml_node_t* doc_info = cyaml_new_map(out);

        cyaml_map_set(out, doc_info, "index", cyaml_new_int(out, i));
        cyaml_map_set(out, doc_info, "flags", cyaml_new_int(out, doc->flags));

        cyaml_node_t* root_info = cyaml_new_map(out);
        dbg_add_node_info(out, root_info, doc, doc->root, 0);
        cyaml_map_set(out, doc_info, "root", root_info);

        cyaml_seq_push(docs, doc_info);
    }
    cyaml_map_set(out, root, "documents", docs);

    size_t got_len;
    char* got = cyaml_stream_json(stream, 2, &got_len);

    cyaml_node_t* json_cmp = cyaml_new_map(out);

    if (t->in_json) {
        dbg_add_raw_info(out, json_cmp, "expected", t->in_json, strlen(t->in_json));
    } else {
        cyaml_map_set(out, json_cmp, "expected", cyaml_new_cstr(out, "(no in.json file)"));
    }

    if (got) {
        dbg_add_raw_info(out, json_cmp, "got", got, got_len);
    } else {
        cyaml_map_set(out, json_cmp, "got", cyaml_new_cstr(out, "(json emit failed)"));
    }

    if (t->in_json && got) {
        dbg_diff_t str_diff = dbg_compare_strings(t->in_json, strlen(t->in_json), got, got_len);
        dbg_add_diff_info(out, json_cmp, "string_comparison", &str_diff);
    }

    cyaml_node_t* semantic = cyaml_new_map(out);

    if (t->in_json) {
        uint32_t json_doc_count = 0;
        cyaml_stream_t** json_docs = dbg_parse_multi_json(t->in_json, strlen(t->in_json), &json_doc_count);

        cyaml_map_set(out, semantic, "yaml_doc_count", cyaml_new_int(out, stream->count));
        cyaml_map_set(out, semantic, "json_doc_count", cyaml_new_int(out, json_doc_count));

        if (!json_docs) {
            if (json_doc_count == 0 && stream->count == 0) {
                cyaml_map_set(out, semantic, "match", cyaml_new_bool(out, true));
                cyaml_map_set(out, semantic, "note", cyaml_new_cstr(out, "both empty"));
            } else {
                cyaml_map_set(out, semantic, "match", cyaml_new_bool(out, false));
                cyaml_map_set(out, semantic, "error", cyaml_new_cstr(out, "failed to parse expected JSON"));
            }
        } else if (json_doc_count != stream->count) {
            cyaml_map_set(out, semantic, "match", cyaml_new_bool(out, false));
            cyaml_map_set(out, semantic, "error", cyaml_new_cstr(out, "document count mismatch"));
            dbg_free_multi_json(json_docs, json_doc_count);
        } else {
            cyaml_node_t* diffs = cyaml_new_seq(out);
            dbg_cmp_ctx_t ctx = { .out = out, .diffs = diffs, .max_diffs = 10, .diff_count = 0 };

            bool all_match = true;
            for (uint32_t i = 0; i < stream->count; i++) {
                char path[32];
                snprintf(path, sizeof(path), "doc[%u]", i);
                if (!dbg_nodes_equal(&ctx, stream->docs[i], stream->docs[i]->root,
                        json_docs[i]->docs[0], json_docs[i]->docs[0]->root, 0, path)) {
                    all_match = false;
                }
            }

            cyaml_map_set(out, semantic, "match", cyaml_new_bool(out, all_match));

            if (!all_match && diffs->seq.count > 0) {
                cyaml_map_set(out, semantic, "differences", diffs);
            }

            dbg_free_multi_json(json_docs, json_doc_count);
        }
    } else {
        if (stream->count == 0) {
            cyaml_map_set(out, semantic, "match", cyaml_new_bool(out, true));
            cyaml_map_set(out, semantic, "note", cyaml_new_cstr(out, "no in.json, empty stream"));
        } else {
            cyaml_map_set(out, semantic, "match", cyaml_new_bool(out, false));
            cyaml_map_set(out, semantic, "note", cyaml_new_cstr(out, "no in.json but stream has documents"));
        }
    }

    cyaml_map_set(out, json_cmp, "semantic", semantic);
    cyaml_map_set(out, root, "json", json_cmp);

    free(got);
    cyaml_stream_free(stream);

emit_output:;
    size_t len;
    char* yaml_out = cyaml_emit(out, NULL, &len);
    printf("%s", yaml_out);
    free(yaml_out);
    cyaml_free(out);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <suite_path> <test_id> [case_num]\n", argv[0]);
        fprintf(stderr, "  suite_path: path to yaml-test-suite/data directory\n");
        fprintf(stderr, "  case_num: 1-based index for multi-case tests (default: 1)\n");
        return 1;
    }

    const char* suite_dir = argv[1];
    const char* test_id = argv[2];
    int case_idx = (argc > 3) ? atoi(argv[3]) - 1 : 0;
    if (case_idx < 0)
        case_idx = 0;

    dbg_test_files_t test;
    if (!dbg_load_test(&test, suite_dir, test_id, case_idx)) {
        fprintf(stderr, "Cannot read in.yaml for test: %s\n", test_id);
        return 1;
    }

    run_debug(&test);
    dbg_free_test(&test);

    return 0;
}
