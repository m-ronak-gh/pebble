import "random.pb";

function number main() {
    seed(999);
    number r1 randomInt(10, 50);
    if (r1 >= 10 && r1 <= 50) {
        print 1;
    } else {
        print 0;
    }

    seed(999);
    number r2 randomInt(10, 50);
    if (r1 == r2) {
        print 1;
    } else {
        print 0;
    }

    return 0;
}
