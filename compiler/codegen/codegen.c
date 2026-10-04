#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void codegen_free_variables(Codegen *codegen);

static void codegen_variable_declaration(
    Codegen *codegen,
    ASTNode *node
);

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