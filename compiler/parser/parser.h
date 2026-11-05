#ifndef PEBBLEBIT_PARSER_H
#define PEBBLEBIT_PARSER_H

#include "../ast/ast.h"
#include "../lexer/lexer.h"

typedef struct {
    Lexer lexer;
    Token current;
    Token next;
    Token previous;
    int had_error;
} Parser;

void parser_init(Parser *parser, const char *source);

ASTNode *parser_parse_expression(Parser *parser);
ASTNode *parser_parse_statement(Parser *parser);
ASTNode *parser_parse_program(Parser *parser);

#endif /* PEBBLEBIT_PARSER_H */