#include <stdio.h>
#include <string.h>

#include "parser.h"

static int check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static ASTNode *find_var(ASTNode *block, const char *name)
{
    for (size_t i = 0; i < block->data.block.statement_count; ++i) {
        ASTNode *node = block->data.block.statements[i];
        if (node->kind == AST_VAR_DECL &&
            strlen(name) == node->data.var_decl.name_length &&
            memcmp(name, node->data.var_decl.name, node->data.var_decl.name_length) == 0) {
            return node;
        }
    }
    return NULL;
}

int main(void)
{

    const char *source =
        "struct Point {"
            "number x;"
            "decimal y;"
        "}"
        "function number[10][20] main("
            "number a,"
            "decimal b,"
            "bool flag,"
            "char c,"
            "string text,"
            "Point[3][4] p"
        "){"
            "number n 1;"
            "decimal f 1.5;"
            "Point[2][3] grid;"
            "return 0;"
        "}";

    Parser parser;
    parser_init(&parser, source);

    ASTNode *program = parser_parse_program(&parser);
    if (!check(program != NULL, "program parses")) {
        return 1;
    }

    ASTNode *point = program->data.program.statements[0];
    ASTNode *function = program->data.program.statements[1];

    int ok = 1;

    ok &= check(point->kind == AST_STRUCT, "struct parsed");
    ok &= check(point->data.struct_decl.fields[0]->data.var_decl.type.kind == AST_TYPE_NUMBER,
                "struct number field uses AST_TYPE_NUMBER");
    ok &= check(point->data.struct_decl.fields[0]->data.var_decl.type.name == NULL,
                "builtin type does not retain source spelling");
    ok &= check(point->data.struct_decl.fields[1]->data.var_decl.type.kind == AST_TYPE_DECIMAL,
                "struct decimal field uses AST_TYPE_DECIMAL");

    ok &= check(function->data.function_decl.return_type.kind == AST_TYPE_NUMBER,
                "function return uses AST_TYPE_NUMBER");
    printf("return dimension_count = %zu\n",
        function->data.function_decl.return_type.dimension_count);

    ok &= check(function->data.function_decl.return_type.dimension_count == 2,
                "function return preserves array dimensions");

    if (function->data.function_decl.return_type.dimensions != NULL &&
        function->data.function_decl.return_type.dimension_count >= 2) {
        ok &= check(function->data.function_decl.return_type.dimensions[0].size == 10 &&
                    function->data.function_decl.return_type.dimensions[1].size == 20,
                    "function return preserves array lengths");
    } else {
        ok &= check(0, "function return preserves array lengths");
    }

    ASTNode **params = function->data.function_decl.parameters;
    ok &= check(params[0]->data.var_decl.type.kind == AST_TYPE_NUMBER, "number parameter kind");
    ok &= check(params[1]->data.var_decl.type.kind == AST_TYPE_DECIMAL, "decimal parameter kind");
    ok &= check(params[2]->data.var_decl.type.kind == AST_TYPE_BOOL, "bool parameter kind");
    ok &= check(params[3]->data.var_decl.type.kind == AST_TYPE_CHAR, "char parameter kind");
    ok &= check(params[4]->data.var_decl.type.kind == AST_TYPE_STRING, "string parameter kind");
    ok &= check(params[5]->data.var_decl.type.kind == AST_TYPE_USER, "user parameter kind");
    ok &= check(params[5]->data.var_decl.type.name_length == 5 &&
                memcmp(params[5]->data.var_decl.type.name, "Point", 5) == 0,
                "user type retains identifier spelling");
    ok &= check(params[5]->data.var_decl.type.dimension_count == 2,
                "user array parameter preserves dimensions");

    ASTNode *body = function->data.function_decl.body;
    ASTNode *grid = find_var(body, "grid");
    ok &= check(grid != NULL, "array variable exists");
    ok &= check(grid->data.var_decl.type.kind == AST_TYPE_USER,
                "array variable preserves user type kind");
    ok &= check(grid->data.var_decl.type.dimension_count == 2 &&
                grid->data.var_decl.type.dimensions[0].size == 2 &&
                grid->data.var_decl.type.dimensions[1].size == 3,
                "array variable preserves dimensions and lengths");

    ast_free(program);

    if (!ok) {
        return 1;
    }

    printf("primitive and user type AST kind tests PASS\n");
    return 0;
}
