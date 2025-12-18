//! Usage: dump_debug <suite_path> <test_id> [case_num] [--1.3]
//!
//! Compares cyaml_stream_dump() output against expected out.yaml

#include "debug_utils.h"

static cyaml_spec_t g_spec = CYAML_SPEC_1_2;

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
    cyaml_opts_t opts = { .spec = g_spec };
    cyaml_stream_t* stream = cyaml_parse_stream(t->yaml, t->yaml_len, &opts, &err);

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
        cyaml_map_set(out, doc_info, "has_doc_start",
            cyaml_new_bool(out, (doc->flags & CYAML_DOC_START) != 0));
        cyaml_map_set(out, doc_info, "has_doc_end",
            cyaml_new_bool(out, (doc->flags & CYAML_DOC_END) != 0));

        cyaml_node_t* root_info = cyaml_new_map(out);
        dbg_add_node_info(out, root_info, doc, doc->root, 0);
        cyaml_map_set(out, doc_info, "root", root_info);

        cyaml_seq_push(docs, doc_info);
    }
    cyaml_map_set(out, root, "documents", docs);

    size_t got_len;
    char* got = cyaml_stream_dump(stream, &got_len);

    cyaml_node_t* dump_cmp = cyaml_new_map(out);

    if (t->out_yaml) {
        dbg_add_raw_info(out, dump_cmp, "expected", t->out_yaml, strlen(t->out_yaml));
    } else {
        cyaml_map_set(out, dump_cmp, "expected", cyaml_new_cstr(out, "(no out.yaml file)"));
    }

    if (got) {
        dbg_add_raw_info(out, dump_cmp, "got", got, got_len);
    } else {
        cyaml_map_set(out, dump_cmp, "got", cyaml_new_cstr(out, "(dump failed)"));
    }

    if (t->out_yaml && got) {
        dbg_diff_t diff = dbg_compare_strings(t->out_yaml, strlen(t->out_yaml), got, got_len);
        dbg_add_diff_info(out, dump_cmp, "comparison", &diff);
    }

    cyaml_map_set(out, root, "dump", dump_cmp);

    if (t->test_event) {
        size_t events_len;
        char* got_events = cyaml_stream_events(stream, false, &events_len);

        cyaml_node_t* events_cmp = cyaml_new_map(out);
        cyaml_map_set(out, events_cmp, "expected", cyaml_new_cstr(out, t->test_event));

        if (got_events) {
            cyaml_map_set(out, events_cmp, "got", cyaml_new_cstr(out, got_events));
            dbg_diff_t diff = dbg_compare_strings(t->test_event, strlen(t->test_event),
                got_events, events_len);
            dbg_add_diff_info(out, events_cmp, "comparison", &diff);
            free(got_events);
        }

        cyaml_map_set(out, root, "events", events_cmp);
    }

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
        fprintf(stderr, "Usage: %s <suite_path> <test_id> [case_num] [--1.3]\n", argv[0]);
        fprintf(stderr, "  suite_path: path to yaml-test-suite/data directory\n");
        fprintf(stderr, "  case_num: 1-based index for multi-case tests (default: 1)\n");
        fprintf(stderr, "  --1.3: parse in YAML 1.3 mode\n");
        return 1;
    }

    const char* suite_dir = argv[1];
    const char* test_id = argv[2];
    int case_idx = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--1.3") == 0) {
            g_spec = CYAML_SPEC_1_3;
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            case_idx = atoi(argv[i]) - 1;
            if (case_idx < 0)
                case_idx = 0;
        }
    }

    dbg_test_files_t test;
    if (!dbg_load_test(&test, suite_dir, test_id, case_idx)) {
        fprintf(stderr, "Cannot read in.yaml for test: %s\n", test_id);
        return 1;
    }

    run_debug(&test);
    dbg_free_test(&test);

    return 0;
}
