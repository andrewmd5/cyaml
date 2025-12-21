#include "cyaml.h"
#include "unity.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) { }
void tearDown(void) { }

void test_cyaml_version(void)
{
    const char* ver = cyaml_version();
    TEST_ASSERT_NOT_NULL(ver);
    TEST_ASSERT_TRUE(strlen(ver) > 0);
}

void test_cyaml_strerror(void)
{
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_OK));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_NOMEM));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_SYNTAX));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_EOF));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_INDENT));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_ESCAPE));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_ANCHOR));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_ALIAS));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_TAG));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_DUP_KEY));
    TEST_ASSERT_NOT_NULL(cyaml_strerror(CYAML_ERR_IO));
}

void test_cyaml_parse_null_input(void)
{
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(NULL, 0, NULL, &err);
    TEST_ASSERT_NULL(doc);
}

void test_cyaml_parse_empty_string(void)
{
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse("", 0, NULL, &err);
    if (doc)
        cyaml_free(doc);
}

void test_cyaml_parse_simple_scalar(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(cyaml_root(doc));
    TEST_ASSERT_TRUE(cyaml_is_scalar(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_parse_simple_map(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(cyaml_root(doc));
    TEST_ASSERT_TRUE(cyaml_is_map(cyaml_root(doc)));
    TEST_ASSERT_EQUAL_UINT32(1, cyaml_map_len(cyaml_root(doc)));

    
    cyaml_pair_t* pair = cyaml_map_at(cyaml_root(doc), 0);
    TEST_ASSERT_NOT_NULL(pair);
    TEST_ASSERT_NOT_NULL(pair->key);
    TEST_ASSERT_NOT_NULL(pair->val);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->key->span, "key"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->val->span, "value"));
    cyaml_free(doc);
}

void test_cyaml_parse_simple_seq(void)
{
    const char* yaml = "- one\n- two\n- three";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(cyaml_root(doc));
    TEST_ASSERT_TRUE(cyaml_is_seq(cyaml_root(doc)));
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_seq_len(cyaml_root(doc)));

    
    const char* expected[] = { "one", "two", "three" };
    for (uint32_t i = 0; i < cyaml_seq_len(cyaml_root(doc)); i++) {
        cyaml_node_t* item = cyaml_seq_get(cyaml_root(doc), i);
        TEST_ASSERT_NOT_NULL(item);
        TEST_ASSERT_TRUE(cyaml_is_scalar(item));
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, item->span, expected[i]));
    }
    cyaml_free(doc);
}

void test_cyaml_parse_with_options(void)
{
    const char* yaml = "nested:\n  deep:\n    value: 1";
    cyaml_opts_t opts = { .max_depth = 5 };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_free(doc);
}

void test_cyaml_parse_syntax_error(void)
{
    const char* yaml = ":\n  invalid";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    if (doc)
        cyaml_free(doc);
}

void test_cyaml_free_null(void)
{
    cyaml_free(NULL);
}

void test_cyaml_parse_stream_single_doc(void)
{
    const char* yaml = "hello: world";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_UINT32(1, cyaml_stream_count(stream));
    TEST_ASSERT_NOT_NULL(cyaml_stream_doc(stream, 0));
    cyaml_stream_free(stream);
}

void test_cyaml_parse_stream_multi_doc(void)
{
    const char* yaml = "---\nfirst\n---\nsecond\n---\nthird";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_stream_count(stream));

    
    const char* expected[] = { "first", "second", "third" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_doc_t* doc = cyaml_stream_doc(stream, i);
        TEST_ASSERT_NOT_NULL(doc);
        cyaml_node_t* root = cyaml_root(doc);
        TEST_ASSERT_NOT_NULL(root);
        TEST_ASSERT_TRUE(cyaml_is_scalar(root));
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, root->span, expected[i]));
    }
    cyaml_stream_free(stream);
}

void test_cyaml_stream_doc_out_of_bounds(void)
{
    const char* yaml = "doc";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_NULL(cyaml_stream_doc(stream, 100));
    cyaml_stream_free(stream);
}

void test_cyaml_stream_count_null(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_stream_count(NULL));
}

void test_cyaml_stream_doc_null(void)
{
    TEST_ASSERT_NULL(cyaml_stream_doc(NULL, 0));
}

void test_cyaml_stream_free_null(void)
{
    cyaml_stream_free(NULL);
}

void test_cyaml_root_null(void)
{
    TEST_ASSERT_NULL(cyaml_root(NULL));
}

void test_cyaml_src(void)
{
    const char* yaml = "test";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(cyaml_src(doc));
    TEST_ASSERT_EQUAL_PTR(yaml, cyaml_src(doc));
    cyaml_free(doc);
}

void test_cyaml_src_null(void)
{
    TEST_ASSERT_NULL(cyaml_src(NULL));
}

void test_cyaml_src_len(void)
{
    const char* yaml = "test";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_UINT32(strlen(yaml), cyaml_src_len(doc));
    cyaml_free(doc);
}

void test_cyaml_src_len_null(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_src_len(NULL));
}

void test_cyaml_span_ptr(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_root(doc);
    TEST_ASSERT_NOT_NULL(root);
    const char* ptr = cyaml_span_ptr(doc, root->span);
    TEST_ASSERT_NOT_NULL(ptr);
    cyaml_free(doc);
}

void test_cyaml_span_dup(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_root(doc);
    char* dup = cyaml_span_dup(doc, root->span);
    TEST_ASSERT_NOT_NULL(dup);
    TEST_ASSERT_EQUAL_STRING("hello", dup);
    free(dup);
    cyaml_free(doc);
}

void test_cyaml_scalar_str_plain(void)
{
    const char* yaml = "hello world";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    char* str = cyaml_scalar_str(doc, cyaml_root(doc));
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_STRING("hello world", str);
    free(str);
    cyaml_free(doc);
}

void test_cyaml_scalar_str_quoted(void)
{
    const char* yaml = "\"hello\\nworld\"";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    char* str = cyaml_scalar_str(doc, cyaml_root(doc));
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_STRING("hello\nworld", str);
    free(str);
    cyaml_free(doc);
}

void test_cyaml_span_eq(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_root(doc);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, root->span, "hello"));
    TEST_ASSERT_FALSE(cyaml_span_eq(doc, root->span, "world"));
    TEST_ASSERT_FALSE(cyaml_span_eq(doc, root->span, "HELLO"));
    cyaml_free(doc);
}

void test_cyaml_span_ieq(void)
{
    const char* yaml = "Hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_root(doc);
    TEST_ASSERT_TRUE(cyaml_span_ieq(doc, root->span, "hello"));
    TEST_ASSERT_TRUE(cyaml_span_ieq(doc, root->span, "HELLO"));
    TEST_ASSERT_TRUE(cyaml_span_ieq(doc, root->span, "HeLLo"));
    TEST_ASSERT_FALSE(cyaml_span_ieq(doc, root->span, "world"));
    cyaml_free(doc);
}

void test_cyaml_span_cmp(void)
{
    const char* yaml = "key: key";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_root(doc);
    cyaml_pair_t* pair = cyaml_map_at(root, 0);
    TEST_ASSERT_NOT_NULL(pair);
    TEST_ASSERT_TRUE(cyaml_span_cmp(doc, pair->key->span, pair->val->span));
    cyaml_free(doc);
}

void test_cyaml_is_null_with_null_ptr(void)
{
    TEST_ASSERT_TRUE(cyaml_is_null(NULL));
}

void test_cyaml_is_null_with_null_node(void)
{
    const char* yaml = "~";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_null(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_scalar(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_scalar(cyaml_root(doc)));
    TEST_ASSERT_FALSE(cyaml_is_seq(cyaml_root(doc)));
    TEST_ASSERT_FALSE(cyaml_is_map(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_seq(void)
{
    const char* yaml = "- item";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_seq(cyaml_root(doc)));
    TEST_ASSERT_FALSE(cyaml_is_scalar(cyaml_root(doc)));
    TEST_ASSERT_FALSE(cyaml_is_map(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_map(void)
{
    const char* yaml = "key: val";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_map(cyaml_root(doc)));
    TEST_ASSERT_FALSE(cyaml_is_scalar(cyaml_root(doc)));
    TEST_ASSERT_FALSE(cyaml_is_seq(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_alias(void)
{
    const char* yaml = "- &anchor value\n- *anchor";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_root(doc);
    TEST_ASSERT_TRUE(cyaml_is_seq(root));
    cyaml_node_t* first = cyaml_seq_get(root, 0);
    cyaml_node_t* second = cyaml_seq_get(root, 1);
    TEST_ASSERT_TRUE(cyaml_is_scalar(first));
    TEST_ASSERT_TRUE(cyaml_is_alias(second));
    cyaml_free(doc);
}

void test_cyaml_val(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_span_t span = cyaml_val(cyaml_root(doc));
    TEST_ASSERT_EQUAL_UINT32(5, span.len);
    cyaml_free(doc);
}

void test_cyaml_str(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    const char* str = cyaml_str(doc, cyaml_root(doc));
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_MEMORY("hello", str, 5);
    cyaml_free(doc);
}

void test_cyaml_len(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_UINT32(5, cyaml_len(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_as_int_positive(void)
{
    const char* yaml = "42";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    int64_t val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_root(doc), &val));
    TEST_ASSERT_EQUAL_INT64(42, val);
    cyaml_free(doc);
}

void test_cyaml_as_int_negative(void)
{
    const char* yaml = "-123";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    int64_t val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_root(doc), &val));
    TEST_ASSERT_EQUAL_INT64(-123, val);
    cyaml_free(doc);
}

void test_cyaml_as_int_hex(void)
{
    const char* yaml = "0xff";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    int64_t val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_root(doc), &val));
    TEST_ASSERT_EQUAL_INT64(255, val);
    cyaml_free(doc);
}

void test_cyaml_as_int_octal(void)
{
    const char* yaml = "0o77";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    int64_t val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_root(doc), &val));
    TEST_ASSERT_EQUAL_INT64(63, val);
    cyaml_free(doc);
}

void test_cyaml_as_int_invalid(void)
{
    const char* yaml = "not_a_number";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    int64_t val;
    TEST_ASSERT_FALSE(cyaml_as_int(doc, cyaml_root(doc), &val));
    cyaml_free(doc);
}

void test_cyaml_as_uint(void)
{
    const char* yaml = "12345678901234";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    uint64_t val;
    TEST_ASSERT_TRUE(cyaml_as_uint(doc, cyaml_root(doc), &val));
    TEST_ASSERT_EQUAL_UINT64(12345678901234ULL, val);
    cyaml_free(doc);
}

void test_cyaml_as_float_normal(void)
{
    const char* yaml = "3.14159";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    double val;
    TEST_ASSERT_TRUE(cyaml_as_float(doc, cyaml_root(doc), &val));
    TEST_ASSERT_DOUBLE_WITHIN(0.00001, 3.14159, val);
    cyaml_free(doc);
}

void test_cyaml_as_float_inf(void)
{
    const char* yaml = ".inf";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    double val;
    TEST_ASSERT_TRUE(cyaml_as_float(doc, cyaml_root(doc), &val));
    TEST_ASSERT_TRUE(isinf(val) && val > 0);
    cyaml_free(doc);
}

void test_cyaml_as_float_neg_inf(void)
{
    const char* yaml = "-.inf";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    double val;
    TEST_ASSERT_TRUE(cyaml_as_float(doc, cyaml_root(doc), &val));
    TEST_ASSERT_TRUE(isinf(val) && val < 0);
    cyaml_free(doc);
}

void test_cyaml_as_float_nan(void)
{
    const char* yaml = ".nan";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    double val;
    TEST_ASSERT_TRUE(cyaml_as_float(doc, cyaml_root(doc), &val));
    TEST_ASSERT_TRUE(isnan(val));
    cyaml_free(doc);
}

void test_cyaml_as_bool_true(void)
{
    const char* yaml = "true";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    bool val;
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, cyaml_root(doc), &val));
    TEST_ASSERT_TRUE(val);
    cyaml_free(doc);
}

void test_cyaml_as_bool_false(void)
{
    const char* yaml = "false";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    bool val;
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, cyaml_root(doc), &val));
    TEST_ASSERT_FALSE(val);
    cyaml_free(doc);
}

void test_cyaml_as_bool_case_insensitive(void)
{
    const char* yaml = "TRUE";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    bool val;
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, cyaml_root(doc), &val));
    TEST_ASSERT_TRUE(val);
    cyaml_free(doc);
}

void test_cyaml_is_null_val_tilde(void)
{
    const char* yaml = "~";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_null_val(doc, cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_null_val_null(void)
{
    const char* yaml = "null";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_null_val(doc, cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_null_val_NULL(void)
{
    const char* yaml = "NULL";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_null_val(doc, cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_seq_len(void)
{
    const char* yaml = "- a\n- b\n- c";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_seq_len(cyaml_root(doc)));

    
    const char* expected[] = { "a", "b", "c" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_node_t* item = cyaml_seq_get(cyaml_root(doc), i);
        TEST_ASSERT_NOT_NULL(item);
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, item->span, expected[i]));
    }
    cyaml_free(doc);
}

void test_cyaml_seq_len_null(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_seq_len(NULL));
}

void test_cyaml_seq_get(void)
{
    const char* yaml = "- first\n- second";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* first = cyaml_seq_get(cyaml_root(doc), 0);
    cyaml_node_t* second = cyaml_seq_get(cyaml_root(doc), 1);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, first->span, "first"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, second->span, "second"));
    cyaml_free(doc);
}

void test_cyaml_seq_get_out_of_bounds(void)
{
    const char* yaml = "- item";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NULL(cyaml_seq_get(cyaml_root(doc), 100));
    cyaml_free(doc);
}

void test_cyaml_seq_get_null(void)
{
    TEST_ASSERT_NULL(cyaml_seq_get(NULL, 0));
}

void test_cyaml_map_len(void)
{
    const char* yaml = "a: 1\nb: 2\nc: 3";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_map_len(cyaml_root(doc)));

    
    const char* keys[] = { "a", "b", "c" };
    const char* vals[] = { "1", "2", "3" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_pair_t* pair = cyaml_map_at(cyaml_root(doc), i);
        TEST_ASSERT_NOT_NULL(pair);
        TEST_ASSERT_NOT_NULL(pair->key);
        TEST_ASSERT_NOT_NULL(pair->val);
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->key->span, keys[i]));
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->val->span, vals[i]));
    }
    cyaml_free(doc);
}

void test_cyaml_map_len_null(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_map_len(NULL));
}

void test_cyaml_map_at(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_pair_t* pair = cyaml_map_at(cyaml_root(doc), 0);
    TEST_ASSERT_NOT_NULL(pair);
    TEST_ASSERT_NOT_NULL(pair->key);
    TEST_ASSERT_NOT_NULL(pair->val);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->key->span, "key"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->val->span, "value"));
    cyaml_free(doc);
}

void test_cyaml_map_at_out_of_bounds(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NULL(cyaml_map_at(cyaml_root(doc), 100));
    cyaml_free(doc);
}

void test_cyaml_map_at_null(void)
{
    TEST_ASSERT_NULL(cyaml_map_at(NULL, 0));
}

void test_cyaml_get(void)
{
    const char* yaml = "name: John\nage: 30";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* name = cyaml_get(doc, cyaml_root(doc), "name");
    cyaml_node_t* age = cyaml_get(doc, cyaml_root(doc), "age");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_NOT_NULL(age);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, name->span, "John"));
    int64_t age_val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, age, &age_val));
    TEST_ASSERT_EQUAL_INT64(30, age_val);
    cyaml_free(doc);
}

void test_cyaml_get_not_found(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NULL(cyaml_get(doc, cyaml_root(doc), "nonexistent"));
    cyaml_free(doc);
}

void test_cyaml_has(void)
{
    const char* yaml = "exists: yes";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_has(doc, cyaml_root(doc), "exists"));
    TEST_ASSERT_FALSE(cyaml_has(doc, cyaml_root(doc), "missing"));
    cyaml_free(doc);
}

void test_cyaml_path_simple(void)
{
    const char* yaml = "user:\n  name: Alice";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* name = cyaml_path(doc, "/user/name");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, name->span, "Alice"));
    cyaml_free(doc);
}

void test_cyaml_path_array_index(void)
{
    const char* yaml = "items:\n  - first\n  - second";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* item = cyaml_path(doc, "/items[1]");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, item->span, "second"));
    cyaml_free(doc);
}

void test_cyaml_path_nested(void)
{
    const char* yaml = "a:\n  b:\n    c: deep";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* deep = cyaml_path(doc, "/a/b/c");
    TEST_ASSERT_NOT_NULL(deep);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, deep->span, "deep"));
    cyaml_free(doc);
}

void test_cyaml_path_not_found(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NULL(cyaml_path(doc, "/missing/path"));
    cyaml_free(doc);
}

void test_cyaml_doc_new(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_free(doc);
}

void test_cyaml_new_null(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_null(doc);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_null(node));
    cyaml_free(doc);
}

void test_cyaml_new_str(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_str(doc, "hello", 5);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    cyaml_free(doc);
}

void test_cyaml_new_cstr(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_cstr(doc, "hello world");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    cyaml_free(doc);
}

void test_cyaml_new_int(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_int(doc, -42);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    int64_t val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, node, &val));
    TEST_ASSERT_EQUAL_INT64(-42, val);
    cyaml_free(doc);
}

void test_cyaml_new_uint(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_uint(doc, 18446744073709551615ULL);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    uint64_t val;
    TEST_ASSERT_TRUE(cyaml_as_uint(doc, node, &val));
    TEST_ASSERT_EQUAL_UINT64(18446744073709551615ULL, val);
    cyaml_free(doc);
}

void test_cyaml_new_float(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_float(doc, 3.14159);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    double val;
    TEST_ASSERT_TRUE(cyaml_as_float(doc, node, &val));
    TEST_ASSERT_DOUBLE_WITHIN(0.00001, 3.14159, val);
    cyaml_free(doc);
}

void test_cyaml_new_bool_true(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_bool(doc, true);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    bool val;
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, node, &val));
    TEST_ASSERT_TRUE(val);
    cyaml_free(doc);
}

void test_cyaml_new_bool_false(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* node = cyaml_new_bool(doc, false);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(cyaml_is_scalar(node));
    bool val;
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, node, &val));
    TEST_ASSERT_FALSE(val);
    cyaml_free(doc);
}

void test_cyaml_new_seq(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* seq = cyaml_new_seq(doc);
    TEST_ASSERT_NOT_NULL(seq);
    TEST_ASSERT_TRUE(cyaml_is_seq(seq));
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_seq_len(seq));
    cyaml_free(doc);
}

void test_cyaml_new_map(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* map = cyaml_new_map(doc);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_TRUE(cyaml_is_map(map));
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_map_len(map));
    cyaml_free(doc);
}

void test_cyaml_seq_push(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* seq = cyaml_new_seq(doc);
    TEST_ASSERT_TRUE(cyaml_seq_push(seq, cyaml_new_cstr(doc, "one")));
    TEST_ASSERT_TRUE(cyaml_seq_push(seq, cyaml_new_cstr(doc, "two")));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_seq_len(seq));
    cyaml_free(doc);
}

void test_cyaml_map_set(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* map = cyaml_new_map(doc);
    TEST_ASSERT_TRUE(cyaml_map_set(doc, map, "name", cyaml_new_cstr(doc, "value")));
    TEST_ASSERT_EQUAL_UINT32(1, cyaml_map_len(map));
    TEST_ASSERT_TRUE(cyaml_has(doc, map, "name"));
    cyaml_free(doc);
}

void test_cyaml_set_root(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* root = cyaml_new_cstr(doc, "root value");
    cyaml_set_root(doc, root);
    TEST_ASSERT_EQUAL_PTR(root, cyaml_root(doc));
    cyaml_free(doc);
}

void test_cyaml_node_new(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* scalar = cyaml_node_new(doc, CYAML_SCALAR);
    TEST_ASSERT_NOT_NULL(scalar);
    TEST_ASSERT_EQUAL_INT(CYAML_SCALAR, scalar->type);
    cyaml_node_t* seq = cyaml_node_new(doc, CYAML_SEQ);
    TEST_ASSERT_NOT_NULL(seq);
    TEST_ASSERT_EQUAL_INT(CYAML_SEQ, seq->type);
    cyaml_node_t* map = cyaml_node_new(doc, CYAML_MAP);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_INT(CYAML_MAP, map->type);
    cyaml_free(doc);
}

void test_cyaml_emit_simple(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);
    cyaml_map_set(doc, root, "key", cyaml_new_cstr(doc, "value"));

    size_t len;
    char* yaml = cyaml_emit(doc, NULL, &len);
    TEST_ASSERT_NOT_NULL(yaml);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(yaml, "key"));
    TEST_ASSERT_NOT_NULL(strstr(yaml, "value"));
    free(yaml);
    cyaml_free(doc);
}

void test_cyaml_emit_with_options(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_cstr(doc, "hello");
    cyaml_set_root(doc, root);

    cyaml_emit_opts_t opts = { .indent = 4, .doc_start = true };
    size_t len;
    char* yaml = cyaml_emit(doc, &opts, &len);
    TEST_ASSERT_NOT_NULL(yaml);
    TEST_ASSERT_NOT_NULL(strstr(yaml, "---"));
    free(yaml);
    cyaml_free(doc);
}

void test_cyaml_emit_null_doc(void)
{
    size_t len;
    char* yaml = cyaml_emit(NULL, NULL, &len);
    TEST_ASSERT_NULL(yaml);
}

void test_cyaml_stream_emit(void)
{
    const char* input = "---\nfirst\n---\nsecond";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);

    size_t len;
    char* yaml = cyaml_stream_emit(stream, &len);
    TEST_ASSERT_NOT_NULL(yaml);
    TEST_ASSERT_TRUE(len > 0);
    free(yaml);
    cyaml_stream_free(stream);
}

void test_cyaml_dump(void)
{
    const char* input = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    size_t len;
    char* dump = cyaml_dump(doc, &len);
    TEST_ASSERT_NOT_NULL(dump);
    TEST_ASSERT_TRUE(len > 0);
    free(dump);
    cyaml_free(doc);
}

void test_cyaml_stream_dump(void)
{
    const char* input = "---\ndoc1\n---\ndoc2";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);

    size_t len;
    char* dump = cyaml_stream_dump(stream, &len);
    TEST_ASSERT_NOT_NULL(dump);
    TEST_ASSERT_TRUE(len > 0);
    free(dump);
    cyaml_stream_free(stream);
}

void test_cyaml_events(void)
{
    const char* input = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    size_t len;
    char* events = cyaml_events(doc, false, &len);
    TEST_ASSERT_NOT_NULL(events);
    TEST_ASSERT_NOT_NULL(strstr(events, "+DOC"));
    TEST_ASSERT_NOT_NULL(strstr(events, "-DOC"));
    free(events);
    cyaml_free(doc);
}

void test_cyaml_stream_events(void)
{
    const char* input = "hello";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);

    size_t len;
    char* events = cyaml_stream_events(stream, false, &len);
    TEST_ASSERT_NOT_NULL(events);
    TEST_ASSERT_NOT_NULL(strstr(events, "+STR"));
    TEST_ASSERT_NOT_NULL(strstr(events, "-STR"));
    free(events);
    cyaml_stream_free(stream);
}

void test_cyaml_json_simple(void)
{
    const char* input = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    size_t len;
    char* json = cyaml_json(doc, 0, &len);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"key\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"value\""));
    free(json);
    cyaml_free(doc);
}

void test_cyaml_json_with_indent(void)
{
    const char* input = "a: 1\nb: 2";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    size_t len;
    char* json = cyaml_json(doc, 2, &len);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\n"));
    free(json);
    cyaml_free(doc);
}

void test_cyaml_stream_json_single(void)
{
    const char* input = "value";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);

    size_t len;
    char* json = cyaml_stream_json(stream, 0, &len);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"value\""));
    free(json);
    cyaml_stream_free(stream);
}

void test_cyaml_stream_json_multi(void)
{
    const char* input = "---\none\n---\ntwo";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_stream_count(stream));

    size_t len;
    char* json = cyaml_stream_json(stream, 0, &len);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL_CHAR('[', json[0]);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"one\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"two\""));
    free(json);
    cyaml_stream_free(stream);
}

void test_nested_structures(void)
{
    const char* yaml = "users:\n"
                       "  - name: Alice\n"
                       "    age: 30\n"
                       "  - name: Bob\n"
                       "    age: 25\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* alice_name = cyaml_path(doc, "/users[0]/name");
    TEST_ASSERT_NOT_NULL(alice_name);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, alice_name->span, "Alice"));

    cyaml_node_t* bob_age = cyaml_path(doc, "/users[1]/age");
    TEST_ASSERT_NOT_NULL(bob_age);
    int64_t age;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, bob_age, &age));
    TEST_ASSERT_EQUAL_INT64(25, age);

    cyaml_free(doc);
}

void test_build_and_emit(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);

    cyaml_node_t* users = cyaml_new_seq(doc);
    cyaml_map_set(doc, root, "users", users);

    cyaml_node_t* user1 = cyaml_new_map(doc);
    cyaml_map_set(doc, user1, "name", cyaml_new_cstr(doc, "Alice"));
    cyaml_map_set(doc, user1, "active", cyaml_new_bool(doc, true));
    cyaml_seq_push(users, user1);

    size_t len;
    char* yaml = cyaml_emit(doc, NULL, &len);
    TEST_ASSERT_NOT_NULL(yaml);
    TEST_ASSERT_NOT_NULL(strstr(yaml, "users"));
    TEST_ASSERT_NOT_NULL(strstr(yaml, "Alice"));

    free(yaml);
    cyaml_free(doc);
}

void test_round_trip(void)
{
    const char* input = "config:\n  port: 8080\n  host: localhost\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    size_t len;
    char* output = cyaml_emit(doc, NULL, &len);
    TEST_ASSERT_NOT_NULL(output);

    cyaml_doc_t* doc2 = cyaml_parse(output, len, NULL, &err);
    TEST_ASSERT_NOT_NULL(doc2);

    cyaml_node_t* port = cyaml_path(doc2, "/config/port");
    TEST_ASSERT_NOT_NULL(port);
    int64_t port_val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc2, port, &port_val));
    TEST_ASSERT_EQUAL_INT64(8080, port_val);

    free(output);
    cyaml_free(doc);
    cyaml_free(doc2);
}

void test_iteration_macros(void)
{
    const char* yaml = "- a\n- b\n- c";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* root = cyaml_root(doc);
    cyaml_node_t* item;
    uint32_t count = 0;
    const char* expected[] = { "a", "b", "c" };

    CYAML_EACH_SEQ(root, item, i)
    {
        TEST_ASSERT_NOT_NULL(item);
        TEST_ASSERT_TRUE(cyaml_is_scalar(item));
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, item->span, expected[i]));
        count++;
    }
    TEST_ASSERT_EQUAL_UINT32(3, count);

    cyaml_free(doc);

    // Test CYAML_EACH_MAP
    const char* yaml_map = "x: 1\ny: 2\nz: 3";
    doc = cyaml_parse(yaml_map, strlen(yaml_map), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    root = cyaml_root(doc);
    cyaml_pair_t* pair;
    count = 0;
    const char* exp_keys[] = { "x", "y", "z" };
    const char* exp_vals[] = { "1", "2", "3" };

    CYAML_EACH_MAP(root, pair, j)
    {
        TEST_ASSERT_NOT_NULL(pair);
        TEST_ASSERT_NOT_NULL(pair->key);
        TEST_ASSERT_NOT_NULL(pair->val);
        TEST_ASSERT_TRUE(cyaml_is_scalar(pair->key));
        TEST_ASSERT_TRUE(cyaml_is_scalar(pair->val));
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->key->span, exp_keys[j]));
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, pair->val->span, exp_vals[j]));
        count++;
    }
    TEST_ASSERT_EQUAL_UINT32(3, count);

    cyaml_free(doc);
}

void test_flow_style(void)
{
    const char* yaml = "{key: value, num: 123}";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_map(cyaml_root(doc)));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_map_len(cyaml_root(doc)));

    
    cyaml_node_t* key_val = cyaml_get(doc, cyaml_root(doc), "key");
    cyaml_node_t* num_val = cyaml_get(doc, cyaml_root(doc), "num");
    TEST_ASSERT_NOT_NULL(key_val);
    TEST_ASSERT_NOT_NULL(num_val);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, key_val->span, "value"));
    int64_t num;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, num_val, &num));
    TEST_ASSERT_EQUAL_INT64(123, num);
    cyaml_free(doc);
}

void test_flow_sequence(void)
{
    const char* yaml = "[one, two, three]";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_is_seq(cyaml_root(doc)));
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_seq_len(cyaml_root(doc)));

    
    const char* expected[] = { "one", "two", "three" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_node_t* item = cyaml_seq_get(cyaml_root(doc), i);
        TEST_ASSERT_NOT_NULL(item);
        TEST_ASSERT_TRUE(cyaml_span_eq(doc, item->span, expected[i]));
    }
    cyaml_free(doc);
}

void test_multiline_string(void)
{
    const char* yaml = "text: |\n  line 1\n  line 2\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* text = cyaml_get(doc, cyaml_root(doc), "text");
    TEST_ASSERT_NOT_NULL(text);
    char* str = cyaml_scalar_str(doc, text);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_NOT_NULL(strstr(str, "line 1"));
    TEST_ASSERT_NOT_NULL(strstr(str, "line 2"));
    free(str);
    cyaml_free(doc);
}

void test_folded_string(void)
{
    const char* yaml = "text: >\n  line 1\n  line 2\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_node_t* text = cyaml_get(doc, cyaml_root(doc), "text");
    TEST_ASSERT_NOT_NULL(text);
    char* str = cyaml_scalar_str(doc, text);
    TEST_ASSERT_NOT_NULL(str);
    free(str);
    cyaml_free(doc);
}



void test_cyaml_set_anchor(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* node = cyaml_new_cstr(doc, "value");
    TEST_ASSERT_TRUE(cyaml_set_anchor(doc, node, "myanchor"));
    TEST_ASSERT_EQUAL_UINT32(8, cyaml_anchor_len(node));
    const char* anchor = cyaml_anchor(doc, node);
    TEST_ASSERT_NOT_NULL(anchor);
    TEST_ASSERT_EQUAL_MEMORY("myanchor", anchor, 8);
    cyaml_free(doc);
}

void test_cyaml_set_anchor_clear(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* node = cyaml_new_cstr(doc, "value");
    TEST_ASSERT_TRUE(cyaml_set_anchor(doc, node, "anchor"));
    TEST_ASSERT_TRUE(cyaml_anchor_len(node) > 0);
    TEST_ASSERT_TRUE(cyaml_set_anchor(doc, node, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_anchor_len(node));
    cyaml_free(doc);
}

void test_cyaml_new_alias(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* target = cyaml_new_cstr(doc, "value");
    cyaml_set_anchor(doc, target, "anchor");
    cyaml_node_t* alias = cyaml_new_alias(doc, target);
    TEST_ASSERT_NOT_NULL(alias);
    TEST_ASSERT_TRUE(cyaml_is_alias(alias));
    TEST_ASSERT_EQUAL_PTR(target, alias->alias.target);
    cyaml_free(doc);
}

void test_cyaml_new_alias_no_anchor(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* target = cyaml_new_cstr(doc, "value");
    
    cyaml_node_t* alias = cyaml_new_alias(doc, target);
    TEST_ASSERT_NULL(alias);
    cyaml_free(doc);
}

void test_cyaml_find_anchor(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);

    cyaml_node_t* val = cyaml_new_cstr(doc, "anchored");
    cyaml_set_anchor(doc, val, "myref");
    cyaml_map_set(doc, root, "key", val);

    cyaml_node_t* found = cyaml_find_anchor(doc, "myref");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(val, found);

    TEST_ASSERT_NULL(cyaml_find_anchor(doc, "nonexistent"));
    cyaml_free(doc);
}

void test_cyaml_find_anchor_nested(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_seq(doc);
    cyaml_set_root(doc, root);

    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_seq_push(root, map);

    cyaml_node_t* deep = cyaml_new_cstr(doc, "deep value");
    cyaml_set_anchor(doc, deep, "deepanchor");
    cyaml_map_set(doc, map, "nested", deep);

    cyaml_node_t* found = cyaml_find_anchor(doc, "deepanchor");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(deep, found);
    cyaml_free(doc);
}



void test_cyaml_node_copy_scalar(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* orig = cyaml_new_cstr(doc, "hello");
    cyaml_node_t* copy = cyaml_node_copy(doc, doc, orig);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_TRUE(cyaml_is_scalar(copy));
    TEST_ASSERT_NOT_EQUAL(orig, copy);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, copy->span, "hello"));
    cyaml_free(doc);
}

void test_cyaml_node_copy_seq(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* seq = cyaml_new_seq(doc);
    cyaml_seq_push(seq, cyaml_new_cstr(doc, "a"));
    cyaml_seq_push(seq, cyaml_new_cstr(doc, "b"));

    cyaml_node_t* copy = cyaml_node_copy(doc, doc, seq);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_TRUE(cyaml_is_seq(copy));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_seq_len(copy));
    TEST_ASSERT_NOT_EQUAL(seq, copy);
    cyaml_free(doc);
}

void test_cyaml_node_copy_map(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_map_set(doc, map, "key1", cyaml_new_cstr(doc, "val1"));
    cyaml_map_set(doc, map, "key2", cyaml_new_int(doc, 42));

    cyaml_node_t* copy = cyaml_node_copy(doc, doc, map);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_TRUE(cyaml_is_map(copy));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_map_len(copy));
    TEST_ASSERT_NOT_EQUAL(map, copy);

    cyaml_node_t* val1 = cyaml_get(doc, copy, "key1");
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, val1->span, "val1"));
    cyaml_free(doc);
}

void test_cyaml_node_copy_deep(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_node_t* nested = cyaml_new_map(doc);
    cyaml_map_set(doc, nested, "inner", cyaml_new_cstr(doc, "value"));
    cyaml_map_set(doc, root, "outer", nested);

    cyaml_node_t* copy = cyaml_node_copy(doc, doc, root);
    TEST_ASSERT_NOT_NULL(copy);

    cyaml_node_t* copy_nested = cyaml_get(doc, copy, "outer");
    TEST_ASSERT_NOT_NULL(copy_nested);
    TEST_ASSERT_NOT_EQUAL(nested, copy_nested);

    cyaml_node_t* copy_inner = cyaml_get(doc, copy_nested, "inner");
    TEST_ASSERT_NOT_NULL(copy_inner);
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, copy_inner->span, "value"));
    cyaml_free(doc);
}



void test_cyaml_map_merge_simple(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* dst = cyaml_new_map(doc);
    cyaml_map_set(doc, dst, "a", cyaml_new_int(doc, 1));

    cyaml_node_t* src = cyaml_new_map(doc);
    cyaml_map_set(doc, src, "b", cyaml_new_int(doc, 2));

    TEST_ASSERT_TRUE(cyaml_map_merge(doc, dst, src));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_map_len(dst));
    TEST_ASSERT_TRUE(cyaml_has(doc, dst, "a"));
    TEST_ASSERT_TRUE(cyaml_has(doc, dst, "b"));
    cyaml_free(doc);
}

void test_cyaml_map_merge_overwrite(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* dst = cyaml_new_map(doc);
    cyaml_map_set(doc, dst, "key", cyaml_new_int(doc, 1));

    cyaml_node_t* src = cyaml_new_map(doc);
    cyaml_map_set(doc, src, "key", cyaml_new_int(doc, 99));

    TEST_ASSERT_TRUE(cyaml_map_merge(doc, dst, src));
    TEST_ASSERT_EQUAL_UINT32(1, cyaml_map_len(dst));

    int64_t val;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_get(doc, dst, "key"), &val));
    TEST_ASSERT_EQUAL_INT64(99, val);
    cyaml_free(doc);
}

void test_cyaml_map_merge_deep(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();

    
    cyaml_node_t* dst = cyaml_new_map(doc);
    cyaml_node_t* dst_config = cyaml_new_map(doc);
    cyaml_map_set(doc, dst_config, "port", cyaml_new_int(doc, 80));
    cyaml_map_set(doc, dst_config, "host", cyaml_new_cstr(doc, "localhost"));
    cyaml_map_set(doc, dst, "config", dst_config);

    
    cyaml_node_t* src = cyaml_new_map(doc);
    cyaml_node_t* src_config = cyaml_new_map(doc);
    cyaml_map_set(doc, src_config, "port", cyaml_new_int(doc, 8080));
    cyaml_map_set(doc, src_config, "debug", cyaml_new_bool(doc, true));
    cyaml_map_set(doc, src, "config", src_config);

    TEST_ASSERT_TRUE(cyaml_map_merge(doc, dst, src));

    
    cyaml_node_t* merged_config = cyaml_get(doc, dst, "config");
    TEST_ASSERT_NOT_NULL(merged_config);
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_map_len(merged_config));

    int64_t port;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_get(doc, merged_config, "port"), &port));
    TEST_ASSERT_EQUAL_INT64(8080, port); 

    TEST_ASSERT_TRUE(cyaml_has(doc, merged_config, "host")); 
    TEST_ASSERT_TRUE(cyaml_has(doc, merged_config, "debug")); 

    cyaml_free(doc);
}



void test_cyaml_resolve_aliases(void)
{
    const char* yaml = "- &anchor value\n- *anchor\n- *anchor";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* root = cyaml_root(doc);
    TEST_ASSERT_TRUE(cyaml_is_alias(cyaml_seq_get(root, 1)));
    TEST_ASSERT_TRUE(cyaml_is_alias(cyaml_seq_get(root, 2)));

    TEST_ASSERT_TRUE(cyaml_resolve_aliases(doc));

    
    TEST_ASSERT_TRUE(cyaml_is_scalar(cyaml_seq_get(root, 1)));
    TEST_ASSERT_TRUE(cyaml_is_scalar(cyaml_seq_get(root, 2)));

    cyaml_free(doc);
}



void test_cyaml_comment_count_null(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_comment_count(NULL));
}

void test_cyaml_comment_count_no_comments_option(void)
{
    const char* yaml = "key: value  # comment";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    
    TEST_ASSERT_EQUAL_UINT32(0, cyaml_comment_count(doc));
    cyaml_free(doc);
}

void test_cyaml_comment_count_with_comments(void)
{
    const char* yaml = "# header comment\nkey: value  # inline comment";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    
    TEST_ASSERT_TRUE(cyaml_comment_count(doc) >= 1);
    cyaml_free(doc);
}

void test_cyaml_comment_at_null(void)
{
    cyaml_span_t span = cyaml_comment_at(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, span.len);
    TEST_ASSERT_EQUAL_UINT32(0, span.off);
}

void test_cyaml_comment_at_out_of_bounds(void)
{
    const char* yaml = "# comment\nkey: value";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    
    cyaml_span_t span = cyaml_comment_at(doc, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, span.len);
    cyaml_free(doc);
}

void test_cyaml_comment_at_valid(void)
{
    const char* yaml = "# this is a comment\nkey: value";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    if (cyaml_comment_count(doc) > 0) {
        cyaml_span_t span = cyaml_comment_at(doc, 0);
        TEST_ASSERT_TRUE(span.len > 0);
        
        const char* ptr = cyaml_span_ptr(doc, span);
        TEST_ASSERT_NOT_NULL(ptr);
    }
    cyaml_free(doc);
}

void test_cyaml_comment_multiple(void)
{
    const char* yaml = "# first comment\n# second comment\nkey: value";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    uint32_t count = cyaml_comment_count(doc);
    if (count >= 2) {
        cyaml_span_t span0 = cyaml_comment_at(doc, 0);
        cyaml_span_t span1 = cyaml_comment_at(doc, 1);
        TEST_ASSERT_TRUE(span0.len > 0);
        TEST_ASSERT_TRUE(span1.len > 0);
        
        TEST_ASSERT_NOT_EQUAL(span0.off, span1.off);
    }
    cyaml_free(doc);
}



void test_cyaml_emit_with_comments_disabled(void)
{
    const char* yaml = "# header comment\nkey: value";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = false };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NULL(strstr(output, "# header"));
    TEST_ASSERT_NOT_NULL(strstr(output, "key"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_with_comments_enabled(void)
{
    const char* yaml = "# header comment\nkey: value";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "# header comment"));
    TEST_ASSERT_NOT_NULL(strstr(output, "key"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_inline_comment(void)
{
    const char* yaml = "key: value  # inline comment";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "# inline comment"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_seq_with_comments(void)
{
    const char* yaml = "# list header\n- one  # first item\n- two\n# trailing";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "# list header"));
    TEST_ASSERT_NOT_NULL(strstr(output, "one"));
    TEST_ASSERT_NOT_NULL(strstr(output, "two"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_map_with_comments(void)
{
    const char* yaml = "# config section\nhost: localhost\n# port setting\nport: 8080";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "# config section"));
    TEST_ASSERT_NOT_NULL(strstr(output, "host"));
    TEST_ASSERT_NOT_NULL(strstr(output, "port"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_no_comments_parsed(void)
{
    const char* yaml = "# comment\nkey: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NULL(strstr(output, "# comment"));
    TEST_ASSERT_NOT_NULL(strstr(output, "key"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_map_inline_comments(void)
{
    const char* yaml = "variables:\n"
                       "  MY_VARIABLE: 'true'    # Comment 1\n"
                       "  OTHER_VARIABLE: 'true' # Comment 2\n"
                       "  BEST_VARIABLE: 'true'  # Comment 3\n";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "# Comment 1"));
    TEST_ASSERT_NOT_NULL(strstr(output, "# Comment 2"));
    TEST_ASSERT_NOT_NULL(strstr(output, "# Comment 3"));
    TEST_ASSERT_NOT_NULL(strstr(output, "MY_VARIABLE"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_seq_complex_comments(void)
{
    const char* yaml = "# comment before a sequence\n"
                       "- first item\n"
                       "- # comment before a scalar\n"
                       "  second item # trailing comment\n"
                       "              # continuation comment\n"
                       "# comment describing last item\n"
                       "- last item\n";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_TRUE(cyaml_comment_count(doc) >= 4);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "# comment before a sequence"));
    TEST_ASSERT_NOT_NULL(strstr(output, "first item"));
    TEST_ASSERT_NOT_NULL(strstr(output, "second item"));
    TEST_ASSERT_NOT_NULL(strstr(output, "last item"));
    free(output);
    cyaml_free(doc);
}



void test_cyaml_map_sort_alphabetical(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_map_set(doc, map, "zebra", cyaml_new_int(doc, 1));
    cyaml_map_set(doc, map, "apple", cyaml_new_int(doc, 2));
    cyaml_map_set(doc, map, "mango", cyaml_new_int(doc, 3));

    TEST_ASSERT_TRUE(cyaml_map_sort(doc, map, NULL));

    
    cyaml_pair_t* p0 = cyaml_map_at(map, 0);
    cyaml_pair_t* p1 = cyaml_map_at(map, 1);
    cyaml_pair_t* p2 = cyaml_map_at(map, 2);

    TEST_ASSERT_TRUE(cyaml_span_eq(doc, p0->key->span, "apple"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, p1->key->span, "mango"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, p2->key->span, "zebra"));

    cyaml_free(doc);
}

static int reverse_cmp(const cyaml_doc_t* doc, const cyaml_node_t* a, const cyaml_node_t* b)
{
    const char* src = cyaml_src(doc);
    if (!src || !a || !b)
        return 0;
    
    size_t la = a->span.len, lb = b->span.len;
    size_t min_len = la < lb ? la : lb;
    int cmp = memcmp(src + a->span.off, src + b->span.off, min_len);
    if (cmp != 0)
        return -cmp;
    return (lb > la) - (lb < la);
}

void test_cyaml_map_sort_custom(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_map_set(doc, map, "apple", cyaml_new_int(doc, 1));
    cyaml_map_set(doc, map, "zebra", cyaml_new_int(doc, 2));
    cyaml_map_set(doc, map, "mango", cyaml_new_int(doc, 3));

    TEST_ASSERT_TRUE(cyaml_map_sort(doc, map, reverse_cmp));

    
    cyaml_pair_t* p0 = cyaml_map_at(map, 0);
    cyaml_pair_t* p1 = cyaml_map_at(map, 1);
    cyaml_pair_t* p2 = cyaml_map_at(map, 2);

    TEST_ASSERT_TRUE(cyaml_span_eq(doc, p0->key->span, "zebra"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, p1->key->span, "mango"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, p2->key->span, "apple"));

    cyaml_free(doc);
}

void test_cyaml_map_sort_recursive(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);

    cyaml_node_t* nested = cyaml_new_map(doc);
    cyaml_map_set(doc, nested, "z", cyaml_new_int(doc, 1));
    cyaml_map_set(doc, nested, "a", cyaml_new_int(doc, 2));

    cyaml_map_set(doc, root, "z", cyaml_new_int(doc, 3));
    cyaml_map_set(doc, root, "a", nested);

    TEST_ASSERT_TRUE(cyaml_map_sort_recursive(doc, root, NULL));

    
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, cyaml_map_at(root, 0)->key->span, "a"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, cyaml_map_at(root, 1)->key->span, "z"));

    
    cyaml_node_t* sorted_nested = cyaml_get(doc, root, "a");
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, cyaml_map_at(sorted_nested, 0)->key->span, "a"));
    TEST_ASSERT_TRUE(cyaml_span_eq(doc, cyaml_map_at(sorted_nested, 1)->key->span, "z"));

    cyaml_free(doc);
}


void test_cyaml_scanf_basic(void)
{
    const char* yaml = "server:\n  host: localhost\n  port: 8080\n  ssl: true";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);

    char host[256] = { 0 };
    unsigned int port = 0;
    bool ssl = false;

    int count = cyaml_scanf(doc, "/server/host %255s /server/port %u /server/ssl %b", host, &port, &ssl);
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_STRING("localhost", host);
    TEST_ASSERT_EQUAL_UINT(8080, port);
    TEST_ASSERT_TRUE(ssl);

    cyaml_free(doc);
}

void test_cyaml_scanf_integers(void)
{
    const char* yaml = "a: 42\nb: -17\nc: 0x1F\nd: 0o77";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);

    int a = 0;
    int64_t b = 0;
    uint64_t c = 0;
    int64_t d = 0;

    int count = cyaml_scanf(doc, "/a %d /b %lld /c %llu /d %lld", &a, &b, &c, &d);
    TEST_ASSERT_EQUAL_INT(4, count);
    TEST_ASSERT_EQUAL_INT(42, a);
    TEST_ASSERT_EQUAL_INT64(-17, b);
    TEST_ASSERT_EQUAL_UINT64(31, c);
    TEST_ASSERT_EQUAL_INT64(63, d);

    cyaml_free(doc);
}

void test_cyaml_scanf_floats(void)
{
    const char* yaml = "pi: 3.14159\ntemp: -273.15";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);

    float pi = 0;
    double temp = 0;

    int count = cyaml_scanf(doc, "/pi %f /temp %lf", &pi, &temp);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159f, pi);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -273.15, temp);

    cyaml_free(doc);
}

void test_cyaml_scanf_node_ptr(void)
{
    const char* yaml = "items:\n  - one\n  - two";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* items = NULL;
    int count = cyaml_scanf(doc, "/items %n", &items);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_NOT_NULL(items);
    TEST_ASSERT_TRUE(cyaml_is_seq(items));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_seq_len(items));

    cyaml_free(doc);
}

void test_cyaml_node_scanf_relative(void)
{
    const char* yaml = "users:\n  - name: alice\n    age: 30\n  - name: bob\n    age: 25";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* user = cyaml_path(doc, "/users[0]");
    TEST_ASSERT_NOT_NULL(user);

    char name[64] = { 0 };
    int age = 0;
    int count = cyaml_node_scanf(doc, user, "name %63s age %d", name, &age);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("alice", name);
    TEST_ASSERT_EQUAL_INT(30, age);

    cyaml_free(doc);
}


void test_cyaml_buildf_scalar(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "%d", 42);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(cyaml_is_scalar(n));

    int64_t v;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, n, &v));
    TEST_ASSERT_EQUAL_INT64(42, v);

    cyaml_free(doc);
}

void test_cyaml_buildf_string(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "%s", "hello world");
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(cyaml_is_scalar(n));

    cyaml_free(doc);
}

void test_cyaml_buildf_map(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "name: %s\nage: %d", "alice", 30);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(cyaml_is_map(n));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_map_len(n));

    cyaml_free(doc);
}

void test_cyaml_buildf_seq(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "- %s\n- %s\n- %d", "one", "two", 3);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(cyaml_is_seq(n));
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_seq_len(n));

    cyaml_free(doc);
}

void test_cyaml_buildf_bool(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "enabled: %b\ndisabled: %b", 1, 0);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(cyaml_is_map(n));

    bool enabled, disabled;
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, cyaml_get(doc, n, "enabled"), &enabled));
    TEST_ASSERT_TRUE(cyaml_as_bool(doc, cyaml_get(doc, n, "disabled"), &disabled));
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_FALSE(disabled);

    cyaml_free(doc);
}


void test_cyaml_insert_at_simple(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);
    doc->root = cyaml_new_map(doc);

    TEST_ASSERT_TRUE(cyaml_insert_at(doc, "/name", cyaml_new_cstr(doc, "test")));
    TEST_ASSERT_NOT_NULL(cyaml_get(doc, doc->root, "name"));

    cyaml_free(doc);
}

void test_cyaml_insert_at_nested(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    TEST_ASSERT_TRUE(cyaml_insert_at(doc, "/server/host", cyaml_new_cstr(doc, "localhost")));
    TEST_ASSERT_TRUE(cyaml_insert_at(doc, "/server/port", cyaml_new_int(doc, 8080)));

    cyaml_node_t* server = cyaml_path(doc, "/server");
    TEST_ASSERT_NOT_NULL(server);
    TEST_ASSERT_TRUE(cyaml_is_map(server));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_map_len(server));

    cyaml_free(doc);
}

void test_cyaml_insertf(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    TEST_ASSERT_TRUE(cyaml_insertf(doc, "/config", "timeout: %d\nretries: %d", 30, 3));

    cyaml_node_t* config = cyaml_path(doc, "/config");
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_TRUE(cyaml_is_map(config));

    int64_t timeout;
    TEST_ASSERT_TRUE(cyaml_as_int(doc, cyaml_get(doc, config, "timeout"), &timeout));
    TEST_ASSERT_EQUAL_INT64(30, timeout);

    cyaml_free(doc);
}

void test_cyaml_delete_at(void)
{
    const char* yaml = "a: 1\nb: 2\nc: 3";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    TEST_ASSERT_NOT_NULL(doc);

    TEST_ASSERT_EQUAL_UINT32(3, cyaml_map_len(doc->root));
    TEST_ASSERT_TRUE(cyaml_delete_at(doc, "/b"));
    TEST_ASSERT_EQUAL_UINT32(2, cyaml_map_len(doc->root));
    TEST_ASSERT_NULL(cyaml_get(doc, doc->root, "b"));

    cyaml_free(doc);
}

void test_cyaml_append_at(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_node_t* items = cyaml_new_seq(doc);
    cyaml_seq_push(items, cyaml_new_cstr(doc, "one"));
    cyaml_seq_push(items, cyaml_new_cstr(doc, "two"));
    cyaml_map_set(doc, root, "items", items);
    doc->root = root;

    TEST_ASSERT_EQUAL_UINT32(2, cyaml_seq_len(items));

    TEST_ASSERT_TRUE(cyaml_append_at(doc, "/items", cyaml_new_cstr(doc, "three")));
    TEST_ASSERT_EQUAL_UINT32(3, cyaml_seq_len(items));

    cyaml_free(doc);
}

void test_cyaml_appendf(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    TEST_ASSERT_NOT_NULL(doc);

    
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_map_set(doc, root, "users", cyaml_new_seq(doc));
    doc->root = root;

    TEST_ASSERT_TRUE(cyaml_appendf(doc, "/users", "name: %s\nage: %d", "alice", 30));

    cyaml_node_t* users = cyaml_path(doc, "/users");
    TEST_ASSERT_EQUAL_UINT32(1, cyaml_seq_len(users));

    cyaml_node_t* user = cyaml_seq_get(users, 0);
    TEST_ASSERT_TRUE(cyaml_is_map(user));

    cyaml_free(doc);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cyaml_version);
    RUN_TEST(test_cyaml_strerror);
    RUN_TEST(test_cyaml_parse_null_input);
    RUN_TEST(test_cyaml_parse_empty_string);
    RUN_TEST(test_cyaml_parse_simple_scalar);
    RUN_TEST(test_cyaml_parse_simple_map);
    RUN_TEST(test_cyaml_parse_simple_seq);
    RUN_TEST(test_cyaml_parse_with_options);
    RUN_TEST(test_cyaml_parse_syntax_error);
    RUN_TEST(test_cyaml_free_null);
    RUN_TEST(test_cyaml_parse_stream_single_doc);
    RUN_TEST(test_cyaml_parse_stream_multi_doc);
    RUN_TEST(test_cyaml_stream_doc_out_of_bounds);
    RUN_TEST(test_cyaml_stream_count_null);
    RUN_TEST(test_cyaml_stream_doc_null);
    RUN_TEST(test_cyaml_stream_free_null);
    RUN_TEST(test_cyaml_root_null);
    RUN_TEST(test_cyaml_src);
    RUN_TEST(test_cyaml_src_null);
    RUN_TEST(test_cyaml_src_len);
    RUN_TEST(test_cyaml_src_len_null);
    RUN_TEST(test_cyaml_span_ptr);
    RUN_TEST(test_cyaml_span_dup);
    RUN_TEST(test_cyaml_scalar_str_plain);
    RUN_TEST(test_cyaml_scalar_str_quoted);
    RUN_TEST(test_cyaml_span_eq);
    RUN_TEST(test_cyaml_span_ieq);
    RUN_TEST(test_cyaml_span_cmp);
    RUN_TEST(test_cyaml_is_null_with_null_ptr);
    RUN_TEST(test_cyaml_is_null_with_null_node);
    RUN_TEST(test_cyaml_is_scalar);
    RUN_TEST(test_cyaml_is_seq);
    RUN_TEST(test_cyaml_is_map);
    RUN_TEST(test_cyaml_is_alias);
    RUN_TEST(test_cyaml_val);
    RUN_TEST(test_cyaml_str);
    RUN_TEST(test_cyaml_len);
    RUN_TEST(test_cyaml_as_int_positive);
    RUN_TEST(test_cyaml_as_int_negative);
    RUN_TEST(test_cyaml_as_int_hex);
    RUN_TEST(test_cyaml_as_int_octal);
    RUN_TEST(test_cyaml_as_int_invalid);
    RUN_TEST(test_cyaml_as_uint);
    RUN_TEST(test_cyaml_as_float_normal);
    RUN_TEST(test_cyaml_as_float_inf);
    RUN_TEST(test_cyaml_as_float_neg_inf);
    RUN_TEST(test_cyaml_as_float_nan);
    RUN_TEST(test_cyaml_as_bool_true);
    RUN_TEST(test_cyaml_as_bool_false);
    RUN_TEST(test_cyaml_as_bool_case_insensitive);
    RUN_TEST(test_cyaml_is_null_val_tilde);
    RUN_TEST(test_cyaml_is_null_val_null);
    RUN_TEST(test_cyaml_is_null_val_NULL);
    RUN_TEST(test_cyaml_seq_len);
    RUN_TEST(test_cyaml_seq_len_null);
    RUN_TEST(test_cyaml_seq_get);
    RUN_TEST(test_cyaml_seq_get_out_of_bounds);
    RUN_TEST(test_cyaml_seq_get_null);
    RUN_TEST(test_cyaml_map_len);
    RUN_TEST(test_cyaml_map_len_null);
    RUN_TEST(test_cyaml_map_at);
    RUN_TEST(test_cyaml_map_at_out_of_bounds);
    RUN_TEST(test_cyaml_map_at_null);
    RUN_TEST(test_cyaml_get);
    RUN_TEST(test_cyaml_get_not_found);
    RUN_TEST(test_cyaml_has);
    RUN_TEST(test_cyaml_path_simple);
    RUN_TEST(test_cyaml_path_array_index);
    RUN_TEST(test_cyaml_path_nested);
    RUN_TEST(test_cyaml_path_not_found);
    RUN_TEST(test_cyaml_doc_new);
    RUN_TEST(test_cyaml_new_null);
    RUN_TEST(test_cyaml_new_str);
    RUN_TEST(test_cyaml_new_cstr);
    RUN_TEST(test_cyaml_new_int);
    RUN_TEST(test_cyaml_new_uint);
    RUN_TEST(test_cyaml_new_float);
    RUN_TEST(test_cyaml_new_bool_true);
    RUN_TEST(test_cyaml_new_bool_false);
    RUN_TEST(test_cyaml_new_seq);
    RUN_TEST(test_cyaml_new_map);
    RUN_TEST(test_cyaml_seq_push);
    RUN_TEST(test_cyaml_map_set);
    RUN_TEST(test_cyaml_set_root);
    RUN_TEST(test_cyaml_node_new);
    RUN_TEST(test_cyaml_emit_simple);
    RUN_TEST(test_cyaml_emit_with_options);
    RUN_TEST(test_cyaml_emit_null_doc);
    RUN_TEST(test_cyaml_stream_emit);
    RUN_TEST(test_cyaml_dump);
    RUN_TEST(test_cyaml_stream_dump);
    RUN_TEST(test_cyaml_events);
    RUN_TEST(test_cyaml_stream_events);
    RUN_TEST(test_cyaml_json_simple);
    RUN_TEST(test_cyaml_json_with_indent);
    RUN_TEST(test_cyaml_stream_json_single);
    RUN_TEST(test_cyaml_stream_json_multi);
    RUN_TEST(test_nested_structures);
    RUN_TEST(test_build_and_emit);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_iteration_macros);
    RUN_TEST(test_flow_style);
    RUN_TEST(test_flow_sequence);
    RUN_TEST(test_multiline_string);
    RUN_TEST(test_folded_string);

    
    RUN_TEST(test_cyaml_set_anchor);
    RUN_TEST(test_cyaml_set_anchor_clear);
    RUN_TEST(test_cyaml_new_alias);
    RUN_TEST(test_cyaml_new_alias_no_anchor);
    RUN_TEST(test_cyaml_find_anchor);
    RUN_TEST(test_cyaml_find_anchor_nested);

    
    RUN_TEST(test_cyaml_node_copy_scalar);
    RUN_TEST(test_cyaml_node_copy_seq);
    RUN_TEST(test_cyaml_node_copy_map);
    RUN_TEST(test_cyaml_node_copy_deep);

    
    RUN_TEST(test_cyaml_map_merge_simple);
    RUN_TEST(test_cyaml_map_merge_overwrite);
    RUN_TEST(test_cyaml_map_merge_deep);

    
    RUN_TEST(test_cyaml_resolve_aliases);

    
    RUN_TEST(test_cyaml_comment_count_null);
    RUN_TEST(test_cyaml_comment_count_no_comments_option);
    RUN_TEST(test_cyaml_comment_count_with_comments);
    RUN_TEST(test_cyaml_comment_at_null);
    RUN_TEST(test_cyaml_comment_at_out_of_bounds);
    RUN_TEST(test_cyaml_comment_at_valid);
    RUN_TEST(test_cyaml_comment_multiple);

    
    RUN_TEST(test_cyaml_emit_with_comments_disabled);
    RUN_TEST(test_cyaml_emit_with_comments_enabled);
    RUN_TEST(test_cyaml_emit_inline_comment);
    RUN_TEST(test_cyaml_emit_seq_with_comments);
    RUN_TEST(test_cyaml_emit_map_with_comments);
    RUN_TEST(test_cyaml_emit_no_comments_parsed);
    RUN_TEST(test_cyaml_emit_map_inline_comments);
    RUN_TEST(test_cyaml_emit_seq_complex_comments);

    
    RUN_TEST(test_cyaml_map_sort_alphabetical);
    RUN_TEST(test_cyaml_map_sort_custom);
    RUN_TEST(test_cyaml_map_sort_recursive);

    
    RUN_TEST(test_cyaml_scanf_basic);
    RUN_TEST(test_cyaml_scanf_integers);
    RUN_TEST(test_cyaml_scanf_floats);
    RUN_TEST(test_cyaml_scanf_node_ptr);
    RUN_TEST(test_cyaml_node_scanf_relative);

    
    RUN_TEST(test_cyaml_buildf_scalar);
    RUN_TEST(test_cyaml_buildf_string);
    RUN_TEST(test_cyaml_buildf_map);
    RUN_TEST(test_cyaml_buildf_seq);
    RUN_TEST(test_cyaml_buildf_bool);

    
    RUN_TEST(test_cyaml_insert_at_simple);
    RUN_TEST(test_cyaml_insert_at_nested);
    RUN_TEST(test_cyaml_insertf);
    RUN_TEST(test_cyaml_delete_at);
    RUN_TEST(test_cyaml_append_at);
    RUN_TEST(test_cyaml_appendf);

    return UNITY_END();
}
