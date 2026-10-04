#include <stdio.h>

#include "../parser/parser.h"
#include "../semantic/semantic.h"
#include "codegen.h"

int main(void)
{
    const char *source =
        "function number main() {"
            "number a 10;"
            "number b 20;"
            "number sum a + b;"
            "number product sum * 2;"
            "number remainder product % 3;"
            "decimal x 10.5;"
            "decimal y 2.5;"
            "decimal decimalResult x + y;"
            "bool flag true;"
            "bool notFlag !flag;"
            "char letter 'A';"
            "print a;"
            "print sum;"
            "print product;"
            "print remainder;"
            "print x;"
            "print decimalResult;"
            "print flag;"
            "print notFlag;"
            "print letter;"
            "return 42;"
        "}";

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

    printf("integration test: LLVM IR generated successfully\n");

    codegen_free(codegen);
    semantic_free(semantic);
    ast_free(program);

    return 0;
}