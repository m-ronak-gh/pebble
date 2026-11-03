/*
 * Pebble Standard Library: math
 *
 * Provides common mathematical functions.
 */

extern function decimal pebble_math_sqrt(decimal x);
extern function decimal pebble_math_pow(decimal base, decimal exp_val);
extern function decimal pebble_math_floor(decimal x);
extern function decimal pebble_math_ceil(decimal x);

function number abs(number n) {
    if (n < 0) {
        return -n;
    }
    return n;
}

function number min(number a, number b) {
    if (a < b) {
        return a;
    }
    return b;
}

function number max(number a, number b) {
    if (a > b) {
        return a;
    }
    return b;
}

function decimal sqrt(decimal x) {
    return pebble_math_sqrt(x);
}

function decimal pow(decimal base, decimal exp_val) {
    return pebble_math_pow(base, exp_val);
}

function decimal floor(decimal x) {
    return pebble_math_floor(x);
}

function decimal ceil(decimal x) {
    return pebble_math_ceil(x);
}
