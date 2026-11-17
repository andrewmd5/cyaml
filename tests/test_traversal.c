#include "cyaml.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

// Regression for the traversal stack UAF reported by He Huang (NexusSan).

void setUp(void) { }
void tearDown(void) { }

// Cross each stack growth boundary, including inputs with siblings so that
// advancing a child index through a push is also checked.
static const size_t depths[] = { 31, 32, 33, 63, 64, 65, 127, 128, 129 };

static char* nested_yaml(size_t depth, const char* open,
    const char* leaf, const char* close)
{
    size_t open_len = strlen(open);
    size_t leaf_len = strlen(leaf);
    size_t close_len = strlen(close);
    size_t len = depth * (open_len + close_len) + leaf_len;
    char* yaml = malloc(len + 1);
    TEST_ASSERT_NOT_NULL(yaml);
    char* p = yaml;
    for (size_t i = 0; i < depth; i++, p += open_len)
        memcpy(p, open, open_len);
    memcpy(p, leaf, leaf_len);
    p += leaf_len;
    for (size_t i = 0; i < depth; i++, p += close_len)
        memcpy(p, close, close_len);
    *p = '\0';
    return yaml;
}

static void assert_same_tree(const cyaml_doc_t* expected_doc,
    const cyaml_node_t* expected, const cyaml_doc_t* actual_doc,
    const cyaml_node_t* actual)
{
    TEST_ASSERT_NOT_NULL(expected);
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_INT(expected->type, actual->type);
    switch (expected->type) {
    case CYAML_SEQ:
        TEST_ASSERT_EQUAL_UINT32(expected->seq.count, actual->seq.count);
        for (uint32_t i = 0; i < expected->seq.count; i++)
            assert_same_tree(expected_doc, expected->seq.items[i],
                actual_doc, actual->seq.items[i]);
        break;
    case CYAML_MAP:
        TEST_ASSERT_EQUAL_UINT32(expected->map.count, actual->map.count);
        for (uint32_t i = 0; i < expected->map.count; i++) {
            assert_same_tree(expected_doc, expected->map.pairs[i].key,
                actual_doc, actual->map.pairs[i].key);
            assert_same_tree(expected_doc, expected->map.pairs[i].val,
                actual_doc, actual->map.pairs[i].val);
        }
        break;
    case CYAML_SCALAR: {
        char* expected_value = cyaml_scalar_str(expected_doc, expected);
        char* actual_value = cyaml_scalar_str(actual_doc, actual);
        TEST_ASSERT_NOT_NULL(expected_value);
        TEST_ASSERT_NOT_NULL(actual_value);
        TEST_ASSERT_EQUAL_STRING(expected_value, actual_value);
        free(actual_value);
        free(expected_value);
        break;
    }
    default:
        break;
    }
}

static void assert_round_trip(const cyaml_doc_t* doc, char* output, size_t len)
{
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL_size_t(strlen(output), len);
    cyaml_doc_t* parsed = cyaml_parse(output, len, NULL, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(parsed, output);
    assert_same_tree(doc, cyaml_root(doc), parsed, cyaml_root(parsed));
    cyaml_free(parsed);
    free(output);
}

static void assert_output(char* output, size_t len)
{
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL_size_t(strlen(output), len);
    free(output);
}

static void check_traversals(const char* yaml, bool json_compatible,
    bool round_trip_all)
{
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);
    size_t len = 0;
    UNITY_SET_DETAIL("emit block");
    char* output = cyaml_emit(doc, NULL, &len);
    if (json_compatible)
        assert_round_trip(doc, output, len);
    else
        assert_output(output, len);

    cyaml_emit_opts_t opts = CYAML_EMIT_DEFAULT;
    opts.preserve_style = true;
    UNITY_SET_DETAIL("emit flow");
    output = cyaml_emit(doc, &opts, &len);
    assert_round_trip(doc, output, len);

    UNITY_SET_DETAIL("dump");
    output = cyaml_dump(doc, &len);
    // Exercise these paths even for shapes with existing serialization issues.
    if (round_trip_all)
        assert_round_trip(doc, output, len);
    else
        assert_output(output, len);

    for (int mode = 0; mode < 2; mode++) {
        UNITY_SET_DETAIL("events");
        output = cyaml_events(doc, mode != 0, &len);
        TEST_ASSERT_NOT_NULL(output);
        TEST_ASSERT_EQUAL_size_t(strlen(output), len);
        TEST_ASSERT_NOT_NULL(strstr(output, "=VAL :leaf"));
        // A second traversal must produce exactly the same event stream.
        char* again = cyaml_events(doc, mode != 0, NULL);
        TEST_ASSERT_NOT_NULL(again);
        TEST_ASSERT_EQUAL_STRING(output, again);
        free(again);
        free(output);

        if (json_compatible) {
            UNITY_SET_DETAIL("json");
            output = cyaml_json(doc, mode * 2, &len);
            if (round_trip_all)
                assert_round_trip(doc, output, len);
            else
                assert_output(output, len);
        }
    }

    TEST_ASSERT_TRUE(cyaml_resolve_aliases(doc));
    cyaml_free(doc);
    UNITY_CLR_DETAILS();
}

void test_sequence_stack_growth(void)
{
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
        char* yaml = nested_yaml(depths[i], "[", "leaf", "]");
        check_traversals(yaml, true, true);
        free(yaml);
        yaml = nested_yaml(depths[i], "[", "leaf", ", tail]");
        check_traversals(yaml, true, false);
        free(yaml);
    }
}

void test_mapping_stack_growth(void)
{
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
        char* yaml = nested_yaml(depths[i], "{key: ", "leaf", ", tail: done}");
        check_traversals(yaml, true, true);
        free(yaml);
        yaml = nested_yaml(depths[i], "{", "leaf", ": value}");
        check_traversals(yaml, false, false);
        free(yaml);
    }
}

void test_alias_resolution_stack_growth(void)
{
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
        char* yaml = nested_yaml(depths[i], "{", "&target leaf",
            ": *target}");
        char* expected_yaml = nested_yaml(depths[i], "{", "leaf",
            ": leaf}");
        cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
        cyaml_doc_t* expected = cyaml_parse(expected_yaml,
            strlen(expected_yaml), NULL, NULL);
        TEST_ASSERT_NOT_NULL(doc);
        TEST_ASSERT_NOT_NULL(expected);
        TEST_ASSERT_TRUE(cyaml_resolve_aliases(doc));
        assert_same_tree(expected, cyaml_root(expected), doc, cyaml_root(doc));
        cyaml_free(expected);
        cyaml_free(doc);
        free(expected_yaml);
        free(yaml);
    }
}

void test_dump_multiple_pushes_at_stack_growth(void)
{
    const char* leaves[] = {
        "[{[leaf, other]: value}]",
        "[[leaf, inner], tail]",
        "[[{leaf: value}, tail]]",
        "[[{[leaf]: value}, tail]]",
    };
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
        for (size_t j = 0; j < sizeof(leaves) / sizeof(leaves[0]); j++) {
            char* yaml = nested_yaml(depths[i], "{key: ", leaves[j], "}");
            check_traversals(yaml, false, false);
            free(yaml);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sequence_stack_growth);
    RUN_TEST(test_mapping_stack_growth);
    RUN_TEST(test_alias_resolution_stack_growth);
    RUN_TEST(test_dump_multiple_pushes_at_stack_growth);
    return UNITY_END();
}
