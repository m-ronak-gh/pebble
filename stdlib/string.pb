/*
 * Pebble Standard Library: string
 *
 * Provides string manipulation functions.
 */

extern function string pebble_string_concat(string a, string b);
extern function string pebble_string_substring(string s, number start, number len);
extern function string pebble_string_charAt(string s, number i);
extern function number pebble_string_toNumber(string s);
extern function string pebble_string_toString_number(number n);

function number length(string s) {
    return length(s);
}

function string concat(string a, string b) {
    return pebble_string_concat(a, b);
}

function string substring(string s, number start, number len) {
    return pebble_string_substring(s, start, len);
}

function string charAt(string s, number i) {
    return pebble_string_charAt(s, i);
}

function number toNumber(string s) {
    return pebble_string_toNumber(s);
}

function string toString(number n) {
    return pebble_string_toString_number(n);
}
