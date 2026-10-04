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

#endif // PEBBLE_SEMANTIC_H */
