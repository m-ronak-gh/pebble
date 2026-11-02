#ifndef PEBBLE_RUNTIME_H
#define PEBBLE_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocate heap memory.
 *
 * Returns NULL if:
 * - size is zero or negative;
 * - size cannot be represented by the host size_t;
 * - allocation fails.
 *
 * The returned memory must eventually be released with pebble_free().
 */
void *pebble_alloc(int64_t size);

/*
 * Release memory previously returned by pebble_alloc().
 * Passing NULL is safe.
 */
void pebble_free(void *pointer);

#ifdef __cplusplus
}
#endif

#endif
