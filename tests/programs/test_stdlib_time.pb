import "time.pb";

function number main() {
    number t1 now();
    if (t1 > 0) {
        print 1;
    } else {
        print 0;
    }

    sleep(10);

    number t2 now();
    if (t2 >= t1) {
        print 1;
    } else {
        print 0;
    }

    return 0;
}
