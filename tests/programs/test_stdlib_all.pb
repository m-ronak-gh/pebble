import "string.pb";
import "math.pb";
import "time.pb";
import "random.pb";

function number main() {
    number n abs(-123);
    string s toString(n);
    string full concat("Result: ", s);
    print full;

    seed(now());
    number r randomInt(1, 100);
    number m max(r, 0);
    if (m >= 1) {
        print 1;
    } else {
        print 0;
    }

    return 0;
}
