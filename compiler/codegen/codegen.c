#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

/* Code-generation diagnostics are fatal for the current compilation. */
#define CODEGEN_ERROR(codegen, ...) \
    do { \
        (codegen)->has_error = 1; \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)

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

static LLVMValueRef codegen_builtin_alloc(
    Codegen *codegen,
    ASTNode *expression
);

static LLVMValueRef codegen_builtin_free(
    Codegen *codegen,
    ASTNode *expression
);

static LLVMValueRef codegen_lvalue_pointer(
    Codegen *codegen,
    ASTNode *node,
    LLVMTypeRef *out_type
);

static CodegenStructDef *codegen_find_struct(
    const Codegen *codegen,
    const char *name,
    size_t length
);

static void codegen_panic(
    Codegen *codegen,
    LLVMValueRef msg_val
);

static void codegen_repeat(Codegen *codegen, ASTNode *node);
static void codegen_for(Codegen *codegen, ASTNode *node);

static LLVMValueRef codegen_array_element_pointer(
    Codegen *codegen,
    ASTNode *expression,
    LLVMTypeRef *element_type
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

static void codegen_break(Codegen *codegen);
static void codegen_continue(Codegen *codegen);

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

static LLVMTypeRef codegen_size_t_type(
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

    codegen->string_type =
        LLVMStructCreateNamed(
            codegen->context,
            "PebbleString"
        );

    LLVMTypeRef string_fields[] = {
        LLVMInt64TypeInContext(codegen->context),
        LLVMPointerType(
            LLVMInt8TypeInContext(codegen->context),
            0
        )
    };

    LLVMStructSetBody(
        codegen->string_type,
        string_fields,
        2,
        0
    );

    codegen->string_literal_count = 0;

    codegen->semantic = semantic;
    codegen->current_function = NULL;
    codegen->variables = NULL;
    codegen->current_loop = NULL;
    codegen->structs = NULL;
    codegen->has_error = 0;

    if (codegen->semantic != NULL) {
        const ASTNode *prog = semantic_get_program(codegen->semantic);
        if (prog != NULL && prog->kind == AST_PROGRAM) {
            for (size_t i = 0; i < prog->data.program.statement_count; ++i) {
                ASTNode *stmt = prog->data.program.statements[i];
                if (stmt != NULL && stmt->kind == AST_STRUCT) {
                    codegen_declare_struct(codegen, stmt);
                }
            }
        }
    }

    return codegen;
}

int codegen_has_error(const Codegen *codegen)
{
    return codegen == NULL || codegen->has_error;
}

void codegen_free(Codegen *codegen)
{
    if (codegen == NULL) {
        return;
    }

    codegen_free_variables(codegen);

    CodegenStructDef *s = codegen->structs;
    while (s != NULL) {
        CodegenStructDef *next = s->next;
        free(s->name);
        free(s);
        s = next;
    }

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

static CodegenStructDef *codegen_find_struct(
    const Codegen *codegen,
    const char *name,
    size_t length
)
{
    if (codegen == NULL || name == NULL) return NULL;
    for (CodegenStructDef *s = codegen->structs; s != NULL; s = s->next) {
        if (s->name_length == length && memcmp(s->name, name, length) == 0) {
            return s;
        }
    }
    return NULL;
}

LLVMTypeRef codegen_declare_struct(
    Codegen *codegen,
    const ASTNode *struct_node
)
{
    if (codegen == NULL || struct_node == NULL || struct_node->kind != AST_STRUCT) {
        return NULL;
    }

    const char *sname = struct_node->data.struct_decl.name;
    size_t slen = struct_node->data.struct_decl.name_length;

    CodegenStructDef *existing = codegen_find_struct(codegen, sname, slen);
    if (existing != NULL) {
        return existing->llvm_type;
    }

    char *name = malloc(slen + 1);
    if (name == NULL) {
        CODEGEN_ERROR(codegen, "fatal: failed to allocate struct name\n");
        return NULL;
    }
    memcpy(name, sname, slen);
    name[slen] = '\0';

    LLVMTypeRef struct_type = LLVMStructCreateNamed(codegen->context, name);

    CodegenStructDef *sdef = malloc(sizeof(CodegenStructDef));
    if (sdef == NULL) {
        free(name);
        CODEGEN_ERROR(codegen, "fatal: failed to allocate struct def\n");
        return NULL;
    }
    sdef->name = name;
    sdef->name_length = slen;
    sdef->llvm_type = struct_type;
    sdef->decl = struct_node;
    sdef->next = codegen->structs;
    codegen->structs = sdef;

    size_t field_count = struct_node->data.struct_decl.field_count;
    LLVMTypeRef *field_types = field_count ? malloc(field_count * sizeof(LLVMTypeRef)) : NULL;
    if (field_count > 0 && field_types == NULL) {
        CODEGEN_ERROR(codegen, "fatal: failed to allocate struct field types\n");
        return NULL;
    }

    for (size_t i = 0; i < field_count; ++i) {
        ASTNode *field = struct_node->data.struct_decl.fields[i];
        field_types[i] = llvm_type_from_ast(codegen, field->data.var_decl.type);
        if (field_types[i] == NULL) {
            free(field_types);
            return NULL;
        }
    }

    LLVMStructSetBody(struct_type, field_types, (unsigned)field_count, 0);
    free(field_types);

    return struct_type;
}

static LLVMTypeRef llvm_type_from_ast(
    Codegen *codegen,
    ASTType type
)
{
    LLVMTypeRef element_type = NULL;

    switch (type.kind) {
        case AST_TYPE_NUMBER:
            element_type = llvm_number_type(codegen);
            break;

        case AST_TYPE_DECIMAL:
            element_type = llvm_decimal_type(codegen);
            break;

        case AST_TYPE_BOOL:
            element_type = llvm_bool_type(codegen);
            break;

        case AST_TYPE_CHAR:
            element_type = llvm_char_type(codegen);
            break;

        case AST_TYPE_VOID:
            if (type.dimension_count != 0) {
                CODEGEN_ERROR(codegen, "codegen: array element type cannot be void\n"
                );
                return NULL;
            }

            return LLVMVoidTypeInContext(codegen->context);

        case AST_TYPE_STRING:
            element_type = codegen->string_type;
            break;

        case AST_TYPE_USER: {
            if (codegen->semantic != NULL &&
                semantic_lookup_enum(codegen->semantic, type.name, type.name_length) != NULL) {
                element_type = llvm_number_type(codegen);
                break;
            }
            CodegenStructDef *sdef = codegen_find_struct(codegen, type.name, type.name_length);
            if (sdef != NULL) {
                element_type = sdef->llvm_type;
                break;
            }
            if (codegen->semantic != NULL) {
                const ASTNode *snode = semantic_lookup_struct(codegen->semantic, type.name, type.name_length);
                if (snode != NULL) {
                    element_type = codegen_declare_struct(codegen, snode);
                    break;
                }
            }
            CODEGEN_ERROR(codegen, "codegen: unknown user type '%.*s'\n",
                (int)type.name_length, type.name != NULL ? type.name : ""
            );
            return NULL;
        }
    }

    if (element_type == NULL) {
        return NULL;
    }

    /*
     * Construct dimensions from innermost to outermost.
     * For number a[2][3], this creates [2 x [3 x i64]].
     */
    for (size_t i = type.dimension_count; i > 0; --i) {
        ASTArrayDimension dimension = type.dimensions[i - 1];

        if (!dimension.has_size || dimension.size == 0) {
            CODEGEN_ERROR(codegen, "codegen: array dimensions must have a positive fixed size\n"
            );
            return NULL;
        }

        if (dimension.size > UINT_MAX) {
            CODEGEN_ERROR(codegen, "codegen: array dimension is too large\n"
            );
            return NULL;
        }

        element_type = LLVMArrayType(
            element_type,
            (unsigned)dimension.size
        );
    }

    return element_type;
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

static LLVMValueRef codegen_string_constant(
    Codegen *codegen,
    const char *chars,
    size_t length
)
{
    if (length > UINT_MAX) {
        CODEGEN_ERROR(
            codegen,
            "codegen: string literal is too large\n"
        );
        return NULL;
    }

    char global_name[64];

    int name_length = snprintf(
        global_name,
        sizeof(global_name),
        ".pebble.str.%zu",
        codegen->string_literal_count
    );

    if (name_length < 0 ||
        (size_t)name_length >= sizeof(global_name)) {

        CODEGEN_ERROR(
            codegen,
            "codegen: failed to name string literal\n"
        );
        return NULL;
    }

    codegen->string_literal_count++;

    LLVMValueRef string_data =
        LLVMConstStringInContext(
            codegen->context,
            chars,
            (unsigned)length,
            0
        );

    LLVMTypeRef data_type = LLVMTypeOf(string_data);

    LLVMValueRef global =
        LLVMAddGlobal(
            codegen->module,
            data_type,
            global_name
        );

    LLVMSetInitializer(global, string_data);
    LLVMSetGlobalConstant(global, 1);
    LLVMSetLinkage(global, LLVMPrivateLinkage);

    LLVMValueRef zero = LLVMConstInt(
        LLVMInt32TypeInContext(codegen->context),
        0,
        0
    );

    LLVMValueRef indices[] = {
        zero,
        zero
    };

    LLVMValueRef data_pointer =
        LLVMConstInBoundsGEP2(
            data_type,
            global,
            indices,
            2
        );

    LLVMValueRef fields[] = {
        LLVMConstInt(
            LLVMInt64TypeInContext(codegen->context),
            (unsigned long long)length,
            0
        ),
        data_pointer
    };

    return LLVMConstNamedStruct(
        codegen->string_type,
        fields,
        2
    );
}

static LLVMValueRef codegen_string_literal(
    Codegen *codegen,
    ASTNode *expression
)
{
    return codegen_string_constant(
        codegen,
        expression->data.string.start,
        expression->data.string.length
    );
}

static void codegen_panic(Codegen *codegen, LLVMValueRef msg_val)
{
    LLVMValueRef printf_func = codegen_declare_printf(codegen);
    LLVMTypeRef printf_type = codegen_printf_type(codegen);

    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(
        codegen->builder,
        "panic: %.*s\n",
        "panic.fmt"
    );

    LLVMValueRef length = LLVMBuildExtractValue(
        codegen->builder,
        msg_val,
        0,
        "panic.len"
    );
    LLVMValueRef data = LLVMBuildExtractValue(
        codegen->builder,
        msg_val,
        1,
        "panic.data"
    );

    LLVMValueRef len32 = LLVMBuildTrunc(
        codegen->builder,
        length,
        LLVMInt32TypeInContext(codegen->context),
        "panic.len32"
    );

    LLVMValueRef args[] = { fmt, len32, data };
    LLVMBuildCall2(codegen->builder, printf_type, printf_func, args, 3, "");

    LLVMValueRef one = LLVMConstInt(
        LLVMInt32TypeInContext(codegen->context),
        1,
        0
    );
    LLVMTypeRef exit_param = LLVMInt32TypeInContext(codegen->context);
    LLVMTypeRef exit_fn_type = LLVMFunctionType(
        LLVMVoidTypeInContext(codegen->context),
        &exit_param,
        1,
        0
    );
    LLVMValueRef exit_fn = LLVMGetNamedFunction(codegen->module, "exit");
    if (exit_fn == NULL) {
        exit_fn = LLVMAddFunction(codegen->module, "exit", exit_fn_type);
    }
    LLVMBuildCall2(codegen->builder, exit_fn_type, exit_fn, &one, 1, "");
    LLVMBuildUnreachable(codegen->builder);
}

static LLVMValueRef codegen_string_equality(
    Codegen *codegen,
    LLVMValueRef left,
    LLVMValueRef right,
    int is_not_equal
)
{
    LLVMValueRef len1 = LLVMBuildExtractValue(codegen->builder, left, 0, "len1");
    LLVMValueRef len2 = LLVMBuildExtractValue(codegen->builder, right, 0, "len2");
    LLVMValueRef data1 = LLVMBuildExtractValue(codegen->builder, left, 1, "data1");
    LLVMValueRef data2 = LLVMBuildExtractValue(codegen->builder, right, 1, "data2");

    LLVMValueRef len_match = LLVMBuildICmp(
        codegen->builder,
        LLVMIntEQ,
        len1,
        len2,
        "len.match"
    );

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(codegen->builder);
    LLVMBasicBlockRef check_bytes = LLVMAppendBasicBlockInContext(
        codegen->context,
        codegen->current_function,
        "streq.bytes"
    );
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(
        codegen->context,
        codegen->current_function,
        "streq.merge"
    );

    LLVMBuildCondBr(codegen->builder, len_match, check_bytes, merge_block);

    LLVMPositionBuilderAtEnd(codegen->builder, check_bytes);
    LLVMTypeRef size_type = codegen_size_t_type(codegen);
    LLVMValueRef len_native = LLVMBuildIntCast2(
        codegen->builder,
        len1,
        size_type,
        0,
        "len.native"
    );

    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(codegen->context), 0);
    LLVMTypeRef memcmp_params[] = { ptr_type, ptr_type, size_type };
    LLVMTypeRef memcmp_type = LLVMFunctionType(
        LLVMInt32TypeInContext(codegen->context),
        memcmp_params,
        3,
        0
    );
    LLVMValueRef memcmp_fn = LLVMGetNamedFunction(codegen->module, "memcmp");
    if (memcmp_fn == NULL) {
        memcmp_fn = LLVMAddFunction(codegen->module, "memcmp", memcmp_type);
    }
    LLVMValueRef memcmp_args[] = { data1, data2, len_native };
    LLVMValueRef cmp_res = LLVMBuildCall2(
        codegen->builder,
        memcmp_type,
        memcmp_fn,
        memcmp_args,
        3,
        "memcmp.res"
    );
    LLVMValueRef bytes_match = LLVMBuildICmp(
        codegen->builder,
        LLVMIntEQ,
        cmp_res,
        LLVMConstInt(LLVMInt32TypeInContext(codegen->context), 0, 0),
        "bytes.match"
    );
    LLVMBasicBlockRef bytes_end_block = LLVMGetInsertBlock(codegen->builder);
    LLVMBuildBr(codegen->builder, merge_block);

    LLVMPositionBuilderAtEnd(codegen->builder, merge_block);
    LLVMValueRef phi = LLVMBuildPhi(
        codegen->builder,
        llvm_bool_type(codegen),
        "streq.result"
    );
    LLVMValueRef vals[] = {
        LLVMConstInt(llvm_bool_type(codegen), 0, 0),
        bytes_match
    };
    LLVMBasicBlockRef blks[] = { cur_block, bytes_end_block };
    LLVMAddIncoming(phi, vals, blks, 2);

    if (is_not_equal) {
        return LLVMBuildXor(
            codegen->builder,
            phi,
            LLVMConstInt(llvm_bool_type(codegen), 1, 0),
            "strne.result"
        );
    }
    return phi;
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

        case AST_STRING_LITERAL:
            return codegen_string_literal(
                codegen,
                expression
            );

        case AST_CHAR_LITERAL:
            return LLVMConstInt(
                llvm_char_type(codegen),
                (unsigned char)
                    expression->data.character,
                0
            );

        case AST_NULL_LITERAL:
            return LLVMConstNull(codegen->string_type);

        case AST_INDEX: {
            LLVMTypeRef element_type = NULL;

            LLVMValueRef element_pointer =
                codegen_array_element_pointer(
                    codegen,
                    expression,
                    &element_type
                );

            if (element_pointer == NULL || element_type == NULL) {
                return NULL;
            }

            return LLVMBuildLoad2(
                codegen->builder,
                element_type,
                element_pointer,
                "array.element"
            );
        }

        case AST_MEMBER_ACCESS: {
            LLVMTypeRef val_type = NULL;
            LLVMValueRef ptr = codegen_lvalue_pointer(codegen, expression, &val_type);
            if (ptr == NULL || val_type == NULL) {
                return NULL;
            }
            return LLVMBuildLoad2(
                codegen->builder,
                val_type,
                ptr,
                "member.load"
            );
        }

        case AST_CALL: {
            ASTNode *callee_node = expression->data.call.callee;
            if (callee_node == NULL || callee_node->kind != AST_IDENTIFIER) {
                CODEGEN_ERROR(codegen, "codegen: only direct function calls are supported\n");
                return NULL;
            }

            if (callee_node->data.identifier.length == 5 &&
                memcmp(
                    callee_node->data.identifier.start,
                    "alloc",
                    5
                ) == 0) {

                return codegen_builtin_alloc(codegen, expression);
            }

            if (callee_node->data.identifier.length == 4 &&
                memcmp(
                    callee_node->data.identifier.start,
                    "free",
                    4
                ) == 0) {

                return codegen_builtin_free(codegen, expression);
            }

            if (callee_node->data.identifier.length == 6 &&
                memcmp(callee_node->data.identifier.start, "length", 6) == 0) {
                if (expression->data.call.argument_count != 1) {
                    CODEGEN_ERROR(codegen, "codegen: length() expects 1 argument\n");
                    return NULL;
                }
                ASTNode *arg = expression->data.call.arguments[0];
                SemanticType sem_type;
                if (codegen->semantic == NULL ||
                    !semantic_type_of(codegen->semantic, arg, &sem_type)) {
                    CODEGEN_ERROR(codegen, "codegen: missing semantic type for length argument\n");
                    return NULL;
                }
                if (sem_type.kind == SEM_TYPE_STRING) {
                    LLVMValueRef str_val = codegen_expression(codegen, arg);
                    if (str_val == NULL) return NULL;
                    return LLVMBuildExtractValue(codegen->builder, str_val, 0, "string.len");
                } else if (sem_type.array_dimension_count > 0) {
                    return LLVMConstInt(llvm_number_type(codegen), (unsigned long long)sem_type.array_lengths[0], 1);
                }
                CODEGEN_ERROR(codegen, "codegen: length() expects string or array argument\n");
                return NULL;
            }

            if (callee_node->data.identifier.length == 6 &&
                memcmp(callee_node->data.identifier.start, "typeof", 6) == 0) {
                if (expression->data.call.argument_count != 1) {
                    CODEGEN_ERROR(codegen, "codegen: typeof() expects 1 argument\n");
                    return NULL;
                }
                SemanticType sem_type;
                if (codegen->semantic == NULL ||
                    !semantic_type_of(codegen->semantic, expression->data.call.arguments[0], &sem_type)) {
                    CODEGEN_ERROR(codegen, "codegen: missing semantic type for typeof\n");
                    return NULL;
                }
                if ((sem_type.kind == SEM_TYPE_STRUCT || sem_type.kind == SEM_TYPE_ENUM) && sem_type.name != NULL) {
                    return codegen_string_constant(codegen, sem_type.name, sem_type.name_length);
                }
                const char *tname = semantic_type_name(sem_type.kind);
                return codegen_string_constant(codegen, tname, strlen(tname));
            }

            if (callee_node->data.identifier.length == 4 &&
                memcmp(callee_node->data.identifier.start, "exit", 4) == 0) {
                if (expression->data.call.argument_count != 1) {
                    CODEGEN_ERROR(codegen, "codegen: exit() expects 1 argument\n");
                    return NULL;
                }
                LLVMValueRef code_val = codegen_expression(codegen, expression->data.call.arguments[0]);
                if (code_val == NULL) return NULL;
                LLVMValueRef code32 = LLVMBuildTrunc(codegen->builder, code_val, LLVMInt32TypeInContext(codegen->context), "exit.code");
                LLVMTypeRef exit_param = LLVMInt32TypeInContext(codegen->context);
                LLVMTypeRef exit_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(codegen->context), &exit_param, 1, 0);
                LLVMValueRef exit_fn = LLVMGetNamedFunction(codegen->module, "exit");
                if (exit_fn == NULL) {
                    exit_fn = LLVMAddFunction(codegen->module, "exit", exit_fn_type);
                }
                LLVMBuildCall2(codegen->builder, exit_fn_type, exit_fn, &code32, 1, "");
                LLVMBuildUnreachable(codegen->builder);
                return NULL;
            }

            if (callee_node->data.identifier.length == 5 &&
                memcmp(callee_node->data.identifier.start, "panic", 5) == 0) {
                if (expression->data.call.argument_count != 1) {
                    CODEGEN_ERROR(codegen, "codegen: panic() expects 1 argument\n");
                    return NULL;
                }
                LLVMValueRef msg_val = codegen_expression(codegen, expression->data.call.arguments[0]);
                if (msg_val == NULL) return NULL;
                codegen_panic(codegen, msg_val);
                return NULL;
            }

            if (callee_node->data.identifier.length == 6 &&
                memcmp(callee_node->data.identifier.start, "assert", 6) == 0) {
                if (expression->data.call.argument_count != 2) {
                    CODEGEN_ERROR(codegen, "codegen: assert() expects 2 arguments\n");
                    return NULL;
                }
                LLVMValueRef cond = codegen_expression(codegen, expression->data.call.arguments[0]);
                if (cond == NULL) return NULL;
                LLVMBasicBlockRef pass_block = LLVMAppendBasicBlockInContext(codegen->context, codegen->current_function, "assert.pass");
                LLVMBasicBlockRef fail_block = LLVMAppendBasicBlockInContext(codegen->context, codegen->current_function, "assert.fail");
                LLVMBuildCondBr(codegen->builder, cond, pass_block, fail_block);

                LLVMPositionBuilderAtEnd(codegen->builder, fail_block);
                LLVMValueRef msg_val = codegen_expression(codegen, expression->data.call.arguments[1]);
                if (msg_val != NULL) {
                    codegen_panic(codegen, msg_val);
                } else {
                    LLVMBuildUnreachable(codegen->builder);
                }

                LLVMPositionBuilderAtEnd(codegen->builder, pass_block);
                return NULL;
            }

            char *name = malloc(callee_node->data.identifier.length + 1);
            if (name == NULL) {
                CODEGEN_ERROR(codegen, "fatal: failed to allocate called function name\n");
                return NULL;
            }
            memcpy(name, callee_node->data.identifier.start, callee_node->data.identifier.length);
            name[callee_node->data.identifier.length] = '\0';
            LLVMValueRef callee = LLVMGetNamedFunction(codegen->module, name);
            free(name);
            if (callee == NULL) {
                CODEGEN_ERROR(codegen, "codegen: function declaration not found for call\n");
                return NULL;
            }

            size_t count = expression->data.call.argument_count;
            LLVMValueRef *arguments = count ? malloc(count * sizeof(*arguments)) : NULL;
            if (count && arguments == NULL) {
                CODEGEN_ERROR(codegen, "fatal: failed to allocate call arguments\n");
                return NULL;
            }
            for (size_t i = 0; i < count; ++i) {
                arguments[i] = codegen_expression(codegen, expression->data.call.arguments[i]);
                if (arguments[i] == NULL) { free(arguments); return NULL; }
            }
            LLVMTypeRef function_type = LLVMGlobalGetValueType(callee);
            LLVMTypeRef result_type = LLVMGetReturnType(function_type);
            LLVMValueRef result = LLVMBuildCall2(
                codegen->builder, function_type, callee, arguments, (unsigned)count,
                result_type == LLVMVoidTypeInContext(codegen->context) ? "" : "calltmp"
            );
            free(arguments);
            return result_type == LLVMVoidTypeInContext(codegen->context) ? NULL : result;
        }

        case AST_IDENTIFIER: {
            CodegenVariable *variable =
                codegen_find_variable(
                    codegen,
                    expression->data.identifier.start,
                    expression->data.identifier.length
                );

            if (variable != NULL) {
                LLVMTypeRef type = variable->storage_type;
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

            long long enum_value = 0;
            if (codegen->semantic != NULL &&
                semantic_lookup_enum_member(
                    codegen->semantic,
                    expression->data.identifier.start,
                    expression->data.identifier.length,
                    &enum_value
                )) {
                return LLVMConstInt(
                    llvm_number_type(codegen),
                    (unsigned long long)enum_value,
                    1
                );
            }

            CODEGEN_ERROR(codegen, "codegen: undefined variable '%.*s'\n",
                (int)expression->data.identifier.length,
                expression->data.identifier.start
            );

            return NULL;
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
                    if (LLVMGetTypeKind(LLVMTypeOf(operand)) == LLVMDoubleTypeKind) {
                        return LLVMBuildFNeg(
                            codegen->builder,
                            operand,
                            "fnegtmp"
                        );
                    }
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

            CODEGEN_ERROR(codegen, "codegen: unsupported unary operator\n"
            );

            return NULL;
        }

        case AST_BINARY: {
            if (expression->data.binary.operator == AST_BINARY_LOGICAL_AND) {
                LLVMValueRef left = codegen_expression(codegen, expression->data.binary.left);
                if (left == NULL) return NULL;
                LLVMBasicBlockRef left_block = LLVMGetInsertBlock(codegen->builder);
                LLVMBasicBlockRef rhs_block = LLVMAppendBasicBlockInContext(codegen->context, codegen->current_function, "and.rhs");
                LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(codegen->context, codegen->current_function, "and.end");
                LLVMBuildCondBr(codegen->builder, left, rhs_block, merge_block);

                LLVMPositionBuilderAtEnd(codegen->builder, rhs_block);
                LLVMValueRef right = codegen_expression(codegen, expression->data.binary.right);
                if (right == NULL) return NULL;
                LLVMBasicBlockRef rhs_end_block = LLVMGetInsertBlock(codegen->builder);
                LLVMBuildBr(codegen->builder, merge_block);

                LLVMPositionBuilderAtEnd(codegen->builder, merge_block);
                LLVMValueRef phi = LLVMBuildPhi(codegen->builder, llvm_bool_type(codegen), "and.result");
                LLVMValueRef incoming_values[] = { LLVMConstInt(llvm_bool_type(codegen), 0, 0), right };
                LLVMBasicBlockRef incoming_blocks[] = { left_block, rhs_end_block };
                LLVMAddIncoming(phi, incoming_values, incoming_blocks, 2);
                return phi;
            }

            if (expression->data.binary.operator == AST_BINARY_LOGICAL_OR) {
                LLVMValueRef left = codegen_expression(codegen, expression->data.binary.left);
                if (left == NULL) return NULL;
                LLVMBasicBlockRef left_block = LLVMGetInsertBlock(codegen->builder);
                LLVMBasicBlockRef rhs_block = LLVMAppendBasicBlockInContext(codegen->context, codegen->current_function, "or.rhs");
                LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(codegen->context, codegen->current_function, "or.end");
                LLVMBuildCondBr(codegen->builder, left, merge_block, rhs_block);

                LLVMPositionBuilderAtEnd(codegen->builder, rhs_block);
                LLVMValueRef right = codegen_expression(codegen, expression->data.binary.right);
                if (right == NULL) return NULL;
                LLVMBasicBlockRef rhs_end_block = LLVMGetInsertBlock(codegen->builder);
                LLVMBuildBr(codegen->builder, merge_block);

                LLVMPositionBuilderAtEnd(codegen->builder, merge_block);
                LLVMValueRef phi = LLVMBuildPhi(codegen->builder, llvm_bool_type(codegen), "or.result");
                LLVMValueRef incoming_values[] = { LLVMConstInt(llvm_bool_type(codegen), 1, 0), right };
                LLVMBasicBlockRef incoming_blocks[] = { left_block, rhs_end_block };
                LLVMAddIncoming(phi, incoming_values, incoming_blocks, 2);
                return phi;
            }

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
                CODEGEN_ERROR(codegen, "codegen: missing semantic type for binary expression\n"
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

                case AST_BINARY_BITWISE_AND:
                    return LLVMBuildAnd(codegen->builder, left, right, "andtmp");

                case AST_BINARY_BITWISE_OR:
                    return LLVMBuildOr(codegen->builder, left, right, "ortmp");

                case AST_BINARY_BITWISE_XOR:
                    return LLVMBuildXor(codegen->builder, left, right, "xortmp");

                case AST_BINARY_SHIFT_LEFT:
                    return LLVMBuildShl(codegen->builder, left, right, "shltmp");

                case AST_BINARY_SHIFT_RIGHT:
                    return LLVMBuildAShr(codegen->builder, left, right, "ashrtmp");

                case AST_BINARY_EQUAL:
                case AST_BINARY_NOT_EQUAL:
                case AST_BINARY_LESS:
                case AST_BINARY_LESS_EQUAL:
                case AST_BINARY_GREATER:
                case AST_BINARY_GREATER_EQUAL: {
                    if (left_type.kind == SEM_TYPE_STRING) {
                        if (expression->data.binary.operator == AST_BINARY_EQUAL ||
                            expression->data.binary.operator == AST_BINARY_NOT_EQUAL) {
                            return codegen_string_equality(
                                codegen,
                                left,
                                right,
                                expression->data.binary.operator == AST_BINARY_NOT_EQUAL
                            );
                        }
                    }

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
                    CODEGEN_ERROR(codegen, "codegen: unsupported binary operator\n"
                    );
                    return NULL;
            }
        }

        default:
            CODEGEN_ERROR(codegen, "codegen: unsupported expression\n"
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

        CODEGEN_ERROR(codegen, "codegen: duplicate variable '%.*s'\n",
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
        CODEGEN_ERROR(codegen, "codegen: variable cannot have type void\n"
        );
        return;
    }

    LLVMValueRef initializer = NULL;

    if (node->data.var_decl.initializer != NULL) {
        initializer = codegen_expression(
            codegen,
            node->data.var_decl.initializer
        );

        if (initializer == NULL) {
            return;
        }
    } else if (ast_type.dimension_count == 0 && ast_type.kind != AST_TYPE_USER) {
        CODEGEN_ERROR(codegen, "codegen: scalar variable requires an initializer\n"
        );
        return;
    }

    char *variable_name =
        malloc(name_length + 1);

    if (variable_name == NULL) {
        CODEGEN_ERROR(codegen,
            "fatal: failed to allocate variable name\n");
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

    if (initializer != NULL) {
        LLVMBuildStore(
            codegen->builder,
            initializer,
            alloca
        );
    } else {
        LLVMBuildStore(
            codegen->builder,
            LLVMConstNull(llvm_type),
            alloca
        );
    }

    CodegenVariable *variable =
        malloc(sizeof(*variable));

    if (variable == NULL) {
        free(variable_name);

        CODEGEN_ERROR(codegen,
            "fatal: failed to allocate codegen variable\n");

        return;
    }

    memset(&variable->type, 0, sizeof(variable->type));

    if (node->data.var_decl.initializer != NULL) {
        if (codegen->semantic == NULL ||
            !semantic_type_of(
                codegen->semantic,
                node->data.var_decl.initializer,
                &variable->type
            )) {
            CODEGEN_ERROR(codegen, "codegen: missing semantic type for initializer\n"
            );

            free(variable_name);
            free(variable);
            return;
        }
    } else {
        switch (ast_type.kind) {
            case AST_TYPE_NUMBER:
                variable->type.kind = SEM_TYPE_NUMBER;
                break;
            case AST_TYPE_DECIMAL:
                variable->type.kind = SEM_TYPE_DECIMAL;
                break;
            case AST_TYPE_BOOL:
                variable->type.kind = SEM_TYPE_BOOL;
                break;
            case AST_TYPE_CHAR:
                variable->type.kind = SEM_TYPE_CHAR;
                break;
            case AST_TYPE_USER:
                if (codegen->semantic != NULL &&
                    semantic_lookup_enum(codegen->semantic, ast_type.name, ast_type.name_length) != NULL) {
                    variable->type.kind = SEM_TYPE_ENUM;
                    variable->type.name = ast_type.name;
                    variable->type.name_length = ast_type.name_length;
                } else {
                    variable->type.kind = SEM_TYPE_STRUCT;
                    variable->type.name = ast_type.name;
                    variable->type.name_length = ast_type.name_length;
                }
                break;
            default:
                CODEGEN_ERROR(codegen, "codegen: unsupported uninitialized variable type\n"
                );
                free(variable_name);
                free(variable);
                return;
        }
    }

    variable->storage_type = llvm_type;
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

    if (target == NULL) {
        CODEGEN_ERROR(codegen, "codegen: missing assignment target\n");
        return;
    }

    LLVMTypeRef expected_type = NULL;
    LLVMValueRef target_pointer = codegen_lvalue_pointer(
        codegen,
        target,
        &expected_type
    );

    if (target_pointer == NULL || expected_type == NULL) {
        return;
    }

    LLVMValueRef value = codegen_expression(
        codegen,
        value_node
    );

    if (value == NULL) {
        return;
    }

    if (LLVMTypeOf(value) != expected_type) {
        CODEGEN_ERROR(codegen, "codegen: assignment type mismatch\n"
        );
        return;
    }

    LLVMBuildStore(
        codegen->builder,
        value,
        target_pointer
    );
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

static LLVMTypeRef codegen_pebble_alloc_type(Codegen *codegen)
{
    LLVMTypeRef pointer_type =
        LLVMPointerType(
            LLVMInt8TypeInContext(codegen->context),
            0
        );

    LLVMTypeRef parameters[] = {
        LLVMInt64TypeInContext(codegen->context)
    };

    return LLVMFunctionType(
        pointer_type,
        parameters,
        1,
        0
    );
}

static LLVMValueRef codegen_declare_pebble_alloc(Codegen *codegen)
{
    LLVMTypeRef function_type =
        codegen_pebble_alloc_type(codegen);

    LLVMValueRef function =
        LLVMGetNamedFunction(codegen->module, "pebble_alloc");

    if (function != NULL) {
        if (LLVMGlobalGetValueType(function) != function_type) {
            CODEGEN_ERROR(
                codegen,
                "codegen: incompatible declaration of pebble_alloc\n"
            );
            return NULL;
        }

        return function;
    }

    return LLVMAddFunction(
        codegen->module,
        "pebble_alloc",
        function_type
    );
}

static LLVMTypeRef codegen_pebble_free_type(Codegen *codegen)
{
    LLVMTypeRef pointer_type =
        LLVMPointerType(
            LLVMInt8TypeInContext(codegen->context),
            0
        );

    return LLVMFunctionType(
        LLVMVoidTypeInContext(codegen->context),
        &pointer_type,
        1,
        0
    );
}

static LLVMValueRef codegen_declare_pebble_free(Codegen *codegen)
{
    LLVMTypeRef function_type =
        codegen_pebble_free_type(codegen);

    LLVMValueRef function =
        LLVMGetNamedFunction(codegen->module, "pebble_free");

    if (function != NULL) {
        if (LLVMGlobalGetValueType(function) != function_type) {
            CODEGEN_ERROR(
                codegen,
                "codegen: incompatible declaration of pebble_free\n"
            );
            return NULL;
        }

        return function;
    }

    return LLVMAddFunction(
        codegen->module,
        "pebble_free",
        function_type
    );
}

static LLVMValueRef codegen_builtin_alloc(
    Codegen *codegen,
    ASTNode *expression
)
{
    if (expression->data.call.argument_count != 1) {
        CODEGEN_ERROR(codegen, "codegen: alloc() expects one argument\n");
        return NULL;
    }

    LLVMValueRef size =
        codegen_expression(
            codegen,
            expression->data.call.arguments[0]
        );

    if (size == NULL) {
        return NULL;
    }

    LLVMTypeRef number_type =
        LLVMInt64TypeInContext(codegen->context);

    if (LLVMTypeOf(size) != number_type) {
        CODEGEN_ERROR(codegen, "codegen: alloc() argument must be number\n");
        return NULL;
    }

    LLVMValueRef function =
        codegen_declare_pebble_alloc(codegen);

    if (function == NULL) {
        return NULL;
    }

    return LLVMBuildCall2(
        codegen->builder,
        codegen_pebble_alloc_type(codegen),
        function,
        &size,
        1,
        "alloc.tmp"
    );
}

static LLVMValueRef codegen_builtin_free(
    Codegen *codegen,
    ASTNode *expression
)
{
    if (expression->data.call.argument_count != 1) {
        CODEGEN_ERROR(codegen, "codegen: free() expects one argument\n");
        return NULL;
    }

    ASTNode *argument_node =
        expression->data.call.arguments[0];

    SemanticType argument_type;

    if (!semantic_type_of(
            codegen->semantic,
            argument_node,
            &argument_type)) {

        CODEGEN_ERROR(
            codegen,
            "codegen: could not determine free() argument type\n"
        );
        return NULL;
    }

    LLVMValueRef argument =
        codegen_expression(codegen, argument_node);

    if (argument == NULL) {
        return NULL;
    }

    if (argument_type.kind == SEM_TYPE_STRING) {
        argument = LLVMBuildExtractValue(
            codegen->builder,
            argument,
            1,
            "free.string.data"
        );
    } else if (argument_type.kind != SEM_TYPE_POINTER) {
        CODEGEN_ERROR(
            codegen,
            "codegen: free() expects a pointer or string\n"
        );
        return NULL;
    }

    if (LLVMGetTypeKind(LLVMTypeOf(argument)) != LLVMPointerTypeKind) {
        CODEGEN_ERROR(
            codegen,
            "codegen: free() argument is not an LLVM pointer\n"
        );
        return NULL;
    }

    LLVMValueRef function =
        codegen_declare_pebble_free(codegen);

    if (function == NULL) {
        return NULL;
    }

    LLVMBuildCall2(
        codegen->builder,
        codegen_pebble_free_type(codegen),
        function,
        &argument,
        1,
        ""
    );

    return NULL;
}

/*
 * Pebble currently targets the host architecture.
 * Therefore, the compiler's native size_t width matches
 * the generated program's size_t width.
 */
static LLVMTypeRef codegen_size_t_type(Codegen *codegen)
{
    return LLVMIntTypeInContext(
        codegen->context,
        (unsigned)(sizeof(size_t) * CHAR_BIT)
    );
}

static LLVMValueRef codegen_declare_io_print_string(Codegen *codegen)
{
    LLVMTypeRef pointer_type =
        LLVMPointerType(
            LLVMInt8TypeInContext(codegen->context),
            0
        );

    LLVMTypeRef parameters[] = {
        pointer_type,
        LLVMInt64TypeInContext(codegen->context)
    };

    LLVMTypeRef function_type =
        LLVMFunctionType(
            LLVMVoidTypeInContext(codegen->context),
            parameters,
            2,
            0
        );

    LLVMValueRef function =
        LLVMGetNamedFunction(
            codegen->module,
            "pebble_io_print_string"
        );

    if (function != NULL) {
        if (LLVMGlobalGetValueType(function) != function_type) {
            CODEGEN_ERROR(
                codegen,
                "codegen: incompatible declaration of "
                "pebble_io_print_string\n"
            );
            return NULL;
        }

        return function;
    }

    return LLVMAddFunction(
        codegen->module,
        "pebble_io_print_string",
        function_type
    );
}

static void codegen_print_string(
    Codegen *codegen,
    LLVMValueRef value
)
{
    LLVMValueRef length =
        LLVMBuildExtractValue(
            codegen->builder,
            value,
            0,
            "string.length"
        );

    LLVMValueRef data =
        LLVMBuildExtractValue(
            codegen->builder,
            value,
            1,
            "string.data"
        );

    LLVMValueRef function =
        codegen_declare_io_print_string(codegen);

    if (function == NULL) {
        return;
    }

    LLVMTypeRef function_type =
        LLVMGlobalGetValueType(function);

    LLVMValueRef arguments[] = {
        data,
        length
    };

    LLVMBuildCall2(
        codegen->builder,
        function_type,
        function,
        arguments,
        2,
        ""
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

        CODEGEN_ERROR(
            codegen,
            "codegen: missing semantic type for print expression\n"
        );

        return;
    }

    /* Strings use fwrite so their complete byte length is preserved. */
    if (semantic_type.kind == SEM_TYPE_STRING) {
        codegen_print_string(codegen, value);
        return;
    }

    LLVMValueRef printf_function =
        codegen_declare_printf(codegen);

    LLVMTypeRef printf_type =
        codegen_printf_type(codegen);

    const char *format_text = NULL;

    switch (semantic_type.kind) {

        case SEM_TYPE_NUMBER:
        case SEM_TYPE_ENUM:
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
            CODEGEN_ERROR(
                codegen,
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

    LLVMValueRef arguments[] = {
        format,
        value
    };

    LLVMBuildCall2(
        codegen->builder,
        printf_type,
        printf_function,
        arguments,
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
        LLVMBuildRetVoid(codegen->builder);
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
            CODEGEN_ERROR(codegen, "codegen: unsupported statement kind %d\n",
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

        CODEGEN_ERROR(codegen, "codegen: expected block\n"
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

static char *codegen_function_name(ASTNode *function)
{
    size_t length = function->data.function_decl.name_length;
    char *name = malloc(length + 1);
    if (name == NULL) return NULL;
    memcpy(name, function->data.function_decl.name, length);
    name[length] = '\0';
    return name;
}

void codegen_declare_function(Codegen *codegen, ASTNode *function)
{
    if (codegen == NULL || function == NULL ||
        (function->kind != AST_FUNCTION && function->kind != AST_EXTERN_FUNCTION)) return;
    char *name = codegen_function_name(function);
    if (name == NULL) {
        CODEGEN_ERROR(codegen, "fatal: failed to allocate function name\n");
        return;
    }
    if (LLVMGetNamedFunction(codegen->module, name) != NULL) {
        free(name);
        return;
    }

    size_t count = function->data.function_decl.parameter_count;
    LLVMTypeRef *parameter_types = count ? malloc(count * sizeof(*parameter_types)) : NULL;
    if (count && parameter_types == NULL) {
        free(name);
        CODEGEN_ERROR(codegen, "fatal: failed to allocate function parameter types\n");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        ASTNode *parameter = function->data.function_decl.parameters[i];
        if (parameter == NULL || parameter->kind != AST_VAR_DECL) {
            CODEGEN_ERROR(codegen, "codegen: malformed function parameter\n");
            free(parameter_types); free(name); return;
        }
        parameter_types[i] = llvm_type_from_ast(codegen, parameter->data.var_decl.type);
        if (parameter_types[i] == NULL || parameter->data.var_decl.type.dimension_count != 0) {
            if (parameter_types[i] != NULL)
                CODEGEN_ERROR(codegen, "codegen: array parameters are not supported yet\n");
            free(parameter_types); free(name); return;
        }
    }
    LLVMTypeRef return_type = codegen_return_type(codegen, function);
    if (return_type != NULL) {
        LLVMTypeRef function_type = LLVMFunctionType(return_type, parameter_types, (unsigned)count, 0);
        LLVMAddFunction(codegen->module, name, function_type);
    }
    free(parameter_types);
    free(name);
}

void codegen_function(Codegen *codegen, ASTNode *function)
{
    if (codegen == NULL || function == NULL || function->kind != AST_FUNCTION) return;
    /* AST_EXTERN_FUNCTION is declaration-only: codegen_declare_function handles it. */
    codegen_declare_function(codegen, function);
    if (codegen->has_error) return;

    char *name = codegen_function_name(function);
    if (name == NULL) {
        CODEGEN_ERROR(codegen, "fatal: failed to allocate function name\n");
        return;
    }
    LLVMValueRef llvm_function = LLVMGetNamedFunction(codegen->module, name);
    free(name);
    if (llvm_function == NULL) {
        CODEGEN_ERROR(codegen, "codegen: function declaration missing\n");
        return;
    }
    /* A declaration may be visited in the body pass only once. */
    if (LLVMCountBasicBlocks(llvm_function) != 0) {
        CODEGEN_ERROR(codegen, "codegen: function body generated more than once\n");
        return;
    }

    codegen->current_function = llvm_function;
    codegen_free_variables(codegen);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(codegen->context, llvm_function, "entry");
    LLVMPositionBuilderAtEnd(codegen->builder, entry);

    size_t count = function->data.function_decl.parameter_count;
    for (size_t i = 0; i < count; ++i) {
        ASTNode *parameter = function->data.function_decl.parameters[i];
        const char *source_name = parameter->data.var_decl.name;
        size_t length = parameter->data.var_decl.name_length;
        char *parameter_name = malloc(length + 1);
        CodegenVariable *variable = malloc(sizeof(*variable));
        if (parameter_name == NULL || variable == NULL) {
            free(parameter_name); free(variable);
            CODEGEN_ERROR(codegen, "fatal: failed to allocate parameter storage\n");
            return;
        }
        memcpy(parameter_name, source_name, length);
        parameter_name[length] = '\0';
        LLVMValueRef alloca = LLVMBuildAlloca(codegen->builder,
            LLVMTypeOf(LLVMGetParam(llvm_function, (unsigned)i)), parameter_name);
        LLVMBuildStore(codegen->builder, LLVMGetParam(llvm_function, (unsigned)i), alloca);

        memset(&variable->type, 0, sizeof(variable->type));
        switch (parameter->data.var_decl.type.kind) {
            case AST_TYPE_NUMBER:
                variable->type.kind = SEM_TYPE_NUMBER;
                break;

            case AST_TYPE_DECIMAL:
                variable->type.kind = SEM_TYPE_DECIMAL;
                break;

            case AST_TYPE_BOOL:
                variable->type.kind = SEM_TYPE_BOOL;
                break;

            case AST_TYPE_CHAR:
                variable->type.kind = SEM_TYPE_CHAR;
                break;

            case AST_TYPE_STRING:
                variable->type.kind = SEM_TYPE_STRING;
                break;

            case AST_TYPE_USER: {
                ASTType ptype = parameter->data.var_decl.type;
                if (codegen->semantic != NULL &&
                    semantic_lookup_enum(codegen->semantic, ptype.name, ptype.name_length) != NULL) {
                    variable->type.kind = SEM_TYPE_ENUM;
                    variable->type.name = ptype.name;
                    variable->type.name_length = ptype.name_length;
                } else {
                    variable->type.kind = SEM_TYPE_STRUCT;
                    variable->type.name = ptype.name;
                    variable->type.name_length = ptype.name_length;
                }
                break;
            }

            default:
                free(parameter_name);
                free(variable);
                CODEGEN_ERROR(codegen,
                    "codegen: unsupported parameter type\n");
                return;
        }
        variable->name = parameter_name;
        variable->alloca = alloca;
        variable->storage_type = LLVMTypeOf(LLVMGetParam(llvm_function, (unsigned)i));
        variable->next = codegen->variables;
        codegen->variables = variable;
    }

    codegen_block(codegen, function->data.function_decl.body);

    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(codegen->builder);
    if (current_block != NULL && LLVMGetBasicBlockTerminator(current_block) == NULL) {
        if (function->data.function_decl.return_type.kind == AST_TYPE_VOID &&
            function->data.function_decl.return_type.dimension_count == 0) {
            LLVMBuildRetVoid(codegen->builder);
        } else {
            LLVMTypeRef ret_type = codegen_return_type(codegen, function);
            if (ret_type != NULL) {
                LLVMBuildRet(codegen->builder, LLVMConstNull(ret_type));
            }
        }
    }
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
        CODEGEN_ERROR(codegen, "codegen: failed to generate if condition\n");
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

        CODEGEN_ERROR(codegen, "codegen: while condition must be bool\n"
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
        CODEGEN_ERROR(codegen, "codegen: break used outside a loop\n");
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
        CODEGEN_ERROR(codegen, "codegen: continue used outside a loop\n");
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
        CODEGEN_ERROR(codegen, "codegen: repeat count must be number\n");
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
        CODEGEN_ERROR(codegen, "codegen: for condition must be bool\n");

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

static LLVMValueRef codegen_lvalue_pointer(
    Codegen *codegen,
    ASTNode *node,
    LLVMTypeRef *out_type
)
{
    if (codegen == NULL || node == NULL) {
        return NULL;
    }

    if (node->kind == AST_IDENTIFIER) {
        CodegenVariable *variable = codegen_find_variable(
            codegen,
            node->data.identifier.start,
            node->data.identifier.length
        );

        if (variable == NULL || variable->storage_type == NULL) {
            CODEGEN_ERROR(codegen, "codegen: unknown variable '%.*s'\n",
                (int)node->data.identifier.length,
                node->data.identifier.start
            );
            return NULL;
        }

        if (out_type != NULL) {
            *out_type = variable->storage_type;
        }
        return variable->alloca;

    } else if (node->kind == AST_INDEX) {
        return codegen_array_element_pointer(codegen, node, out_type);

    } else if (node->kind == AST_MEMBER_ACCESS) {
        ASTNode *obj_node = node->data.member_access.object;
        const char *member_name = node->data.member_access.member;
        size_t member_len = node->data.member_access.member_length;

        LLVMTypeRef obj_type = NULL;
        LLVMValueRef obj_ptr = codegen_lvalue_pointer(codegen, obj_node, &obj_type);
        if (obj_ptr == NULL || obj_type == NULL) {
            return NULL;
        }

        if (LLVMGetTypeKind(obj_type) != LLVMStructTypeKind) {
            CODEGEN_ERROR(codegen, "codegen: member access on non-struct type\n");
            return NULL;
        }

        SemanticType sem_type;
        if (codegen->semantic == NULL ||
            !semantic_type_of(codegen->semantic, obj_node, &sem_type) ||
            sem_type.kind != SEM_TYPE_STRUCT) {
            CODEGEN_ERROR(codegen, "codegen: failed to determine struct type for member access\n");
            return NULL;
        }

        size_t field_idx = 0;
        ASTType field_ast_type;
        if (!semantic_struct_field_index(
                codegen->semantic,
                sem_type.name,
                sem_type.name_length,
                member_name,
                member_len,
                &field_idx,
                &field_ast_type)) {
            CODEGEN_ERROR(codegen, "codegen: unknown struct field '%.*s'\n",
                (int)member_len, member_name);
            return NULL;
        }

        LLVMTypeRef field_llvm_type = llvm_type_from_ast(codegen, field_ast_type);
        if (field_llvm_type == NULL) {
            return NULL;
        }

        LLVMValueRef field_ptr = LLVMBuildStructGEP2(
            codegen->builder,
            obj_type,
            obj_ptr,
            (unsigned)field_idx,
            "field.gep"
        );

        if (out_type != NULL) {
            *out_type = field_llvm_type;
        }
        return field_ptr;

    } else {
        CODEGEN_ERROR(codegen, "codegen: unsupported lvalue\n");
        return NULL;
    }
}

static LLVMValueRef codegen_array_element_pointer(
    Codegen *codegen,
    ASTNode *expression,
    LLVMTypeRef *element_type
)
{
    if (expression == NULL ||
        expression->kind != AST_INDEX ||
        element_type == NULL) {
        return NULL;
    }

    ASTNode *array_node = expression->data.index.array;
    ASTNode *index_node = expression->data.index.index;

    LLVMValueRef array_pointer = NULL;
    LLVMTypeRef array_type = NULL;

    array_pointer = codegen_lvalue_pointer(
        codegen,
        array_node,
        &array_type
    );

    if (array_pointer == NULL || array_type == NULL) {
        return NULL;
    }

    if (LLVMGetTypeKind(array_type) != LLVMArrayTypeKind) {
        CODEGEN_ERROR(codegen, "codegen: indexed expression is not an array\n");
        return NULL;
    }

    LLVMValueRef index = codegen_expression(codegen, index_node);

    if (index == NULL ||
        LLVMTypeOf(index) != llvm_number_type(codegen)) {
        CODEGEN_ERROR(codegen, "codegen: invalid array index value\n");
        return NULL;
    }

    unsigned length = LLVMGetArrayLength(array_type);
    LLVMTypeRef index_type = LLVMTypeOf(index);

    /*
     * Runtime bounds check: index must satisfy 0 <= index < length.
     */
    LLVMValueRef zero = LLVMConstInt(index_type, 0, 0);
    LLVMValueRef upper_bound = LLVMConstInt(
        index_type,
        length,
        0
    );

    LLVMValueRef below_zero = LLVMBuildICmp(
        codegen->builder,
        LLVMIntSLT,
        index,
        zero,
        "array.index.negative"
    );

    LLVMValueRef above_bound = LLVMBuildICmp(
        codegen->builder,
        LLVMIntSGE,
        index,
        upper_bound,
        "array.index.too.large"
    );

    LLVMValueRef out_of_bounds = LLVMBuildOr(
        codegen->builder,
        below_zero,
        above_bound,
        "array.index.out.of.bounds"
    );

    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlockInContext(
        codegen->context,
        codegen->current_function,
        "array.bounds.fail"
    );

    LLVMBasicBlockRef valid_block = LLVMAppendBasicBlockInContext(
        codegen->context,
        codegen->current_function,
        "array.bounds.valid"
    );

    LLVMBuildCondBr(
        codegen->builder,
        out_of_bounds,
        fail_block,
        valid_block
    );

    LLVMPositionBuilderAtEnd(codegen->builder, fail_block);

    LLVMTypeRef abort_type = LLVMFunctionType(
        LLVMVoidTypeInContext(codegen->context),
        NULL,
        0,
        0
    );

    LLVMValueRef abort_function = LLVMGetNamedFunction(
        codegen->module,
        "abort"
    );

    if (abort_function == NULL) {
        abort_function = LLVMAddFunction(
            codegen->module,
            "abort",
            abort_type
        );
    }

    LLVMBuildCall2(
        codegen->builder,
        abort_type,
        abort_function,
        NULL,
        0,
        ""
    );

    LLVMBuildUnreachable(codegen->builder);

    LLVMPositionBuilderAtEnd(codegen->builder, valid_block);

    LLVMValueRef indices[] = {
        LLVMConstInt(llvm_number_type(codegen), 0, 0),
        index
    };

    LLVMValueRef element_pointer = LLVMBuildGEP2(
        codegen->builder,
        array_type,
        array_pointer,
        indices,
        2,
        "array.element.ptr"
    );

    *element_type = LLVMGetElementType(array_type);

    return element_pointer;
}