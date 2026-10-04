#include <stdio.h>
#include "semantic.h"
#include "../parser/parser.h"

static int run(const char *label, const char *source, int expect_ok)
{
    Parser parser;
    parser_init(&parser, source);
    ASTNode *program = parser_parse_program(&parser);

    if (program == NULL) {
        printf("%-24s %s\n", label, expect_ok ? "FAIL (parse)" : "PASS (parser rejected)");
        return expect_ok ? 0 : 1;
    }

    SemanticContext *context = semantic_analyze(program);
    int ok = context != NULL;
    printf("%-24s %s\n", label, ok == expect_ok ? "PASS" : "FAIL");

    semantic_free(context);
    ast_free(program);
    return ok == expect_ok;
}

int main(void)
{
    int pass = 1;

    pass &= run("valid arithmetic",
        "function number main() { number x 10; number y 20; return x + y; }", 1);

    pass &= run("valid control flow",
        "function number main() { number x 1; if (x < 10) { x = x + 1; } else { x = 0; } while (x < 3) { x = x + 1; } return x; }", 1);

    pass &= run("unknown variable",
        "function number main() { number x 1; return y; }", 0);

    pass &= run("unknown type",
        "function number main() { Thing x; return 0; }", 0);

    pass &= run("wrong initializer",
        "function number main() { number x true; return x; }", 0);

    pass &= run("const assignment",
        "function number main() { const number x 1; x = 2; return x; }", 0);

    pass &= run("scope violation",
        "function number main() { if (true) { number x 1; } return x; }", 0);

    pass &= run("struct + array",
        "struct Point { number x; number y; } function number main() { Point p; number[3] a; p.x = 5; a[0] = p.x; return a[0]; }", 1);

    pass &= run("const aggregate write",
        "struct Point { number x; } function number main() { const Point p; p.x = 1; return 0; }", 0);

    pass &= run("enum value",
        "enum Color { RED, GREEN } function number main() { Color c RED; if (c == RED) { return 0; } return 1; }", 1);

    pass &= run("bad function argument",
        "function number add(number a, number b) { return a + b; } function number main() { return add(1, true); }", 0);

    pass &= run("return mismatch",
        "function number main() { return true; }", 0);

    pass &= run("builtin functions",
        "function number main() { string s \"hello\"; number[3] a; assert(length(s) == 5, \"bad\"); print typeof(a); free(alloc(8)); return 0; }", 1);

    pass &= run("void return",
        "function void greet() { return; } function number main() { greet(); return 0; }", 1);

    pass &= run("builtin type identity",
        "function number main() { "
            "number x 10; "
            "decimal d 2.5; "
            "bool b true; "
            "char c 'x'; "
            "string s \"hello\"; "
            "return x; "
        "}",
        1
    );

    return pass ? 0 : 1;
}
