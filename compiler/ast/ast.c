#include "ast.h"

#include <stdio.h>
#include <stdlib.h>

static ASTNode *ast_alloc(ASTNodeKind kind, size_t line, size_t column)
{
    ASTNode *node = malloc(sizeof(ASTNode));

    if (node == NULL) {
        fprintf(stderr, "fatal: failed to allocate AST node\n");
        exit(EXIT_FAILURE);
    }

    node->kind = kind;
    node->line = line;
    node->column = column;

    return node;
}

ASTNode *ast_new_number(long long value, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_NUMBER_LITERAL, line, column);

    node->data.number = value;

    return node;
}

ASTNode *ast_new_decimal(double value, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_DECIMAL_LITERAL, line, column);

    node->data.decimal = value;

    return node;
}

ASTNode *ast_new_string(
    const char *start,
    size_t length,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_STRING_LITERAL, line, column);

    node->data.string.start = start;
    node->data.string.length = length;

    return node;
}

ASTNode *ast_new_char(char value, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_CHAR_LITERAL, line, column);

    node->data.character = value;

    return node;
}

ASTNode *ast_new_bool(int value, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_BOOL_LITERAL, line, column);

    node->data.boolean = value;

    return node;
}

ASTNode *ast_new_identifier(
    const char *start,
    size_t length,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_IDENTIFIER, line, column);

    node->data.identifier.start = start;
    node->data.identifier.length = length;

    return node;
}

ASTNode *ast_new_unary(
    ASTUnaryOperator operator,
    ASTNode *operand,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_UNARY, line, column);

    node->data.unary.operator = operator;
    node->data.unary.operand = operand;

    return node;
}

ASTNode *ast_new_binary(
    ASTBinaryOperator operator,
    ASTNode *left,
    ASTNode *right,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_BINARY, line, column);

    node->data.binary.operator = operator;
    node->data.binary.left = left;
    node->data.binary.right = right;

    return node;
}

ASTNode *ast_new_import(
    const char *path,
    size_t path_length,
    size_t line,
    size_t column)
{
    ASTNode *node = malloc(sizeof(ASTNode));

    if (node == NULL) {
        return NULL;
    }

    node->kind = AST_IMPORT;
    node->line = line;
    node->column = column;

    node->data.import_decl.path = path;
    node->data.import_decl.path_length = path_length;

    return node;
}

void ast_free(ASTNode *node)
{
    if (node == NULL) {
        return;
    }

    switch (node->kind) {
        case AST_IMPORT:
            break;

        case AST_NULL_LITERAL:
            break;

        case AST_STRUCT:
            for (size_t i = 0; i < node->data.struct_decl.field_count; i++) {
                ast_free(node->data.struct_decl.fields[i]);
            }

            free(node->data.struct_decl.fields);
            break;

        case AST_ENUM:
            free(node->data.enum_decl.members);
            free(node->data.enum_decl.member_lengths);
            break;

        case AST_FUNCTION:
            for (size_t i = 0; i < node->data.function_decl.parameter_count; i++) {
                ast_free(node->data.function_decl.parameters[i]);
            }

            free(node->data.function_decl.parameters);
            free(node->data.function_decl.return_type.dimensions);
            ast_free(node->data.function_decl.body);
            break;

        case AST_FOR:
            ast_free(node->data.for_stmt.initializer);
            ast_free(node->data.for_stmt.condition);
            ast_free(node->data.for_stmt.assignment);
            ast_free(node->data.for_stmt.body);
            break;

        case AST_REPEAT:
            ast_free(node->data.repeat_stmt.count);
            ast_free(node->data.repeat_stmt.body);
            break;

        case AST_RETURN:
            ast_free(node->data.return_stmt.expression);
            break;

        case AST_WHILE:
            ast_free(node->data.while_stmt.condition);
            ast_free(node->data.while_stmt.body);
            break;

        case AST_IF:
            ast_free(node->data.if_stmt.condition);
            ast_free(node->data.if_stmt.then_branch);
            ast_free(node->data.if_stmt.else_branch);
            break;

        case AST_BLOCK:
            for (size_t i = 0; i < node->data.block.statement_count; i++) {
                ast_free(node->data.block.statements[i]);
            }

            free(node->data.block.statements);
            break;

        case AST_PRINT:
            ast_free(node->data.print.expression);
            break;

        case AST_EXPR_STMT:
            ast_free(node->data.expr_stmt.expression);
            break;

        case AST_UNARY:
            ast_free(node->data.unary.operand);
            break;

        case AST_BINARY:
            ast_free(node->data.binary.left);
            ast_free(node->data.binary.right);
            break;

        case AST_CALL:
            ast_free(node->data.call.callee);

            for (size_t i = 0;
                i < node->data.call.argument_count;
                i++) {
                ast_free(node->data.call.arguments[i]);
            }

            free(node->data.call.arguments);
            break;

        case AST_MEMBER_ACCESS:
            ast_free(node->data.member_access.object);
            break;

        case AST_INDEX:
            ast_free(node->data.index.array);
            ast_free(node->data.index.index);
            break;

        case AST_ASSIGNMENT:
            ast_free(node->data.assignment.target);
            ast_free(node->data.assignment.value);
            break;

        case AST_VAR_DECL:
            free(node->data.var_decl.type.dimensions);
            ast_free(node->data.var_decl.initializer);
            break;

        case AST_PROGRAM:
            for (size_t i = 0; i < node->data.program.statement_count; i++) {
                ast_free(node->data.program.statements[i]);
            }

            free(node->data.program.statements);
            break;

        default:
            break;
    }

    free(node);
}

static void print_indent(int indent)
{
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

static const char *unary_operator_name(ASTUnaryOperator operator)
{
    switch (operator) {
        case AST_UNARY_NEGATE:
            return "NEGATE";

        case AST_UNARY_NOT:
            return "NOT";

        case AST_UNARY_BITWISE_NOT:
            return "BITWISE_NOT";
    }

    return "UNKNOWN";
}

static const char *binary_operator_name(ASTBinaryOperator operator)
{
    switch (operator) {
        case AST_BINARY_ADD:
            return "ADD";

        case AST_BINARY_SUBTRACT:
            return "SUBTRACT";

        case AST_BINARY_MULTIPLY:
            return "MULTIPLY";

        case AST_BINARY_DIVIDE:
            return "DIVIDE";

        case AST_BINARY_MODULO:
            return "MODULO";

        case AST_BINARY_EQUAL:
            return "EQUAL";

        case AST_BINARY_NOT_EQUAL:
            return "NOT_EQUAL";

        case AST_BINARY_LESS:
            return "LESS";

        case AST_BINARY_LESS_EQUAL:
            return "LESS_EQUAL";

        case AST_BINARY_GREATER:
            return "GREATER";

        case AST_BINARY_GREATER_EQUAL:
            return "GREATER_EQUAL";

        case AST_BINARY_LOGICAL_AND:
            return "LOGICAL_AND";

        case AST_BINARY_LOGICAL_OR:
            return "LOGICAL_OR";

        case AST_BINARY_BITWISE_OR:
            return "BITWISE_OR";

        case AST_BINARY_BITWISE_XOR:
            return "BITWISE_XOR";

        case AST_BINARY_BITWISE_AND:
            return "BITWISE_AND";

        case AST_BINARY_SHIFT_LEFT:
            return "SHIFT_LEFT";

        case AST_BINARY_SHIFT_RIGHT:
            return "SHIFT_RIGHT";
    }

    return "UNKNOWN";
}

static const char *ast_type_kind_name(ASTTypeKind kind)
{
    switch (kind) {
        case AST_TYPE_NUMBER: return "number";
        case AST_TYPE_DECIMAL: return "decimal";
        case AST_TYPE_BOOL: return "bool";
        case AST_TYPE_CHAR: return "char";
        case AST_TYPE_STRING: return "string";
        case AST_TYPE_VOID: return "void";
        case AST_TYPE_USER: return NULL;
    }

    return NULL;
}

static void ast_print_type(const ASTType *type)
{
    if (type->kind == AST_TYPE_USER) {
        printf("%.*s", (int)type->name_length, type->name);
    } else {
        printf("%s", ast_type_kind_name(type->kind));
    }

    for (size_t i = 0; i < type->dimension_count; i++) {
        if (type->dimensions[i].has_size) {
            printf("[%zu]", type->dimensions[i].size);
        } else {
            printf("[]");
        }
    }
}

void ast_print(const ASTNode *node, int indent)
{
    if (node == NULL) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);

    switch (node->kind) {
        case AST_NULL_LITERAL:
            printf("NULL\n");
            break;
        case AST_IMPORT:
            printf("IMPORT %.*s\n",
                (int)node->data.import_decl.path_length,
                node->data.import_decl.path);
            break;

        case AST_STRUCT:
            printf("STRUCT %.*s\n",
                (int)node->data.struct_decl.name_length,
                node->data.struct_decl.name);

            print_indent(indent + 1);
            printf("%*sFIELDS\n", indent + 1, "");

            for (size_t i = 0; i < node->data.struct_decl.field_count; i++) {
                ast_print(node->data.struct_decl.fields[i], indent + 3);
            }
            break;

        case AST_ENUM:
            printf("ENUM %.*s\n",
                (int)node->data.enum_decl.name_length,
                node->data.enum_decl.name);

            print_indent(indent + 1);
            printf("%*sMEMBERS\n", indent + 1, "");

            for (size_t i = 0; i < node->data.enum_decl.member_count; i++) {
                print_indent(indent + 3);
                printf("%.*s\n",
                    (int)node->data.enum_decl.member_lengths[i],
                    node->data.enum_decl.members[i]);
            }
            break;

        case AST_FUNCTION:
            printf("FUNCTION %.*s\n",
                (int)node->data.function_decl.name_length,
                node->data.function_decl.name);

            print_indent(indent + 1);
            printf("PARAMETERS\n");

            for (size_t i = 0; i < node->data.function_decl.parameter_count; i++) {
                ast_print(node->data.function_decl.parameters[i], indent + 2);
            }

            print_indent(indent + 1);
            printf("RETURN_TYPE ");
            ast_print_type(&node->data.function_decl.return_type);
            printf("\n");

            print_indent(indent + 1);
            printf("BODY\n");
            ast_print(node->data.function_decl.body, indent + 2);
            break;

        case AST_FOR:
            print_indent(indent);
            printf("FOR\n");

            print_indent(indent + 1);
            printf("INITIALIZER\n");
            ast_print(node->data.for_stmt.initializer, indent + 2);

            print_indent(indent + 1);
            printf("CONDITION\n");
            ast_print(node->data.for_stmt.condition, indent + 2);

            print_indent(indent + 1);
            printf("UPDATE\n");
            ast_print(node->data.for_stmt.assignment, indent + 2);

            print_indent(indent + 1);
            printf("BODY\n");
            ast_print(node->data.for_stmt.body, indent + 2);
            break;

        case AST_REPEAT:
            print_indent(indent);
            printf("REPEAT\n");

            print_indent(indent + 1);
            printf("COUNT\n");
            ast_print(node->data.repeat_stmt.count, indent + 2);

            print_indent(indent + 1);
            printf("BODY\n");
            ast_print(node->data.repeat_stmt.body, indent + 2);
            break;

        case AST_RETURN:
            printf("RETURN\n");

            if (node->data.return_stmt.expression != NULL) {
                ast_print(node->data.return_stmt.expression, indent + 1);
            }
            break;

        case AST_BREAK:
            printf("BREAK\n");
            break;

        case AST_CONTINUE:
            printf("CONTINUE\n");
            break;

        case AST_WHILE:
            printf("WHILE\n");

            printf("%*sCONDITION\n", (indent + 1) * 2, "");
            ast_print(node->data.while_stmt.condition, indent + 2);

            printf("%*sBODY\n", (indent + 1) * 2, "");
            ast_print(node->data.while_stmt.body, indent + 2);
            break;

        case AST_IF:
            printf("IF\n");

            printf("%*sCONDITION\n", (indent + 1) * 2, "");
            ast_print(node->data.if_stmt.condition, indent + 2);

            printf("%*sTHEN\n", (indent + 1) * 2, "");
            ast_print(node->data.if_stmt.then_branch, indent + 2);

            if (node->data.if_stmt.else_branch != NULL) {
                printf("%*sELSE\n", (indent + 1) * 2, "");
                ast_print(node->data.if_stmt.else_branch, indent + 2);
            }
            break;

        case AST_BLOCK:
            printf("BLOCK\n");

            for (size_t i = 0; i < node->data.block.statement_count; i++) {
                ast_print(node->data.block.statements[i], indent + 1);
            }
            break;

        case AST_PRINT:
            printf("PRINT\n");
            ast_print(node->data.print.expression, indent + 1);
            break;

        case AST_EXPR_STMT:
            printf("EXPR_STMT\n");
            ast_print(node->data.expr_stmt.expression, indent + 1);
            break;

        case AST_NUMBER_LITERAL:
            printf("NUMBER %lld\n", node->data.number);
            break;

        case AST_DECIMAL_LITERAL:
            printf("DECIMAL %g\n", node->data.decimal);
            break;

        case AST_STRING_LITERAL:
            printf("STRING \"");
            fwrite(
                node->data.string.start,
                1,
                node->data.string.length,
                stdout
            );
            printf("\"\n");
            break;

        case AST_CHAR_LITERAL:
            printf("CHAR '%c'\n", node->data.character);
            break;

        case AST_BOOL_LITERAL:
            printf("BOOL %s\n",
                   node->data.boolean ? "true" : "false");
            break;

        case AST_IDENTIFIER:
            printf("IDENTIFIER ");
            fwrite(
                node->data.identifier.start,
                1,
                node->data.identifier.length,
                stdout
            );
            printf("\n");
            break;

        case AST_UNARY:
            printf("UNARY %s\n",
                   unary_operator_name(node->data.unary.operator));

            ast_print(node->data.unary.operand, indent + 1);
            break;

        case AST_BINARY:
            printf("BINARY %s\n",
                   binary_operator_name(node->data.binary.operator));

            ast_print(node->data.binary.left, indent + 1);
            ast_print(node->data.binary.right, indent + 1);
            break;

        case AST_CALL:
            printf("CALL\n");

            print_indent(indent + 1);
            printf("CALLEE\n");
            ast_print(node->data.call.callee, indent + 2);

            for (size_t i = 0;
                i < node->data.call.argument_count;
                i++) {
                print_indent(indent + 1);
                printf("ARGUMENT\n");
                ast_print(
                    node->data.call.arguments[i],
                    indent + 2
                );
            }
            break;

        case AST_MEMBER_ACCESS:
            printf("MEMBER_ACCESS ");

            fwrite(
                node->data.member_access.member,
                1,
                node->data.member_access.member_length,
                stdout
            );

            printf("\n");

            ast_print(
                node->data.member_access.object,
                indent + 1
            );
            break;

        case AST_INDEX:
            printf("INDEX\n");

            print_indent(indent + 1);
            printf("ARRAY\n");
            ast_print(
                node->data.index.array,
                indent + 2
            );

            print_indent(indent + 1);
            printf("INDEX_EXPRESSION\n");
            ast_print(
                node->data.index.index,
                indent + 2
            );
            break;

        case AST_VAR_DECL:
            printf("VAR_DECL ");
            ast_print_type(&node->data.var_decl.type);

            printf(" %.*s%s\n",
                (int)node->data.var_decl.name_length,
                node->data.var_decl.name,
                node->data.var_decl.is_const ? " const" : "");

            if (node->data.var_decl.initializer != NULL) {
                ast_print(node->data.var_decl.initializer, indent + 1);
            }
            break;

        case AST_ASSIGNMENT:
            printf("ASSIGNMENT\n");

            printf("%*sTARGET\n", (indent + 1) * 2, "");
            ast_print(node->data.assignment.target, indent + 2);

            printf("%*sVALUE\n", (indent + 1) * 2, "");
            ast_print(node->data.assignment.value, indent + 2);
            break;

        case AST_PROGRAM:
            printf("PROGRAM\n");

            for (size_t i = 0; i < node->data.program.statement_count; i++) {
                ast_print(node->data.program.statements[i], indent + 1);
            }
            break;

        default:
            printf("UNKNOWN NODE\n");
            break;
    }
}

ASTNode *ast_new_call(
    ASTNode *callee,
    ASTNode **arguments,
    size_t argument_count,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_CALL, line, column);

    node->data.call.callee = callee;
    node->data.call.arguments = arguments;
    node->data.call.argument_count = argument_count;

    return node;
}

ASTNode *ast_new_member_access(
    ASTNode *object,
    const char *member,
    size_t member_length,
    size_t line,
    size_t column)
{
    ASTNode *node =
        ast_alloc(AST_MEMBER_ACCESS, line, column);

    node->data.member_access.object = object;
    node->data.member_access.member = member;
    node->data.member_access.member_length = member_length;

    return node;
}

ASTNode *ast_new_index(
    ASTNode *array,
    ASTNode *index,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_INDEX, line, column);

    node->data.index.array = array;
    node->data.index.index = index;

    return node;
}

ASTNode *ast_new_var_decl(
    int is_const,
    const char *name,
    size_t name_length,
    ASTType type,
    ASTNode *initializer,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_VAR_DECL, line, column);

    node->kind = AST_VAR_DECL;
    node->line = line;
    node->column = column;

    node->data.var_decl.is_const = is_const;
    node->data.var_decl.name = name;
    node->data.var_decl.name_length = name_length;
    node->data.var_decl.type = type;
    node->data.var_decl.initializer = initializer;

    return node;
}

ASTNode *ast_new_program(ASTNode **statements, size_t statement_count)
{
    ASTNode *node = ast_alloc(AST_PROGRAM, 1, 1);

    node->kind = AST_PROGRAM;
    node->line = 1;
    node->column = 1;

    node->data.program.statements = statements;
    node->data.program.statement_count = statement_count;

    return node;
}

ASTNode *ast_new_assignment(
    ASTNode *target,
    ASTNode *value,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_ASSIGNMENT, line, column);

    node->kind = AST_ASSIGNMENT;
    node->line = line;
    node->column = column;

    node->data.assignment.target = target;
    node->data.assignment.value = value;

    return node;
}

ASTNode *ast_new_expr_stmt(
    ASTNode *expression,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_EXPR_STMT, line, column);

    node->kind = AST_EXPR_STMT;
    node->line = line;
    node->column = column;
    node->data.expr_stmt.expression = expression;

    return node;
}

ASTNode *ast_new_print(
    ASTNode *expression,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_PRINT, line, column);

    node->kind = AST_PRINT;
    node->line = line;
    node->column = column;
    node->data.print.expression = expression;

    return node;
}

ASTNode *ast_new_block(
    ASTNode **statements,
    size_t statement_count,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_BLOCK, line, column);

    node->kind = AST_BLOCK;
    node->line = line;
    node->column = column;
    node->data.block.statements = statements;
    node->data.block.statement_count = statement_count;

    return node;
}

ASTNode *ast_new_if(
    ASTNode *condition,
    ASTNode *then_branch,
    ASTNode *else_branch,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_IF, line, column);

    node->kind = AST_IF;
    node->line = line;
    node->column = column;

    node->data.if_stmt.condition = condition;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;

    return node;
}

ASTNode *ast_new_while(ASTNode *condition, ASTNode *body, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_WHILE, line, column);

    node->kind = AST_WHILE;
    node->line = line;
    node->column = column;

    node->data.while_stmt.condition = condition;
    node->data.while_stmt.body = body;

    return node;
}

ASTNode *ast_new_for(
    ASTNode *initializer,
    ASTNode *condition,
    ASTNode *assignment,
    ASTNode *body,
    size_t line,
    size_t column)
{
    ASTNode *node = ast_alloc(AST_FOR, line, column);

    node->kind = AST_FOR;
    node->line = line;
    node->column = column;

    node->data.for_stmt.initializer = initializer;
    node->data.for_stmt.condition = condition;
    node->data.for_stmt.assignment = assignment;
    node->data.for_stmt.body = body;

    return node;
}

ASTNode *ast_new_repeat(ASTNode *count, ASTNode *body, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_REPEAT, line, column);

    node->kind = AST_REPEAT;
    node->line = line;
    node->column = column;

    node->data.repeat_stmt.count = count;
    node->data.repeat_stmt.body = body;

    return node;
}

ASTNode *ast_new_break(size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_BREAK, line, column);

    node->kind = AST_BREAK;
    node->line = line;
    node->column = column;

    return node;
}

ASTNode *ast_new_continue(size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_CONTINUE, line, column);

    node->kind = AST_CONTINUE;
    node->line = line;
    node->column = column;

    return node;
}

ASTNode *ast_new_function(
    const char *name,
    size_t name_length,
    ASTNode **parameters,
    size_t parameter_count,
    ASTType return_type,
    ASTNode *body,
    size_t line,
    size_t column
)
{
    ASTNode *node = malloc(sizeof(ASTNode));

    if (node == NULL) {
        return NULL;
    }

    node->kind = AST_FUNCTION;
    node->line = line;
    node->column = column;

    node->data.function_decl.name = name;
    node->data.function_decl.name_length = name_length;
    node->data.function_decl.parameters = parameters;
    node->data.function_decl.parameter_count = parameter_count;
    node->data.function_decl.return_type = return_type;
    node->data.function_decl.body = body;

    return node;
}

ASTNode *ast_new_return(ASTNode *expression, size_t line, size_t column)
{
    ASTNode *node = ast_alloc(AST_RETURN, line, column);

    node->kind = AST_RETURN;
    node->line = line;
    node->column = column;
    node->data.return_stmt.expression = expression;

    return node;
}

ASTNode *ast_new_struct(
    const char *name,
    size_t name_length,
    ASTNode **fields,
    size_t field_count,
    size_t line,
    size_t column)
{
    ASTNode *node = malloc(sizeof(ASTNode));

    if (node == NULL) {
        return NULL;
    }

    node->kind = AST_STRUCT;
    node->line = line;
    node->column = column;

    node->data.struct_decl.name = name;
    node->data.struct_decl.name_length = name_length;
    node->data.struct_decl.fields = fields;
    node->data.struct_decl.field_count = field_count;

    return node;
}

ASTNode *ast_new_enum(
    const char *name,
    size_t name_length,
    const char **members,
    size_t *member_lengths,
    size_t member_count,
    size_t line,
    size_t column)
{
    ASTNode *node = malloc(sizeof(ASTNode));

    if (node == NULL) {
        return NULL;
    }

    node->kind = AST_ENUM;
    node->line = line;
    node->column = column;

    node->data.enum_decl.name = name;
    node->data.enum_decl.name_length = name_length;
    node->data.enum_decl.members = members;
    node->data.enum_decl.member_lengths = member_lengths;
    node->data.enum_decl.member_count = member_count;

    return node;
}