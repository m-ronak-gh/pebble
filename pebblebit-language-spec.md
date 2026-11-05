# Pebble Language Specification v1.0

## 1. Language Manifesto

**Why Pebble exists:** most people's first compiled language is C or C++, both of which bury the "here's how a computer actually runs your code" lesson under decades of accumulated features. Most people's first *easy* language is Python, which hides that lesson entirely behind an interpreter. Pebble exists to be the missing middle: small enough to hold the whole language in your head, but a real compiler producing a real native executable — so the connection between "code you wrote" and "instructions the CPU runs" stays visible the whole time.

**Who it's for:** students learning how compilers and computers work; beginners who want their first language to *not* lie to them about what's happening under the hood; anyone who wants to write real console programs (calculators, parsers, games, sorting/data-structure exercises) without wading through a large language spec first.

**What it solves:** the gap between "toy interpreted teaching language" and "production systems language with a 500-page spec."

**What it deliberately refuses to solve:** production software engineering at scale. Pebble will never grow generics, traits, macros, async, or a borrow checker. It is not trying to be anyone's second language forever — it's trying to be an excellent first one.

**Core principles** (carried directly from the design brief, they're correct as stated): one statement, one action; explicit over implicit; readability over cleverness; minimal syntax; small grammar; native compilation; tiny standard library; easy to parse; easy to teach; easy to extend later.

---

## 2. Formal Grammar (EBNF)

```ebnf
program        ::= { import_stmt | extern_decl | function_decl | struct_decl | enum_decl } ;

import_stmt    ::= "import" ( string_literal | identifier ) [ ";" ] ;

extern_decl    ::= "extern" "function" type identifier "(" [ param_list ] ")" ";" ;

function_decl  ::= "function" type identifier "(" [ param_list ] ")" block ;
param_list     ::= param { "," param } ;
param          ::= type identifier ;

struct_decl    ::= "struct" identifier "{" { field_decl } "}" ;
field_decl     ::= type identifier ";" ;

enum_decl      ::= "enum" identifier "{" identifier { "," identifier } "}" ;

block          ::= "{" { statement } "}" ;

statement      ::= var_decl
                 | assignment
                 | print_stmt
                 | if_stmt
                 | while_stmt
                 | repeat_stmt
                 | for_stmt
                 | break_stmt
                 | continue_stmt
                 | return_stmt
                 | expr_stmt
                 | block ;

var_decl       ::= [ "const" ] type identifier [ expression ] ";" ;
                 (* structs/enums/arrays may omit the initializer: zero-initialized *)

assignment     ::= lvalue "=" expression ";" ;
lvalue         ::= identifier { "." identifier | "[" expression "]" } ;

print_stmt     ::= "print" expression ";" ;

if_stmt        ::= "if" "(" expression ")" block [ "else" ( if_stmt | block ) ] ;
while_stmt     ::= "while" "(" expression ")" block ;
repeat_stmt    ::= "repeat" "(" expression ")" block ;
for_stmt       ::= "for" "(" var_decl expression ";" assignment_expr ")" block ;
assignment_expr::= lvalue "=" expression ;

break_stmt     ::= "break" ";" ;
continue_stmt  ::= "continue" ";" ;
return_stmt    ::= "return" [ expression ] ";" ;
expr_stmt      ::= expression ";" ;

type           ::= "number" | "decimal" | "bool" | "char" | "string"
                 | "void" | identifier                 (* identifier = struct/enum name *)
                 | type "[" [ number_literal ] "]" ;    (* fixed-size array *)

expression     ::= logic_or ;
logic_or       ::= logic_and { "||" logic_and } ;
logic_and      ::= bit_or { "&&" bit_or } ;
bit_or         ::= bit_xor { "|" bit_xor } ;
bit_xor        ::= bit_and { "^" bit_and } ;
bit_and        ::= equality { "&" equality } ;
equality       ::= comparison { ( "==" | "!=" ) comparison } ;
comparison     ::= shift { ( "<" | ">" | "<=" | ">=" ) shift } ;
shift          ::= term { ( "<<" | ">>" ) term } ;
term           ::= factor { ( "+" | "-" ) factor } ;
factor         ::= unary { ( "*" | "/" | "%" ) unary } ;
unary          ::= ( "!" | "-" | "~" ) unary | call ;
call           ::= primary { "(" [ arg_list ] ")" | "." identifier | "[" expression "]" } ;
arg_list       ::= expression { "," expression } ;
primary        ::= number_literal | decimal_literal | string_literal | char_literal
                 | "true" | "false" | "null" | identifier | "(" expression ")" ;
```

---

## 3. Keywords (23 total)

| Keyword | Purpose |
|---|---|
| `number` | integer type |
| `decimal` | floating-point type |
| `bool` | boolean type |
| `char` | single-character type |
| `string` | text type |
| `void` | "no return value" for functions |
| `print` | the one built-in output statement — elevated to a keyword because it's the single most common operation a beginner performs, and giving it dedicated syntax (no parens needed) keeps first programs uncluttered |
| `if` / `else` | conditional branching |
| `while` | condition-checked loop |
| `repeat` | fixed-count loop — deliberately distinct from `while` so "loop N times" and "loop until condition" read as visibly different concepts, which is a real pedagogical distinction worth keeping |
| `for` | C-style counted loop with explicit init/condition/step, chosen over `repeat` when the loop variable itself matters |
| `break` / `continue` | loop control |
| `return` | exit a function, optionally with a value |
| `function` | declare a function |
| `extern` | declare an externally-provided function (C ABI) — used by the standard library to expose runtime implementations; not needed in ordinary Pebble programs |
| `struct` | declare a plain-data user type |
| `enum` | declare a named set of integer constants |
| `const` | mark a variable immutable after declaration |
| `true` / `false` | boolean literals |
| `import` | bring in another `.pb` file's declarations or a stdlib module by name |
| `null` | the empty/absent value (only valid for `string` and array types) |

Everything else (`+ - * / % == != < > && || ! & | ^ ~ << >>`) is a symbolic operator, not a keyword — keeping the keyword count low while still giving Pebble a complete, honestly-labeled operator set as the brief asked for.

---

## 4. Primitive Types

| Type | Size | Representation | Notes |
|---|---|---|---|
| `number` | 8 bytes | signed 64-bit integer | one integer type, not a zoo of int8/16/32/64 — matches "small grammar" |
| `decimal` | 8 bytes | IEEE-754 double | |
| `bool` | 1 byte | 0 or 1 | |
| `char` | 1 byte | ASCII byte | Unicode is explicitly out of scope for v1 |
| `string` | 16 bytes (fat pointer) | `{ number length; char* data; }` | string literals point into the compiled binary's static data (never freed); strings built at runtime (concatenation, substring, `toString`) are heap-allocated and must be released with `free()` |

All variables must be initialized at the point of declaration — Pebble has no such thing as an uninitialized variable. This is a deliberate rule, not a limitation: it removes an entire category of bug (reading garbage memory) before students ever encounter it. The one documented exception: `struct`, `enum`, and array declarations may omit an initializer, in which case all fields/elements are zero-initialized, because requiring every field to be specified inline on every struct declaration would make structs unusably verbose.

---

## 5. Variables

```
number age 20;
string name "Ronak";
bool ready true;
const number MAX 100;

age = 21;          -- assignment to an already-declared variable
```

- **Scope:** block-scoped. Every `{ }` opens a new scope; a variable declared inside is invisible outside it.
- **Lifetime:** stack variables are reclaimed automatically when their block exits — no action needed. Heap memory reachable through a `string` or via `alloc()` is **not** automatically reclaimed; the programmer calls `free()` explicitly. This asymmetry is intentional and stated plainly in the docs: Pebble teaches manual memory management, it doesn't hide it.
- **Constants:** `const` prefixes the type; reassignment to a `const` variable is a compile error with a clear message pointing at the original declaration.

---

## 6. Operators

| Category | Operators | Associativity |
|---|---|---|
| Unary | `!` `-` `~` | right |
| Multiplicative | `*` `/` `%` | left |
| Additive | `+` `-` | left |
| Shift | `<<` `>>` | left |
| Relational | `<` `>` `<=` `>=` | left |
| Equality | `==` `!=` | left |
| Bitwise AND | `&` | left |
| Bitwise XOR | `^` | left |
| Bitwise OR | `\|` | left |
| Logical AND | `&&` | left |
| Logical OR | `\|\|` | left |
| Assignment | `=` | right |

Precedence runs top-to-bottom in the table above (unary binds tightest). This is the same shape as C's precedence table, deliberately — there's no pedagogical value in inventing a *different* order from what students will meet in every other language afterward. Bitwise operators apply to `number` only; logical operators apply to `bool` only — mixing the two is a type error, not an implicit conversion, per "explicit over implicit."

---

## 7. Control Flow

```
if (age >= 18) {
    print "adult";
} else if (age >= 13) {
    print "teen";
} else {
    print "child";
}

while (count < 10) {
    count = count + 1;
}

repeat (5) {
    print "hi";
}

for (number i 0; i < 10; i = i + 1) {
    print i;
}
```

`break` and `continue` work inside any loop. `return` exits the current function; bare `return;` is only valid in a `void` function.

---

## 8. Functions

```
function number add(number a, number b) {
    return a + b;
}

function void greet(string name) {
    print "Hello, ";
    print name;
}

function number main() {
    return 0;
}
```

- Return type comes first, matching variable declaration order — one consistent "type, then name" rule across the whole language.
- **No overloading.** One name, one function. This isn't a missing feature — it's a deliberate cut, because overload resolution rules are exactly the kind of hidden-implicit-behavior Pebble's philosophy rejects.
- **Recursion** works normally; there's nothing special about it, which is itself worth teaching (a function calling itself is just a function call).
- **Entry point:** every program needs exactly one `function number main()`, returning a process exit code — mirroring C's convention on purpose, so the concept transfers directly to the next language a student learns.

---

## 9. User Types

```
struct Point {
    number x;
    number y;
}

Point p;          -- zero-initialized: p.x = 0, p.y = 0
p.x = 3;
p.y = 4;

enum Color { RED, GREEN, BLUE }
Color c RED;

number scores[10];   -- zero-initialized array of 10
scores[0] = 100;
```

- **Structs** are plain data — fields only, no methods attached to them. There is no `this`/`self` in Pebble.
- **Enums** are backed by `number` under the hood; each name gets the next integer starting at 0.
- **Arrays** are fixed-size, stack-allocated, bounds-checked at runtime (an out-of-bounds access is a `panic`, not silent memory corruption — this is the one runtime safety check Pebble keeps, because the alternative is a genuinely confusing class of beginner bug with no pedagogical upside).
- **No unions.** Cut per the brief's own instruction — the pedagogical payoff doesn't justify the added type-checking complexity.
- **No classes, no inheritance, no methods-on-types.** This is Pebble's clearest "does this help beginners" cut. Struct-plus-free-functions is a complete, teachable model of data and behavior on its own; inheritance hierarchies are exactly the kind of feature that exists in other languages "because other languages have it," which the brief explicitly asks to avoid.

---

## 10. Module System

Pebble has two import forms:

```
import System;          -- stdlib module: bare identifier, semicolon optional
import Math             -- stdlib module: no semicolon also accepted
import "mathutils.pb";  -- user file: quoted path, semicolon optional
```

**Stdlib modules** are imported by bare identifier (e.g. `import System;`, `import Math`). The identifier is resolved by the compiler against its built-in module list — no file path or extension is needed.

**User files** are imported by quoted relative path (e.g. `import "mathutils.pb";`). Paths are resolved relative to the importing file's directory — no package registry, no manifest file, no version resolution. For a single-project teaching language, that machinery would be pure overhead.

**Semicolons are optional** on both forms. `import System` and `import System;` are both valid.

Everything at a file's top level (functions, structs, enums) becomes visible to any file that imports it. There is no `public`/`private` modifier in v1 and **no namespace-qualified access** (`System.func()` is invalid) — imported names enter the importing file's global scope directly. For example, importing `System` makes `argsCount` and `argsGet` callable without any prefix. Name collisions become a compile error the student has to resolve by hand, which is a reasonable and small teaching moment rather than a real limitation at this project's scale. A qualified-access syntax is a natural, low-risk v1.1 addition if collisions prove annoying in practice.

---

## 11. Standard Library

Deliberately tiny — every module here earns its place:

| Module | Provides |
|---|---|
| `io` | `input() -> string`, `readFile(path) -> string`, `writeFile(path, contents) -> bool` |
| `string` | `length(s)`, `concat(a, b)`, `substring(s, start, len)`, `charAt(s, i)`, `toNumber(s)`, `toString(n)` |
| `math` | `abs`, `min`, `max`, `sqrt`, `pow`, `floor`, `ceil` |
| `time` | `now() -> number`, `sleep(ms)` |
| `random` | `seed(n)`, `randomInt(min, max)` |
| `system` | `argsCount() -> number` — returns the number of user-supplied command-line arguments (excluding the program name); `argsGet(i) -> string` — returns the `i`-th user argument (0-indexed); returns an empty string for negative or out-of-range `i` |

Deliberately **excluded** from v1: a `collections` module (fixed arrays cover the pedagogical need; a dynamic list type is a clean, low-risk future addition once the core compiler is stable) and `networking` (sockets bring real platform-specific complexity for very little teaching payoff relative to the cost, at this project's scale).

---

## 12. Built-in Functions (no import needed)

| Function | Purpose | Why it's a true builtin and not stdlib |
|---|---|---|
| `print` | output (keyword, see §3) | fundamental to every program |
| `length(x)` | array/string length | needs compiler-level knowledge of layout |
| `typeof(x)` | returns the type name as a string | needs compiler introspection |
| `assert(cond, message)` | halts with `message` if `cond` is false | core debugging tool, should never require an import |
| `panic(message)` | halts immediately with `message` | same reasoning as `assert` |
| `exit(code)` | terminates the process | fundamental control, not domain-specific |
| `alloc(size)` / `free(ptr)` | manual heap allocation | this is where Pebble teaches manual memory management directly — making it feel like part of the language rather than a bolt-on library is the point |

Everything else lives behind `import` specifically so students exercise the module system for real — if literally everything were a builtin, `import` would never actually get used.

---

## 13. Diagnostic Philosophy

Every Pebble error follows the same shape: **what happened → where → why it's a rule → how to fix it**, with a source snippet and a caret pointing at the exact location.

```
error: missing semicolon
  --> hello.pb:3:15
   |
 3 | print "Hello"
   |               ^ expected ';' here
   |
   Every Pebble statement ends with a semicolon so the compiler
   knows exactly where it stops.

   Try: print "Hello";
```

Rules for every diagnostic:
- Never emit a bare "syntax error" or "unexpected token" with no explanation attached.
- Use plain words a beginner has actually been taught — prefer "word" over "token" in the default mode.
- Always end with a concrete, copy-pasteable fix, not just a description of the problem.
- Never sarcastic, never falsely cheerful about a real problem — encouraging tone comes from clarity and respect, not jokes.

---

## 14. Style Guide

- File extension: `.pb`
- Naming: `camelCase` for variables and functions, `PascalCase` for `struct`/`enum` names, `SCREAMING_SNAKE_CASE` for `const`
- Indentation: 4 spaces, opening brace on the same line (`if (x) {`)
- One statement per line; every statement ends in `;`
- Comments: `// line comment` and `/* block comment */`, both supported (cheap to lex, genuinely useful)
- Best practices: always `free()` what you `alloc()`; prefer `repeat` over `for` when the loop variable itself is unused; keep functions short enough to read on one screen

---

## 15. Compiler Architecture

| Stage | Input → Output | Notes |
|---|---|---|
| Lexer | source text → token stream | hand-written, C17 |
| Parser | tokens → AST | recursive descent for statements, Pratt parsing for expressions (the grammar in §2 maps directly onto this) |
| Semantic Analysis | AST → type-checked AST | symbol tables, scope resolution, enforces "must initialize," array bounds where statically knowable |
| LLVM IR Generation | AST → LLVM IR | via LLVM's **C API** (`llvm-c/Core.h`, `llvm-c/Target.h`, `llvm-c/TargetMachine.h`) — keeps the whole compiler in C17 rather than splitting into a C++ island |
| Optimization | LLVM IR → optimized IR | use LLVM's built-in `-O1`-equivalent pass pipeline; don't write custom passes, that's out of scope |
| Executable Generation | IR → object file → linked binary | LLVM's `TargetMachine` emits a `.o` file; the system linker (invoked via `cc`) produces the final `.exe`/native binary — Pebble never implements its own linker |

Build system: a **hand-written Makefile**, not CMake — consistent with the language's own "small and understandable" ethos. If LLVM's linking flags become unmanageable, falling back to CMake's `find_package(LLVM)` is a reasonable, documented escape hatch, but start with Make.

---

## 16. Repository Structure

```
pebblebit/
├── compiler/
│   ├── lexer/
│   ├── parser/
│   ├── ast/
│   ├── semantic/
│   └── codegen/          -- LLVM IR generation via llvm-c
├── runtime/               -- small C runtime linked into every Pebblebit binary
│                             (alloc/free wrappers, string helpers, bounds-check panic handler)
├── stdlib/                -- C implementations of io/string/math/time/random/system,
│                             exposed to Pebble programs via extern declarations
├── examples/               -- calculator, tic-tac-toe, sorting, linked list, binary tree, CSV parser
├── tests/
│   ├── unit/                -- lexer/parser/semantic/codegen unit tests
│   └── programs/            -- .pb test programs with expected output, run end-to-end
├── docs/                     -- docs site source, deployed via GitHub Pages
├── benchmarks/
├── .github/workflows/       -- CI + Pages deploy
├── Makefile
├── LANGUAGE_SPEC.md
├── README.md
├── CONTRIBUTING.md
└── LICENSE
```

---
**Risks & mitigations**

- *LLVM/toolchain mismatch across machines or CI* — mitigated by the week-1 smoke test and a pinned LLVM version documented in the README.
- *Manual memory management causing crashes in example programs* — mitigated by keeping the stdlib's own heap usage minimal and well-tested; documented plainly as an intentional teaching trade-off, not hidden.
- *Scope creep* (someone wants to add generics, classes, etc. mid-project) — mitigated by treating this document as the source of truth; any addition requires a deliberate team discussion, not a solo decision in a PR.
- *Uneven workstream velocity* — Programmer D's testing/docs work is blocked on a working compiler if done reactively. Mitigation: write test programs and documentation *against this spec* starting week 1, before the compiler can run them, so there's no idle time waiting on the compiler team.
