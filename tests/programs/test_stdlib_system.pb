import "system.pb";

function number main() {
    number c argsCount();
    if (c >= 1) {
        print 1;
    } else {
        print 0;
    }

    string arg0 argsGet(0);
    if (length(arg0) > 0) {
        print 1;
    } else {
        print 0;
    }

    return 0;
}
