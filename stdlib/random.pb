/*
 * Pebble Standard Library: random
 *
 * Provides pseudo-random number generation.
 */

extern function void pebblebit_random_seed(number n);
extern function number pebblebit_random_int(number min_val, number max_val);

function void seed(number n) {
    pebblebit_random_seed(n);
}

function number randomInt(number min, number max) {
    return pebblebit_random_int(min, max);
}
