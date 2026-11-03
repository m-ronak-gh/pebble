/*
 * Pebble Standard Library: system
 *
 * Provides access to command-line arguments.
 * Note: Since Pebble v1 has only fixed-size arrays and does not support
 * returning dynamic arrays (string[]), system provides argsCount() and
 * argsGet(i) to inspect CLI arguments cleanly without dynamic array overhead.
 */

extern function number pebble_system_args_count();
extern function string pebble_system_args_get(number i);

function number argsCount() {
    return pebble_system_args_count();
}

function string argsGet(number i) {
    return pebble_system_args_get(i);
}
