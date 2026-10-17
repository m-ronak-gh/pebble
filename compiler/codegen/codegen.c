#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void codegen_free_variables(Codegen *codegen);

static void codegen_variable_declaration(
    Codegen *codegen,
    ASTNode *node
);

static void codegen_if(
    Codegen *codegen,
    ASTNode *statement
);

static void codegen_assignment(
    Codegen *codegen,
    ASTNode *node
);

static void codegen_while(
    Codegen *codegen,
    ASTNode *node
);

static void codegen_repeat(Codegen *codegen, ASTNode *node);
static void codegen_for(Codegen *codegen, ASTNode *node);

static void codegen_return(
    Codegen *codegen,
    ASTNode *node
);

static void codegen_block(
    Codegen *codegen,
    ASTNode *block
);

static void codegen_print(
    Codegen *codegen,
    ASTNode *node
);

static void codegen_break(Codegen *codegen);
static void codegen_continue(Codegen *codegen);

static LLVMTypeRef llvm_type_from_semantic(
    Codegen *codegen,
    SemanticType type
);

static LLVMTypeRef llvm_type_from_ast(
    Codegen *codegen,
    ASTType type
);

static CodegenVariable *codegen_find_variable(
    Codegen *codegen,
    const char *name,
    size_t length
);

static int codegen_variable_exists(
    Codegen *codegen,
    const char *name,
    size_t length
);

static LLVMValueRef codegen_declare_printf(
    Codegen *codegen
);

static LLVMTypeRef codegen_printf_type(
    Codegen *codegen
);

// Creation / destruction
Codegen *codegen_create(
    const char *module_name,
    const SemanticContext *semantic
)
{
    Codegen *codegen =
        malloc(sizeof(*codegen));

    if (codegen == NULL) {
        fprintf(
            stderr,
            "fatal: failed to allocate codegen\n"
        );
        return NULL;
    }

    codegen->context =
        LLVMContextCreate();

    if (codegen->context == NULL) {
        free(codegen);
        return NULL;
    }

    codegen->module =
        LLVMModuleCreateWithNameInContext(
            module_name,
            codegen->context
        );

    codegen->builder =
        LLVMCreateBuilderInContext(
            codegen->context
        );

    codegen->semantic = semantic;
    codegen->current_function = NULL;
    codegen->variables = NULL;
    codegen->current_loop = NULL;

    return codegen;
}

void codegen_free(Codegen *codegen)
{
    if (codegen == NULL) {
        return;
    }

    codegen_free_variables(codegen);

    LLVMDisposeBuilder(codegen->builder);
    LLVMDisposeModule(codegen->module);
    LLVMContextDispose(codegen->context);

    free(codegen);
}

void codegen_dump(const Codegen *codegen)
{
    char *ir =
        LLVMPrintModuleToString(
            codegen->module
        );

    if (ir == NULL) {
        return;
    }

    printf("%s", ir);

    LLVMDisposeMessage(ir);
}

LLVMModuleRef codegen_module(
    const Codegen *codegen
)
{
    return codegen->module;
}

int codegen_write_ir(
    const Codegen *codegen,
    const char *path
)
{
    char *error = NULL;

    if (LLVMPrintModuleToFile(
            codegen->module,
            path,
            &error
        ) != 0) {

        fprintf(
            stderr,
            "codegen: failed to write LLVM IR: %s\n",
            error != NULL
                ? error
                : "unknown error"
        );

        if (error != NULL) {
            LLVMDisposeMessage(error);
        }

        return 0;
    }

    return 1;
}

// LLVM primitive types
static LLVMTypeRef llvm_number_type(
    Codegen *codegen
)
{
    return LLVMInt64TypeInContext(
        codegen->context
    );
}

static LLVMTypeRef llvm_decimal_type(
    Codegen *codegen
)
{
    return LLVMDoubleTypeInContext(
        codegen->context
    );
}

static LLVMTypeRef llvm_bool_type(
    Codegen *codegen
)
{
    return LLVMInt1TypeInContext(
        codegen->context
    );
}

static LLVMTypeRef llvm_char_type(
    Codegen *codegen
)
{
    return LLVMInt8TypeInContext(
        codegen->context
    );
}

// Semantic type -> LLVM type
static LLVMTypeRef llvm_type_from_semantic(
    Codegen *codegen,
    SemanticType type
)
{
    switch (type.kind) {

        case SEM_TYPE_NUMBER:
            return llvm_number_type(codegen);

        case SEM_TYPE_DECIMAL:
            return llvm_decimal_type(codegen);

        case SEM_TYPE_BOOL:
            return llvm_bool_type(codegen);

        case SEM_TYPE_CHAR:
            return llvm_char_type(codegen);

        case SEM_TYPE_VOID:
            return LLVMVoidTypeInContext(
                codegen->context
            );

        default:
            fprintf(
                stderr,
                "codegen: unsupported semantic type %d\n",
                type.kind
            );
            return NULL;
    }
}

static LLVMTypeRef llvm_type_from_ast(
    Codegen *codegen,
    ASTType type
)
{
    if (type.dimension_count != 0) {
        fprintf(
            stderr,
            "codegen: arrays are not supported yet\n"
        );
        return NULL;
    }

    switch (type.kind) {

        case AST_TYPE_NUMBER:
            return llvm_number_type(codegen);

        case AST_TYPE_DECIMAL:
            return llvm_decimal_type(codegen);

        case AST_TYPE_BOOL:
            return llvm_bool_type(codegen);

        case AST_TYPE_CHAR:
            return llvm_char_type(codegen);

        case AST_TYPE_VOID:
            return LLVMVoidTypeInContext(
                codegen->context
            );

        case AST_TYPE_STRING:
        case AST_TYPE_USER:
            fprintf(
                stderr,
                "codegen: unsupported declaration type kind %d\n",
                type.kind
            );
            return NULL;
    }

    fprintf(
        stderr,
        "codegen: unknown AST type kind %d\n",
        type.kind
    );

    return NULL;
}

// Variables
static CodegenVariable *codegen_find_variable(
    Codegen *codegen,
    const char *name,
    size_t length
)
{
    CodegenVariable *variable =
        codegen->variables;

    while (variable != NULL) {

        if (strlen(variable->name) == length &&
            strncmp(
                variable->name,
                name,
                length
            ) == 0) {

            return variable;
        }

        variable = variable->next;
    }

    return NULL;
}

static int codegen_variable_exists(
    Codegen *codegen,
    const char *name,
    size_t length
)
{
    return codegen_find_variable(
        codegen,
        name,
        length
    ) != NULL;
}

static void codegen_free_variables(
    Codegen *codegen
)
{
    CodegenVariable *variable =
        codegen->variables;

    while (variable != NULL) {

        CodegenVariable *next =
            variable->next;

        free(variable->name);
        free(variable);

        variable = next;
    }

    codegen->variables = NULL;
}

// Expressions
LLVMValueRef codegen_expression(
    Codegen *codegen,
    ASTNode *expression
)
{
    if (expression == NULL) {
        return NULL;
    }

    switch (expression->kind) {

        case AST_NUMBER_LITERAL:
            return LLVMConstInt(
                llvm_number_type(codegen),
                expression->data.number,
                1
            );

        case AST_DECIMAL_LITERAL:
            return LLVMConstReal(
                llvm_decimal_type(codegen),
                expression->data.decimal
            );

        case AST_BOOL_LITERAL:
            return LLVMConstInt(
                llvm_bool_type(codegen),
                expression->data.boolean ? 1 : 0,
                0
            );

        case AST_CHAR_LITERAL:
            return LLVMConstInt(
                llvm_char_type(codegen),
                (unsigned char)
                    expression->data.character,
                0
            );

        case AST_IDENTIFIER: {
            CodegenVariable *variable =
                codegen_find_variable(
                    codegen,
                    expression->data.identifier.start,
                    expression->data.identifier.length
                );

            if (variable == NULL) {
                fprintf(
                    stderr,
                    "codegen: undefined variable '%.*s'\n",
                    (int)expression->data.identifier.length,
                    expression->data.identifier.start
                );

                return NULL;
            }

            LLVMTypeRef type =
                llvm_type_from_semantic(
                    codegen,
                    variable->type
                );

            if (type == NULL) {
                return NULL;
            }

            return LLVMBuildLoad2(
                codegen->builder,
                type,
                variable->alloca,
                "loadtmp"
            );
        }

        case AST_UNARY: {
            LLVMValueRef operand =
                codegen_expression(
                    codegen,
                    expression->data.unary.operand
                );

            if (operand == NULL) {
                return NULL;
            }

            switch (
                expression->data.unary.operator
            ) {

                case AST_UNARY_NEGATE:
                    return LLVMBuildNeg(
                        codegen->builder,
                        operand,
                        "negtmp"
                    );

                case AST_UNARY_NOT:
                    return LLVMBuildXor(
                        codegen->builder,
                        operand,
                        LLVMConstInt(
                            llvm_bool_type(codegen),
                            1,
                            0
                        ),
                        "nottmp"
                    );

                case AST_UNARY_BITWISE_NOT:
                    return LLVMBuildNot(
                        codegen->builder,
                        operand,
                        "bitwisenottmp"
                    );
            }

            fprintf(
                stderr,
                "codegen: unsupported unary operator\n"
            );

            return NULL;
        }

        case AST_BINARY: {
            LLVMValueRef left =
                codegen_expression(
                    codegen,
                    expression->data.binary.left
                );

            LLVMValueRef right =
                codegen_expression(
                    codegen,
                    expression->data.binary.right
                );

            if (left == NULL || right == NULL) {
                return NULL;
            }

            SemanticType left_type;
            int has_type =
                codegen->semantic != NULL &&
                semantic_type_of(
                    codegen->semantic,
                    expression->data.binary.left,
                    &left_type
                );

            if (!has_type) {
                fprintf(
                    stderr,
                    "codegen: missing semantic type for binary expression\n"
                );
                return NULL;
            }

            switch (expression->data.binary.operator) {

                case AST_BINARY_ADD:
                    if (left_type.kind ==
                        SEM_TYPE_DECIMAL) {

                        return LLVMBuildFAdd(
                            codegen->builder,
                            left,
                            right,
                            "faddtmp"
                        );
                    }

                    return LLVMBuildAdd(
                        codegen->builder,
                        left,
                        right,
                        "addtmp"
                    );

                case AST_BINARY_SUBTRACT:
                    if (left_type.kind ==
                        SEM_TYPE_DECIMAL) {

                        return LLVMBuildFSub(
                            codegen->builder,
                            left,
                            right,
                            "fsubtmp"
                        );
                    }

                    return LLVMBuildSub(
                        codegen->builder,
                        left,
                        right,
                        "subtmp"
                    );

                case AST_BINARY_MULTIPLY:
                    if (left_type.kind ==
                        SEM_TYPE_DECIMAL) {

                        return LLVMBuildFMul(
                            codegen->builder,
                            left,
                            right,
                            "fmultmp"
                        );
                    }

                    return LLVMBuildMul(
                        codegen->builder,
                        left,
                        right,
                        "multmp"
                    );

                case AST_BINARY_DIVIDE:
                    if (left_type.kind ==
                        SEM_TYPE_DECIMAL) {

                        return LLVMBuildFDiv(
                            codegen->builder,
                            left,
                            right,
                            "fdivtmp"
                        );
                    }

                    return LLVMBuildSDiv(
                        codegen->builder,
                        left,
                        right,
                        "divtmp"
                    );

                case AST_BINARY_MODULO:
                    if (left_type.kind ==
                        SEM_TYPE_DECIMAL) {

                        return LLVMBuildFRem(
                            codegen->builder,
                            left,
                            right,
                            "fmodtmp"
                        );
                    }

                    return LLVMBuildSRem(
                        codegen->builder,
                        left,
                        right,
                        "modtmp"
                    );
                case AST_BINARY_EQUAL:
                case AST_BINARY_NOT_EQUAL:
                case AST_BINARY_LESS:
                case AST_BINARY_LESS_EQUAL:
                case AST_BINARY_GREATER:
                case AST_BINARY_GREATER_EQUAL: {
                    int is_decimal =
                        left_type.kind == SEM_TYPE_DECIMAL;

                    LLVMRealPredicate real_predicate;

                    switch (
                        expression->data.binary.operator
                    ) {
                        case AST_BINARY_EQUAL:
                            real_predicate = LLVMRealOEQ;
                            break;

                        case AST_BINARY_NOT_EQUAL:
                            real_predicate = LLVMRealUNE;
                            break;

                        case AST_BINARY_LESS:
                            real_predicate = LLVMRealOLT;
                            break;

                        case AST_BINARY_LESS_EQUAL:
                            real_predicate = LLVMRealOLE;
                            break;

                        case AST_BINARY_GREATER:
                            real_predicate = LLVMRealOGT;
                            break;

                        default:
                            real_predicate = LLVMRealOGE;
                            break;
                    }

                    if (is_decimal) {
                        return LLVMBuildFCmp(
                            codegen->builder,
                            real_predicate,
                            left,
                            right,
                            "fcmp"
                        );
                    }

                    LLVMIntPredicate int_predicate;

                    switch (
                        expression->data.binary.operator
                    ) {
                        case AST_BINARY_EQUAL:
                            int_predicate = LLVMIntEQ;
                            break;

                        case AST_BINARY_NOT_EQUAL:
                            int_predicate = LLVMIntNE;
                            break;

                        case AST_BINARY_LESS:
                            int_predicate = LLVMIntSLT;
                            break;

                        case AST_BINARY_LESS_EQUAL:
                            int_predicate = LLVMIntSLE;
                            break;

                        case AST_BINARY_GREATER:
                            int_predicate = LLVMIntSGT;
                            break;

                        default:
                            int_predicate = LLVMIntSGE;
                            break;
                    }

                    return LLVMBuildICmp(
                        codegen->builder,
                        int_predicate,
                        left,
                        right,
                        "icmp"
                    );
                }

                default:
                    fprintf(
                        stderr,
                        "codegen: unsupported binary operator\n"
                    );
                    return NULL;
            }
        }

        default:
            fprintf(
                stderr,
                "codegen: unsupported expression\n"
            );

            return NULL;
    }
}

// Variable declarations
static void codegen_variable_declaration(
    Codegen *codegen,
    ASTNode *node
)
{
    const char *name =
        node->data.var_decl.name;

    size_t name_length =
        node->data.var_decl.name_length;

    if (codegen_variable_exists(
            codegen,
            name,
            name_length
        )) {

        fprintf(
            stderr,
            "codegen: duplicate variable '%.*s'\n",
            (int)name_length,
            name
        );

        return;
    }

    ASTType ast_type =
        node->data.var_decl.type;

    LLVMTypeRef llvm_type =
        llvm_type_from_ast(
            codegen,
            ast_type
        );

    if (llvm_type == NULL) {
        return;
    }

    if (ast_type.kind == AST_TYPE_VOID) {
        fprintf(
            stderr,
            "codegen: variable cannot have type void\n"
        );
        return;
    }

    LLVMValueRef initializer =
        codegen_expression(
            codegen,
            node->data.var_decl.initializer
        );

    if (initializer == NULL) {
        return;
    }

    char *variable_name =
        malloc(name_length + 1);

    if (variable_name == NULL) {
        fprintf(
            stderr,
            "fatal: failed to allocate variable name\n"
        );
        return;
    }

    memcpy(
        variable_name,
        name,
        name_length
    );

    variable_name[name_length] = '\0';

    LLVMValueRef alloca =
        LLVMBuildAlloca(
            codegen->builder,
            llvm_type,
            variable_name
        );

    LLVMBuildStore(
        codegen->builder,
        initializer,
        alloca
    );

    CodegenVariable *variable =
        malloc(sizeof(*variable));

    if (variable == NULL) {
        free(variable_name);

        fprintf(
            stderr,
            "fatal: failed to allocate codegen variable\n"
        );

        return;
    }

    memset(
        &variable->type,
        0,
        sizeof(variable->type)
    );

    if (codegen->semantic != NULL &&
        !semantic_type_of(
            codegen->semantic,
            node->data.var_decl.initializer,
            &variable->type
        )) {

        fprintf(
            stderr,
            "codegen: missing semantic type for initializer\n"
        );

        free(variable_name);
        free(variable);
        return;
    }

    variable->name = variable_name;
    variable->alloca = alloca;
    variable->next = codegen->variables;

    codegen->variables = variable;
}

static void codegen_assignment(
    Codegen *codegen,
    ASTNode *node
)
{
    ASTNode *target = node->data.assignment.target;
    ASTNode *value_node = node->data.assignment.value;

    if (target == NULL || target->kind != AST_IDENTIFIER) {
        fprintf(
            stderr,
            "codegen: only identifier assignments are supported currently\n"
        );
        return;
    }

    CodegenVariable *variable = codegen_find_variable(
        codegen,
        target->data.identifier.start,
        target->data.identifier.length
    );

    if (variable == NULL) {
        fprintf(
            stderr,
            "codegen: unknown variable in assignment '%.*s'\n",
            (int)target->data.identifier.length,
            target->data.identifier.start
        );
        return;
    }

    LLVMValueRef value = codegen_expression(codegen, value_node);

    if (value == NULL) {
        return;
    }

    LLVMTypeRef expected_type = llvm_type_from_semantic(
        codegen,
        variable->type
    );

    if (expected_type == NULL ||
        LLVMTypeOf(value) != expected_type) {
        fprintf(
            stderr,
            "codegen: assignment type mismatch for '%.*s'\n",
            (int)target->data.identifier.length,
            target->data.identifier.start
        );
        return;
    }

    LLVMBuildStore(codegen->builder, value, variable->alloca);
}

// printf
static LLVMTypeRef codegen_printf_type(
    Codegen *codegen
)
{
    LLVMTypeRef char_pointer_type =
        LLVMPointerType(
            LLVMInt8TypeInContext(
                codegen->context
            ),
            0
        );

    return LLVMFunctionType(
        LLVMInt32TypeInContext(
            codegen->context
        ),
        &char_pointer_type,
        1,
        1
    );
}

static LLVMValueRef codegen_declare_printf(
    Codegen *codegen
)
{
    LLVMValueRef existing =
        LLVMGetNamedFunction(
            codegen->module,
            "printf"
        );

    if (existing != NULL) {
        return existing;
    }

    LLVMTypeRef type =
        codegen_printf_type(codegen);

    return LLVMAddFunction(
        codegen->module,
        "printf",
        type
    );
}

static void codegen_print(
    Codegen *codegen,
    ASTNode *node
)
{
    ASTNode *expression =
        node->data.print.expression;

    LLVMValueRef value =
        codegen_expression(
            codegen,
            expression
        );

    if (value == NULL) {
        return;
    }

    SemanticType semantic_type;

    if (codegen->semantic == NULL ||
        !semantic_type_of(
            codegen->semantic,
            expression,
            &semantic_type
        )) {

        fprintf(
            stderr,
            "codegen: missing semantic type for print expression\n"
        );

        return;
    }

    LLVMValueRef printf_function =
        codegen_declare_printf(codegen);

    LLVMTypeRef printf_type =
        codegen_printf_type(codegen);

    const char *format_text = NULL;

    switch (semantic_type.kind) {

        case SEM_TYPE_NUMBER:
            format_text = "%lld\n";
            break;

        case SEM_TYPE_BOOL:
            format_text = "%lld\n";
            break;

        case SEM_TYPE_CHAR:
            format_text = "%c\n";
            break;

        case SEM_TYPE_DECIMAL:
            format_text = "%f\n";
            break;

        default:
            fprintf(
                stderr,
                "codegen: print type is not supported yet\n"
            );
            return;
    }

    LLVMValueRef format =
        LLVMBuildGlobalStringPtr(
            codegen->builder,
            format_text,
            "fmt"
        );

    LLVMValueRef args[] = {
        format,
        value
    };

    LLVMBuildCall2(
        codegen->builder,
        printf_type,
        printf_function,
        args,
        2,
        ""
    );
}


// Return
static void codegen_return(
    Codegen *codegen,
    ASTNode *node
)
{
    if (node->data.return_stmt.expression == NULL) {

        fprintf(
            stderr,
            "codegen: return without value is not supported yet\n"
        );

        return;
    }

    LLVMValueRef value =
        codegen_expression(
            codegen,
            node->data.return_stmt.expression
        );

    if (value == NULL) {
        return;
    }

    LLVMBuildRet(
        codegen->builder,
        value
    );
}


// Statements / blocks

static void codegen_statement(
    Codegen *codegen,
    ASTNode *statement
)
{
    switch (statement->kind) {

        case AST_VAR_DECL:
            codegen_variable_declaration(
                codegen,
                statement
            );
            break;

        case AST_REPEAT:
            codegen_repeat(codegen, statement);
            break;

        case AST_FOR:
            codegen_for(codegen, statement);
            break;

        case AST_ASSIGNMENT:
            codegen_assignment(codegen, statement);
            break;

        case AST_PRINT:
            codegen_print(
                codegen,
                statement
            );
            break;

        case AST_RETURN:
            codegen_return(
                codegen,
                statement
            );
            break;

        case AST_BREAK:
            codegen_break(codegen);
            break;

        case AST_CONTINUE:
            codegen_continue(codegen);
            break;

        case AST_BLOCK:
            codegen_block(codegen, statement);
            break;

        case AST_EXPR_STMT:
            (void)codegen_expression(
                codegen,
                statement->data.expr_stmt.expression
            );
            break;

        case AST_IF:
            codegen_if(
                codegen,
                statement
            );
            break;
        
        case AST_WHILE:
            codegen_while(codegen, statement);
            break;

        default:
            fprintf(
                stderr,
                "codegen: unsupported statement kind %d\n",
                statement->kind
            );
            break;
    }
}

static void codegen_block(
    Codegen *codegen,
    ASTNode *block
)
{
    if (block == NULL ||
        block->kind != AST_BLOCK) {

        fprintf(
            stderr,
            "codegen: expected block\n"
        );

        return;
    }

    for (size_t i = 0;
         i < block->data.block.statement_count;
         ++i) {

        ASTNode *statement =
            block->data.block.statements[i];

        if (statement == NULL) {
            continue;
        }
        
        LLVMBasicBlockRef current_block =
            LLVMGetInsertBlock(codegen->builder);

        if (current_block != NULL &&
            LLVMGetBasicBlockTerminator(current_block) != NULL) {
            break;
        }

        codegen_statement(
            codegen,
            statement
        );
    }
}


// Functions
static LLVMTypeRef codegen_return_type(
    Codegen *codegen,
    ASTNode *function
)
{
    ASTType type =
        function->data.function_decl.return_type;

    return llvm_type_from_ast(
        codegen,
        type
    );
}

void codegen_function(
    Codegen *codegen,
    ASTNode *function
)
{
    LLVMTypeRef return_type =
        codegen_return_type(
            codegen,
            function
        );

    if (return_type == NULL) {
        return;
    }

    if (function->data.function_decl.parameter_count != 0) {

        fprintf(
            stderr,
            "codegen: function parameters not supported yet\n"
        );

        return;
    }

    LLVMTypeRef function_type =
        LLVMFunctionType(
            return_type,
            NULL,
            0,
            0
        );

    char *function_name =
        malloc(
            function->data.function_decl.name_length + 1
        );

    if (function_name == NULL) {
        fprintf(
            stderr,
            "fatal: failed to allocate function name\n"
        );
        return;
    }

    memcpy(
        function_name,
        function->data.function_decl.name,
        function->data.function_decl.name_length
    );

    function_name[
        function->data.function_decl.name_length
    ] = '\0';

    LLVMValueRef llvm_function =
        LLVMAddFunction(
            codegen->module,
            function_name,
            function_type
        );

    free(function_name);

    codegen->current_function =
        llvm_function;

    codegen_free_variables(codegen);

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(
            codegen->context,
            llvm_function,
            "entry"
        );

    LLVMPositionBuilderAtEnd(
        codegen->builder,
        entry
    );

    codegen_block(
        codegen,
        function->data.function_decl.body
    );
}

static void codegen_if(
    Codegen *codegen,
    ASTNode *statement
)
{
    LLVMValueRef condition =
        codegen_expression(
            codegen,
            statement->data.if_stmt.condition
        );

    if (condition == NULL ||
        codegen->current_function == NULL) {
        fprintf(stderr, "codegen: failed to generate if condition\n");
        return;
    }

    LLVMBasicBlockRef then_block =
        LLVMAppendBasicBlockInContext(
            codegen->context,
            codegen->current_function,
            "if.then"
        );

    LLVMBasicBlockRef merge_block =
        LLVMAppendBasicBlockInContext(
            codegen->context,
            codegen->current_function,
            "if.end"
        );

    ASTNode *else_statement =
        statement->data.if_stmt.else_branch;

    LLVMBasicBlockRef else_block =
        else_statement != NULL
            ? LLVMAppendBasicBlockInContext(
                  codegen->context,
                  codegen->current_function,
                  "if.else"
              )
            : merge_block;

    LLVMBuildCondBr(
        codegen->builder,
        condition,
        then_block,
        else_block
    );

    LLVMPositionBuilderAtEnd(
        codegen->builder,
        then_block
    );

    codegen_statement(
        codegen,
        statement->data.if_stmt.then_branch
    );

    LLVMBasicBlockRef then_end =
        LLVMGetInsertBlock(codegen->builder);

    int then_terminated =
        LLVMGetBasicBlockTerminator(then_end) != NULL;

    if (!then_terminated) {
        LLVMBuildBr(
            codegen->builder,
            merge_block
        );
    }

    int else_terminated = 0;

    if (else_statement != NULL) {
        LLVMPositionBuilderAtEnd(
            codegen->builder,
            else_block
        );

        codegen_statement(
            codegen,
            else_statement
        );

        LLVMBasicBlockRef else_end =
            LLVMGetInsertBlock(codegen->builder);

        else_terminated =
            LLVMGetBasicBlockTerminator(else_end) != NULL;

        if (!else_terminated) {
            LLVMBuildBr(
                codegen->builder,
                merge_block
            );
        }
    }

    /*
     * With an else branch, if both sides terminate, the merge has
     * no incoming path. Terminate it explicitly to keep IR valid.
     */
    if (else_statement != NULL &&
        then_terminated &&
        else_terminated) {

        LLVMPositionBuilderAtEnd(
            codegen->builder,
            merge_block
        );

        LLVMBuildUnreachable(
            codegen->builder
        );

        return;
    }

    LLVMPositionBuilderAtEnd(
        codegen->builder,
        merge_block
    );
}

static void codegen_while(
    Codegen *codegen,
    ASTNode *node
)
{
    if (node == NULL ||
        node->kind != AST_WHILE ||
        codegen->current_function == NULL) {
        return;
    }

    LLVMBasicBlockRef condition_block =
        LLVMAppendBasicBlockInContext(
            codegen->context,
            codegen->current_function,
            "while.cond"
        );

    LLVMBasicBlockRef body_block =
        LLVMAppendBasicBlockInContext(
            codegen->context,
            codegen->current_function,
            "while.body"
        );

    LLVMBasicBlockRef exit_block =
        LLVMAppendBasicBlockInContext(
            codegen->context,
            codegen->current_function,
            "while.end"
        );

    CodegenLoop loop = {
        .break_target = exit_block,
        .continue_target = condition_block,
        .parent = codegen->current_loop
    };

    codegen->current_loop = &loop;

    LLVMBuildBr(
        codegen->builder,
        condition_block
    );

    LLVMPositionBuilderAtEnd(
        codegen->builder,
        condition_block
    );

    LLVMValueRef condition =
        codegen_expression(
            codegen,
            node->data.while_stmt.condition
        );

    if (condition == NULL ||
        LLVMTypeOf(condition) !=
            LLVMInt1TypeInContext(codegen->context)) {

        fprintf(
            stderr,
            "codegen: while condition must be bool\n"
        );

        LLVMBuildBr(
            codegen->builder,
            exit_block
        );

        LLVMPositionBuilderAtEnd(
            codegen->builder,
            exit_block
        );

        return;
    }

    LLVMBuildCondBr(
        codegen->builder,
        condition,
        body_block,
        exit_block
    );

    LLVMPositionBuilderAtEnd(
        codegen->builder,
        body_block
    );

    codegen_statement(
        codegen,
        node->data.while_stmt.body
    );

    LLVMBasicBlockRef body_end =
        LLVMGetInsertBlock(codegen->builder);

    if (LLVMGetBasicBlockTerminator(body_end) == NULL) {
        LLVMBuildBr(
            codegen->builder,
            condition_block
        );
    }

    LLVMPositionBuilderAtEnd(
        codegen->builder,
        exit_block
    );

    codegen->current_loop = loop.parent;
    return;
}

static void codegen_break(Codegen *codegen)
{
    if (codegen == NULL ||
        codegen->current_loop == NULL) {
        fprintf(stderr,
                "codegen: break used outside a loop\n");
        return;
    }

    LLVMBuildBr(
        codegen->builder,
        codegen->current_loop->break_target
    );
}

static void codegen_continue(Codegen *codegen)
{
    if (codegen == NULL ||
        codegen->current_loop == NULL) {
        fprintf(stderr,
                "codegen: continue used outside a loop\n");
        return;
    }

    LLVMBuildBr(
        codegen->builder,
        codegen->current_loop->continue_target
    );
}

static void codegen_repeat(Codegen *codegen, ASTNode *node)
{
    if (codegen == NULL ||
        node == NULL ||
        node->kind != AST_REPEAT ||
        codegen->current_function == NULL) {
        return;
    }

    /* Evaluate the repeat count exactly once. */
    LLVMValueRef count =
        codegen_expression(codegen, node->data.repeat_stmt.count);

    if (count == NULL ||
        LLVMTypeOf(count) != llvm_number_type(codegen)) {
        fprintf(stderr, "codegen: repeat count must be number\n");
        return;
    }

    LLVMBasicBlockRef condition_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "repeat.cond");

    LLVMBasicBlockRef body_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "repeat.body");

    LLVMBasicBlockRef step_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "repeat.step");

    LLVMBasicBlockRef exit_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "repeat.end");

    LLVMValueRef counter =
        LLVMBuildAlloca(codegen->builder, llvm_number_type(codegen),
                        "repeat.counter");

    LLVMBuildStore(codegen->builder, count, counter);
    LLVMBuildBr(codegen->builder, condition_block);

    CodegenLoop loop = {
        .break_target = exit_block,
        .continue_target = step_block,
        .parent = codegen->current_loop
    };
    codegen->current_loop = &loop;

    LLVMPositionBuilderAtEnd(codegen->builder, condition_block);

    LLVMValueRef remaining =
        LLVMBuildLoad2(codegen->builder, llvm_number_type(codegen),
                       counter, "repeat.remaining");

    /* Non-positive counts execute zero iterations. */
    LLVMValueRef positive =
        LLVMBuildICmp(
            codegen->builder,
            LLVMIntSGT,
            remaining,
            LLVMConstInt(llvm_number_type(codegen), 0, 0),
            "repeat.positive");

    LLVMBuildCondBr(
        codegen->builder, positive, body_block, exit_block);

    LLVMPositionBuilderAtEnd(codegen->builder, body_block);
    codegen_statement(codegen, node->data.repeat_stmt.body);

    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(codegen->builder);
    if (LLVMGetBasicBlockTerminator(body_end) == NULL) {
        LLVMBuildBr(codegen->builder, step_block);
    }

    LLVMPositionBuilderAtEnd(codegen->builder, step_block);

    remaining =
        LLVMBuildLoad2(codegen->builder, llvm_number_type(codegen),
                       counter, "repeat.before_decrement");

    LLVMValueRef decremented =
        LLVMBuildSub(
            codegen->builder,
            remaining,
            LLVMConstInt(llvm_number_type(codegen), 1, 0),
            "repeat.decremented");

    LLVMBuildStore(codegen->builder, decremented, counter);
    LLVMBuildBr(codegen->builder, condition_block);

    LLVMPositionBuilderAtEnd(codegen->builder, exit_block);
    codegen->current_loop = loop.parent;
}

static void codegen_for(Codegen *codegen, ASTNode *node)
{
    if (codegen == NULL ||
        node == NULL ||
        node->kind != AST_FOR ||
        codegen->current_function == NULL) {
        return;
    }

    /*
     * Variables declared by the for initializer are visible throughout
     * the loop, but must not leak beyond it.
     */
    CodegenVariable *saved_variables = codegen->variables;

    codegen_statement(codegen, node->data.for_stmt.initializer);

    LLVMBasicBlockRef condition_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "for.cond");

    LLVMBasicBlockRef body_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "for.body");

    LLVMBasicBlockRef step_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "for.step");

    LLVMBasicBlockRef exit_block =
        LLVMAppendBasicBlockInContext(
            codegen->context, codegen->current_function, "for.end");

    CodegenLoop loop = {
        .break_target = exit_block,
        .continue_target = step_block,
        .parent = codegen->current_loop
    };
    codegen->current_loop = &loop;

    LLVMBuildBr(codegen->builder, condition_block);

    LLVMPositionBuilderAtEnd(codegen->builder, condition_block);

    LLVMValueRef condition =
        codegen_expression(codegen, node->data.for_stmt.condition);

    if (condition == NULL ||
        LLVMTypeOf(condition) != llvm_bool_type(codegen)) {
        fprintf(stderr, "codegen: for condition must be bool\n");

        LLVMBuildBr(codegen->builder, exit_block);
        LLVMPositionBuilderAtEnd(codegen->builder, exit_block);
        codegen->current_loop = loop.parent;
        goto cleanup_for_variables;
    }

    LLVMBuildCondBr(
        codegen->builder, condition, body_block, exit_block);

    LLVMPositionBuilderAtEnd(codegen->builder, body_block);
    codegen_statement(codegen, node->data.for_stmt.body);

    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(codegen->builder);
    if (LLVMGetBasicBlockTerminator(body_end) == NULL) {
        LLVMBuildBr(codegen->builder, step_block);
    }

    LLVMPositionBuilderAtEnd(codegen->builder, step_block);
    codegen_statement(codegen, node->data.for_stmt.assignment);

    LLVMBasicBlockRef step_end = LLVMGetInsertBlock(codegen->builder);
    if (LLVMGetBasicBlockTerminator(step_end) == NULL) {
        LLVMBuildBr(codegen->builder, condition_block);
    }

    LLVMPositionBuilderAtEnd(codegen->builder, exit_block);
    codegen->current_loop = loop.parent;

cleanup_for_variables:
    while (codegen->variables != saved_variables) {
        CodegenVariable *variable = codegen->variables;
        if (variable == NULL) {
            break;
        }

        codegen->variables = variable->next;
        free(variable->name);
        free(variable);
    }
}