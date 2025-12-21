#include "cyaml.h"
#include "cyaml_utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_node(const cyaml_doc_t* doc, const cyaml_node_t* node, int indent)
{
    const char* src = cyaml_src(doc);
    for (int i = 0; i < indent; i++)
        printf("  ");

    switch (node->type) {
    case CYAML_SCALAR:
        printf("SCALAR: '%.*s'\n", (int)node->span.len, src + node->span.off);
        break;
    case CYAML_SEQ:
        printf("SEQ (%u items)\n", node->seq.count);
        for (uint32_t i = 0; i < node->seq.count; i++) {
            print_node(doc, node->seq.items[i], indent + 1);
        }
        break;
    case CYAML_MAP:
        printf("MAP (%u pairs)\n", node->map.count);
        for (uint32_t i = 0; i < node->map.count; i++) {
            for (int j = 0; j < indent + 1; j++)
                printf("  ");
            cyaml_node_t* k = node->map.pairs[i].key;
            printf("key: '%.*s' =>\n", (int)k->span.len, src + k->span.off);
            print_node(doc, node->map.pairs[i].val, indent + 2);
        }
        break;
    default:
        printf("UNKNOWN type %d\n", node->type);
    }
}

static void test_path(const cyaml_doc_t* doc, const cyaml_node_t* ctx, const char* path)
{
    printf("\n");
    cyaml_path_debug(path);

    printf("\n--- Query Result ---\n");
    cyaml_path_result_t r = cyaml_path_query(doc, ctx, path);

    if (r.error) {
        printf("ERROR: %s at position %u\n", r.error, r.error_pos);
        printf("       %s\n", path);
        printf("       ");
        for (uint32_t i = 0; i < r.error_pos; i++)
            printf(" ");
        printf("^\n");
    } else {
        printf("Result: %u node(s)\n", r.count);
        for (uint32_t i = 0; i < r.count; i++) {
            printf("\n[%u] ", i);
            print_node(doc, r.nodes[i], 0);
        }
    }

    cyaml_path_result_free(&r);
}

int main(int argc, char** argv)
{
    const char* yaml = NULL;
    const char* path = NULL;

    if (argc >= 3) {
        yaml = argv[1];
        path = argv[2];
    } else {
        fprintf(stderr, "Usage: %s '<yaml>' '<path>'\n", argv[0]);
        fprintf(stderr, "Example: %s 'items:\\n  - type: fruit\\n  - type: veg' '/items[?@.type == \"fruit\"]'\n", argv[0]);
        return 1;
    }

    char* yaml_unescaped = cyaml_strdup(yaml);
    char* w = yaml_unescaped;
    for (const char* r = yaml; *r; r++) {
        if (*r == '\\' && *(r + 1) == 'n') {
            *w++ = '\n';
            r++;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';

    printf("=== YAML ===\n%s\n", yaml_unescaped);

    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml_unescaped, strlen(yaml_unescaped), NULL, &err);
    if (!doc) {
        fprintf(stderr, "Parse error: %s\n", cyaml_strerror(err.code));
        free(yaml_unescaped);
        return 1;
    }

    printf("\n=== Document Tree ===\n");
    print_node(doc, cyaml_root(doc), 0);

    test_path(doc, NULL, path);

    cyaml_free(doc);
    free(yaml_unescaped);
    return 0;
}
