#ifndef PEBBLE_LEXER_H
#define PEBBLE_LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_EOF,
    TOKEN_ERROR,

    TOKEN_IDENTIFIER,

    TOKEN_NUMBER_LITERAL,
    TOKEN_DECIMAL_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_CHAR_LITERAL,

    /* Operators */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,

    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,

    TOKEN_BANG,
    TOKEN_BANG_EQUAL,

    TOKEN_LESS,
    TOKEN_LESS_EQUAL,

    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,

    TOKEN_AND_AND,
    TOKEN_OR_OR,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,
    TOKEN_AMPERSAND,
    TOKEN_CARET,
    TOKEN_PIPE,
    TOKEN_TILDE,

    /* Punctuation */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,

    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_DOT,

    /* Keywords */
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
    TOKEN_PRINT,
    TOKEN_EXTERN
} TokenKind;

typedef struct {
    TokenKind kind;

    const char *start;
    size_t length;

    size_t line;
    size_t column;

    const char *error_message;
} Token;

typedef struct {
    const char *source;

    size_t start;
    size_t current;

    size_t line;
    size_t column;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);

Token lexer_next_token(Lexer *lexer);

#endif /* PEBBLE_LEXER_H */