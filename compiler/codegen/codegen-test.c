
#include <stdio.h>
#include <string.h>

#include "../parser/parser.h"
#include "../semantic/semantic.h"
#include "codegen.h"

int main(int argc, char **argv)
{
    const char *source;

    if (argc < 2 || strcmp(argv[1], "valid") == 0) {
        source =
            "function number main() {"
                "number[10] scores;"
                "scores[2] = 33;"
                "return scores[2];"
            "}";
    } else if (strcmp(argv[1], "negative") == 0) {
        source =
            "function number main() {"
                "number[10] scores;"
                "return scores[-1];"
            "}";
    } else if (strcmp(argv[1], "upper-literal") == 0) {
        source =
            "function number main() {"
                "number[10] scores;"
                "return scores[10];"
            "}";
    } else if (strcmp(argv[1], "upper") == 0) {
        source =
            "function number main() {"
                "number[10] scores;"
                "number index 5 + 5;"
                "return scores[index];"
            "}";
    } else if (strcmp(argv[1], "multi") == 0) {
        source =
            "function number main() {"
                "number[3][4] matrix;"
                "matrix[1][2] = 33;"
                "return matrix[1][2];"
            "}";
    } else if (strcmp(argv[1], "decimal-array") == 0) {
        source =
            "function number main() {"
                "decimal[4] values;"
                "values[1] = 12.5;"
                "if (values[1] == 12.5) {"
                    "return 33;"
                "}"
                "return 1;"
            "}";

    } else if (strcmp(argv[1], "bool-array") == 0) {
        source =
            "function number main() {"
                "bool[4] flags;"
                "flags[1] = true;"
                "if (flags[1]) {"
                    "return 33;"
                "}"
                "return 1;"
            "}";

    } else if (strcmp(argv[1], "char-array") == 0) {
        source =
            "function number main() {"
                "char[4] letters;"
                "letters[1] = 'A';"
                "if (letters[1] == 'A') {"
                    "return 33;"
                "}"
                "return 1;"
            "}";
    } else if (strcmp(argv[1], "zero-array") == 0) {
        source =
            "function number main() {"
                "number[4] values;"
                "return values[0];"
            "}";
    } else if (strcmp(argv[1], "alloc-free") == 0) {
        source =
            "function number main() {"
                "free(alloc(64));"
                "return 0;"
            "}";
    } else if (strcmp(argv[1], "free-string") == 0) {
        source =
            "function number main() {"
                "string s \"hello\";"
                "free(s);"
                "return 0;"
            "}";
    } else {
        fprintf(stderr, "usage: %s [valid|negative|upper-literal|upper|multi|decimal-array|bool-array|char-array|zero-array|alloc-free|free-string]\n", argv[0]);
        return 2;
    }

    Parser parser;
    parser_init(&parser, source);

    ASTNode *program = parser_parse_program(&parser);
    if (program == NULL) {
        fprintf(stderr, "integration test: parser failed\n");
        return 1;
    }

    SemanticContext *semantic = semantic_analyze(program);
    if (semantic == NULL) {
        fprintf(stderr, "integration test: semantic analysis failed\n");
        ast_free(program);
        return 1;
    }

    Codegen *codegen = codegen_create("pebble_test", semantic);
    if (codegen == NULL) {
        fprintf(stderr, "integration test: codegen creation failed\n");
        semantic_free(semantic);
        ast_free(program);
        return 1;
    }

    ASTNode *main_function = program->data.program.statements[0];
    codegen_function(codegen, main_function);

    if (!codegen_write_ir(codegen, "integration-test.ll")) {
        fprintf(stderr, "integration test: failed to write LLVM IR\n");
        codegen_free(codegen);
        semantic_free(semantic);
        ast_free(program);
        return 1;
    }

    printf("integration test: LLVM IR generated successfully (%s)\n",
           argc >= 2 ? argv[1] : "valid");

    codegen_free(codegen);
    semantic_free(semantic);
    ast_free(program);

    return 0;
}
