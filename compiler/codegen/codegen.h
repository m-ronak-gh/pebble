#ifndef PEBBLE_CODEGEN_H
#define PEBBLE_CODEGEN_H

#include <stddef.h>

#include <llvm-c/Core.h>

#include "../ast/ast.h"
#include "../semantic/semantic.h"

typedef struct CodegenVariable {
    char *name;
    LLVMValueRef alloca;
    SemanticType type;
    LLVMTypeRef storage_type;
    struct CodegenVariable *next;
} CodegenVariable;

typedef struct CodegenLoop {
    LLVMBasicBlockRef break_target;
    LLVMBasicBlockRef continue_target;
    struct CodegenLoop *parent;
} CodegenLoop;
typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;

    const SemanticContext *semantic;

    LLVMValueRef current_function;
    CodegenVariable *variables;
    CodegenLoop *current_loop;
    int has_error;
} Codegen;

Codegen *codegen_create(
    const char *module_name,
    const SemanticContext *semantic
);

void codegen_free(Codegen *codegen);

/* Nonzero if any code-generation error has occurred. */
int codegen_has_error(const Codegen *codegen);

void codegen_dump(const Codegen *codegen);

LLVMModuleRef codegen_module(const Codegen *codegen);

int codegen_write_ir(
    const Codegen *codegen,
    const char *path
);

LLVMValueRef codegen_expression(
    Codegen *codegen,
    ASTNode *expression
);

void codegen_function(
    Codegen *codegen,
    ASTNode *function
);

#endif /* PEBBLE_CODEGEN_H */