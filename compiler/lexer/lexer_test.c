#include "lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *token_name(TokenKind kind)
{
    switch (kind) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_ERROR: return "ERROR";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";

        case TOKEN_NUMBER_LITERAL: return "NUMBER_LITERAL";
        case TOKEN_DECIMAL_LITERAL: return "DECIMAL_LITERAL";
        case TOKEN_STRING_LITERAL: return "STRING_LITERAL";
        case TOKEN_CHAR_LITERAL: return "CHAR_LITERAL";

        case TOKEN_IMPORT: return "IMPORT";
        case TOKEN_FUNCTION: return "FUNCTION";
        case TOKEN_STRUCT: return "STRUCT";
        case TOKEN_ENUM: return "ENUM";
        case TOKEN_CONST: return "CONST";

        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_DECIMAL: return "DECIMAL";
        case TOKEN_BOOL: return "BOOL";
        case TOKEN_CHAR: return "CHAR";
        case TOKEN_STRING: return "STRING";
        case TOKEN_VOID: return "VOID";

        case TOKEN_IF: return "IF";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_WHILE: return "WHILE";
        case TOKEN_REPEAT: return "REPEAT";
        case TOKEN_FOR: return "FOR";

        case TOKEN_BREAK: return "BREAK";
        case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_RETURN: return "RETURN";

        case TOKEN_TRUE: return "TRUE";
        case TOKEN_FALSE: return "FALSE";
        case TOKEN_PRINT: return "PRINT";

        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_STAR: return "STAR";
        case TOKEN_SLASH: return "SLASH";
        case TOKEN_PERCENT: return "PERCENT";

        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TOKEN_BANG: return "BANG";
        case TOKEN_BANG_EQUAL: return "BANG_EQUAL";

        case TOKEN_LESS: return "LESS";
        case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";

        case TOKEN_AND_AND: return "AND_AND";
        case TOKEN_OR_OR: return "OR_OR";
        case TOKEN_SHIFT_LEFT: return "SHIFT_LEFT";
        case TOKEN_SHIFT_RIGHT: return "SHIFT_RIGHT";
        case TOKEN_AMPERSAND: return "AMPERSAND";
        case TOKEN_CARET: return "CARET";
        case TOKEN_PIPE: return "PIPE";

        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";

        case TOKEN_COMMA: return "COMMA";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_DOT: return "DOT";
    }

    return "UNKNOWN";
}

static void test_all_keywords(void)
{
    const char *source =
        "import "
        "function "
        "struct "
        "enum "
        "const "
        "number "
        "decimal "
        "bool "
        "char "
        "string "
        "void "
        "if "
        "else "
        "while "
        "repeat "
        "for "
        "break "
        "continue "
        "return "
        "true "
        "false "
        "null "
        "print";

    const TokenKind expected[] = {
        TOKEN_IMPORT,
        TOKEN_FUNCTION,
        TOKEN_STRUCT,
        TOKEN_ENUM,
        TOKEN_CONST,

        TOKEN_NUMBER,
        TOKEN_DECIMAL,
        TOKEN_BOOL,
        TOKEN_CHAR,
        TOKEN_STRING,
        TOKEN_VOID,

        TOKEN_IF,
        TOKEN_ELSE,
        TOKEN_WHILE,
        TOKEN_REPEAT,
        TOKEN_FOR,

        TOKEN_BREAK,
        TOKEN_CONTINUE,
        TOKEN_RETURN,

        TOKEN_TRUE,
        TOKEN_FALSE,
        TOKEN_NULL,
        TOKEN_PRINT
    };

    const size_t expected_count =
        sizeof(expected) / sizeof(expected[0]);

    Lexer lexer;
    lexer_init(&lexer, source);

    for (size_t i = 0; i < expected_count; i++) {
        Token token = lexer_next_token(&lexer);

        assert(token.kind == expected[i]);
        assert(token.length > 0);
    }

    Token token = lexer_next_token(&lexer);
    assert(token.kind == TOKEN_EOF);
}

int main(void)
{
    test_all_keywords();
    const char *source =
        "import \"io\";\n"
        "\n"
        "function number main() {\n"
        "    number x = 10;\n"
        "    decimal pi = 3.14;\n"
        "    bool ready = true;\n"
        "\n"
        "    // Check the value\n"
        "    if (x >= 10 && ready) {\n"
        "        print \"Ready!\";\n"
        "    }\n"
        "\n"
        "    return 0;\n"
        "}";

    Lexer lexer;
    lexer_init(&lexer, source);

    for (;;) {
        Token token = lexer_next_token(&lexer);

        printf(
            "%-18s line=%zu column=%zu length=%zu\n",
            token_name(token.kind),
            token.line,
            token.column,
            token.length
        );

        if (token.kind == TOKEN_ERROR) {
            printf("  ERROR: %.*s\n",
                   (int)token.length,
                   token.start);
            return 1;
        }

        if (token.kind == TOKEN_EOF) {
            break;
        }
    }

    printf("\nlexer integration test passed\n");

    const char *source_err = "\"hello";
    lexer_init(&lexer, source_err);

    Token token = lexer_next_token(&lexer);
    assert(token.kind == TOKEN_ERROR);
    assert(token.error_message != NULL);
    printf("error: %s\n", token.error_message);

    return 0;
}