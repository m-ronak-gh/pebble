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
void pebble_io_print_string(const char *data, int64_t length);
void pebble_io_input(PebbleString *result);
void pebble_io_readFile(PebbleString *result, const PebbleString *path);
int64_t pebble_io_writeFile(
    const PebbleString *path,
    const PebbleString *contents
);

/* String module support */
void pebble_string_concat(
    PebbleString *result,
    const PebbleString *a,
    const PebbleString *b
);
void pebble_string_substring(
    PebbleString *result,
    const PebbleString *s,
    int64_t start,
    int64_t len
);
void pebble_string_charAt(
    PebbleString *result,
    const PebbleString *s,
    int64_t i
);
int64_t pebble_string_toNumber(const PebbleString *s);
void pebble_string_toString_number(PebbleString *result, int64_t n);
void pebble_string_toString_decimal(PebbleString *result, double n);

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
void pebble_system_args_get(PebbleString *result, int64_t i);

#ifdef __cplusplus
}
#endif

#endif
