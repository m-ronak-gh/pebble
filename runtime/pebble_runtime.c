#include "pebble_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void *pebble_alloc(int64_t size)
{
    if (size <= 0) {
        return NULL;
    }

    if ((uint64_t)size > SIZE_MAX) {
        return NULL;
    }

    return malloc((size_t)size);
}

void pebble_free(void *pointer)
{
    free(pointer);
}
