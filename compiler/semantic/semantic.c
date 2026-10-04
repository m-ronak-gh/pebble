#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16

typedef enum {
    SYMBOL_VARIABLE,
    SYMBOL_FUNCTION,
    SYMBOL_STRUCT,
    SYMBOL_ENUM,
    SYMBOL_ENUM_MEMBER
} SymbolKind;

typedef struct TypeInfo TypeInfo;

typedef struct {
    TypeInfo *type;
    int initialized;
    int is_const;
    const ASTNode *decl;
} VariableInfo;

typedef struct {
    const char *name;
    size_t length;
    SymbolKind kind;

    union {
        VariableInfo variable;

        struct {
            const ASTNode *node;
        } function;

        struct {
            const ASTNode *node;
        } aggregate;

        struct {
            TypeInfo *enum_type;
        } enum_member;
    } data;
} Symbol;

typedef struct Scope Scope;

struct Scope {
    Scope *parent;
    Symbol *symbols;
    size_t count;
    size_t capacity;
};

struct TypeInfo {
    SemanticTypeKind kind;
    TypeInfo *element_type;

    const char *name;
    size_t name_length;

    size_t *array_lengths;
    size_t array_dimension_count;
};

typedef struct {
    const ASTNode *node;
    TypeInfo *type;
} NodeType;

struct SemanticContext {
    ASTNode *program;

    Scope *global;

    NodeType *node_types;
    size_t node_type_count;
    size_t node_type_capacity;

    TypeInfo **types;
    size_t type_count;
    size_t type_capacity;

    int had_error;
};

typedef struct {
    SemanticContext *ctx;
    Scope *scope;
    TypeInfo *return_type;
    int loop_depth;
} Analyzer;


// Utilities                                                                 */

static void fatal_oom(void)
{
    fprintf(stderr, "fatal: semantic analyzer out of memory\n");
    exit(EXIT_FAILURE);
}

static int text_equal(
    const char *a,
    size_t alen,
    const char *b,
    size_t blen
)
{
    return alen == blen && memcmp(a, b, alen) == 0;
}

static void semantic_error(
    SemanticContext *ctx,
    const ASTNode *node,
    const char *message
)
{
    if (ctx->had_error) {
        return;
    }

    ctx->had_error = 1;

    fprintf(
        stderr,
        "error: %s at %zu:%zu\n",
        message,
        node ? node->line : 0,
        node ? node->column : 0
    );
}


// Semantic type names                                                       */

const char *semantic_type_name(SemanticTypeKind kind)
{
    switch (kind) {
        case SEM_TYPE_VOID:
            return "void";

        case SEM_TYPE_NUMBER:
            return "number";

        case SEM_TYPE_DECIMAL:
            return "decimal";

        case SEM_TYPE_BOOL:
            return "bool";

        case SEM_TYPE_CHAR:
            return "char";

        case SEM_TYPE_STRING:
            return "string";

        case SEM_TYPE_STRUCT:
            return "struct";

        case SEM_TYPE_ENUM:
            return "enum";

        case SEM_TYPE_ARRAY:
            return "array";

        case SEM_TYPE_POINTER:
            return "pointer";

        case SEM_TYPE_ERROR:
            return "<error>";
    }

    return "<error>";
}


// Type storage                                                              */

static TypeInfo *new_type(SemanticContext *ctx)
{
    if (ctx->type_count == ctx->type_capacity) {
        size_t capacity =
            ctx->type_capacity
                ? ctx->type_capacity * 2
                : INITIAL_CAPACITY;

        TypeInfo **types =
            realloc(
                ctx->types,
                capacity * sizeof(*types)
            );

        if (!types) {
            fatal_oom();
        }

        ctx->types = types;
        ctx->type_capacity = capacity;
    }

    TypeInfo *type = calloc(1, sizeof(*type));

    if (!type) {
        fatal_oom();
    }

    ctx->types[ctx->type_count++] = type;

    return type;
}

static TypeInfo *make_simple_type(
    SemanticContext *ctx,
    SemanticTypeKind kind,
    const char *name,
    size_t length
)
{
    TypeInfo *type = new_type(ctx);

    type->kind = kind;
    type->name = name;
    type->name_length = length;

    return type;
}

static TypeInfo *make_array_type(
    SemanticContext *ctx,
    TypeInfo *element,
    const size_t *lengths,
    size_t count
)
{
    TypeInfo *type = new_type(ctx);

    type->kind = SEM_TYPE_ARRAY;
    type->element_type = element;

    type->name = element->name;
    type->name_length = element->name_length;

    type->array_dimension_count = count;

    if (count != 0) {
        type->array_lengths =
            malloc(count * sizeof(*type->array_lengths));

        if (!type->array_lengths) {
            fatal_oom();
        }

        memcpy(
            type->array_lengths,
            lengths,
            count * sizeof(*type->array_lengths)
        );
    }

    return type;
}

// Scopes                                                                    */

static Scope *new_scope(Scope *parent)
{
    Scope *scope = calloc(1, sizeof(*scope));

    if (!scope) {
        fatal_oom();
    }

    scope->parent = parent;

    return scope;
}

static void free_scope(Scope *scope)
{
    if (!scope) {
        return;
    }

    free(scope->symbols);
    free(scope);
}

static Symbol *scope_find_local(
    Scope *scope,
    const char *name,
    size_t length
)
{
    for (size_t i = scope->count; i > 0; --i) {
        Symbol *symbol = &scope->symbols[i - 1];

        if (text_equal(
                symbol->name,
                symbol->length,
                name,
                length
            )) {
            return symbol;
        }
    }

    return NULL;
}

static Symbol *scope_find(
    Scope *scope,
    const char *name,
    size_t length
)
{
    for (Scope *current = scope;
         current;
         current = current->parent) {

        Symbol *symbol =
            scope_find_local(current, name, length);

        if (symbol) {
            return symbol;
        }
    }

    return NULL;
}

static Symbol *scope_add(
    Scope *scope,
    const char *name,
    size_t length
)
{
    if (scope->count == scope->capacity) {
        size_t capacity =
            scope->capacity
                ? scope->capacity * 2
                : INITIAL_CAPACITY;

        Symbol *symbols =
            realloc(scope->symbols, capacity * sizeof(*symbols));

        if (!symbols) {
            fatal_oom();
        }

        scope->symbols = symbols;
        scope->capacity = capacity;
    }

    Symbol *symbol =
        &scope->symbols[scope->count++];

    memset(symbol, 0, sizeof(*symbol));

    symbol->name = name;
    symbol->length = length;

    return symbol;
}


// AST type -> semantic type                                                 */

static TypeInfo *lookup_user_type(
    SemanticContext *ctx,
    const char *name,
    size_t length
)
{
    Symbol *symbol =
        scope_find(ctx->global, name, length);

    if (!symbol) {
        return NULL;
    }

    if (symbol->kind == SYMBOL_STRUCT) {
        return make_simple_type(
            ctx,
            SEM_TYPE_STRUCT,
            name,
            length
        );
    }

    if (symbol->kind == SYMBOL_ENUM) {
        return make_simple_type(
            ctx,
            SEM_TYPE_ENUM,
            name,
            length
        );
    }

    return NULL;
}

static TypeInfo *resolve_ast_type(
    SemanticContext *ctx,
    const ASTType *ast_type,
    const ASTNode *node
)
{
    if (!ast_type) {
        semantic_error(ctx, node, "missing type");
        return NULL;
    }

    TypeInfo *base = NULL;

    switch (ast_type->kind) {
        case AST_TYPE_NUMBER:
            base = make_simple_type(
                ctx,
                SEM_TYPE_NUMBER,
                NULL,
                0
            );
            break;

        case AST_TYPE_DECIMAL:
            base = make_simple_type(
                ctx,
                SEM_TYPE_DECIMAL,
                NULL,
                0
            );
            break;

        case AST_TYPE_BOOL:
            base = make_simple_type(
                ctx,
                SEM_TYPE_BOOL,
                NULL,
                0
            );
            break;

        case AST_TYPE_CHAR:
            base = make_simple_type(
                ctx,
                SEM_TYPE_CHAR,
                NULL,
                0
            );
            break;

        case AST_TYPE_STRING:
            base = make_simple_type(
                ctx,
                SEM_TYPE_STRING,
                NULL,
                0
            );
            break;

        case AST_TYPE_VOID:
            base = make_simple_type(
                ctx,
                SEM_TYPE_VOID,
                NULL,
                0
            );
            break;

        case AST_TYPE_USER:
            base = lookup_user_type(
                ctx,
                ast_type->name,
                ast_type->name_length
            );

            if (!base) {
                semantic_error(ctx, node, "unknown type");
                return NULL;
            }
            break;

        default:
            semantic_error(ctx, node, "unknown type");
            return NULL;
    }

    if (base->kind == SEM_TYPE_VOID &&
        ast_type->dimension_count != 0) {

        semantic_error(
            ctx,
            node,
            "void cannot be used as an array element type"
        );

        return NULL;
    }

    for (size_t i = 0;
         i < ast_type->dimension_count;
         ++i) {

        if (!ast_type->dimensions[i].has_size ||
            ast_type->dimensions[i].size == 0) {

            semantic_error(
                ctx,
                node,
                "array size must be greater than zero"
            );

            return NULL;
        }
    }

    if (ast_type->dimension_count != 0) {
        size_t *lengths =
            malloc(
                ast_type->dimension_count *
                sizeof(*lengths)
            );

        if (!lengths) {
            fatal_oom();
        }

        for (size_t i = 0;
             i < ast_type->dimension_count;
             ++i) {

            lengths[i] =
                ast_type->dimensions[i].size;
        }

        TypeInfo *array =
            make_array_type(
                ctx,
                base,
                lengths,
                ast_type->dimension_count
            );

        free(lengths);

        return array;
    }

    return base;
}


// Type comparison                                                           */

static int type_equal(
    const TypeInfo *a,
    const TypeInfo *b
)
{
    if (!a || !b) {
        return 0;
    }

    if (a->kind != b->kind) {
        return 0;
    }

    if (a->kind == SEM_TYPE_ARRAY) {
        if (a->name_length != b->name_length) {
            return 0;
        }

        if (!text_equal(
                a->name,
                a->name_length,
                b->name,
                b->name_length
            )) {
            return 0;
        }

        if (a->array_dimension_count !=
            b->array_dimension_count) {
            return 0;
        }

        for (size_t i = 0;
             i < a->array_dimension_count;
             ++i) {

            if (a->array_lengths[i] !=
                b->array_lengths[i]) {
                return 0;
            }
        }

        return 1;
    }

    if (a->kind == SEM_TYPE_STRUCT ||
        a->kind == SEM_TYPE_ENUM) {

        return text_equal(
            a->name,
            a->name_length,
            b->name,
            b->name_length
        );
    }

    return 1;
}

static int assignable(
    const TypeInfo *target,
    const TypeInfo *value,
    int allow_null
)
{
    if (type_equal(target, value)) {
        return 1;
    }

    if (allow_null &&
        value &&
        value->kind == SEM_TYPE_VOID &&
        target &&
        (target->kind == SEM_TYPE_STRING ||
         target->kind == SEM_TYPE_ARRAY)) {

        return 1;
    }

    return 0;
}


// Node type tracking                                                        */

static void remember_node_type(
    SemanticContext *ctx,
    const ASTNode *node,
    TypeInfo *type
)
{
    for (size_t i = 0;
         i < ctx->node_type_count;
         ++i) {

        if (ctx->node_types[i].node == node) {
            ctx->node_types[i].type = type;
            return;
        }
    }

    if (ctx->node_type_count ==
        ctx->node_type_capacity) {

        size_t capacity =
            ctx->node_type_capacity
                ? ctx->node_type_capacity * 2
                : 64;

        NodeType *items =
            realloc(
                ctx->node_types,
                capacity * sizeof(*items)
            );

        if (!items) {
            fatal_oom();
        }

        ctx->node_types = items;
        ctx->node_type_capacity = capacity;
    }

    ctx->node_types[ctx->node_type_count++] =
        (NodeType){node, type};
}


// Expression analysis                                                       */

static TypeInfo *analyze_expr(
    Analyzer *a,
    ASTNode *node
);

static int analyze_stmt(
    Analyzer *a,
    ASTNode *node
);

static int analyze_block(
    Analyzer *a,
    ASTNode *block,
    int create_scope
);

static TypeInfo *error_type(
    SemanticContext *ctx,
    ASTNode *node
)
{
    TypeInfo *type =
        make_simple_type(
            ctx,
            SEM_TYPE_ERROR,
            "<error>",
            7
        );

    remember_node_type(ctx, node, type);

    return type;
}

static TypeInfo *builtin_primitive(
    SemanticContext *ctx,
    SemanticTypeKind kind
)
{
    return make_simple_type(
        ctx,
        kind,
        NULL,
        0
    );
}

static TypeInfo *analyze_identifier(
    Analyzer *a,
    ASTNode *node
)
{
    Symbol *symbol =
        scope_find(
            a->scope,
            node->data.identifier.start,
            node->data.identifier.length
        );

    if (!symbol) {
        semantic_error(a->ctx, node, "unknown name");
        return error_type(a->ctx, node);
    }

    if (symbol->kind == SYMBOL_VARIABLE) {
        if (!symbol->data.variable.initialized) {
            semantic_error(
                a->ctx,
                node,
                "variable is used before it is initialized"
            );

            return error_type(a->ctx, node);
        }

        remember_node_type(
            a->ctx,
            node,
            symbol->data.variable.type
        );

        return symbol->data.variable.type;
    }

    if (symbol->kind == SYMBOL_ENUM_MEMBER) {
        remember_node_type(
            a->ctx,
            node,
            symbol->data.enum_member.enum_type
        );

        return symbol->data.enum_member.enum_type;
    }

    semantic_error(
        a->ctx,
        node,
        "name is not a value"
    );

    return error_type(a->ctx, node);
}

static int is_numeric(const TypeInfo *type)
{
    return type &&
           (type->kind == SEM_TYPE_NUMBER ||
            type->kind == SEM_TYPE_DECIMAL);
}


// Built-ins                                                                 */

static TypeInfo *analyze_builtin_call(
    Analyzer *a,
    ASTNode *node
)
{
    ASTNode *callee =
        node->data.call.callee;

    if (callee->kind != AST_IDENTIFIER) {
        return NULL;
    }

    const char *name =
        callee->data.identifier.start;

    size_t length =
        callee->data.identifier.length;


    if (text_equal(name, length, "length", 6)) {
        if (node->data.call.argument_count != 1) {
            semantic_error(
                a->ctx,
                node,
                "length() expects one argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *arg =
            analyze_expr(
                a,
                node->data.call.arguments[0]
            );

        if (arg->kind != SEM_TYPE_STRING &&
            arg->kind != SEM_TYPE_ARRAY) {

            semantic_error(
                a->ctx,
                node,
                "length() requires a string or array"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            builtin_primitive(
                a->ctx,
                SEM_TYPE_NUMBER
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    if (text_equal(name, length, "typeof", 6)) {
        if (node->data.call.argument_count != 1) {
            semantic_error(
                a->ctx,
                node,
                "typeof() expects one argument"
            );

            return error_type(a->ctx, node);
        }

        (void)analyze_expr(
            a,
            node->data.call.arguments[0]
        );

        if (a->ctx->had_error) {
            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            builtin_primitive(
                a->ctx,
                SEM_TYPE_STRING
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    if (text_equal(name, length, "assert", 6)) {
        if (node->data.call.argument_count != 2) {
            semantic_error(
                a->ctx,
                node,
                "assert() expects a bool and a string"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *condition =
            analyze_expr(
                a,
                node->data.call.arguments[0]
            );

        TypeInfo *message =
            analyze_expr(
                a,
                node->data.call.arguments[1]
            );

        if (condition->kind != SEM_TYPE_BOOL ||
            message->kind != SEM_TYPE_STRING) {

            semantic_error(
                a->ctx,
                node,
                "assert() expects a bool and a string"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            builtin_primitive(
                a->ctx,
                SEM_TYPE_VOID
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    if (text_equal(name, length, "panic", 5)) {
        if (node->data.call.argument_count != 1) {
            semantic_error(
                a->ctx,
                node,
                "panic() expects one string argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *message =
            analyze_expr(
                a,
                node->data.call.arguments[0]
            );

        if (message->kind != SEM_TYPE_STRING) {
            semantic_error(
                a->ctx,
                node,
                "panic() expects a string argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            builtin_primitive(
                a->ctx,
                SEM_TYPE_VOID
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    if (text_equal(name, length, "exit", 4)) {
        if (node->data.call.argument_count != 1) {
            semantic_error(
                a->ctx,
                node,
                "exit() expects one number argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *code =
            analyze_expr(
                a,
                node->data.call.arguments[0]
            );

        if (code->kind != SEM_TYPE_NUMBER) {
            semantic_error(
                a->ctx,
                node,
                "exit() expects a number argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            builtin_primitive(
                a->ctx,
                SEM_TYPE_VOID
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    if (text_equal(name, length, "alloc", 5)) {
        if (node->data.call.argument_count != 1) {
            semantic_error(
                a->ctx,
                node,
                "alloc() expects one number argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *size =
            analyze_expr(
                a,
                node->data.call.arguments[0]
            );

        if (size->kind != SEM_TYPE_NUMBER) {
            semantic_error(
                a->ctx,
                node,
                "alloc() expects a number argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            make_simple_type(
                a->ctx,
                SEM_TYPE_POINTER,
                "pointer",
                7
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    if (text_equal(name, length, "free", 4)) {
        if (node->data.call.argument_count != 1) {
            semantic_error(
                a->ctx,
                node,
                "free() expects one pointer argument"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *ptr =
            analyze_expr(
                a,
                node->data.call.arguments[0]
            );

        if (ptr->kind != SEM_TYPE_POINTER) {
            semantic_error(
                a->ctx,
                node,
                "free() expects a value returned by alloc()"
            );

            return error_type(a->ctx, node);
        }

        TypeInfo *result =
            builtin_primitive(
                a->ctx,
                SEM_TYPE_VOID
            );

        remember_node_type(a->ctx, node, result);

        return result;
    }


    return NULL;
}


// Expressions                                                               */

static TypeInfo *analyze_expr(
    Analyzer *a,
    ASTNode *node
)
{
    if (!node) {
        return error_type(a->ctx, node);
    }

    TypeInfo *type = NULL;

    switch (node->kind) {

        case AST_NUMBER_LITERAL:
            type = builtin_primitive(
                a->ctx,
                SEM_TYPE_NUMBER
            );
            break;

        case AST_DECIMAL_LITERAL:
            type = builtin_primitive(
                a->ctx,
                SEM_TYPE_DECIMAL
            );
            break;

        case AST_STRING_LITERAL:
            type = builtin_primitive(
                a->ctx,
                SEM_TYPE_STRING
            );
            break;

        case AST_CHAR_LITERAL:
            type = builtin_primitive(
                a->ctx,
                SEM_TYPE_CHAR
            );
            break;

        case AST_BOOL_LITERAL:
            type = builtin_primitive(
                a->ctx,
                SEM_TYPE_BOOL
            );
            break;

        case AST_NULL_LITERAL:
            type = make_simple_type(
                a->ctx,
                SEM_TYPE_VOID,
                "null",
                4
            );
            break;

        case AST_IDENTIFIER:
            return analyze_identifier(a, node);


        case AST_UNARY: {
            TypeInfo *operand =
                analyze_expr(
                    a,
                    node->data.unary.operand
                );

            if (a->ctx->had_error) {
                return error_type(a->ctx, node);
            }

            if (node->data.unary.operator ==
                AST_UNARY_NOT) {

                if (operand->kind != SEM_TYPE_BOOL) {
                    semantic_error(
                        a->ctx,
                        node,
                        "'!' requires a bool operand"
                    );

                    return error_type(a->ctx, node);
                }

                type = operand;

            } else {

                if (node->data.unary.operator ==
                    AST_UNARY_BITWISE_NOT) {

                    if (operand->kind != SEM_TYPE_NUMBER) {
                        semantic_error(
                            a->ctx,
                            node,
                            "'~' requires a number operand"
                        );

                        return error_type(a->ctx, node);
                    }

                } else if (!is_numeric(operand)) {

                    semantic_error(
                        a->ctx,
                        node,
                        "unary '-' requires a number or decimal operand"
                    );

                    return error_type(a->ctx, node);
                }

                type = operand;
            }

            break;
        }


        case AST_BINARY: {
            TypeInfo *left =
                analyze_expr(
                    a,
                    node->data.binary.left
                );

            TypeInfo *right =
                analyze_expr(
                    a,
                    node->data.binary.right
                );

            if (a->ctx->had_error) {
                return error_type(a->ctx, node);
            }

            ASTBinaryOperator op =
                node->data.binary.operator;

            int arithmetic =
                op == AST_BINARY_ADD ||
                op == AST_BINARY_SUBTRACT ||
                op == AST_BINARY_MULTIPLY ||
                op == AST_BINARY_DIVIDE ||
                op == AST_BINARY_MODULO;

            int comparison =
                op == AST_BINARY_LESS ||
                op == AST_BINARY_LESS_EQUAL ||
                op == AST_BINARY_GREATER ||
                op == AST_BINARY_GREATER_EQUAL;

            int equality =
                op == AST_BINARY_EQUAL ||
                op == AST_BINARY_NOT_EQUAL;

            int logical =
                op == AST_BINARY_LOGICAL_AND ||
                op == AST_BINARY_LOGICAL_OR;

            int bitwise =
                op == AST_BINARY_BITWISE_AND ||
                op == AST_BINARY_BITWISE_OR ||
                op == AST_BINARY_BITWISE_XOR ||
                op == AST_BINARY_SHIFT_LEFT ||
                op == AST_BINARY_SHIFT_RIGHT;


            if (arithmetic) {
                if (!is_numeric(left) ||
                    !is_numeric(right) ||
                    left->kind != right->kind) {

                    semantic_error(
                        a->ctx,
                        node,
                        "arithmetic operands must have the same numeric type"
                    );

                    return error_type(a->ctx, node);
                }

                if (op == AST_BINARY_MODULO &&
                    left->kind != SEM_TYPE_NUMBER) {

                    semantic_error(
                        a->ctx,
                        node,
                        "'%' requires number operands"
                    );

                    return error_type(a->ctx, node);
                }

                type = left;

            } else if (logical) {

                if (left->kind != SEM_TYPE_BOOL ||
                    right->kind != SEM_TYPE_BOOL) {

                    semantic_error(
                        a->ctx,
                        node,
                        "logical operators require bool operands"
                    );

                    return error_type(a->ctx, node);
                }

                type = builtin_primitive(
                    a->ctx,
                    SEM_TYPE_BOOL
                );

            } else if (bitwise) {

                if (left->kind != SEM_TYPE_NUMBER ||
                    right->kind != SEM_TYPE_NUMBER) {

                    semantic_error(
                        a->ctx,
                        node,
                        "bitwise operators require number operands"
                    );

                    return error_type(a->ctx, node);
                }

                type = builtin_primitive(
                    a->ctx,
                    SEM_TYPE_NUMBER
                );

            } else if (comparison) {

                if (!is_numeric(left) ||
                    !is_numeric(right) ||
                    left->kind != right->kind) {

                    semantic_error(
                        a->ctx,
                        node,
                        "comparison operands must have the same numeric type"
                    );

                    return error_type(a->ctx, node);
                }

                type = builtin_primitive(
                    a->ctx,
                    SEM_TYPE_BOOL
                );

            } else if (equality) {

                if (!type_equal(left, right) &&
                    !(left->kind == SEM_TYPE_VOID ||
                      right->kind == SEM_TYPE_VOID)) {

                    semantic_error(
                        a->ctx,
                        node,
                        "equality operands must have compatible types"
                    );

                    return error_type(a->ctx, node);
                }

                type = builtin_primitive(
                    a->ctx,
                    SEM_TYPE_BOOL
                );
            }

            break;
        }


        case AST_CALL: {
            TypeInfo *builtin =
                analyze_builtin_call(a, node);

            if (builtin) {
                return builtin;
            }

            ASTNode *callee =
                node->data.call.callee;

            if (callee->kind != AST_IDENTIFIER) {
                semantic_error(
                    a->ctx,
                    node,
                    "function calls require a function name"
                );

                return error_type(a->ctx, node);
            }

            Symbol *symbol =
                scope_find(
                    a->scope,
                    callee->data.identifier.start,
                    callee->data.identifier.length
                );

            if (!symbol ||
                symbol->kind != SYMBOL_FUNCTION) {

                semantic_error(
                    a->ctx,
                    callee,
                    "unknown function"
                );

                return error_type(a->ctx, node);
            }

            const ASTNode *fn =
                symbol->data.function.node;

            if (node->data.call.argument_count !=
                fn->data.function_decl.parameter_count) {

                semantic_error(
                    a->ctx,
                    node,
                    "wrong number of arguments in function call"
                );

                return error_type(a->ctx, node);
            }

            for (size_t i = 0;
                 i < node->data.call.argument_count;
                 ++i) {

                TypeInfo *arg =
                    analyze_expr(
                        a,
                        node->data.call.arguments[i]
                    );

                ASTNode *param =
                    fn->data.function_decl.parameters[i];

                TypeInfo *param_type =
                    resolve_ast_type(
                        a->ctx,
                        &param->data.var_decl.type,
                        param
                    );

                if (!assignable(
                        param_type,
                        arg,
                        1
                    )) {

                    semantic_error(
                        a->ctx,
                        node->data.call.arguments[i],
                        "argument type does not match parameter type"
                    );

                    return error_type(a->ctx, node);
                }
            }

            type =
                resolve_ast_type(
                    a->ctx,
                    &fn->data.function_decl.return_type,
                    fn
                );

            break;
        }


        case AST_MEMBER_ACCESS: {
            TypeInfo *object =
                analyze_expr(
                    a,
                    node->data.member_access.object
                );

            if (!object ||
                object->kind != SEM_TYPE_STRUCT) {

                semantic_error(
                    a->ctx,
                    node,
                    "member access requires a struct value"
                );

                return error_type(a->ctx, node);
            }

            Symbol *struct_symbol =
                scope_find(
                    a->ctx->global,
                    object->name,
                    object->name_length
                );

            if (!struct_symbol ||
                struct_symbol->kind != SYMBOL_STRUCT) {

                semantic_error(
                    a->ctx,
                    node,
                    "unknown struct type"
                );

                return error_type(a->ctx, node);
            }

            const ASTNode *decl =
                struct_symbol->data.aggregate.node;

            for (size_t i = 0;
                 i < decl->data.struct_decl.field_count;
                 ++i) {

                ASTNode *field =
                    decl->data.struct_decl.fields[i];

                if (text_equal(
                        field->data.var_decl.name,
                        field->data.var_decl.name_length,
                        node->data.member_access.member,
                        node->data.member_access.member_length
                    )) {

                    type =
                        resolve_ast_type(
                            a->ctx,
                            &field->data.var_decl.type,
                            field
                        );

                    break;
                }
            }

            if (!type) {
                semantic_error(
                    a->ctx,
                    node,
                    "struct has no member with that name"
                );

                return error_type(a->ctx, node);
            }

            break;
        }


        case AST_INDEX: {
            TypeInfo *array =
                analyze_expr(
                    a,
                    node->data.index.array
                );

            TypeInfo *index =
                analyze_expr(
                    a,
                    node->data.index.index
                );

            if (!array ||
                array->kind != SEM_TYPE_ARRAY) {

                semantic_error(
                    a->ctx,
                    node,
                    "indexing requires an array"
                );

                return error_type(a->ctx, node);
            }

            if (!index ||
                index->kind != SEM_TYPE_NUMBER) {

                semantic_error(
                    a->ctx,
                    node->data.index.index,
                    "array index must be a number"
                );

                return error_type(a->ctx, node);
            }

            if (array->array_dimension_count == 0) {
                return error_type(a->ctx, node);
            }

            if (node->data.index.index->kind ==
                AST_NUMBER_LITERAL) {

                long long literal =
                    node->data.index.index->data.number;

                if (literal < 0 ||
                    (unsigned long long)literal >=
                        array->array_lengths[0]) {

                    semantic_error(
                        a->ctx,
                        node->data.index.index,
                        "array index is outside the fixed array bounds"
                    );

                    return error_type(a->ctx, node);
                }
            }

            if (array->array_dimension_count == 1) {

                switch (array->kind) {
                    case SEM_TYPE_ARRAY:
                        break;

                    default:
                        break;
                }
            }

            type =
                lookup_user_type(
                    a->ctx,
                    array->name,
                    array->name_length
                );

            if (!type) {
                semantic_error(
                    a->ctx,
                    node,
                    "unable to resolve array element type"
                );

                return error_type(a->ctx, node);
            }

            TypeInfo *element = array->element_type;

            if (array->array_dimension_count > 1) {
                type =
                    make_array_type(
                        a->ctx,
                        type,
                        array->array_lengths + 1,
                        array->array_dimension_count - 1
                    );
            }   else {
                type = element;
            }

            break;
        }


        default:
            semantic_error(
                a->ctx,
                node,
                "node is not a valid expression"
            );

            return error_type(a->ctx, node);
    }

    if (!type) {
        return error_type(a->ctx, node);
    }

    remember_node_type(
        a->ctx,
        node,
        type
    );

    return type;
}


// Assignment analysis                                                       */

static int target_is_const(
    Analyzer *a,
    ASTNode *target
)
{
    if (target->kind == AST_IDENTIFIER) {
        Symbol *symbol =
            scope_find(
                a->scope,
                target->data.identifier.start,
                target->data.identifier.length
            );

        return symbol &&
               symbol->kind == SYMBOL_VARIABLE &&
               symbol->data.variable.is_const;
    }

    if (target->kind == AST_MEMBER_ACCESS) {
        return target_is_const(
            a,
            target->data.member_access.object
        );
    }

    if (target->kind == AST_INDEX) {
        return target_is_const(
            a,
            target->data.index.array
        );
    }

    return 0;
}

static int analyze_assignment_target(
    Analyzer *a,
    ASTNode *target,
    TypeInfo **out_type
)
{
    if (target->kind == AST_IDENTIFIER) {

        Symbol *symbol =
            scope_find(
                a->scope,
                target->data.identifier.start,
                target->data.identifier.length
            );

        if (!symbol ||
            symbol->kind != SYMBOL_VARIABLE) {

            semantic_error(
                a->ctx,
                target,
                "assignment target is not a variable"
            );

            return 0;
        }

        if (symbol->data.variable.is_const) {
            semantic_error(
                a->ctx,
                target,
                "cannot assign to a const variable"
            );

            return 0;
        }

        *out_type =
            symbol->data.variable.type;

        return 1;
    }

    if (target->kind == AST_MEMBER_ACCESS ||
        target->kind == AST_INDEX) {

        if (target_is_const(a, target)) {
            semantic_error(
                a->ctx,
                target,
                "cannot assign through a const value"
            );

            return 0;
        }

        *out_type =
            analyze_expr(a, target);

        return !a->ctx->had_error;
    }

    semantic_error(
        a->ctx,
        target,
        "invalid assignment target"
    );

    return 0;
}


// Statements                                                                */

static int analyze_stmt(
    Analyzer *a,
    ASTNode *node
)
{
    if (!node) {
        return 1;
    }

    switch (node->kind) {

        case AST_VAR_DECL: {
            if (scope_find_local(
                    a->scope,
                    node->data.var_decl.name,
                    node->data.var_decl.name_length
                )) {

                semantic_error(
                    a->ctx,
                    node,
                    "name is already declared in this scope"
                );

                return 0;
            }

            TypeInfo *type =
                resolve_ast_type(
                    a->ctx,
                    &node->data.var_decl.type,
                    node
                );

            if (!type) {
                return 0;
            }

            if (type->kind == SEM_TYPE_VOID) {
                semantic_error(
                    a->ctx,
                    node,
                    "variable cannot have type void"
                );

                return 0;
            }

            Symbol *symbol =
                scope_add(
                    a->scope,
                    node->data.var_decl.name,
                    node->data.var_decl.name_length
                );

            symbol->kind =
                SYMBOL_VARIABLE;

            symbol->data.variable.type =
                type;

            symbol->data.variable.is_const =
                node->data.var_decl.is_const;

            symbol->data.variable.decl =
                node;

            symbol->data.variable.initialized = 0;

            if (node->data.var_decl.initializer) {

                TypeInfo *value =
                    analyze_expr(
                        a,
                        node->data.var_decl.initializer
                    );

                if (!assignable(type, value, 1)) {
                    semantic_error(
                        a->ctx,
                        node->data.var_decl.initializer,
                        "initializer type does not match variable type"
                    );

                    return 0;
                }

                symbol->data.variable.initialized = 1;

            } else if (
                type->kind == SEM_TYPE_STRUCT ||
                type->kind == SEM_TYPE_ENUM ||
                type->kind == SEM_TYPE_ARRAY
            ) {

                symbol->data.variable.initialized = 1;

            } else if (node->data.var_decl.is_const) {

                semantic_error(
                    a->ctx,
                    node,
                    "const variables must be initialized at declaration"
                );

                return 0;

            } else {

                semantic_error(
                    a->ctx,
                    node,
                    "variable must be initialized at declaration"
                );

                return 0;
            }

            return 1;
        }


        case AST_ASSIGNMENT: {
            TypeInfo *target_type = NULL;

            if (!analyze_assignment_target(
                    a,
                    node->data.assignment.target,
                    &target_type
                )) {
                return 0;
            }

            TypeInfo *value_type =
                analyze_expr(
                    a,
                    node->data.assignment.value
                );

            if (!assignable(
                    target_type,
                    value_type,
                    1
                )) {

                semantic_error(
                    a->ctx,
                    node->data.assignment.value,
                    "assigned value type does not match target type"
                );

                return 0;
            }

            if (node->data.assignment.target->kind ==
                AST_IDENTIFIER) {

                Symbol *symbol =
                    scope_find(
                        a->scope,
                        node->data.assignment.target->data.identifier.start,
                        node->data.assignment.target->data.identifier.length
                    );

                symbol->data.variable.initialized = 1;
            }

            return 1;
        }


        case AST_EXPR_STMT:
            (void)analyze_expr(
                a,
                node->data.expr_stmt.expression
            );

            return !a->ctx->had_error;


        case AST_PRINT:
            (void)analyze_expr(
                a,
                node->data.print.expression
            );

            return !a->ctx->had_error;


        case AST_BLOCK:
            return analyze_block(a, node, 1);


        case AST_IF: {
            TypeInfo *condition =
                analyze_expr(
                    a,
                    node->data.if_stmt.condition
                );

            if (!condition ||
                condition->kind != SEM_TYPE_BOOL) {

                semantic_error(
                    a->ctx,
                    node->data.if_stmt.condition,
                    "if condition must be bool"
                );

                return 0;
            }

            Scope *scope = a->scope;

            size_t n = scope->count;

            int *before =
                n ? malloc(n * sizeof(*before)) : NULL;

            if (n && !before) {
                fatal_oom();
            }

            for (size_t i = 0; i < n; ++i) {
                before[i] =
                    scope->symbols[i].data.variable.initialized;
            }

            if (!analyze_stmt(
                    a,
                    node->data.if_stmt.then_branch
                )) {

                free(before);
                return 0;
            }

            int *then_state =
                n ? malloc(n * sizeof(*then_state)) : NULL;

            if (n && !then_state) {
                fatal_oom();
            }

            for (size_t i = 0; i < n; ++i) {
                then_state[i] =
                    scope->symbols[i].data.variable.initialized;
            }

            for (size_t i = 0; i < n; ++i) {
                scope->symbols[i].data.variable.initialized =
                    before[i];
            }

            int has_else =
                node->data.if_stmt.else_branch != NULL;

            if (has_else) {

                if (!analyze_stmt(
                        a,
                        node->data.if_stmt.else_branch
                    )) {

                    free(before);
                    free(then_state);
                    return 0;
                }

                for (size_t i = 0; i < n; ++i) {
                    scope->symbols[i].data.variable.initialized =
                        then_state[i] &&
                        scope->symbols[i].data.variable.initialized;
                }

            } else {

                for (size_t i = 0; i < n; ++i) {
                    scope->symbols[i].data.variable.initialized =
                        before[i];
                }
            }

            free(before);
            free(then_state);

            return 1;
        }


        case AST_WHILE:
        case AST_REPEAT: {
            ASTNode *condition =
                node->kind == AST_WHILE
                    ? node->data.while_stmt.condition
                    : node->data.repeat_stmt.count;

            TypeInfo *type =
                analyze_expr(
                    a,
                    condition
                );

            if (node->kind == AST_WHILE) {

                if (!type ||
                    type->kind != SEM_TYPE_BOOL) {

                    semantic_error(
                        a->ctx,
                        condition,
                        "while condition must be bool"
                    );

                    return 0;
                }

            } else {

                if (!type ||
                    type->kind != SEM_TYPE_NUMBER) {

                    semantic_error(
                        a->ctx,
                        condition,
                        "repeat count must be number"
                    );

                    return 0;
                }
            }

            Scope *scope = a->scope;

            size_t n = scope->count;

            int *before =
                n ? malloc(n * sizeof(*before)) : NULL;

            if (n && !before) {
                fatal_oom();
            }

            for (size_t i = 0; i < n; ++i) {
                before[i] =
                    scope->symbols[i].data.variable.initialized;
            }

            a->loop_depth++;

            int ok =
                node->kind == AST_WHILE
                    ? analyze_stmt(
                        a,
                        node->data.while_stmt.body
                    )
                    : analyze_stmt(
                        a,
                        node->data.repeat_stmt.body
                    );

            a->loop_depth--;

            for (size_t i = 0; i < n; ++i) {
                scope->symbols[i].data.variable.initialized =
                    before[i];
            }

            free(before);

            return ok;
        }


        case AST_FOR: {
            Scope *saved = a->scope;

            a->scope =
                new_scope(saved);

            if (!analyze_stmt(
                    a,
                    node->data.for_stmt.initializer
                )) {

                free_scope(a->scope);
                a->scope = saved;

                return 0;
            }

            TypeInfo *condition =
                analyze_expr(
                    a,
                    node->data.for_stmt.condition
                );

            if (!condition ||
                condition->kind != SEM_TYPE_BOOL) {

                semantic_error(
                    a->ctx,
                    node->data.for_stmt.condition,
                    "for condition must be bool"
                );

                free_scope(a->scope);
                a->scope = saved;

                return 0;
            }

            a->loop_depth++;

            if (!analyze_stmt(
                    a,
                    node->data.for_stmt.assignment
                ) ||
                !analyze_stmt(
                    a,
                    node->data.for_stmt.body
                )) {

                a->loop_depth--;

                free_scope(a->scope);
                a->scope = saved;

                return 0;
            }

            a->loop_depth--;

            free_scope(a->scope);
            a->scope = saved;

            return 1;
        }


        case AST_BREAK:
        case AST_CONTINUE:
            if (a->loop_depth == 0) {
                semantic_error(
                    a->ctx,
                    node,
                    "loop control statement is only valid inside a loop"
                );

                return 0;
            }

            return 1;


        case AST_RETURN: {
            TypeInfo *actual =
                node->data.return_stmt.expression
                    ? analyze_expr(
                        a,
                        node->data.return_stmt.expression
                    )
                    : builtin_primitive(
                        a->ctx,
                        SEM_TYPE_VOID
                    );

            if (!type_equal(
                    a->return_type,
                    actual
                )) {

                semantic_error(
                    a->ctx,
                    node,
                    "return value does not match function return type"
                );

                return 0;
            }

            return 1;
        }


        default:
            semantic_error(
                a->ctx,
                node,
                "invalid statement in function body"
            );

            return 0;
    }
}


// Blocks                                                                    */

static int analyze_block(
    Analyzer *a,
    ASTNode *block,
    int create_scope
)
{
    Scope *saved = a->scope;

    if (create_scope) {
        a->scope =
            new_scope(saved);
    }

    int ok = 1;

    for (size_t i = 0;
         i < block->data.block.statement_count;
         ++i) {

        if (!analyze_stmt(
                a,
                block->data.block.statements[i]
            )) {

            ok = 0;
            break;
        }
    }

    if (create_scope) {
        free_scope(a->scope);
        a->scope = saved;
    }

    return ok;
}


// Global declarations                                                       */

static int declare_globals(
    SemanticContext *ctx
)
{
    ASTNode *program = ctx->program;

    size_t main_count = 0;

    for (size_t i = 0;
         i < program->data.program.statement_count;
         ++i) {

        ASTNode *node =
            program->data.program.statements[i];

        const char *name = NULL;
        size_t length = 0;
        SymbolKind kind;

        if (node->kind == AST_FUNCTION) {

            name = node->data.function_decl.name;
            length = node->data.function_decl.name_length;
            kind = SYMBOL_FUNCTION;

            if (text_equal(
                    name,
                    length,
                    "main",
                    4
                )) {
                main_count++;
            }

        } else if (node->kind == AST_STRUCT) {

            name = node->data.struct_decl.name;
            length = node->data.struct_decl.name_length;
            kind = SYMBOL_STRUCT;

        } else if (node->kind == AST_ENUM) {

            name = node->data.enum_decl.name;
            length = node->data.enum_decl.name_length;
            kind = SYMBOL_ENUM;

        } else if (node->kind == AST_IMPORT) {

            continue;

        } else {

            semantic_error(
                ctx,
                node,
                "only imports, functions, structs, and enums are allowed at the top level"
            );

            return 0;
        }

        if (scope_find_local(
                ctx->global,
                name,
                length
            )) {

            semantic_error(
                ctx,
                node,
                "top-level name is already declared"
            );

            return 0;
        }

        Symbol *symbol =
            scope_add(
                ctx->global,
                name,
                length
            );

        symbol->kind = kind;

        if (kind == SYMBOL_FUNCTION) {
            symbol->data.function.node = node;
        } else {
            symbol->data.aggregate.node = node;
        }
    }

    if (main_count != 1) {
        semantic_error(
            ctx,
            program,
            "program must contain exactly one function number main()"
        );

        return 0;
    }

    return 1;
}


// Structs and enums                                                         */

static int analyze_structs_and_enums(
    SemanticContext *ctx
)
{
    for (size_t i = 0;
         i < ctx->program->data.program.statement_count;
         ++i) {

        ASTNode *node =
            ctx->program->data.program.statements[i];

        if (node->kind == AST_STRUCT) {

            for (size_t j = 0;
                 j < node->data.struct_decl.field_count;
                 ++j) {

                ASTNode *field =
                    node->data.struct_decl.fields[j];

                if (field->data.var_decl.type.kind ==
                    AST_TYPE_VOID) {

                    semantic_error(
                        ctx,
                        field,
                        "struct fields cannot have type void"
                    );

                    return 0;
                }

                if (!resolve_ast_type(
                        ctx,
                        &field->data.var_decl.type,
                        field
                    )) {
                    return 0;
                }

                for (size_t k = 0; k < j; ++k) {

                    ASTNode *other =
                        node->data.struct_decl.fields[k];

                    if (text_equal(
                            field->data.var_decl.name,
                            field->data.var_decl.name_length,
                            other->data.var_decl.name,
                            other->data.var_decl.name_length
                        )) {

                        semantic_error(
                            ctx,
                            field,
                            "struct field is declared more than once"
                        );

                        return 0;
                    }
                }
            }

        } else if (node->kind == AST_ENUM) {

            for (size_t j = 0;
                 j < node->data.enum_decl.member_count;
                 ++j) {

                for (size_t k = 0; k < j; ++k) {

                    if (text_equal(
                            node->data.enum_decl.members[j],
                            node->data.enum_decl.member_lengths[j],
                            node->data.enum_decl.members[k],
                            node->data.enum_decl.member_lengths[k]
                        )) {

                        semantic_error(
                            ctx,
                            node,
                            "enum member is declared more than once"
                        );

                        return 0;
                    }
                }

                if (scope_find_local(
                        ctx->global,
                        node->data.enum_decl.members[j],
                        node->data.enum_decl.member_lengths[j]
                    )) {

                    semantic_error(
                        ctx,
                        node,
                        "enum member name conflicts with a top-level name"
                    );

                    return 0;
                }

                Symbol *member =
                    scope_add(
                        ctx->global,
                        node->data.enum_decl.members[j],
                        node->data.enum_decl.member_lengths[j]
                    );

                member->kind =
                    SYMBOL_ENUM_MEMBER;

                member->data.enum_member.enum_type =
                    lookup_user_type(
                        ctx,
                        node->data.enum_decl.name,
                        node->data.enum_decl.name_length
                    );
            }
        }
    }

    return 1;
}


// Functions                                                                 */

static int analyze_functions(
    SemanticContext *ctx
)
{
    for (size_t i = 0;
         i < ctx->program->data.program.statement_count;
         ++i) {

        ASTNode *node =
            ctx->program->data.program.statements[i];

        if (node->kind != AST_FUNCTION) {
            continue;
        }

        TypeInfo *return_type =
            resolve_ast_type(
                ctx,
                &node->data.function_decl.return_type,
                node
            );

        if (!return_type) {
            return 0;
        }

        if (text_equal(
                node->data.function_decl.name,
                node->data.function_decl.name_length,
                "main",
                4
            )) {

            TypeInfo *number_type =
                builtin_primitive(
                    ctx,
                    SEM_TYPE_NUMBER
                );

            if (!type_equal(
                    return_type,
                    number_type
                ) ||
                node->data.function_decl.parameter_count != 0) {

                semantic_error(
                    ctx,
                    node,
                    "main must have signature function number main()"
                );

                return 0;
            }
        }

        Scope *function_scope =
            new_scope(ctx->global);

        Analyzer analyzer = {
            ctx,
            function_scope,
            return_type,
            0
        };

        for (size_t p = 0;
             p < node->data.function_decl.parameter_count;
             ++p) {

            ASTNode *param =
                node->data.function_decl.parameters[p];

            TypeInfo *param_type =
                resolve_ast_type(
                    ctx,
                    &param->data.var_decl.type,
                    param
                );

            if (!param_type) {
                free_scope(function_scope);
                return 0;
            }

            if (param_type->kind == SEM_TYPE_VOID) {

                semantic_error(
                    ctx,
                    param,
                    "parameter cannot have type void"
                );

                free_scope(function_scope);
                return 0;
            }

            if (scope_find_local(
                    function_scope,
                    param->data.var_decl.name,
                    param->data.var_decl.name_length
                )) {

                semantic_error(
                    ctx,
                    param,
                    "parameter is declared more than once"
                );

                free_scope(function_scope);
                return 0;
            }

            Symbol *symbol =
                scope_add(
                    function_scope,
                    param->data.var_decl.name,
                    param->data.var_decl.name_length
                );

            symbol->kind =
                SYMBOL_VARIABLE;

            symbol->data.variable.type =
                param_type;

            symbol->data.variable.initialized =
                1;

            symbol->data.variable.is_const =
                0;

            symbol->data.variable.decl =
                param;
        }

        if (!analyze_block(
                &analyzer,
                node->data.function_decl.body,
                1
            )) {

            free_scope(function_scope);
            return 0;
        }

        free_scope(function_scope);
    }

    return 1;
}


// Public API                                                                */

SemanticContext *semantic_analyze(
    ASTNode *program
)
{
    if (!program ||
        program->kind != AST_PROGRAM) {

        return NULL;
    }

    SemanticContext *ctx =
        calloc(1, sizeof(*ctx));

    if (!ctx) {
        fatal_oom();
    }

    ctx->program = program;
    ctx->global = new_scope(NULL);

    if (!declare_globals(ctx) ||
        !analyze_structs_and_enums(ctx) ||
        !analyze_functions(ctx)) {

        semantic_free(ctx);
        return NULL;
    }

    return ctx;
}

void semantic_free(
    SemanticContext *context
)
{
    if (!context) {
        return;
    }

    free_scope(context->global);

    for (size_t i = 0;
         i < context->type_count;
         ++i) {

        free(context->types[i]->array_lengths);
        free(context->types[i]);
    }

    free(context->types);
    free(context->node_types);
    free(context);
}

int semantic_type_of(
    const SemanticContext *context,
    const ASTNode *node,
    SemanticType *out_type
)
{
    if (!context ||
        !node ||
        !out_type) {

        return 0;
    }

    for (size_t i = 0;
         i < context->node_type_count;
         ++i) {

        if (context->node_types[i].node == node) {

            const TypeInfo *type =
                context->node_types[i].type;

            out_type->kind =
                type->kind;

            out_type->name =
                type->name;

            out_type->name_length =
                type->name_length;

            out_type->array_lengths =
                type->array_lengths;

            out_type->array_dimension_count =
                type->array_dimension_count;

            return 1;
        }
    }

    return 0;
}