
#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "cyaml.h"
#define TEST_RESULTS_IMPL
#include "test_results.h"

static void print_summary(tr_results_t* r)
{
    int tree_pass = 0, tree_fail = 0;
    int dump_pass = 0, dump_fail = 0;
    int emit_pass = 0, emit_fail = 0;
    int json_pass = 0, json_fail = 0;

    for (int i = 0; i < r->test_count; i++) {
        tr_test_t* t = &r->tests[i];
        if (t->tree == TR_PASS) tree_pass++;
        else if (t->tree == TR_FAIL) tree_fail++;
        if (t->dump == TR_PASS) dump_pass++;
        else if (t->dump == TR_FAIL) dump_fail++;
        if (t->emit == TR_PASS) emit_pass++;
        else if (t->emit == TR_FAIL) emit_fail++;
        if (t->json == TR_PASS) json_pass++;
        else if (t->json == TR_FAIL) json_fail++;
    }

    const char* tree_icon = tree_fail == 0 ? ":white_check_mark:" : ":x:";
    const char* emit_icon = emit_fail == 0 ? ":white_check_mark:" : ":x:";
    const char* json_icon = json_fail == 0 ? ":white_check_mark:" : ":x:";
    const char* dump_icon = dump_fail == 0 ? ":white_check_mark:" : ":warning:";

    printf("## YAML Test Suite Results\n\n");
    printf("**Total Tests:** %d\n\n", r->test_count);
    printf("| Test | Pass | Fail | Status |\n");
    printf("|------|------|------|--------|\n");
    printf("| Tree (Parse) | %d | %d | %s |\n", tree_pass, tree_fail, tree_icon);
    printf("| Emit | %d | %d | %s |\n", emit_pass, emit_fail, emit_icon);
    printf("| JSON | %d | %d | %s |\n", json_pass, json_fail, json_icon);
    printf("| Dump | %d | %d | %s |\n", dump_pass, dump_fail, dump_icon);
    printf("\n");

    // Regressions
    if (r->reg_count > 0) {
        printf("### :rotating_light: Regressions Detected\n\n");
        printf("| Test | Type |\n");
        printf("|------|------|\n");
        for (int i = 0; i < r->reg_count; i++) {
            printf("| %s | %s |\n", r->regressions[i].id, r->regressions[i].type);
        }
        printf("\n");
    }

    // Improvements
    if (r->imp_count > 0) {
        printf("### :tada: Improvements\n\n");
        printf("| Test | Type |\n");
        printf("|------|------|\n");
        for (int i = 0; i < r->imp_count; i++) {
            printf("| %s | %s |\n", r->improvements[i].id, r->improvements[i].type);
        }
        printf("\n");
    }

    // Pass rates
    int tree_total = tree_pass + tree_fail;
    int emit_total = emit_pass + emit_fail;
    int json_total = json_pass + json_fail;
    int dump_total = dump_pass + dump_fail;

    int tree_pct = tree_total > 0 ? (tree_pass * 100 / tree_total) : 0;
    int emit_pct = emit_total > 0 ? (emit_pass * 100 / emit_total) : 0;
    int json_pct = json_total > 0 ? (json_pass * 100 / json_total) : 0;
    int dump_pct = dump_total > 0 ? (dump_pass * 100 / dump_total) : 0;

    printf("### Pass Rates\n\n");
    printf("| Test | Rate |\n");
    printf("|------|------|\n");
    printf("| Tree | %d%% |\n", tree_pct);
    printf("| Emit | %d%% |\n", emit_pct);
    printf("| JSON | %d%% |\n", json_pct);
    printf("| Dump | %d%% |\n", dump_pct);
}

int main(int argc, char** argv)
{
    const char* file = ".cyaml_suite_results";
    if (argc > 1)
        file = argv[1];

    tr_results_t results = { 0 };
    if (!tr_load(file, &results)) {
        fprintf(stderr, "Failed to load results from %s\n", file);
        return 1;
    }

    print_summary(&results);
    return 0;
}
