
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

# Test: allocate and free heap memory at runtime.
cat > "$TMP_DIR/allocator.pb" <<'EOF'
function number main() {
    free(alloc(64));
    print "allocator test passed";
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/allocator.pb" -o "$TMP_DIR/allocator" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/allocator")" = "allocator test passed" ]; then
    pass "runtime allocation and deallocation"
else
    fail "runtime allocation and deallocation"
    cat "$TMP_DIR/log"
fi

# Test: alloc() requires exactly one argument.
cat > "$TMP_DIR/bad_alloc.pb" <<'EOF'
function number main() {
    alloc();
    return 0;
}
EOF

if ! "$COMPILER" build "$TMP_DIR/bad_alloc.pb" \
    -o "$TMP_DIR/bad_alloc" >"$TMP_DIR/log" 2>&1 &&
    grep -q "alloc() expects one number argument" "$TMP_DIR/log" &&
    [ ! -e "$TMP_DIR/bad_alloc" ]; then
    pass "reject alloc with missing argument"
else
    fail "reject alloc with missing argument"
    cat "$TMP_DIR/log"
fi

# Test: free() must receive a pointer or string.
cat > "$TMP_DIR/bad_free.pb" <<'EOF'
function number main() {
    free(42);
    return 0;
}
EOF

if ! "$COMPILER" build "$TMP_DIR/bad_free.pb" \
    -o "$TMP_DIR/bad_free" >"$TMP_DIR/log" 2>&1 &&
    grep -q "free() expects a pointer or string argument" "$TMP_DIR/log" &&
    [ ! -e "$TMP_DIR/bad_free" ]; then
    pass "reject free with invalid argument type"
else
    fail "reject free with invalid argument type"
    cat "$TMP_DIR/log"
fi

# IMPORT TESTS

# Test: basic import of function from another module
cat > "$TMP_DIR/math_helper.pb" <<'EOF'
function number multiply(number a, number b) {
    return a * b;
}
EOF

cat > "$TMP_DIR/import_basic.pb" <<'EOF'
import "math_helper.pb";

function number main() {
    print multiply(6, 7);
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/import_basic.pb" -o "$TMP_DIR/import_basic" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/import_basic")" = "42" ]; then
    pass "basic import and function call"
else
    fail "basic import and function call"
    cat "$TMP_DIR/log"
fi

# Test: nested chained imports (A -> B -> C)
cat > "$TMP_DIR/import_c.pb" <<'EOF'
function number get_constant() {
    return 100;
}
EOF

cat > "$TMP_DIR/import_b.pb" <<'EOF'
import "import_c.pb";

function number add_constant(number x) {
    return x + get_constant();
}
EOF

cat > "$TMP_DIR/import_a.pb" <<'EOF'
import "import_b.pb";

function number main() {
    print add_constant(23);
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/import_a.pb" -o "$TMP_DIR/import_a" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/import_a")" = "123" ]; then
    pass "nested chained imports"
else
    fail "nested chained imports"
    cat "$TMP_DIR/log"
fi

# Test: diamond import (A -> B, C; B -> D; C -> D)
cat > "$TMP_DIR/diamond_d.pb" <<'EOF'
function number base_value() {
    return 10;
}
EOF

cat > "$TMP_DIR/diamond_b.pb" <<'EOF'
import "diamond_d.pb";

function number b_val() {
    return base_value() + 5;
}
EOF

cat > "$TMP_DIR/diamond_c.pb" <<'EOF'
import "diamond_d.pb";

function number c_val() {
    return base_value() + 20;
}
EOF

cat > "$TMP_DIR/diamond_a.pb" <<'EOF'
import "diamond_b.pb";
import "diamond_c.pb";

function number main() {
    print b_val() + c_val();
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/diamond_a.pb" -o "$TMP_DIR/diamond_a" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/diamond_a")" = "45" ]; then
    pass "diamond import deduplication"
else
    fail "diamond import deduplication"
    cat "$TMP_DIR/log"
fi

# Test: circular import (A -> B -> A)
cat > "$TMP_DIR/circ_b.pb" <<'EOF'
import "circ_a.pb";

function number from_b() {
    return 77;
}
EOF

cat > "$TMP_DIR/circ_a.pb" <<'EOF'
import "circ_b.pb";

function number main() {
    print from_b();
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/circ_a.pb" -o "$TMP_DIR/circ_a" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/circ_a")" = "77" ]; then
    pass "circular import resolution"
else
    fail "circular import resolution"
    cat "$TMP_DIR/log"
fi

# Test: reject non-existent import file
cat > "$TMP_DIR/bad_import.pb" <<'EOF'
import "non_existent_module.pb";

function number main() {
    return 0;
}
EOF

if ! "$COMPILER" build "$TMP_DIR/bad_import.pb" \
    -o "$TMP_DIR/bad_import" >"$TMP_DIR/log" 2>&1 &&
    grep -q "failed to import" "$TMP_DIR/log" &&
    [ ! -e "$TMP_DIR/bad_import" ]; then
    pass "reject non-existent imported file"
else
    fail "reject non-existent imported file"
    cat "$TMP_DIR/log"
fi

# Test: reject duplicate symbol name across imported files
cat > "$TMP_DIR/dup_mod1.pb" <<'EOF'
function number duplicate_symbol() {
    return 1;
}
EOF

cat > "$TMP_DIR/dup_mod2.pb" <<'EOF'
function number duplicate_symbol() {
    return 2;
}
EOF

cat > "$TMP_DIR/dup_main.pb" <<'EOF'
import "dup_mod1.pb";
import "dup_mod2.pb";

function number main() {
    return 0;
}
EOF

if ! "$COMPILER" build "$TMP_DIR/dup_main.pb" \
    -o "$TMP_DIR/dup_main" >"$TMP_DIR/log" 2>&1 &&
    grep -q "top-level name is already declared" "$TMP_DIR/log" &&
    [ ! -e "$TMP_DIR/dup_main" ]; then
    pass "reject name collision across imports"
else
    fail "reject name collision across imports"
    cat "$TMP_DIR/log"
fi

# CROSS-PLATFORM DRIVER TESTS

# Test: peb run command
cat > "$TMP_DIR/run_test.pb" <<'EOF'
function number main() {
    print 99;
    return 0;
}
EOF

RUN_OUT="$("$COMPILER" run "$TMP_DIR/run_test.pb" 2>/dev/null)"
if [ "$RUN_OUT" = "99" ]; then
    pass "peb run execution"
else
    fail "peb run execution"
fi

# Test: output path containing spaces
if "$COMPILER" build "$TMP_DIR/basic.pb" -o "$TMP_DIR/output with spaces" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/output with spaces")" = "42" ]; then
    pass "output path containing spaces"
else
    fail "output path containing spaces"
    cat "$TMP_DIR/log"
fi

# Test: source path containing spaces
mkdir -p "$TMP_DIR/dir with spaces"
cat > "$TMP_DIR/dir with spaces/space_src.pb" <<'EOF'
function number main() {
    print 101;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/dir with spaces/space_src.pb" -o "$TMP_DIR/space_src_out" \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/space_src_out")" = "101" ]; then
    pass "source path containing spaces"
else
    fail "source path containing spaces"
    cat "$TMP_DIR/log"
fi

# Test: running compiler outside installation directory / unrelated working directory
ABS_COMPILER="$(cd "$(dirname "$COMPILER")" && pwd)/$(basename "$COMPILER")"
mkdir -p "$TMP_DIR/unrelated_cwd"
if (cd "$TMP_DIR/unrelated_cwd" && "$ABS_COMPILER" build "$TMP_DIR/basic.pb" -o "$TMP_DIR/remote_build") \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/remote_build")" = "42" ]; then
    pass "runtime discovery outside installation directory"
else
    fail "runtime discovery outside installation directory"
    cat "$TMP_DIR/log"
fi

# Test: runtime discovery using PEBBLE_RUNTIME_DIR
mkdir -p "$TMP_DIR/custom_runtime"
cp runtime/pebble_runtime.c runtime/pebble_runtime.h "$TMP_DIR/custom_runtime/"
if (PEBBLE_RUNTIME_DIR="$TMP_DIR/custom_runtime" "$COMPILER" build "$TMP_DIR/basic.pb" -o "$TMP_DIR/custom_rt_build") \
    >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/custom_rt_build")" = "42" ]; then
    pass "runtime discovery with PEBBLE_RUNTIME_DIR"
else
    fail "runtime discovery with PEBBLE_RUNTIME_DIR"
    cat "$TMP_DIR/log"
fi

# Test: correct process exit code propagation
cat > "$TMP_DIR/exit_code.pb" <<'EOF'
function number main() {
    return 42;
}
EOF

set +e
"$COMPILER" run "$TMP_DIR/exit_code.pb" >/dev/null 2>&1
EXIT_STATUS=$?
set -e
if [ "$EXIT_STATUS" -eq 42 ]; then
    pass "process exit code propagation"
else
    fail "process exit code propagation (expected 42, got $EXIT_STATUS)"
fi

# Test: correct default executable naming
cat > "$TMP_DIR/default_name.pb" <<'EOF'
function number main() {
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/default_name.pb" >"$TMP_DIR/log" 2>&1; then
    EXPECTED_NAME="$TMP_DIR/default_name"
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) EXPECTED_NAME="$TMP_DIR/default_name.exe" ;;
    esac
    if [ -f "$EXPECTED_NAME" ]; then
        pass "correct executable naming"
    else
        fail "correct executable naming (expected $EXPECTED_NAME)"
    fi
else
    fail "default naming build failed"
    cat "$TMP_DIR/log"
fi

# Test: structs basic read/write, zero-init, and copy
cat > "$TMP_DIR/struct_basic.pb" <<'EOF'
struct Point {
    number x;
    number y;
}

function number main() {
    Point p1;
    print p1.x;
    print p1.y;
    p1.x = 10;
    p1.y = 20;
    print p1.x;
    print p1.y;

    Point p2;
    p2 = p1;
    p2.x = 30;
    print p2.x;
    print p1.x;
    return 0;
}
EOF

EXPECTED=$(printf '0\n0\n10\n20\n30\n10')
if "$COMPILER" build "$TMP_DIR/struct_basic.pb" -o "$TMP_DIR/struct_basic" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/struct_basic")" = "$EXPECTED" ]; then
    pass "structs basic read/write, zero-init, and copy"
else
    fail "structs basic read/write, zero-init, and copy"
    cat "$TMP_DIR/log"
fi

# Test: nested structs and struct arrays
cat > "$TMP_DIR/struct_nested.pb" <<'EOF'
struct Point {
    number x;
    number y;
}

struct Rect {
    Point topLeft;
    Point bottomRight;
}

function number main() {
    Rect r;
    r.topLeft.x = 1;
    r.topLeft.y = 2;
    r.bottomRight.x = 10;
    r.bottomRight.y = 20;
    print r.topLeft.x;
    print r.bottomRight.y;

    Point[3] pts;
    pts[0].x = 100;
    pts[1].x = 200;
    pts[2].x = 300;
    print pts[0].x;
    print pts[1].x;
    print pts[2].x;
    return 0;
}
EOF

EXPECTED=$(printf '1\n20\n100\n200\n300')
if "$COMPILER" build "$TMP_DIR/struct_nested.pb" -o "$TMP_DIR/struct_nested" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/struct_nested")" = "$EXPECTED" ]; then
    pass "nested structs and struct arrays"
else
    fail "nested structs and struct arrays"
    cat "$TMP_DIR/log"
fi

# Test: structs in function parameters and return values
cat > "$TMP_DIR/struct_fn.pb" <<'EOF'
struct Vector {
    number dx;
    number dy;
}

function Vector makeVector(number dx, number dy) {
    Vector v;
    v.dx = dx;
    v.dy = dy;
    return v;
}

function number lengthSquared(Vector v) {
    return v.dx * v.dx + v.dy * v.dy;
}

function number main() {
    Vector v makeVector(3, 4);
    print v.dx;
    print v.dy;
    print lengthSquared(v);
    return 0;
}
EOF

EXPECTED=$(printf '3\n4\n25')
if "$COMPILER" build "$TMP_DIR/struct_fn.pb" -o "$TMP_DIR/struct_fn" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/struct_fn")" = "$EXPECTED" ]; then
    pass "structs in function parameters and return values"
else
    fail "structs in function parameters and return values"
    cat "$TMP_DIR/log"
fi

# Test: enums declaration, values, comparisons, functions, and print
cat > "$TMP_DIR/enum_test.pb" <<'EOF'
enum Status {
    PENDING,
    ACTIVE,
    CLOSED
}

function Status getNextStatus(Status s) {
    if (s == PENDING) {
        return ACTIVE;
    }
    return CLOSED;
}

function number main() {
    Status s1;
    print s1;

    Status s2 ACTIVE;
    print s2;

    if (s2 == ACTIVE) {
        print 1;
    } else {
        print 0;
    }

    if (s2 != PENDING) {
        print 1;
    }

    Status next getNextStatus(s1);
    print next;
    return 0;
}
EOF

EXPECTED=$(printf '0\n1\n1\n1\n1')
if "$COMPILER" build "$TMP_DIR/enum_test.pb" -o "$TMP_DIR/enum_test" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/enum_test")" = "$EXPECTED" ]; then
    pass "enums declaration, values, comparisons, functions, and print"
else
    fail "enums declaration, values, comparisons, functions, and print"
    cat "$TMP_DIR/log"
fi

# Test: void functions and bare return
cat > "$TMP_DIR/void_return.pb" <<'EOF'
function void logNumber(number n) {
    if (n < 0) {
        return;
    }
    print n;
}

function number main() {
    logNumber(-5);
    logNumber(42);
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/void_return.pb" -o "$TMP_DIR/void_return" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/void_return")" = "42" ]; then
    pass "void functions and bare return"
else
    fail "void functions and bare return"
    cat "$TMP_DIR/log"
fi

# Test: logical and bitwise operators
cat > "$TMP_DIR/operators.pb" <<'EOF'
function number main() {
    number a 6;
    number b 3;

    print a & b;
    print a | b;
    print a ^ b;
    print ~0;
    print 1 << 4;
    print 32 >> 2;

    bool t true;
    bool f false;
    if (t && f) {
        print 0;
    } else {
        print 1;
    }

    if (t || f) {
        print 1;
    } else {
        print 0;
    }

    return 0;
}
EOF

EXPECTED=$(printf '2\n7\n5\n-1\n16\n8\n1\n1')
if "$COMPILER" build "$TMP_DIR/operators.pb" -o "$TMP_DIR/operators" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/operators")" = "$EXPECTED" ]; then
    pass "logical and bitwise operators"
else
    fail "logical and bitwise operators"
    cat "$TMP_DIR/log"
fi

# Test: builtins length, typeof, and string equality
cat > "$TMP_DIR/builtins.pb" <<'EOF'
struct Node {
    number val;
}

enum State { ON, OFF }

function number main() {
    string s "hello";
    print length(s);

    number[5] arr;
    print length(arr);

    print typeof(10);
    print typeof(true);
    print typeof(s);

    Node n;
    print typeof(n);

    State st ON;
    print typeof(st);

    string s1 "pebble";
    string s2 "pebble";
    string s3 "rock";

    if (s1 == s2) {
        print 1;
    } else {
        print 0;
    }

    if (s1 != s3) {
        print 1;
    } else {
        print 0;
    }

    return 0;
}
EOF

EXPECTED=$(printf '5\n5\nnumber\nbool\nstring\nNode\nState\n1\n1')
if "$COMPILER" build "$TMP_DIR/builtins.pb" -o "$TMP_DIR/builtins" >"$TMP_DIR/log" 2>&1 &&
    [ "$("$TMP_DIR/builtins")" = "$EXPECTED" ]; then
    pass "builtins length, typeof, and string equality"
else
    fail "builtins length, typeof, and string equality"
    cat "$TMP_DIR/log"
fi

# Test: builtin panic terminates execution
cat > "$TMP_DIR/panic_test.pb" <<'EOF'
function number main() {
    print 100;
    panic("something broke");
    print 200;
    return 0;
}
EOF

set +e
"$COMPILER" build "$TMP_DIR/panic_test.pb" -o "$TMP_DIR/panic_test" >/dev/null 2>&1
PANIC_OUTPUT=$("$TMP_DIR/panic_test" 2>&1)
PANIC_EXIT=$?
set -e
if [ "$PANIC_EXIT" -ne 0 ] && echo "$PANIC_OUTPUT" | grep -q "panic: something broke"; then
    pass "builtin panic halts execution"
else
    fail "builtin panic halts execution (exit $PANIC_EXIT, output: $PANIC_OUTPUT)"
fi

# Test: builtin assert stops on false
cat > "$TMP_DIR/assert_test.pb" <<'EOF'
function number main() {
    assert(1 == 1, "math works");
    assert(1 == 2, "assertion failed");
    return 0;
}
EOF

set +e
"$COMPILER" build "$TMP_DIR/assert_test.pb" -o "$TMP_DIR/assert_test" >/dev/null 2>&1
ASSERT_OUTPUT=$("$TMP_DIR/assert_test" 2>&1)
ASSERT_EXIT=$?
set -e
if [ "$ASSERT_EXIT" -ne 0 ] && echo "$ASSERT_OUTPUT" | grep -q "panic: assertion failed"; then
    pass "builtin assert halts on false condition"
else
    fail "builtin assert halts on false condition (exit $ASSERT_EXIT, output: $ASSERT_OUTPUT)"
fi

# Test: reject unknown struct field
cat > "$TMP_DIR/bad_field.pb" <<'EOF'
struct Point {
    number x;
}

function number main() {
    Point p;
    p.y = 10;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/bad_field.pb" -o "$TMP_DIR/bad_field" >"$TMP_DIR/log" 2>&1; then
    fail "reject unknown struct field"
else
    pass "reject unknown struct field"
fi

# Test: reject member access on primitive
cat > "$TMP_DIR/bad_member.pb" <<'EOF'
function number main() {
    number x 5;
    x.field = 10;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/bad_member.pb" -o "$TMP_DIR/bad_member" >"$TMP_DIR/log" 2>&1; then
    fail "reject member access on primitive"
else
    pass "reject member access on primitive"
fi

# Test: reject uninitialized primitive scalar
cat > "$TMP_DIR/bad_uninit.pb" <<'EOF'
function number main() {
    number x;
    return 0;
}
EOF

if "$COMPILER" build "$TMP_DIR/bad_uninit.pb" -o "$TMP_DIR/bad_uninit" >"$TMP_DIR/log" 2>&1; then
    fail "reject uninitialized primitive scalar"
else
    pass "reject uninitialized primitive scalar"
fi

echo "Driver tests: $PASS passed, $FAIL failed"

[ "$FAIL" -eq 0 ]

