
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

# Test 7: unsupported string printing must fail honestly
cat > "$TMP_DIR/string.pb" <<'EOF'
function number main() {
    print "Hello World!";
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/string.pb" -o "$TMP_DIR/string" \
    >"$TMP_DIR/log" 2>&1; then
    fail "reject unsupported string printing"
else
    pass "unsupported string printing reports failure"
fi

if [ -e "$TMP_DIR/string" ]; then
    fail "failed code generation left an executable"
else
    pass "no executable after code-generation error"
fi

echo
echo "Driver tests: $PASS passed, $FAIL failed"

[ "$FAIL" -eq 0 ]

