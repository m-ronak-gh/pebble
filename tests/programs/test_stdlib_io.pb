import "io.pb";

function number main() {
    bool ok writeFile("pebblebit_test_io_driver.txt", "Pebblebit File Content");
    if (ok) {
        print 1;
    } else {
        print 0;
    }

    string content readFile("pebblebit_test_io_driver.txt");
    print content;

    return 0;
}
