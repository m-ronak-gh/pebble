
#!/usr/bin/env bash
set -eu

COMPILER="${1:-./peb}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

PASS=0
FAIL=0

pass() {
    echo "PASS: $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "FAIL: $1"
    FAIL=$((FAIL + 1))
}

# Test 1: basic native compilation and execution
cat > "$TMP_DIR/basic.pb" <<'EOF'
function number main() {
    print 42;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/basic.pb" -o "$TMP_DIR/basic" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/basic")" = "42" ]; then
    pass "basic compile and execute"
else
    fail "basic compile and execute"
    cat "$TMP_DIR/log"
fi

# Test 2: nested control flow, arithmetic, and arrays
cat > "$TMP_DIR/control.pb" <<'EOF'
function number main() {
    number[5] values;
    number sum 0;

    for (number i 0; i < 5; i = i + 1) {
        values[i] = i * 2 + 1;
        sum = sum + values[i];
    }

    print sum;

    if (sum == 25) {
        print 1;
    } else {
        print 0;
    }

    number countdown 3;
    while (countdown > 0) {
        print countdown;
        countdown = countdown - 1;
    }

    repeat (3) {
        print 7;
    }

    return 0;
}
EOF

EXPECTED=$(printf '25\n1\n3\n2\n1\n7\n7\n7')
if "$COMPILER" build "$TMP_DIR/control.pb" -o "$TMP_DIR/control" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/control")" = "$EXPECTED" ]; then
    pass "control flow and arithmetic"
else
    fail "control flow and arithmetic"
    cat "$TMP_DIR/log"
fi

# Test 3: character arrays and character printing
cat > "$TMP_DIR/chars.pb" <<'EOF'
function number main() {
    char[5] hello;
    hello[0] = 'H';
    hello[1] = 'e';
    hello[2] = 'l';
    hello[3] = 'l';
    hello[4] = 'o';

    print hello[0];
    print hello[1];
    print hello[2];
    print hello[3];
    print hello[4];
    return 0;
}
EOF

EXPECTED=$(printf 'H\ne\nl\nl\no')
if "$COMPILER" build "$TMP_DIR/chars.pb" -o "$TMP_DIR/chars" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/chars")" = "$EXPECTED" ]; then
    pass "character arrays"
else
    fail "character arrays"
    cat "$TMP_DIR/log"
fi

# Test 4: semantic type mismatch must fail
cat > "$TMP_DIR/bad-type.pb" <<'EOF'
function number main() {
    number value true;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/bad-type.pb" -o "$TMP_DIR/bad-type" \
    >"$TMP_DIR/log" 2>&1; then
    fail "reject type mismatch"
else
    pass "reject type mismatch"
fi

if [ -e "$TMP_DIR/bad-type" ]; then
    fail "failed type-check left an executable"
else
    pass "no executable after type error"
fi

# Test 5: missing main must fail
cat > "$TMP_DIR/no-main.pb" <<'EOF'
function number calculate() {
    return 10;
}
EOF

if "$COMPILER" build "$TMP_DIR/no-main.pb" -o "$TMP_DIR/no-main" \
    >"$TMP_DIR/log" 2>&1; then
    fail "reject missing main"
else
    pass "reject missing main"
fi

# Test 6: constant out-of-bounds index must fail
cat > "$TMP_DIR/bounds.pb" <<'EOF'
function number main() {
    number[3] values;
    values[3] = 99;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/bounds.pb" -o "$TMP_DIR/bounds" \
    >"$TMP_DIR/log" 2>&1; then
    fail "reject constant out-of-bounds access"
else
    pass "reject constant out-of-bounds access"
fi

# Test 7: string literal and variable printing.
cat > "$TMP_DIR/string.pb" <<'EOF'
function number main() {
    string message "Hello";
    string empty "";
    print message;
    print empty;
    print "First";
    print "Second";
    print identity("Function round trip");
    return 0;
}

function string identity(string value) {
    return value;
}
EOF

# Test 8: --help and -h should print usage successfully
if "$COMPILER" --help >"$TMP_DIR/log" 2>&1 &&
    grep -q "Usage:" "$TMP_DIR/log"; then
    pass "long help option"
else
    fail "long help option"
    cat "$TMP_DIR/log"
fi

if "$COMPILER" -h >"$TMP_DIR/log" 2>&1 &&
    grep -q "Usage:" "$TMP_DIR/log"; then
    pass "short help option"
else
    fail "short help option"
    cat "$TMP_DIR/log"
fi

# Test 9: missing source and invalid options must be rejected
if "$COMPILER" >"$TMP_DIR/log" 2>&1; then
    fail "reject missing command-line arguments"
else
    pass "reject missing command-line arguments"
fi

if "$COMPILER" --unknown >"$TMP_DIR/log" 2>&1; then
    fail "reject unknown option"
else
    pass "reject unknown option"
fi

if "$COMPILER" build "$TMP_DIR/basic.pb" -o \
    >"$TMP_DIR/log" 2>&1; then
    fail "reject missing output path"
else
    if grep -q "requires an output path" "$TMP_DIR/log"; then
        pass "reject missing output path"
    else
        fail "missing output path has clear diagnostic"
        cat "$TMP_DIR/log"
    fi
fi

if "$COMPILER" build "$TMP_DIR/basic.pb" -o "$TMP_DIR/extra" extra \
    >"$TMP_DIR/log" 2>&1; then
    fail "reject extra command-line argument"
else
    pass "reject extra command-line argument"
fi

# Test 10: a failed rebuild must remove the stale executable
cat > "$TMP_DIR/rebuild.pb" <<'EOF'
function number main() {
    print 123;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/rebuild.pb" -o "$TMP_DIR/rebuild" \
    >"$TMP_DIR/log" 2>&1 &&
    [ -x "$TMP_DIR/rebuild" ]; then

    cat > "$TMP_DIR/rebuild.pb" <<'EOF'
function number main() {
    number value true;
    return 0;
}
EOF

    if "$COMPILER" build "$TMP_DIR/rebuild.pb" -o "$TMP_DIR/rebuild" \
        >"$TMP_DIR/log" 2>&1; then
        fail "reject invalid rebuild"
    else
        pass "reject invalid rebuild"
    fi

    if [ ! -e "$TMP_DIR/rebuild" ]; then
        pass "failed rebuild removes stale executable"
    else
        fail "failed rebuild leaves stale executable"
    fi
else
    fail "prepare stale executable regression test"
    cat "$TMP_DIR/log"
fi

# Test 7: string literals and variables can be printed.
EXPECTED=$(printf 'Hello\n\nFirst\nSecond\nFunction round trip')

if "$COMPILER" build "$TMP_DIR/string.pb" -o "$TMP_DIR/string" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/string")" = "$EXPECTED" ]; then
    pass "string literal and variable printing"
else
    fail "string literal and variable printing"
    cat "$TMP_DIR/log"
fi

echo

# FUNCTION_CALL_REGRESSION_TESTS

# Test: parameters, return values, forward calls, and recursion.
cat > "$TMP_DIR/functions.pb" <<'EOF'
function number main() {
    print add(19, 23);
    print later(5);
    print factorial(5);
    return 0;
}

function number add(number left, number right) {
    return left + right;
}

function number later(number value) {
    return value * 2;
}

function number factorial(number n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}
EOF

EXPECTED=$(printf '42\n10\n120')
if "$COMPILER" build "$TMP_DIR/functions.pb" -o "$TMP_DIR/functions" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/functions")" = "$EXPECTED" ]; then
    pass "function parameters, forward calls, and recursion"
else
    fail "function parameters, forward calls, and recursion"
    cat "$TMP_DIR/log"
fi

# Test: zero-argument function calls.
cat > "$TMP_DIR/zero_arg.pb" <<'EOF'
function number main() {
    print get_value();
    return 0;
}

function number get_value() {
    return 42;
}
EOF

if "$COMPILER" build "$TMP_DIR/zero_arg.pb" -o "$TMP_DIR/zero_arg" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/zero_arg")" = "42" ]; then
    pass "zero-argument function call"
else
    fail "zero-argument function call"
    cat "$TMP_DIR/log"
fi

# Test: reject incorrect argument count.
cat > "$TMP_DIR/bad_arg_count.pb" <<'EOF'
function number main() {
    print add(10);
    return 0;
}

function number add(number a, number b) {
    return a + b;
}
EOF

if ! "$COMPILER" build "$TMP_DIR/bad_arg_count.pb" \
    -o "$TMP_DIR/bad_arg_count" >"$TMP_DIR/log" 2>&1 &&
    grep -q "wrong number of arguments" "$TMP_DIR/log" &&
    [ ! -e "$TMP_DIR/bad_arg_count" ]; then
    pass "reject incorrect function argument count"
else
    fail "reject incorrect function argument count"
    cat "$TMP_DIR/log"
fi

# Test: reject incorrect argument types.
cat > "$TMP_DIR/bad_arg_type.pb" <<'EOF'
function number main() {
    print add(true, 10);
    return 0;
}

function number add(number a, number b) {
    return a + b;
}
EOF

if ! "$COMPILER" build "$TMP_DIR/bad_arg_type.pb" \
    -o "$TMP_DIR/bad_arg_type" >"$TMP_DIR/log" 2>&1 &&
    grep -q "argument type does not match parameter type" "$TMP_DIR/log" &&
    [ ! -e "$TMP_DIR/bad_arg_type" ]; then
    pass "reject incorrect function argument type"
else
    fail "reject incorrect function argument type"
    cat "$TMP_DIR/log"
fi

echo "Driver tests: $PASS passed, $FAIL failed"

[ "$FAIL" -eq 0 ]

