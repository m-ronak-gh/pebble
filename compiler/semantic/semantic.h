#ifndef PEBBLE_SEMANTIC_H
#define PEBBLE_SEMANTIC_H

#include <stddef.h>
#include "../ast/ast.h"

typedef struct SemanticContext SemanticContext;

typedef enum {
    SEM_TYPE_ERROR,
    SEM_TYPE_VOID,
    SEM_TYPE_NUMBER,
    SEM_TYPE_DECIMAL,
    SEM_TYPE_BOOL,
    SEM_TYPE_CHAR,
    SEM_TYPE_STRING,
    SEM_TYPE_STRUCT,
    SEM_TYPE_ENUM,
    SEM_TYPE_ARRAY,
    SEM_TYPE_POINTER
} SemanticTypeKind;

typedef struct {
    SemanticTypeKind kind;
    const char *name;
    size_t name_length;
    const size_t *array_lengths;
    size_t array_dimension_count;
} SemanticType;

// Returns a semantic context on success, NULL on semantic error. */
SemanticContext *semantic_analyze(ASTNode *program);
void semantic_free(SemanticContext *context);

// Useful to later compiler stages. The returned pointer remains valid until
// semantic_free() is called on the context. */
int semantic_type_of(
    const SemanticContext *context,
    const ASTNode *node,
    SemanticType *out_type
);

const char *semantic_type_name(SemanticTypeKind kind);

const ASTNode *semantic_get_program(const SemanticContext *context);

const ASTNode *semantic_lookup_struct(
    const SemanticContext *context,
    const char *name,
    size_t length
);

const ASTNode *semantic_lookup_enum(
    const SemanticContext *context,
    const char *name,
    size_t length
);

int semantic_lookup_enum_member(
    const SemanticContext *context,
    const char *name,
    size_t length,
    long long *out_value
);

int semantic_struct_field_index(
    const SemanticContext *context,
    const char *struct_name,
    size_t struct_name_len,
    const char *field_name,
    size_t field_name_len,
    size_t *out_index,
    ASTType *out_field_type
);

#endif // PEBBLE_SEMANTIC_H */
