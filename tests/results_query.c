#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

//! Usage:
//!   results_query [file]                    Show summary
//!   results_query [file] list [filter]      List tests (filter: pass|fail|skip|tree|emit|json|dump)
//!   results_query [file] show <test_id>     Show details for a test (shows case N of M for multi-case)
//!   results_query [file] fails              List all failing tests with details
//!   results_query [file] ids [filter]       List just test IDs (filter: fail|tree|emit|json|dump)
//!   results_query [file] regressions        List regressions from this run
//!   results_query [file] improvements       List improvements from this run
//!   results_query [file] broken             List all failures with regression dates

#include "cyaml.h"
#define TEST_RESULTS_IMPL
#include "test_results.h"

static void cmd_summary(tr_results_t* r)
{
    printf("Test Results Summary\n");
    printf("====================\n");
    printf("Date:    %s\n", r->date);
    printf("Suite:   %s\n", r->suite);
    printf("Version: %s\n", r->version);
    printf("\n");
    printf("Total:   %d tests\n", r->total);
    printf("Tree:    %d pass, %d fail\n", r->tree_pass, r->tree_fail);
    printf("Emit:    %d pass, %d fail, %d skip\n", r->emit_pass, r->emit_fail, r->emit_skip);
    printf("JSON:    %d pass, %d fail, %d skip\n", r->json_pass, r->json_fail, r->json_skip);
    printf("Dump:    %d pass, %d fail, %d skip\n", r->dump_pass, r->dump_fail, r->dump_skip);

    if (r->reg_count > 0 || r->imp_count > 0) {
        printf("\n");
        if (r->reg_count > 0) {
            printf("!! %d regression(s)\n", r->reg_count);
        }
        if (r->imp_count > 0) {
            printf("++ %d improvement(s)\n", r->imp_count);
        }
    }
}

static void cmd_list(tr_results_t* r, const char* filter)
{
    bool show_all = !filter || filter[0] == '\0';
    bool show_pass = show_all || strcmp(filter, "pass") == 0;
    bool show_fail = show_all || strcmp(filter, "fail") == 0;
    bool show_skip = show_all || strcmp(filter, "skip") == 0;
    bool tree_only = filter && strcmp(filter, "tree") == 0;
    bool dump_only = filter && strcmp(filter, "dump") == 0;
    bool emit_only = filter && strcmp(filter, "emit") == 0;
    bool json_only = filter && strcmp(filter, "json") == 0;

    printf("%-10s %-6s %-6s %-6s %-6s %s\n", "ID", "TREE", "EMIT", "JSON", "DUMP", "NAME");
    printf("---------- ------ ------ ------ ------ ----\n");

    for (int i = 0; i < r->test_count; i++) {
        tr_test_t* t = &r->tests[i];

        if (tree_only) {
            if (t->tree != TR_FAIL)
                continue;
        } else if (dump_only) {
            if (t->dump != TR_FAIL)
                continue;
        } else if (emit_only) {
            if (t->emit != TR_FAIL)
                continue;
        } else if (json_only) {
            if (t->json != TR_FAIL)
                continue;
        } else if (!show_all) {
            bool match = false;
            if ((t->tree == TR_PASS || t->dump == TR_PASS || t->emit == TR_PASS || t->json == TR_PASS) && show_pass)
                match = true;
            if ((t->tree == TR_FAIL || t->dump == TR_FAIL || t->emit == TR_FAIL || t->json == TR_FAIL) && show_fail)
                match = true;
            if ((t->dump == TR_SKIP || t->emit == TR_SKIP || t->json == TR_SKIP) && show_skip)
                match = true;
            if (!match)
                continue;
        }

        printf("%-10s %-6s %-6s %-6s %-6s %s\n",
            t->id,
            tr_result_str(t->tree),
            tr_result_str(t->emit),
            tr_result_str(t->json),
            tr_result_str(t->dump),
            t->name[0] ? t->name : "(unnamed)");
    }
}

static void cmd_ids(tr_results_t* r, const char* filter)
{
    bool dump_fail = filter && strcmp(filter, "dump") == 0;
    bool tree_fail = filter && strcmp(filter, "tree") == 0;
    bool emit_fail = filter && strcmp(filter, "emit") == 0;
    bool json_fail = filter && strcmp(filter, "json") == 0;
    bool any_fail = filter && strcmp(filter, "fail") == 0;

    for (int i = 0; i < r->test_count; i++) {
        tr_test_t* t = &r->tests[i];

        if (dump_fail && t->dump != TR_FAIL)
            continue;
        if (tree_fail && t->tree != TR_FAIL)
            continue;
        if (emit_fail && t->emit != TR_FAIL)
            continue;
        if (json_fail && t->json != TR_FAIL)
            continue;
        if (any_fail && t->tree != TR_FAIL && t->dump != TR_FAIL && t->emit != TR_FAIL && t->json != TR_FAIL)
            continue;

        printf("%s\n", t->id);
    }
}

static void cmd_show(tr_results_t* r, const char* id)
{
    tr_test_t* t = tr_find_test(r, id);
    if (!t) {
        fprintf(stderr, "Test not found: %s\n", id);
        return;
    }

    // Parse case number from ID (format: "TEST:N" for multi-case tests)
    char base_id[64];
    int case_num = 0;
    const char* colon = strchr(t->id, ':');
    if (colon) {
        size_t len = (size_t)(colon - t->id);
        if (len >= sizeof(base_id))
            len = sizeof(base_id) - 1;
        memcpy(base_id, t->id, len);
        base_id[len] = '\0';
        case_num = atoi(colon + 1);

        // Count total cases for this test
        int total_cases = 0;
        for (int i = 0; i < r->test_count; i++) {
            if (strncmp(r->tests[i].id, base_id, len) == 0 && (r->tests[i].id[len] == ':' || r->tests[i].id[len] == '\0')) {
                total_cases++;
            }
        }
        printf("Test: %s (case %d of %d)\n", base_id, case_num, total_cases);
    } else {
        printf("Test: %s\n", t->id);
    }
    printf("Name: %s\n", t->name[0] ? t->name : "(unnamed)");
    printf("Tree: %s", tr_result_str(t->tree));
    if (t->prev_tree != TR_NONE && t->prev_tree != t->tree) {
        printf(" (was %s)", tr_result_str(t->prev_tree));
    }
    printf("\n");

    printf("Emit: %s", tr_result_str(t->emit));
    if (t->prev_emit != TR_NONE && t->prev_emit != t->emit) {
        printf(" (was %s)", tr_result_str(t->prev_emit));
    }
    printf("\n");

    printf("JSON: %s", tr_result_str(t->json));
    if (t->prev_json != TR_NONE && t->prev_json != t->json) {
        printf(" (was %s)", tr_result_str(t->prev_json));
    }
    printf("\n");

    printf("Dump: %s", tr_result_str(t->dump));
    if (t->prev_dump != TR_NONE && t->prev_dump != t->dump) {
        printf(" (was %s)", tr_result_str(t->prev_dump));
    }
    printf("\n");

    if (t->tree_regressed[0]) {
        printf("Tree regressed: %s\n", t->tree_regressed);
    }
    if (t->emit_regressed[0]) {
        printf("Emit regressed: %s\n", t->emit_regressed);
    }
    if (t->json_regressed[0]) {
        printf("JSON regressed: %s\n", t->json_regressed);
    }
    if (t->dump_regressed[0]) {
        printf("Dump regressed: %s\n", t->dump_regressed);
    }

    if (t->fail_type[0]) {
        printf("\nFailure Type: %s\n", t->fail_type);
    }
    if (t->error) {
        printf("Error: %s\n", t->error);
    }
    if (t->input) {
        printf("\n--- Input ---\n%s", t->input);
        if (t->input[strlen(t->input) - 1] != '\n')
            printf("\n");
    }
    if (t->expected) {
        printf("\n--- Expected ---\n%s", t->expected);
        if (t->expected[strlen(t->expected) - 1] != '\n')
            printf("\n");
    }
    if (t->got) {
        printf("\n--- Got ---\n%s", t->got);
        if (t->got[strlen(t->got) - 1] != '\n')
            printf("\n");
    }
}

static void cmd_fails(tr_results_t* r)
{
    int count = 0;
    for (int i = 0; i < r->test_count; i++) {
        tr_test_t* t = &r->tests[i];
        if (t->tree != TR_FAIL && t->dump != TR_FAIL && t->emit != TR_FAIL)
            continue;

        if (count > 0)
            printf("\n========================================\n\n");
        cmd_show(r, t->id);
        count++;
    }
    printf("\n========================================\n");
    printf("Total failures: %d\n", count);
}

static void cmd_regressions(tr_results_t* r)
{
    if (r->reg_count == 0) {
        printf("No regressions.\n");
        return;
    }
    printf("Regressions (%d):\n", r->reg_count);
    for (int i = 0; i < r->reg_count; i++) {
        printf("  %s (%s)\n", r->regressions[i].id, r->regressions[i].type);
    }
}

static void cmd_improvements(tr_results_t* r)
{
    if (r->imp_count == 0) {
        printf("No improvements.\n");
        return;
    }
    printf("Improvements (%d):\n", r->imp_count);
    for (int i = 0; i < r->imp_count; i++) {
        printf("  %s (%s)\n", r->improvements[i].id, r->improvements[i].type);
    }
}

static void cmd_broken(tr_results_t* r)
{
    // Show all failing tests with their regression dates
    int count = 0, new_count = 0;
    printf("%-10s %-12s %-12s %-12s %-12s %s\n", "ID", "TREE_REG", "EMIT_REG", "JSON_REG", "DUMP_REG", "STATUS");
    printf("---------- ------------ ------------ ------------ ------------ ------\n");

    for (int i = 0; i < r->test_count; i++) {
        tr_test_t* t = &r->tests[i];
        if (t->tree != TR_FAIL && t->emit != TR_FAIL && t->json != TR_FAIL && t->dump != TR_FAIL)
            continue;

        // Show "legacy" for pre-existing failures, date for tracked regressions
        const char* tree_date = "-";
        const char* emit_date = "-";
        const char* json_date = "-";
        const char* dump_date = "-";
        if (t->tree == TR_FAIL) {
            tree_date = t->tree_regressed[0] ? t->tree_regressed : "legacy";
            if (t->tree_regressed[0])
                new_count++;
        }
        if (t->emit == TR_FAIL) {
            emit_date = t->emit_regressed[0] ? t->emit_regressed : "legacy";
            if (t->emit_regressed[0])
                new_count++;
        }
        if (t->json == TR_FAIL) {
            json_date = t->json_regressed[0] ? t->json_regressed : "legacy";
            if (t->json_regressed[0])
                new_count++;
        }
        if (t->dump == TR_FAIL) {
            dump_date = t->dump_regressed[0] ? t->dump_regressed : "legacy";
            if (t->dump_regressed[0])
                new_count++;
        }

        char status[64] = "";
        char* p = status;
        char* end = status + sizeof(status);
        if (t->tree == TR_FAIL)
            p += snprintf(p, (size_t)(end - p), "%s", "tree");
        if (t->emit == TR_FAIL)
            p += snprintf(p, (size_t)(end - p), "%s%s", p > status ? "," : "", "emit");
        if (t->json == TR_FAIL)
            p += snprintf(p, (size_t)(end - p), "%s%s", p > status ? "," : "", "json");
        if (t->dump == TR_FAIL)
            p += snprintf(p, (size_t)(end - p), "%s%s", p > status ? "," : "", "dump");
        (void)p;

        printf("%-10s %-12s %-12s %-12s %-12s %s\n", t->id, tree_date, emit_date, json_date, dump_date, status);
        count++;
    }
    printf("\nTotal broken: %d (%d with tracked regression date)\n", count, new_count);
}

static void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s [file] <command> [args]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  (none)              Show summary\n");
    fprintf(stderr, "  list [filter]       List tests (filter: pass|fail|skip|tree|emit|json|dump)\n");
    fprintf(stderr, "  show <test_id>      Show details for a test\n");
    fprintf(stderr, "  fails               List all failing tests with details\n");
    fprintf(stderr, "  ids [filter]        List just test IDs (filter: fail|tree|emit|json|dump)\n");
    fprintf(stderr, "  regressions         List regressions from this run\n");
    fprintf(stderr, "  improvements        List improvements from this run\n");
    fprintf(stderr, "  broken              List all failures with regression dates\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Default file: .cyaml_suite_results\n");
}

int main(int argc, char** argv)
{
    const char* file = ".cyaml_suite_results";
    int arg_start = 1;

    if (argc > 1 && argv[1][0] != '-') {
        FILE* f = fopen(argv[1], "r");
        if (f) {
            char buf[16];
            if (fgets(buf, sizeof(buf), f) && strncmp(buf, "@version", 8) == 0) {
                file = argv[1];
                arg_start = 2;
            }
            fclose(f);
        }
    }

    tr_results_t results;
    if (!tr_load(file, &results)) {
        fprintf(stderr, "Cannot open: %s\n", file);
        return 1;
    }

    const char* cmd = (argc > arg_start) ? argv[arg_start] : NULL;

    if (!cmd) {
        cmd_summary(&results);
    } else if (strcmp(cmd, "list") == 0) {
        const char* filter = (argc > arg_start + 1) ? argv[arg_start + 1] : NULL;
        cmd_list(&results, filter);
    } else if (strcmp(cmd, "show") == 0) {
        if (argc <= arg_start + 1) {
            fprintf(stderr, "Usage: %s show <test_id>\n", argv[0]);
            tr_free_results(&results);
            return 1;
        }
        cmd_show(&results, argv[arg_start + 1]);
    } else if (strcmp(cmd, "fails") == 0) {
        cmd_fails(&results);
    } else if (strcmp(cmd, "ids") == 0) {
        const char* filter = (argc > arg_start + 1) ? argv[arg_start + 1] : NULL;
        cmd_ids(&results, filter);
    } else if (strcmp(cmd, "regressions") == 0) {
        cmd_regressions(&results);
    } else if (strcmp(cmd, "improvements") == 0) {
        cmd_improvements(&results);
    } else if (strcmp(cmd, "broken") == 0) {
        cmd_broken(&results);
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        usage(argv[0]);
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage(argv[0]);
        tr_free_results(&results);
        return 1;
    }

    tr_free_results(&results);
    return 0;
}
