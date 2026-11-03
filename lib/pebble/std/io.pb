/*
 * Pebble Standard Library: io
 *
 * Provides basic input and file operations.
 */

extern function string pebble_io_input();
extern function string pebble_io_readFile(string path);
extern function number pebble_io_writeFile(string path, string contents);

function string input() {
    return pebble_io_input();
}

function string readFile(string path) {
    return pebble_io_readFile(path);
}

function bool writeFile(string path, string contents) {
    return pebble_io_writeFile(path, contents) == 1;
}
