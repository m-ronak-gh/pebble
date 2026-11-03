/*
 * Pebble Standard Library: time
 *
 * Provides time measurement and sleep functions.
 */

extern function number pebble_time_now();
extern function void pebble_time_sleep(number ms);

function number now() {
    return pebble_time_now();
}

function void sleep(number ms) {
    pebble_time_sleep(ms);
}
