#include "pebble_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif
/* =========================================================================
 * Core allocation
 * ========================================================================= */

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

/* =========================================================================
 * Pebble string ABI
 *
 * The Pebble compiler emits strings as LLVM struct { i64, ptr } (length
 * first, then pointer). PebbleString is defined in pebble_runtime.h.
 * ========================================================================= */

/* Helper: allocate a new PebbleString from a C nul-terminated string. */
static PebbleString make_pebble_string(const char *s, int64_t len)
{
    PebbleString result;
    result.ptr = (char *)malloc((size_t)(len + 1));
    if (result.ptr == NULL) {
        result.len = 0;
        result.ptr = NULL;
        return result;
    }
    if (len > 0 && s != NULL) {
        memcpy(result.ptr, s, (size_t)len);
    }
    result.ptr[len] = '\0';
    result.len = len;
    return result;
}

/* =========================================================================
 * io module
 * ========================================================================= */

/*
 * pebble_io_print_string(data, length)
 * Prints exactly length bytes followed by a newline.
 */
void pebble_io_print_string(const char *data, int64_t length)
{
    if (data != NULL && length > 0) {
        fwrite(data, 1, (size_t)length, stdout);
    }

    fputc('\n', stdout);
}

void pebble_io_input(PebbleString *result)
{
    if (result == NULL) return;

    char buf[4096];

    if (fgets(buf, (int)sizeof(buf), stdin) == NULL) {
        *result = make_pebble_string("", 0);
        return;
    }

    int64_t len = (int64_t)strlen(buf);

    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    }

    if (len > 0 && buf[len - 1] == '\r') {
        buf[--len] = '\0';
    }

    *result = make_pebble_string(buf, len);
}

/*
 * pebble_io_readFile(path) -> string
 */
void pebble_io_readFile(
    PebbleString *result,
    const PebbleString *path
)
{
    if (result == NULL) return;

    if (path == NULL || path->ptr == NULL || path->len <= 0) {
        *result = make_pebble_string("", 0);
        return;
    }

    FILE *f = fopen(path->ptr, "rb");

    if (f == NULL) {
        *result = make_pebble_string("", 0);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        *result = make_pebble_string("", 0);
        return;
    }

    char *buf = malloc((size_t)size + 1);

    if (buf == NULL) {
        fclose(f);
        *result = make_pebble_string("", 0);
        return;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);

    buf[read] = '\0';

    result->ptr = buf;
    result->len = (int64_t)read;
}

/*
 * pebble_io_writeFile(path, contents) -> number
 */
int64_t pebble_io_writeFile(
    const PebbleString *path,
    const PebbleString *contents
)
{
    if (path == NULL || path->ptr == NULL || path->len <= 0) {
        return 0;
    }

    FILE *f = fopen(path->ptr, "wb");

    if (f == NULL) {
        return 0;
    }

    size_t written = 0;

    if (contents != NULL &&
        contents->ptr != NULL &&
        contents->len > 0) {
        written = fwrite(
            contents->ptr,
            1,
            (size_t)contents->len,
            f
        );
    }

    fclose(f);

    return written == (contents != NULL && contents->len > 0
        ? (size_t)contents->len
        : 0) ? 1 : 0;
}

/* =========================================================================
 * string module
 * ========================================================================= */

void pebble_string_concat(
    PebbleString *result,
    const PebbleString *a,
    const PebbleString *b
)
{
    if (result == NULL) return;

    int64_t a_len = a != NULL && a->ptr != NULL && a->len > 0
        ? a->len : 0;
    int64_t b_len = b != NULL && b->ptr != NULL && b->len > 0
        ? b->len : 0;

    if (a_len > INT64_MAX - b_len - 1) {
        *result = make_pebble_string("", 0);
        return;
    }

    int64_t new_len = a_len + b_len;

    char *buf = malloc((size_t)new_len + 1);

    if (buf == NULL) {
        *result = make_pebble_string("", 0);
        return;
    }

    if (a_len > 0) {
        memcpy(buf, a->ptr, (size_t)a_len);
    }

    if (b_len > 0) {
        memcpy(buf + a_len, b->ptr, (size_t)b_len);
    }

    buf[new_len] = '\0';

    result->ptr = buf;
    result->len = new_len;
}

void pebble_string_substring(
    PebbleString *result,
    const PebbleString *s,
    int64_t start,
    int64_t len
)
{
    if (result == NULL) return;

    if (s == NULL || s->ptr == NULL ||
        s->len <= 0 || start < 0 || start >= s->len) {
        *result = make_pebble_string("", 0);
        return;
    }

    if (len < 0) len = 0;

    if (len > s->len - start) {
        len = s->len - start;
    }

    *result = make_pebble_string(s->ptr + start, len);
}

void pebble_string_charAt(
    PebbleString *result,
    const PebbleString *s,
    int64_t i
)
{
    if (result == NULL) return;

    if (s == NULL || s->ptr == NULL ||
        i < 0 || i >= s->len) {
        *result = make_pebble_string("", 0);
        return;
    }

    *result = make_pebble_string(s->ptr + i, 1);
}

int64_t pebble_string_toNumber(const PebbleString *s)
{
    if (s == NULL || s->ptr == NULL || s->len <= 0) {
        return 0;
    }

    return (int64_t)strtoll(s->ptr, NULL, 10);
}

void pebble_string_toString_number(
    PebbleString *result,
    int64_t n
)
{
    if (result == NULL) return;

    char buf[32];

    int written = snprintf(
        buf,
        sizeof(buf),
        "%lld",
        (long long)n
    );

    if (written < 0) written = 0;

    *result = make_pebble_string(buf, (int64_t)written);
}

void pebble_string_toString_decimal(
    PebbleString *result,
    double n
)
{
    if (result == NULL) return;

    char buf[64];

    int written = snprintf(buf, sizeof(buf), "%g", n);

    if (written < 0) written = 0;

    *result = make_pebble_string(buf, (int64_t)written);
}

/* =========================================================================
 * math module
 * ========================================================================= */

int64_t pebble_math_abs(int64_t n)
{
    return n < 0 ? -n : n;
}

int64_t pebble_math_min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

int64_t pebble_math_max(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

double pebble_math_sqrt(double x)
{
    return __builtin_sqrt(x);
}

double pebble_math_pow(double base, double exp_val)
{
#if defined(__unix__) || defined(__APPLE__)
    static double (*real_pow)(double, double) = NULL;

    if (real_pow == NULL) {
        void *h = dlopen("libm.so.6", RTLD_LAZY);
        if (!h) h = dlopen("libm.so", RTLD_LAZY);
        if (!h) h = dlopen("libm.dylib", RTLD_LAZY);

        if (h) {
            real_pow = (double (*)(double, double))dlsym(h, "pow");
        }
    }

    if (real_pow != NULL) {
        return real_pow(base, exp_val);
    }
#endif

    if (exp_val == 0.0) return 1.0;
    if (base == 0.0) return 0.0;

    if (base > 0.0) {
        return exp(exp_val * log(base));
    }

    if ((double)(int64_t)exp_val == exp_val) {
        int64_t n = (int64_t)exp_val;
        double r = exp(exp_val * log(-base));
        return (n % 2 != 0) ? -r : r;
    }

    return NAN;
}

double pebble_math_floor(double x)
{
    double integral;
    double fractional = modf(x, &integral);

    return (fractional < 0.0) ? integral - 1.0 : integral;
}

double pebble_math_ceil(double x)
{
    double integral;
    double fractional = modf(x, &integral);

    return (fractional > 0.0) ? integral + 1.0 : integral;
}

/* =========================================================================
 * time module
 * ========================================================================= */

/*
 * pebble_time_now() -> number (milliseconds since Unix epoch)
 */
int64_t pebble_time_now(void)
{
#if defined(_WIN32)

    FILETIME ft;
    ULARGE_INTEGER ticks;

    GetSystemTimeAsFileTime(&ft);

    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;

    /*
     * Windows FILETIME counts 100-nanosecond intervals
     * since January 1, 1601.
     *
     * Unix time starts January 1, 1970.
     */
    const uint64_t WINDOWS_TO_UNIX_EPOCH = 116444736000000000ULL;

    if (ticks.QuadPart < WINDOWS_TO_UNIX_EPOCH) {
        return 0;
    }

    return (int64_t)(
        (ticks.QuadPart - WINDOWS_TO_UNIX_EPOCH) / 10000ULL
    );

#else

    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000
         + (int64_t)(ts.tv_nsec / 1000000);

#endif
}

/*
 * pebble_time_sleep(ms) -- sleep for ms milliseconds
 */
void pebble_time_sleep(int64_t ms)
{
    if (ms <= 0) {
        return;
    }

#if defined(_WIN32)

    /*
     * Sleep() accepts a DWORD. Split very long durations into
     * chunks, avoiding INFINITE (0xFFFFFFFF).
     */
    uint64_t remaining = (uint64_t)ms;

    while (remaining > 0) {
        DWORD chunk = remaining >= (uint64_t)INFINITE
                    ? INFINITE - 1
                    : (DWORD)remaining;

        Sleep(chunk);
        remaining -= chunk;
    }

#else

    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;

    nanosleep(&ts, NULL);

#endif
}

/* =========================================================================
 * random module
 * ========================================================================= */

void pebble_random_seed(int64_t n)
{
    srand((unsigned int)n);
}

/*
 * pebble_random_int(min, max) -> number
 * Returns a random integer in [min, max] (inclusive).
 */
int64_t pebble_random_int(int64_t min_val, int64_t max_val)
{
    if (min_val > max_val) {
        int64_t tmp = min_val;
        min_val = max_val;
        max_val = tmp;
    }
    int64_t range = max_val - min_val + 1;
    return min_val + (int64_t)(rand() % (int)range);
}

/* =========================================================================
 * system module
 * ========================================================================= */

static int    g_argc = 0;
static char **g_argv = NULL;

void pebble_runtime_init(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;
}

int64_t pebble_system_args_count(void)
{
    int64_t user_count = (int64_t)g_argc - 1;
    return user_count < 0 ? 0 : user_count;
}

void pebble_system_args_get(PebbleString *result, int64_t i)
{
    if (result == NULL) return;

    int64_t user_count = (int64_t)g_argc - 1;

    if (i < 0 || i >= user_count || g_argv == NULL) {
        *result = make_pebble_string("", 0);
        return;
    }

    const char *s = g_argv[i + 1];

    *result = make_pebble_string(s, (int64_t)strlen(s));
}
