#include "parser.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef enum {
    PREC_NONE,
    // PREC_ASSIGNMENT,
    PREC_LOGICAL_OR,
    PREC_LOGICAL_AND,

    PREC_BITWISE_OR,
    PREC_BITWISE_XOR,
    PREC_BITWISE_AND,

    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_SHIFT,

    PREC_TERM,
    PREC_FACTOR,

    PREC_UNARY,
    PREC_POSTFIX,
    PREC_PRIMARY,
} Precedence;

typedef ASTType ParsedType;

static ASTNode *parse_precedence(Parser *parser, Precedence precedence);
static ASTNode *parse_unary(Parser *parser);
static ASTNode *parse_postfix(Parser *parser);
static ASTNode *parse_var_decl(Parser *parser);
static ASTNode *parse_print_stmt(Parser *parser);
static ASTNode *parse_block(Parser *parser);
static ASTNode *parse_if_stmt(Parser *parser);
static ASTNode *parse_while_stmt(Parser *parser);
static ASTNode *parse_break_stmt(Parser *parser);
static ASTNode *parse_continue_stmt(Parser *parser);
static ASTNode *parse_return_stmt(Parser *parser);
static ASTNode *parse_repeat_stmt(Parser *parser);
static ASTNode *parse_for_stmt(Parser *parser);
static ASTNode *parse_assignment_expr(Parser *parser);
static int looks_like_user_type_array_decl(Parser *parser);
static void advance(Parser *parser);
static void parser_error(Parser *parser, const char *message);
static int is_lvalue(const ASTNode *node);
static ASTNode *parse_function_decl(Parser *parser);
static ASTNode *parse_struct_decl(Parser *parser);
static ASTNode *parse_enum_decl(Parser *parser);
static ASTNode *parse_import_stmt(Parser *parser);
static ASTNode *ast_new_null(size_t line, size_t column);
static int parse_type(Parser *parser, ParsedType *type)
{
    memset(type, 0, sizeof(*type));

    switch (parser->current.kind) {
        case TOKEN_NUMBER:
            type->kind = AST_TYPE_NUMBER;
            break;
        case TOKEN_DECIMAL:
            type->kind = AST_TYPE_DECIMAL;
            break;
        case TOKEN_BOOL:
            type->kind = AST_TYPE_BOOL;
            break;
        case TOKEN_CHAR:
            type->kind = AST_TYPE_CHAR;
            break;
        case TOKEN_STRING:
            type->kind = AST_TYPE_STRING;
            break;
        case TOKEN_VOID:
            type->kind = AST_TYPE_VOID;
            break;
        case TOKEN_IDENTIFIER:
            type->kind = AST_TYPE_USER;
            type->name = parser->current.start;
            type->name_length = parser->current.length;
            break;
        default:
            parser_error(parser, "expected type");
            return 0;
    }

    advance(parser);

    while (parser->current.kind == TOKEN_LBRACKET) {
        advance(parser);

        if (parser->current.kind != TOKEN_NUMBER_LITERAL) {
            parser_error(parser, "expected array size");
            free(type->dimensions);
            type->dimensions = NULL;
            type->dimension_count = 0;
            return 0;
        }

        char buffer[64];

        if (parser->current.length >= sizeof(buffer)) {
            parser_error(parser, "array size is too large");
            free(type->dimensions);
            type->dimensions = NULL;
            type->dimension_count = 0;
            return 0;
        }

        memcpy(buffer, parser->current.start, parser->current.length);
        buffer[parser->current.length] = '\0';

        char *end = NULL;
        unsigned long long size = strtoull(buffer, &end, 10);

        if (end == buffer || *end != '\0' || size > SIZE_MAX) {
            parser_error(parser, "invalid array size");
            free(type->dimensions);
            type->dimensions = NULL;
            type->dimension_count = 0;
            return 0;
        }

        ASTArrayDimension *new_dimensions = realloc(
            type->dimensions,
            (type->dimension_count + 1) * sizeof(ASTArrayDimension)
        );

        if (new_dimensions == NULL) {
            parser_error(parser, "out of memory");
            free(type->dimensions);
            type->dimensions = NULL;
            type->dimension_count = 0;
            return 0;
        }

        type->dimensions = new_dimensions;
        type->dimensions[type->dimension_count].has_size = 1;
        type->dimensions[type->dimension_count].size = (size_t)size;
        type->dimension_count++;

        advance(parser);

        if (parser->current.kind != TOKEN_RBRACKET) {
            parser_error(parser, "expected ']' after array size");
            free(type->dimensions);
            type->dimensions = NULL;
            type->dimension_count = 0;
            return 0;
        }

        advance(parser);
    }

    return 1;
}

static int is_lvalue(const ASTNode *node)
{
    if (node == NULL) {
        return 0;
    }

    switch (node->kind) {
        case AST_IDENTIFIER:
        case AST_MEMBER_ACCESS:
        case AST_INDEX:
            return 1;

        default:
            return 0;
    }
}

static Precedence token_precedence(TokenKind kind)
{
    switch (kind) {
        // case TOKEN_EQUAL:
        //     return PREC_ASSIGNMENT;

        case TOKEN_OR_OR:
            return PREC_LOGICAL_OR;

        case TOKEN_AND_AND:
            return PREC_LOGICAL_AND;

        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
            return PREC_EQUALITY;

        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
            return PREC_COMPARISON;

        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return PREC_TERM;

        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
            return PREC_FACTOR;

        case TOKEN_PIPE:
            return PREC_BITWISE_OR;

        case TOKEN_CARET:
            return PREC_BITWISE_XOR;

        case TOKEN_AMPERSAND:
            return PREC_BITWISE_AND;

        case TOKEN_SHIFT_LEFT:
        case TOKEN_SHIFT_RIGHT:
            return PREC_SHIFT;

        default:
            return PREC_NONE;
    }
}

static void advance(Parser *parser)
{
    parser->previous = parser->current;
    parser->current = parser->next;
    parser->next = lexer_next_token(&parser->lexer);
}

static void parser_error(Parser *parser, const char *message)
{
    if (parser->had_error) {
        return;
    }

    parser->had_error = 1;

    fprintf(
        stderr,
        "error: %s at %zu:%zu\n",
        message,
        parser->current.line,
        parser->current.column
    );
}

void parser_init(Parser *parser, const char *source)
{
    lexer_init(&parser->lexer, source);

    parser->current.kind = TOKEN_ERROR;
    parser->current.start = NULL;
    parser->current.length = 0;
    parser->current.line = 0;
    parser->current.column = 0;
    parser->current.error_message = NULL;

    parser->previous = parser->current;
    parser->next = parser->current;
    parser->had_error = 0;

    advance(parser);
    advance(parser);
}

ASTNode *parser_parse_expression(Parser *parser)
{
    return parse_precedence(parser, PREC_LOGICAL_OR);
}

static ASTNode *parse_primary(Parser *parser)
{
    Token token = parser->current;

    switch (token.kind) {
    case TOKEN_NULL: {
        Token null_token = parser->current;
        advance(parser);
        return ast_new_null(
            null_token.line,
            null_token.column
        );
    }

        case TOKEN_NUMBER_LITERAL: {
            advance(parser);

            long long value = 0;

            for (size_t i = 0; i < token.length; i++) {
                value = value * 10 + (token.start[i] - '0');
            }

            return ast_new_number(
                value,
                token.line,
                token.column
            );
        }

        case TOKEN_DECIMAL_LITERAL: {
            advance(parser);

            char buffer[64];

            if (token.length >= sizeof(buffer)) {
                parser_error(parser, "decimal literal is too long");
                return NULL;
            }

            memcpy(buffer, token.start, token.length);
            buffer[token.length] = '\0';

            double value = strtod(buffer, NULL);

            return ast_new_decimal(
                value,
                token.line,
                token.column
            );
        }

        case TOKEN_STRING_LITERAL:
            advance(parser);

            return ast_new_string(
                token.start + 1,
                token.length - 2,
                token.line,
                token.column
            );

        case TOKEN_CHAR_LITERAL:
            advance(parser);

            if (token.length != 3) {
                parser_error(
                    parser,
                    "invalid character literal"
                );
                return NULL;
            }

            return ast_new_char(
                token.start[1],
                token.line,
                token.column
            );

        case TOKEN_TRUE:
            advance(parser);

            return ast_new_bool(
                1,
                token.line,
                token.column
            );

        case TOKEN_FALSE:
            advance(parser);

            return ast_new_bool(
                0,
                token.line,
                token.column
            );

        case TOKEN_IDENTIFIER:
            advance(parser);

            return ast_new_identifier(
                token.start,
                token.length,
                token.line,
                token.column
            );

        case TOKEN_LPAREN: {
            advance(parser);

            ASTNode *expression =
                parser_parse_expression(parser);

            if (parser->current.kind != TOKEN_RPAREN) {
                parser_error(
                    parser,
                    "expected ')' after expression"
                );

                ast_free(expression);
                return NULL;
            }

            advance(parser);

            return expression;
        }

        default:
            parser_error(
                parser,
                "expected an expression"
            );

            return NULL;
    }
}

static ASTBinaryOperator binary_operator(TokenKind kind)
{
    switch (kind) {
        case TOKEN_PLUS:
            return AST_BINARY_ADD;

        case TOKEN_MINUS:
            return AST_BINARY_SUBTRACT;

        case TOKEN_STAR:
            return AST_BINARY_MULTIPLY;

        case TOKEN_SLASH:
            return AST_BINARY_DIVIDE;

        case TOKEN_PERCENT:
            return AST_BINARY_MODULO;

        case TOKEN_EQUAL_EQUAL:
            return AST_BINARY_EQUAL;

        case TOKEN_BANG_EQUAL:
            return AST_BINARY_NOT_EQUAL;

        case TOKEN_LESS:
            return AST_BINARY_LESS;

        case TOKEN_LESS_EQUAL:
            return AST_BINARY_LESS_EQUAL;

        case TOKEN_GREATER:
            return AST_BINARY_GREATER;

        case TOKEN_GREATER_EQUAL:
            return AST_BINARY_GREATER_EQUAL;

        case TOKEN_AND_AND:
            return AST_BINARY_LOGICAL_AND;

        case TOKEN_OR_OR:
            return AST_BINARY_LOGICAL_OR;

        case TOKEN_PIPE:
            return AST_BINARY_BITWISE_OR;

        case TOKEN_CARET:
            return AST_BINARY_BITWISE_XOR;

        case TOKEN_AMPERSAND:
            return AST_BINARY_BITWISE_AND;

        case TOKEN_SHIFT_LEFT:
            return AST_BINARY_SHIFT_LEFT;

        case TOKEN_SHIFT_RIGHT:
            return AST_BINARY_SHIFT_RIGHT;
            
        default:
            return AST_BINARY_ADD;
    }
}   

static ASTNode *parse_precedence(Parser *parser, Precedence precedence)
{
    ASTNode *left = parse_unary(parser);

    if (left == NULL) {
        return NULL;
    }

    while (precedence <= token_precedence(parser->current.kind)) {
        Token operator_token = parser->current;
        Precedence operator_precedence =
            token_precedence(operator_token.kind);

        advance(parser);

        Precedence right_precedence = operator_precedence;

        if (operator_token.kind != TOKEN_EQUAL) {
            right_precedence = (Precedence)(operator_precedence + 1);
        }

        ASTNode *right = parse_precedence(parser, right_precedence);

        if (right == NULL) {
            ast_free(left);
            return NULL;
        }

        if (operator_token.kind == TOKEN_EQUAL) {
            left = ast_new_assignment(
                left,
                right,
                operator_token.line,
                operator_token.column
            );
        } else {
            left = ast_new_binary(
                binary_operator(operator_token.kind),
                left,
                right,
                operator_token.line,
                operator_token.column
            );
        }
    }

    return left;
}

static ASTNode *parse_unary(Parser *parser)
{
    if (parser->current.kind == TOKEN_MINUS ||
        parser->current.kind == TOKEN_BANG ||
        parser->current.kind == TOKEN_TILDE) {

        Token operator_token = parser->current;
        advance(parser);

        ASTNode *operand = parse_unary(parser);
        if (operand == NULL) return NULL;

        ASTUnaryOperator operator;

        if (operator_token.kind == TOKEN_MINUS) {
            operator = AST_UNARY_NEGATE;
        } else if (operator_token.kind == TOKEN_BANG) {
            operator = AST_UNARY_NOT;
        } else {
            operator = AST_UNARY_BITWISE_NOT;
        }

        return ast_new_unary(
            operator,
            operand,
            operator_token.line,
            operator_token.column
        );
    }

    return parse_postfix(parser);
}
static ASTNode *parse_postfix(Parser *parser)
{
    ASTNode *expression = parse_primary(parser);

    if (expression == NULL) {
        return NULL;
    }

    for (;;) {
        Token token = parser->current;

        if (token.kind == TOKEN_LPAREN) {
            advance(parser);

            ASTNode **arguments = NULL;
            size_t argument_count = 0;

            if (parser->current.kind != TOKEN_RPAREN) {
                for (;;) {
                    ASTNode *argument =
                        parser_parse_expression(parser);

                    if (argument == NULL) {
                        ast_free(expression);

                        for (size_t i = 0;
                             i < argument_count;
                             i++) {
                            ast_free(arguments[i]);
                        }

                        free(arguments);
                        return NULL;
                    }

                    ASTNode **new_arguments = realloc(
                        arguments,
                        sizeof(ASTNode *) *
                            (argument_count + 1)
                    );

                    if (new_arguments == NULL) {
                        fprintf(
                            stderr,
                            "fatal: failed to allocate argument list\n"
                        );

                        ast_free(argument);
                        ast_free(expression);

                        for (size_t i = 0;
                             i < argument_count;
                             i++) {
                            ast_free(arguments[i]);
                        }

                        free(arguments);
                        exit(EXIT_FAILURE);
                    }

                    arguments = new_arguments;
                    arguments[argument_count++] = argument;

                    if (parser->current.kind != TOKEN_COMMA) {
                        break;
                    }

                    advance(parser);
                }
            }

            if (parser->current.kind != TOKEN_RPAREN) {
                parser_error(
                    parser,
                    "expected ')' after arguments"
                );

                ast_free(expression);

                for (size_t i = 0;
                     i < argument_count;
                     i++) {
                    ast_free(arguments[i]);
                }

                free(arguments);

                return NULL;
            }

            advance(parser);

            expression = ast_new_call(
                expression,
                arguments,
                argument_count,
                token.line,
                token.column
            );

            continue;
        }

        if (token.kind == TOKEN_DOT) {
            advance(parser);

            if (parser->current.kind != TOKEN_IDENTIFIER) {
                parser_error(
                    parser,
                    "expected member name after '.'"
                );

                ast_free(expression);
                return NULL;
            }

            Token member = parser->current;
            advance(parser);

            expression = ast_new_member_access(
                expression,
                member.start,
                member.length,
                token.line,
                token.column
            );

            continue;
        }

        if (token.kind == TOKEN_LBRACKET) {
            advance(parser);

            ASTNode *index =
                parser_parse_expression(parser);

            if (index == NULL) {
                ast_free(expression);
                return NULL;
            }

            if (parser->current.kind != TOKEN_RBRACKET) {
                parser_error(
                    parser,
                    "expected ']' after index expression"
                );

                ast_free(expression);
                ast_free(index);

                return NULL;
            }

            advance(parser);

            expression = ast_new_index(
                expression,
                index,
                token.line,
                token.column
            );

            continue;
        }

        break;
    }

    return expression;
}

static ASTNode *parse_var_decl(Parser *parser)
{
    int is_const = 0;

    if (parser->current.kind == TOKEN_CONST) {
        is_const = 1;
        advance(parser);
    }

    ParsedType type;

    if (!parse_type(parser, &type)) {
        return NULL;
    }
    
    Token name_token = parser->current;

    if (name_token.kind != TOKEN_IDENTIFIER) {
        parser_error(parser, "expected variable name");
        free(type.dimensions);
        return NULL;
    }

    advance(parser);

    ASTNode *initializer = NULL;

    if (parser->current.kind != TOKEN_SEMICOLON) {
        initializer = parser_parse_expression(parser);

        if (initializer == NULL) {
            free(type.dimensions);
            return NULL;
        }
    }

    if (initializer == NULL &&
        (type.kind == AST_TYPE_NUMBER ||
         type.kind == AST_TYPE_DECIMAL ||
         type.kind == AST_TYPE_BOOL ||
         type.kind == AST_TYPE_CHAR ||
         type.kind == AST_TYPE_STRING) &&
        type.dimension_count == 0) {
        parser_error(parser, "primitive variable requires an initializer");
        free(type.dimensions);
        return NULL;
    }

    if (parser->current.kind != TOKEN_SEMICOLON) {
        ast_free(initializer);
        free(type.dimensions);
        parser_error(parser, "expected ';' after variable declaration");
        return NULL;
    }

    advance(parser);

    return ast_new_var_decl(
        is_const,
        name_token.start,
        name_token.length,
        type,
        initializer,
        name_token.line,
        name_token.column
    );
}

ASTNode *parser_parse_statement(Parser *parser)
{
    if (parser->current.kind == TOKEN_CONST ||
        parser->current.kind == TOKEN_NUMBER ||
        parser->current.kind == TOKEN_DECIMAL ||
        parser->current.kind == TOKEN_BOOL ||
        parser->current.kind == TOKEN_CHAR ||
        parser->current.kind == TOKEN_STRING) {

        return parse_var_decl(parser);
    }

    if (parser->current.kind == TOKEN_IDENTIFIER &&
        parser->next.kind == TOKEN_IDENTIFIER) {
        return parse_var_decl(parser);
    }

    if (looks_like_user_type_array_decl(parser)) {
        return parse_var_decl(parser);
    }

    if (parser->current.kind == TOKEN_IDENTIFIER) {
        Token start_token = parser->current;

        ASTNode *expression = parser_parse_expression(parser);
        if (expression == NULL) {
            return NULL;
        }

        if (parser->current.kind == TOKEN_EQUAL) {
            if (!is_lvalue(expression)) {
                ast_free(expression);
                parser_error(parser, "invalid assignment target");
                return NULL;
            }

            advance(parser);

            ASTNode *value = parser_parse_expression(parser);
            if (value == NULL) {
                ast_free(expression);
                return NULL;
            }

            if (parser->current.kind != TOKEN_SEMICOLON) {
                ast_free(expression);
                ast_free(value);
                parser_error(parser, "expected ';' after assignment");
                return NULL;
            }

            advance(parser);

            return ast_new_assignment(
                expression,
                value,
                start_token.line,
                start_token.column
            );
        }

        if (parser->current.kind != TOKEN_SEMICOLON) {
            ast_free(expression);
            parser_error(parser, "expected ';' after expression");
            return NULL;
        }

        advance(parser);

        return ast_new_expr_stmt(
            expression,
            start_token.line,
            start_token.column
        );
    }

    if (parser->current.kind == TOKEN_FOR) {
        return parse_for_stmt(parser);
    }

    if (parser->current.kind == TOKEN_REPEAT) {
        return parse_repeat_stmt(parser);
    }

    if (parser->current.kind == TOKEN_BREAK) {
        return parse_break_stmt(parser);
    }

    if (parser->current.kind == TOKEN_CONTINUE) {
        return parse_continue_stmt(parser);
    }

    if (parser->current.kind == TOKEN_IF) {
        return parse_if_stmt(parser);
    }

    if (parser->current.kind == TOKEN_WHILE) {
        return parse_while_stmt(parser);
    }

    if (parser->current.kind == TOKEN_PRINT) {
        return parse_print_stmt(parser);
    }

    if (parser->current.kind == TOKEN_RETURN) {
        return parse_return_stmt(parser);
    }

    parser_error(parser, "expected statement");
    return NULL;
}

ASTNode *parser_parse_program(Parser *parser)
{
    ASTNode **statements = NULL;
    size_t statement_count = 0;
    size_t statement_capacity = 0;

    while (parser->current.kind != TOKEN_EOF) {
        ASTNode *declaration = NULL;

        if (parser->current.kind == TOKEN_IMPORT) {
            declaration = parse_import_stmt(parser);
        } else if (parser->current.kind == TOKEN_FUNCTION) {
            declaration = parse_function_decl(parser);
        } else if (parser->current.kind == TOKEN_STRUCT) {
            declaration = parse_struct_decl(parser);
        } else if (parser->current.kind == TOKEN_ENUM) {
            declaration = parse_enum_decl(parser);
        } else {
            parser_error(parser, "expected top-level declaration");
            break;
        }

        if (declaration == NULL) {
            for (size_t i = 0; i < statement_count; i++) {
                ast_free(statements[i]);
            }

            free(statements);
            return NULL;
        }

        if (statement_count == statement_capacity) {
            size_t new_capacity =
                statement_capacity == 0 ? 4 : statement_capacity * 2;

            ASTNode **new_statements =
                realloc(statements, new_capacity * sizeof(ASTNode *));

            if (new_statements == NULL) {
                ast_free(declaration);

                for (size_t i = 0; i < statement_count; i++) {
                    ast_free(statements[i]);
                }

                free(statements);

                parser_error(parser, "out of memory");
                return NULL;
            }

            statements = new_statements;
            statement_capacity = new_capacity;
        }

        statements[statement_count++] = declaration;
    }
    return ast_new_program(statements, statement_count);
}

static ASTNode *parse_print_stmt(Parser *parser)
{
    Token print_token = parser->current;
    advance(parser);

    // if (parser->current.kind != TOKEN_LPAREN) {
    //     parser_error(parser, "expected '(' after print");
    //     return NULL;
    // }

    // advance(parser);

    ASTNode *expression = parser_parse_expression(parser);
    if (expression == NULL) {
        return NULL;
    }

    // if (parser->current.kind != TOKEN_RPAREN) {
    //     ast_free(expression);
    //     parser_error(parser, "expected ')' after print expression");
    //     return NULL;
    // }
    // advance(parser);

    if (parser->current.kind != TOKEN_SEMICOLON) {
        ast_free(expression);
        parser_error(parser, "expected ';' after print statement");
        return NULL;
    }
    advance(parser);

    return ast_new_print(
        expression,
        print_token.line,
        print_token.column
    );
}
static ASTNode *parse_block(Parser *parser)
{
    Token open_token = parser->current;

    if (parser->current.kind != TOKEN_LBRACE) {
        parser_error(parser, "expected '{'");
        return NULL;
    }

    advance(parser);

    ASTNode **statements = NULL;
    size_t statement_count = 0;
    size_t statement_capacity = 0;

    while (parser->current.kind != TOKEN_RBRACE &&
           parser->current.kind != TOKEN_EOF) {

        ASTNode *statement = parser_parse_statement(parser);

        if (statement == NULL) {
            for (size_t i = 0; i < statement_count; i++) {
                ast_free(statements[i]);
            }

            free(statements);
            return NULL;
        }

        if (statement_count == statement_capacity) {
            size_t new_capacity =
                statement_capacity == 0 ? 4 : statement_capacity * 2;

            ASTNode **new_statements =
                realloc(statements, new_capacity * sizeof(ASTNode *));

            if (new_statements == NULL) {
                ast_free(statement);

                for (size_t i = 0; i < statement_count; i++) {
                    ast_free(statements[i]);
                }

                free(statements);

                parser_error(parser, "out of memory");
                return NULL;
            }

            statements = new_statements;
            statement_capacity = new_capacity;
        }

        statements[statement_count++] = statement;
    }

    if (parser->current.kind != TOKEN_RBRACE) {
        for (size_t i = 0; i < statement_count; i++) {
            ast_free(statements[i]);
        }

        free(statements);

        parser_error(parser, "expected '}' after block");
        return NULL;
    }

    advance(parser);

    return ast_new_block(
        statements,
        statement_count,
        open_token.line,
        open_token.column
    );
}

static ASTNode *parse_if_stmt(Parser *parser)
{
    Token if_token = parser->current;

    advance(parser);

    if (parser->current.kind != TOKEN_LPAREN) {
        parser_error(parser, "expected '(' after if");
        return NULL;
    }

    advance(parser);

    ASTNode *condition = parser_parse_expression(parser);

    if (condition == NULL) {
        return NULL;
    }

    if (parser->current.kind != TOKEN_RPAREN) {
        ast_free(condition);
        parser_error(parser, "expected ')' after if condition");
        return NULL;
    }

    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        ast_free(condition);
        parser_error(parser, "expected '{' after if condition");
        return NULL;
    }

    ASTNode *then_branch = parse_block(parser);

    if (then_branch == NULL) {
        ast_free(condition);
        return NULL;
    }

    ASTNode *else_branch = NULL;

    if (parser->current.kind == TOKEN_ELSE) {
        advance(parser);

        if (parser->current.kind == TOKEN_IF) {
            else_branch = parse_if_stmt(parser);
            if (else_branch == NULL) {
                return NULL;
            }
        } else if (parser->current.kind == TOKEN_LBRACE) {
            else_branch = parse_block(parser);
            if (else_branch == NULL) {
                return NULL;
            }
        } else {
            parser_error(parser, "expected 'if' or '{' after 'else'");
            return NULL;
        }
    }
    return ast_new_if(
        condition,
        then_branch,
        else_branch,
        if_token.line,
        if_token.column
    );
}

static ASTNode *parse_while_stmt(Parser *parser)
{
    Token while_token = parser->current;

    advance(parser);

    if (parser->current.kind != TOKEN_LPAREN) {
        parser_error(parser, "expected '(' after while");
        return NULL;
    }

    advance(parser);

    ASTNode *condition = parser_parse_expression(parser);

    if (condition == NULL) {
        return NULL;
    }

    if (parser->current.kind != TOKEN_RPAREN) {
        ast_free(condition);
        parser_error(parser, "expected ')' after while condition");
        return NULL;
    }

    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        ast_free(condition);
        parser_error(parser, "expected '{' after while condition");
        return NULL;
    }

    ASTNode *body = parse_block(parser);

    if (body == NULL) {
        ast_free(condition);
        return NULL;
    }

    return ast_new_while(
        condition,
        body,
        while_token.line,
        while_token.column
    );
}

static ASTNode *parse_break_stmt(Parser *parser)
{
    Token token = parser->current;

    advance(parser);

    if (parser->current.kind != TOKEN_SEMICOLON) {
        parser_error(parser, "expected ';' after break");
        return NULL;
    }

    advance(parser);

    return ast_new_break(token.line, token.column);
}
static ASTNode *parse_continue_stmt(Parser *parser)
{
    Token token = parser->current;

    advance(parser);

    if (parser->current.kind != TOKEN_SEMICOLON) {
        parser_error(parser, "expected ';' after continue");
        return NULL;
    }

    advance(parser);

    return ast_new_continue(token.line, token.column);
}

static ASTNode *parse_return_stmt(Parser *parser)
{
    Token return_token = parser->current;

    advance(parser);

    ASTNode *expression = NULL;

    if (parser->current.kind != TOKEN_SEMICOLON) {
        expression = parser_parse_expression(parser);

        if (expression == NULL) {
            return NULL;
        }
    }

    if (parser->current.kind != TOKEN_SEMICOLON) {
        ast_free(expression);
        parser_error(parser, "expected ';' after return");
        return NULL;
    }

    advance(parser);

    return ast_new_return(
        expression,
        return_token.line,
        return_token.column
    );
}

static ASTNode *parse_repeat_stmt(Parser *parser)
{
    Token repeat_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_LPAREN) {
        parser_error(parser, "expected '(' after repeat");
        return NULL;
    }
    advance(parser);

    ASTNode *count = parser_parse_expression(parser);
    if (count == NULL) {
        return NULL;
    }

    if (parser->current.kind != TOKEN_RPAREN) {
        ast_free(count);
        parser_error(parser, "expected ')' after repeat expression");
        return NULL;
    }
    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        ast_free(count);
        parser_error(parser, "expected '{' after repeat condition");
        return NULL;
    }

    ASTNode *body = parse_block(parser);
    if (body == NULL) {
        ast_free(count);
        return NULL;
    }

    return ast_new_repeat(
        count,
        body,
        repeat_token.line,
        repeat_token.column
    );
}

static ASTNode *parse_for_stmt(Parser *parser)
{
    Token for_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_LPAREN) {
        parser_error(parser, "expected '(' after for");
        return NULL;
    }
    advance(parser);

    ASTNode *initializer = parse_var_decl(parser);
    if (initializer == NULL) {
        return NULL;
    }

    ASTNode *condition = parser_parse_expression(parser);
    if (condition == NULL) {
        ast_free(initializer);
        return NULL;
    }

    if (parser->current.kind != TOKEN_SEMICOLON) {
        ast_free(initializer);
        ast_free(condition);
        parser_error(parser, "expected ';' after for condition");
        return NULL;
    }
    advance(parser);

    ASTNode *assignment = parse_assignment_expr(parser);
    if (assignment == NULL) {
        ast_free(initializer);
        ast_free(condition);
        return NULL;
    }

    if (parser->current.kind != TOKEN_RPAREN) {
        ast_free(initializer);
        ast_free(condition);
        ast_free(assignment);
        parser_error(parser, "expected ')' after for assignment");
        return NULL;
    }
    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        ast_free(initializer);
        ast_free(condition);
        ast_free(assignment);
        parser_error(parser, "expected '{' after for");
        return NULL;
    }

    ASTNode *body = parse_block(parser);
    if (body == NULL) {
        ast_free(initializer);
        ast_free(condition);
        ast_free(assignment);
        return NULL;
    }

    return ast_new_for(
        initializer,
        condition,
        assignment,
        body,
        for_token.line,
        for_token.column
    );
}

static ASTNode *parse_assignment_expr(Parser *parser)
{
    Token start_token = parser->current;

    ASTNode *target = parser_parse_expression(parser);
    if (target == NULL) {
        return NULL;
    }

    if (!is_lvalue(target)) {
        ast_free(target);
        parser_error(parser, "invalid assignment target");
        return NULL;
    }

    if (parser->current.kind != TOKEN_EQUAL) {
        ast_free(target);
        parser_error(parser, "expected '=' in for assignment");
        return NULL;
    }

    advance(parser);

    ASTNode *value = parser_parse_expression(parser);
    if (value == NULL) {
        ast_free(target);
        return NULL;
    }

    return ast_new_assignment(
        target,
        value,
        start_token.line,
        start_token.column
    );
}

static ASTNode *parse_function_decl(Parser *parser)
{
    Token function_token = parser->current;
    advance(parser);

    /* Parse return type first. */
    ParsedType return_type;

    if (!parse_type(parser, &return_type)) {
        return NULL;
    }

    /* Then parse function name. */
    if (parser->current.kind != TOKEN_IDENTIFIER) {
        parser_error(parser, "expected function name");
        free(return_type.dimensions);
        return NULL;
    }

    Token name_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_LPAREN) {
        parser_error(parser, "expected '(' after function name");
        free(return_type.dimensions);
        return NULL;
    }

    advance(parser);

    ASTNode **parameters = NULL;
    size_t parameter_count = 0;

    if (parser->current.kind != TOKEN_RPAREN) {
        for (;;) {
            ParsedType parameter_type;

            if (!parse_type(parser, &parameter_type)) {
                goto fail;
            }

            if (parser->current.kind != TOKEN_IDENTIFIER) {
                parser_error(parser, "expected parameter name");
                free(parameter_type.dimensions);
                goto fail;
            }

            Token parameter_name = parser->current;
            advance(parser);

            ASTNode *parameter = ast_new_var_decl(
                0,
                parameter_name.start,
                parameter_name.length,
                parameter_type,
                NULL,
                parameter_name.line,
                parameter_name.column
            );

            if (parameter == NULL) {
                free(parameter_type.dimensions);
                parser_error(parser, "out of memory");
                goto fail;
            }

            ASTNode **new_parameters = realloc(
                parameters,
                (parameter_count + 1) * sizeof(ASTNode *)
            );

            if (new_parameters == NULL) {
                ast_free(parameter);
                parser_error(parser, "out of memory");
                goto fail;
            }

            parameters = new_parameters;
            parameters[parameter_count++] = parameter;

            if (parser->current.kind != TOKEN_COMMA) {
                break;
            }

            advance(parser);
        }
    }

    if (parser->current.kind != TOKEN_RPAREN) {
        parser_error(parser, "expected ')' after parameters");
        goto fail;
    }

    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        parser_error(parser, "expected '{' before function body");
        goto fail;
    }

    ASTNode *body = parse_block(parser);

    if (body == NULL) {
        goto fail;
    }

    return ast_new_function(
    name_token.start,
    name_token.length,
    parameters,
    parameter_count,
    return_type,
    body,
    function_token.line,
    function_token.column
    );

fail:
    if (parameters != NULL) {
        for (size_t i = 0; i < parameter_count; i++) {
            ast_free(parameters[i]);
        }

        free(parameters);
    }

    free(return_type.dimensions);

    return NULL;
}

static ASTNode *parse_struct_decl(Parser *parser)
{
    Token struct_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_IDENTIFIER) {
        parser_error(parser, "expected struct name");
        return NULL;
    }

    Token name_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        parser_error(parser, "expected '{' after struct name");
        return NULL;
    }

    advance(parser);

    ASTNode **fields = NULL;
    size_t field_count = 0;

    while (parser->current.kind != TOKEN_RBRACE &&
           parser->current.kind != TOKEN_EOF) {

        ParsedType field_type;

        if (!parse_type(parser, &field_type)) {
            goto fail;
        }

        if (parser->current.kind != TOKEN_IDENTIFIER) {
            parser_error(parser, "expected field name");
            free(field_type.dimensions);
            goto fail;
        }

        Token field_name = parser->current;
        advance(parser);

        if (parser->current.kind != TOKEN_SEMICOLON) {
            parser_error(parser, "expected ';' after struct field");
            free(field_type.dimensions);
            goto fail;
        }

        advance(parser);

        ASTNode *field = ast_new_var_decl(
            0,
            field_name.start,
            field_name.length,
            field_type,
            NULL,
            field_name.line,
            field_name.column
        );

        if (field == NULL) {
            free(field_type.dimensions);
            parser_error(parser, "out of memory");
            goto fail;
        }

        ASTNode **new_fields = realloc(
            fields,
            (field_count + 1) * sizeof(ASTNode *)
        );

        if (new_fields == NULL) {
            ast_free(field);
            parser_error(parser, "out of memory");
            goto fail;
        }

        fields = new_fields;
        fields[field_count++] = field;
    }

    if (parser->current.kind != TOKEN_RBRACE) {
        parser_error(parser, "expected '}' after struct declaration");
        goto fail;
    }

    advance(parser);

    return ast_new_struct(
        name_token.start,
        name_token.length,
        fields,
        field_count,
        struct_token.line,
        struct_token.column
    );

fail:
    if (fields != NULL) {
        for (size_t i = 0; i < field_count; i++) {
            ast_free(fields[i]);
        }

        free(fields);
    }

    return NULL;
}

static ASTNode *parse_enum_decl(Parser *parser)
{
    Token enum_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_IDENTIFIER) {
        parser_error(parser, "expected enum name");
        return NULL;
    }

    Token name_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_LBRACE) {
        parser_error(parser, "expected '{' after enum name");
        return NULL;
    }

    advance(parser);

    const char **members = NULL;
    size_t *member_lengths = NULL;
    size_t member_count = 0;

    if (parser->current.kind == TOKEN_RBRACE) {
        parser_error(parser, "expected enum member");
        goto fail;
    }

    for (;;) {
        if (parser->current.kind != TOKEN_IDENTIFIER) {
            parser_error(parser, "expected enum member");
            goto fail;
        }

        Token member = parser->current;

        const char **new_members = realloc(
            members,
            (member_count + 1) * sizeof(const char *)
        );

        if (new_members == NULL) {
            parser_error(parser, "out of memory");
            goto fail;
        }

        size_t *new_lengths = realloc(
            member_lengths,
            (member_count + 1) * sizeof(size_t)
        );

        if (new_lengths == NULL) {
            members = new_members;
            parser_error(parser, "out of memory");
            goto fail;
        }

        members = new_members;
        member_lengths = new_lengths;

        members[member_count] = member.start;
        member_lengths[member_count] = member.length;
        member_count++;

        advance(parser);

        if (parser->current.kind != TOKEN_COMMA) {
            break;
        }

        advance(parser);
    }

    if (parser->current.kind != TOKEN_RBRACE) {
        parser_error(parser, "expected '}' after enum members");
        goto fail;
    }

    advance(parser);

    return ast_new_enum(
        name_token.start,
        name_token.length,
        members,
        member_lengths,
        member_count,
        enum_token.line,
        enum_token.column
    );

fail:
    free(members);
    free(member_lengths);
    return NULL;
}

static ASTNode *parse_import_stmt(Parser *parser)
{
    Token import_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_STRING_LITERAL) {
        parser_error(parser, "expected string literal after import");
        return NULL;
    }

    Token path_token = parser->current;
    advance(parser);

    if (parser->current.kind != TOKEN_SEMICOLON) {
        parser_error(parser, "expected ';' after import");
        return NULL;
    }

    advance(parser);

    return ast_new_import(
        path_token.start,
        path_token.length,
        import_token.line,
        import_token.column
    );
}

static ASTNode *ast_new_null(size_t line, size_t column)
{
    ASTNode *node = malloc(sizeof(ASTNode));

    if (node == NULL) {
        return NULL;
    }

    node->kind = AST_NULL_LITERAL;
    node->line = line;
    node->column = column;

    return node;
}
static int looks_like_user_type_array_decl(Parser *parser)
{
    if (parser->current.kind != TOKEN_IDENTIFIER ||
        parser->next.kind != TOKEN_LBRACKET) {
        return 0;
    }

    Lexer lexer = parser->lexer;

    /*
     * parser->next is already the first '['.
     * The copied lexer is positioned after parser->next,
     * so start validation from parser->next manually.
     */
    Token token = parser->next;

    while (token.kind == TOKEN_LBRACKET) {
        token = lexer_next_token(&lexer);

        if (token.kind != TOKEN_NUMBER_LITERAL) {
            return 0;
        }

        token = lexer_next_token(&lexer);

        if (token.kind != TOKEN_RBRACKET) {
            return 0;
        }

        token = lexer_next_token(&lexer);
    }

    return token.kind == TOKEN_IDENTIFIER;
}