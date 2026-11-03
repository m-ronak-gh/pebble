import "io.pb";

function number main() {
    bool ok writeFile("/tmp/pebble_test_io_driver.txt", "Pebble File Content");
    if (ok) {
        print 1;
    } else {
        print 0;
    }

    string content readFile("/tmp/pebble_test_io_driver.txt");
    print content;

    return 0;
}
