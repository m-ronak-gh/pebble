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

/*
 * Pebble string ABI layout matching LLVM { i64, ptr }.
 */
typedef struct {
    int64_t len;
    char   *ptr;
} PebbleString;

/* IO module support */
PebbleString pebble_io_input(void);
PebbleString pebble_io_readFile(PebbleString path);
int64_t pebble_io_writeFile(PebbleString path, PebbleString contents);

/* String module support */
PebbleString pebble_string_concat(PebbleString a, PebbleString b);
PebbleString pebble_string_substring(PebbleString s, int64_t start, int64_t len);
PebbleString pebble_string_charAt(PebbleString s, int64_t i);
int64_t pebble_string_toNumber(PebbleString s);
PebbleString pebble_string_toString_number(int64_t n);
PebbleString pebble_string_toString_decimal(double n);

/* Math module support */
int64_t pebble_math_abs(int64_t n);
int64_t pebble_math_min(int64_t a, int64_t b);
int64_t pebble_math_max(int64_t a, int64_t b);
double pebble_math_sqrt(double x);
double pebble_math_pow(double base, double exp_val);
double pebble_math_floor(double x);
double pebble_math_ceil(double x);

/* Time module support */
int64_t pebble_time_now(void);
void pebble_time_sleep(int64_t ms);

/* Random module support */
void pebble_random_seed(int64_t n);
int64_t pebble_random_int(int64_t min_val, int64_t max_val);

/* System module support */
void pebble_runtime_init(int argc, char **argv);
int64_t pebble_system_args_count(void);
PebbleString pebble_system_args_get(int64_t i);

#ifdef __cplusplus
}
#endif

#endif
