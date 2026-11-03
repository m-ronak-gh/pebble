import System;

function number main() {
    number c argsCount();
    print c;

    string a0 argsGet(0);
    print a0;

    string a1 argsGet(1);
    print a1;

    string a2 argsGet(2);
    print a2;

    string neg argsGet(-1);
    print length(neg);

    string outOfBounds argsGet(3);
    print length(outOfBounds);

    return 0;
}
