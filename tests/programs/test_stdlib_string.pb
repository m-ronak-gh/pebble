import "string.pb";

function number main() {
    string a "Hello";
    string b " World";
    string c concat(a, b);
    print c;
    print length(c);

    string sub substring(c, 0, 5);
    print sub;

    string ch charAt(c, 1);
    print ch;

    number n toNumber("12345");
    print n;

    string s toString(987);
    print s;

    return 0;
}
