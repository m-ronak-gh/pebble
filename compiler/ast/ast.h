#ifndef PEBBLE_AST_H
#define PEBBLE_AST_H

#include <stddef.h>

typedef enum {
    AST_PROGRAM,

    AST_NUMBER_LITERAL,
    AST_DECIMAL_LITERAL,
    AST_STRING_LITERAL,
    AST_CHAR_LITERAL,
    AST_BOOL_LITERAL,
    AST_NULL_LITERAL,
    AST_IDENTIFIER,

    AST_UNARY,
    AST_BINARY,

    AST_CALL,
    AST_MEMBER_ACCESS,
    AST_INDEX,

    AST_VAR_DECL,
    AST_FUNCTION,
    AST_STRUCT,
    AST_ENUM,
    AST_IMPORT,
    AST_ASSIGNMENT,
    AST_EXPR_STMT,
    AST_PRINT,
    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_REPEAT,
    AST_FOR,

    AST_BREAK,
    AST_CONTINUE,
    AST_RETURN
} ASTNodeKind;

typedef enum {
    AST_UNARY_NEGATE,
    AST_UNARY_NOT,
    AST_UNARY_BITWISE_NOT
} ASTUnaryOperator;

typedef enum {
    AST_BINARY_ADD,
    AST_BINARY_SUBTRACT,
    AST_BINARY_MULTIPLY,
    AST_BINARY_DIVIDE,
    AST_BINARY_MODULO,
    AST_BINARY_EQUAL,
    AST_BINARY_NOT_EQUAL,
    AST_BINARY_LESS,
    AST_BINARY_LESS_EQUAL,
    AST_BINARY_GREATER,
    AST_BINARY_GREATER_EQUAL,
    AST_BINARY_LOGICAL_AND,
    AST_BINARY_LOGICAL_OR,

    AST_BINARY_BITWISE_OR,
    AST_BINARY_BITWISE_XOR,
    AST_BINARY_BITWISE_AND,
    AST_BINARY_SHIFT_LEFT,
    AST_BINARY_SHIFT_RIGHT
} ASTBinaryOperator;

typedef enum {
    AST_TYPE_NUMBER,
    AST_TYPE_DECIMAL,
    AST_TYPE_BOOL,
    AST_TYPE_CHAR,
    AST_TYPE_STRING,
    AST_TYPE_VOID,
    AST_TYPE_USER
} ASTTypeKind;

typedef struct {
    int has_size;
    size_t size;
} ASTArrayDimension;

typedef struct {
    ASTTypeKind kind;
    const char *name;
    size_t name_length;

    ASTArrayDimension *dimensions;
    size_t dimension_count;
} ASTType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeKind kind;

    size_t line;
    size_t column;

    union {
        long long number;
        double decimal;
        char character;
        int boolean;

        struct {
            const char *start;
            size_t length;
        } string;

        struct {
            const char *start;
            size_t length;
        } identifier;

        struct {
            ASTNode *condition;
            ASTNode *then_branch;
            ASTNode *else_branch;
        } if_stmt;

        struct {
            ASTNode *condition;
            ASTNode *body;
        } while_stmt;

        struct {
            ASTNode *initializer;
            ASTNode *condition;
            ASTNode *assignment;
            ASTNode *body;
        } for_stmt;

        struct {
            ASTNode *expression;
        } return_stmt;

        struct {
            ASTUnaryOperator operator;
            ASTNode *operand;
        } unary;

        struct {
            ASTNode *expression;
        } print;

        struct {
            ASTNode **statements;
            size_t statement_count;
        } block;

        struct {
            ASTNode *count;
            ASTNode *body;
        } repeat_stmt;

        struct {
            ASTNode *expression;
        } expr_stmt;

        struct {
            ASTBinaryOperator operator;
            ASTNode *left;
            ASTNode *right;
        } binary;

        struct {
            ASTNode *callee;
            ASTNode **arguments;
            size_t argument_count;
        } call;

        struct {
            ASTNode *object;

            const char *member;
            size_t member_length;
        } member_access;

        struct {
            ASTNode *array;
            ASTNode *index;
        } index;

        struct {
            ASTNode *target;
            ASTNode *value;
        } assignment;

        struct {
            ASTNode **statements;
            size_t statement_count;
        } program;

        struct {
            int is_const;
            const char *name;
            size_t name_length;
            ASTType type;
            ASTNode *initializer;
        } var_decl;

        struct {
            const char *name;
            size_t name_length;

            ASTNode **fields;
            size_t field_count;
        } struct_decl;

        struct {
            const char *name;
            size_t name_length;

            const char **members;
            size_t *member_lengths;
            size_t member_count;
        } enum_decl;

        struct {
            const char *name;
            size_t name_length;

            ASTNode **parameters;
            size_t parameter_count;

            ASTType return_type;

            ASTNode *body;
        } function_decl;

        struct {
            const char *path;
            size_t path_length;
        } import_decl;

    } data;
};

ASTNode *ast_new_number(long long value, size_t line, size_t column);
ASTNode *ast_new_decimal(double value, size_t line, size_t column);
ASTNode *ast_new_string(const char *start, size_t length, size_t line, size_t column);
ASTNode *ast_new_char(char value, size_t line, size_t column);
ASTNode *ast_new_bool(int value, size_t line, size_t column);
ASTNode *ast_new_program(ASTNode **statements, size_t statement_count);
ASTNode *ast_new_break(size_t line, size_t column);
ASTNode *ast_new_continue(size_t line, size_t column);

ASTNode *ast_new_import(
    const char *path,
    size_t path_length,
    size_t line,
    size_t column
);

ASTNode *ast_new_function(
    const char *name,
    size_t name_length,
    ASTNode **parameters,
    size_t parameter_count,
    ASTType return_type,
    ASTNode *body,
    size_t line,
    size_t column
);

ASTNode *ast_new_struct(
    const char *name,
    size_t name_length,
    ASTNode **fields,
    size_t field_count,
    size_t line,
    size_t column
);

ASTNode *ast_new_enum(
    const char *name,
    size_t name_length,
    const char **members,
    size_t *member_lengths,
    size_t member_count,
    size_t line,
    size_t column
);

ASTNode *ast_new_for(
    ASTNode *initializer,
    ASTNode *condition,
    ASTNode *assignment,
    ASTNode *body,
    size_t line,
    size_t column
);

ASTNode *ast_new_repeat(
    ASTNode *count,
    ASTNode *body,
    size_t line,
    size_t column
);

ASTNode *ast_new_if(
    ASTNode *condition,
    ASTNode *then_branch,
    ASTNode *else_branch,
    size_t line,
    size_t column
);

ASTNode *ast_new_return(
    ASTNode *expression,
    size_t line,
    size_t column
);

ASTNode *ast_new_while(
    ASTNode *condition,
    ASTNode *body,
    size_t line,
    size_t column
);

ASTNode *ast_new_print(
    ASTNode *expression,
    size_t line,
    size_t column
);

ASTNode *ast_new_block(
    ASTNode **statements,
    size_t statement_count,
    size_t line,
    size_t column
);

ASTNode *ast_new_expr_stmt(
    ASTNode *expression,
    size_t line,
    size_t column
);

ASTNode *ast_new_assignment(
    ASTNode *target,
    ASTNode *value,
    size_t line,
    size_t column
);

ASTNode *ast_new_identifier(
    const char *start,
    size_t length,
    size_t line,
    size_t column
);

ASTNode *ast_new_unary(
    ASTUnaryOperator operator,
    ASTNode *operand,
    size_t line,
    size_t column
);

ASTNode *ast_new_binary(
    ASTBinaryOperator operator,
    ASTNode *left,
    ASTNode *right,
    size_t line,
    size_t column
);

ASTNode *ast_new_call(
    ASTNode *callee,
    ASTNode **arguments,
    size_t argument_count,
    size_t line,
    size_t column
);

ASTNode *ast_new_member_access(
    ASTNode *object,
    const char *member,
    size_t member_length,
    size_t line,
    size_t column
);

ASTNode *ast_new_index(
    ASTNode *array,
    ASTNode *index,
    size_t line,
    size_t column
);

ASTNode *ast_new_var_decl(
    int is_const,
    const char *name,
    size_t name_length,
    ASTType type,
    ASTNode *initializer,
    size_t line,
    size_t column
);

void ast_free(ASTNode *node);

void ast_print(const ASTNode *node, int indent);

#endif /* PEBBLE_AST_H */