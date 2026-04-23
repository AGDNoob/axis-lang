# AXIS v1.3.0 — Practical Programming Update

**Philosophy:** "Think in values, compile to metal"

---

## 💭 Developer Note

v1.1.0 gave AXIS its language features. v1.2.0 made the compiler fast. v1.3.0 starts making AXIS safer and more expressive — one feature at a time.

This release adds **immutable variables** (`const`), **labeled loop control** (loop flags), **explicit type casting** (`copy...as` / `update...as`), **automatic variable aliasing**, **mandatory `copy` for array index access**, a **new `input()` system**, and **string operations** in compile mode. `const` prevents accidental mutation of values that shouldn't change — the compiler catches it at every assignment, compound operator, index write, field write, and `update` parameter. Loop flags let you target outer loops from nested ones with `stop @name` and `skip @name`. Type casting gives you explicit control over integer width conversions. Assigning a variable to another variable creates an alias automatically — use `copy` to get an independent copy. Array element access now requires `copy` to make data extraction explicit. The old `read`/`readln`/`readchar` system is gone, replaced by a type-safe `input()` with per-variable error handling.

All features are fully validated by the semantic analyzer and produce clear error messages when misused.

---

## 🚀 What's New

### Immutable Variables with `const`

Variables declared with `const` are **read-only after initialisation**. The compiler enforces immutability at every point where a variable could be modified: direct assignment, compound assignment, element mutation, field mutation, and pass-by-reference via `update`.

#### Syntax

```axis
const name: type = value
```

The `const` keyword goes before the variable name. An initialiser is required for scalar and array types. Field and enum types with defaults may omit the initialiser (the type's defaults apply).

#### Works with Every Type

```axis
// Scalars
const MAX: i32 = 100
const LIMIT: u64 = 999
const ACTIVE: bool = true
const GREETING: str = "hello"

// Expressions
const AREA: i32 = MAX * 2

// Arrays
const SCORES: i32[3] = [10, 20, 30]

// Fields (initialiser optional — defaults apply)
Vec2: field:
    x: i32 = 0
    y: i32 = 0

const ORIGIN: Vec2

// Enums (initialiser optional — defaults to first variant)
Color: enum:
    Red
    Green
    Blue

const DEFAULT_COLOR: Color
```

#### What `const` Prevents

Every form of mutation is caught by the semantic analyzer at compile time:

| Attempt | Error |
| --- | --- |
| `x = 10` | Cannot assign to immutable variable: x |
| `x += 1` | Cannot assign to immutable variable: x |
| `arr[0] = 99` | Cannot modify elements of immutable array: arr |
| `pos.x = 5` | Cannot modify fields of immutable variable: pos |
| `f(x)` where f takes `update` | variable 'x' must be mutable for 'update' parameter |
| `const x: i32` (no initialiser) | const variable 'x' must have an initialiser |

Mutable and immutable variables coexist without issues:

```axis
const MAX: i32 = 100
counter: i32 = 0           // mutable — can be changed
counter = counter + 1       // ✓
// MAX = 200                // ✗ — compile error
```

#### What `const` Does Not Do

- **Not compile-time evaluation.** `const` does not require the value to be known at compile time. It only prevents mutation after initialisation. It is a runtime immutability guarantee, not a `constexpr`.
- **Not deep freeze for references.** Assigning a variable to another creates an alias (shared storage). `copy` creates an independent value:
  1. An alias inherits the constness of its source.
  2. Declaring `const` on a mutable alias is an error (not an implicit copy).
  3. `copy` breaks the const chain — a copy of a const value is a new, independent mutable variable (unless the copy itself is declared `const`).

---

### Labeled Loop Control

Break or continue outer loops from nested ones:

```axis
@outer for i in range(0, 10):
    for j in range(0, 10):
        when i * j > 50:
            stop @outer          # breaks the outer loop

        when j == 5:
            skip @outer          # continues to next i
```

**Syntax:** Place `@label` before `for`, `while`, or `repeat`. Use `stop @label` or `skip @label` to target that loop. The label can be any valid identifier. Without a label, `stop`/`skip` still target the innermost loop as before.

---

### Type Casting with `copy...as` and `update...as`

Explicit integer type conversions — no implicit casts, ever.

#### `copy...as` — Create a New Value with a Different Type

```axis
x: i64 = 1000
y: i32 = copy x as i32      // widening or narrowing — always explicit
```

`copy...as` creates a **new independent value** of the target type. The original variable is unchanged. Can only be used in variable declarations.

#### `update...as` — Change a Variable's Type In-Place

```axis
x: i64 = 1000
update x as i32              // x is now i32, value truncated to 32 bits
```

`update...as` changes both the type and storage of an existing variable. After the update, the variable has the new type.

#### Widening and Narrowing

| Conversion | Direction | Behaviour |
| --- | --- | --- |
| i8 → i32 | Widening | Sign-extended, always safe |
| i32 → i64 | Widening | Sign-extended, always safe |
| i64 → i32 | Narrowing | Truncated — **compiler warning** |
| i32 → i8 | Narrowing | Truncated — **compiler warning** |
| i32 → i32 | Same type | **Compiler warning** (no effect) |

Narrowing casts produce a warning but compile successfully — the compiler trusts the programmer to know the value range.

#### What's Not Allowed

| Attempt | Error |
| --- | --- |
| `copy x as bool` | Cannot cast between 'i32' and 'bool' |
| `copy flag as i32` (bool) | Cannot cast between 'bool' and 'i32' |
| `copy...as` in assignment | 'copy...as' can only be used in variable declarations |
| `update` on undeclared var | undefined variable |

---

### Automatic Variable Aliasing

Assigning a variable to another variable creates an alias automatically — both names share the same storage. No keyword needed.

#### Syntax

```axis
x: i32 = 42
y: i32 = x                   // y is an alias for x — automatic
```

Mutations through either name are visible through both:

```axis
x: i32 = 10
y: i32 = x
y = 55
writeln(x)                   // prints 55
```

To create an independent copy, use `copy`:

```axis
x: i32 = 10
y: i32 = copy x              // y is a separate value
y = 55
writeln(x)                   // prints 10 — x is unchanged
```

#### `const` Interaction

Aliases inherit constness from their source. A `const` alias of a mutable variable is an error:

```axis
x: i32 = 10
y: i32 = x                   // ✓ — both mutable

const a: i32 = 42
const b: i32 = a             // ✓ — both const

const c: i32 = x             // ✗ — Cannot declare const alias of mutable variable 'x'
```

`copy` breaks the const chain:

```axis
const a: i32 = 42
b: i32 = copy a              // ✓ — b is an independent mutable copy
```

#### Restrictions

| Attempt | Error |
| --- | --- |
| `const` alias of mutable | Cannot declare const alias of mutable variable |
| Assign to alias of `const` | Cannot assign to immutable variable |

---

### Array Index Access Requires `copy`

Reading an array element into a variable now requires explicit `copy`. Array elements cannot be aliased — they can only be copied:

```axis
arr: (i32; 3) = [10, 20, 30]

x: i32 = copy arr[0]       // OK — explicit copy
y: i32 = arr[1]            // ERROR — Array index access requires 'copy'

x = copy arr[2]            // OK — assignment with copy
x = arr[2]                 // ERROR
```

Direct use in expressions still works without `copy`:

```axis
writeln(arr[0])            // OK — no variable binding
if arr[1] > 10:            // OK — used in expression
arr[0] = 99                // OK — writing to array element
```

`for`-loops copy implicitly — no `copy` keyword needed:

```axis
for x in arr:              // OK — implicit copy
    writeln(x)
```

> **Why?** A plain assignment like `x = y` creates an alias (shared storage). Array elements live inside the array's storage and cannot be shared individually. `copy` makes it explicit that you're extracting a value, not creating an alias.

---

### New `input()` System

Explicit, type-safe user input — replacing the old `read`/`readln`/`readchar`:

```axis
x: i32 = input("Enter a number: ")
if x_input_failed():
    writeln("Invalid input!")

name: str = input()         // no prompt
```

- **Typed result:** The variable's type determines how input is parsed — `i32` reads an integer, `str` reads a line
- **Optional prompt:** `input("Prompt: ")` shows a message, `input()` reads silently
- **Per-variable error handling:** `{var}_input_failed()` returns `bool` — e.g. `x_input_failed()`
- **`const` blocked:** `const x: i32 = input()` is a compile error
- Supported in PE and ELF backends (scanf/printf runtime stubs)

---

### String Operations (Compile Mode)

Strings are now full values in compile mode:

```axis
name: str = "World"
greeting: str = "Hello, " + name    // concatenation
if name == "Alice":                  // comparison
    writeln("Found Alice!")
```

- Concatenation with `+` operator
- Comparison with `==` and `!=`
- Runtime stubs (`__axis_str_concat`, `__axis_str_eq`) in PE and ELF backends

---

## 🗑️ Removed

### `read()`, `readln()`, `readchar()`, `read_failed()`

Too implicit. `read()` didn't make the return type clear, `read_failed` was a global variable instead of a variable-specific check. Replaced by `input()` + `{var}_input_failed()`.

### `give` Keyword

Only `return` remains as the return keyword. `give` was redundant.

---

## 🐛 Bug Fixes

### Bug #1: Negative i32 Values Printed as Unsigned (Critical)

**Symptom:** `x: i32 = -1` followed by `writeln(x)` printed `4294967295` instead of `-1`.

**Root Cause:** `emit_load_rbp_sx()` in `x64.c` used a 32-bit `mov` instruction for 4-byte values, which zero-extends into the upper 32 bits of the 64-bit register. The `write`/`writeln` code path then interpreted this as an unsigned 64-bit value.

**Fix:** Changed the 4-byte load case to use `movsxd r64, dword [rbp+off]` (opcode `0x63` with REX.W prefix), which sign-extends the 32-bit value into the full 64-bit register.

### Bug #3: `.rdata` Section ACCESS_VIOLATION with `input()`

**Symptom:** Programs using `input()` crashed with ACCESS_VIOLATION on Windows.

**Root Cause:** `input()` runtime stubs (scanf buffer, input_failed flag) write data into `.rdata`. The section was marked READ-only after the v1.2.1 W^X fix.

**Fix:** Restored WRITE flag on `.rdata` section for programs that use `input()` stubs.

### Bug #2: Negative Integer Literals Could Not Be Assigned to i8/i16

**Symptom:** `a: i8 = -1` produced `"Type mismatch in 'a': expected i8, got i32"`.

**Root Cause:** Negative integer literals are parsed as `EXPR_UNARY(TOK_MINUS, EXPR_INT_LIT)`. The `coerce_literal()` function in `semantic.c` only checked for `EXPR_INT_LIT`, so it never matched negated literals — they kept their inferred type `i32` and failed the type check.

**Fix:** Added a second coercion path in `coerce_literal()` that recognises `EXPR_UNARY` with `TOK_MINUS` on an `EXPR_INT_LIT` operand and coerces both the unary expression and its operand to the target integer type.

### Bug #4: 64-bit IMUL Used 32-bit Encoding (Critical)

**Symptom:** `i64` multiplication produced wrong results — e.g. `100000 * 100000` returned a truncated 32-bit value.

**Root Cause:** `emit_imul_ri3()` in `x64.c` always emitted the 32-bit IMUL form (no REX.W prefix), even when the operands were 8 bytes wide. The upper 32 bits of the result were silently lost.

**Fix:** Added a `wide` parameter to `emit_imul_ri3()`. When the operand size is 8 bytes, the function emits REX.W for full 64-bit multiplication.

### Bug #5: Enum Type Names Not Resolved to Underlying Size (High)

**Symptom:** Variables of enum type received wrong stack allocation — e.g. a 4-variant enum allocated 1 byte instead of 4 bytes.

**Root Cause:** `type_size()` in `irgen.c` was called with the enum's type name string (e.g. `"Color"`), which it didn't recognise — falling back to 0 or a wrong default. The function only handled primitive type names like `i32`, `u8`, etc.

**Fix:** Added `irgen_type_size()` wrapper that first checks if the type name matches a declared enum and, if so, resolves it to the underlying integer type before calling `type_size()`.

### Bug #6: Field Default Values Not Emitted (High)

**Symptom:** Field members with default values retained garbage when the constructor call had fewer arguments than the field has members.

**Root Cause:** `gen_vardecl()` in `irgen.c` only emitted IR for arguments passed in the constructor call. Members beyond the argument count — which should use their declared default values — were never initialised.

**Fix:** After emitting constructor arguments, `gen_vardecl()` now iterates over remaining members and emits `IR_STORE` instructions for each default value.

### Bug #7: LICM Missed Stores Between Multiple Back-Edges (High)

**Symptom:** Loop-invariant code motion incorrectly hoisted stores that were modified inside the loop — producing wrong values at `-O1` and above (4 test failures at O2/O3/Os).

**Root Cause:** `licm_func()` in `opt_loop.c` scanned only the first back-edge to determine the loop body range. After jump threading, a single loop can have multiple back-edges to the same header. Stores in blocks between the first and subsequent back-edges were outside the detected range and thus incorrectly considered invariant.

**Fix:** `licm_func()` now scans all back-edges targeting the same header and takes the maximum block index as the loop body end — ensuring all loop blocks are included in the invariance analysis.

---

## 📋 Updated Examples

New example programs showcasing v1.3.0 features:

- `21_constants.axis` — `const` declarations with scalars, expressions, arrays, and mutable coexistence

---

## 📚 Updated Documentation

- **`docs/guide/02-types.md`** — Updated with `const` modifier documentation
- **`docs/guide/03-control-flow.md`** — Labeled loop control section added
- **`docs/guide/05-arrays.md`** — New section: array index access requires `copy`

---

## 🔒 Compiler Hardening (Internal Audit)

A full internal audit addressed 49 items across security, correctness, and code quality.

### Security

- **OOM safety**: All 62 `malloc`/`realloc`/`calloc` call sites wrapped in `xmalloc`/`xrealloc`/`xcalloc` — the compiler aborts cleanly instead of dereferencing `NULL`.
- **Integer overflow**: `strtoull` ERANGE check added in the lexer — integer literals that exceed `u64` range are now rejected.
- **File size limit**: `.axis` input files are validated before allocation — prevents unbounded `malloc` on malformed input.
- **Path overflow**: File path buffers bounded with `PATH_MAX` — no stack overflow on deeply nested paths.

### Correctness

- **Return-path analysis**: Functions missing a `return` on any code path are rejected at compile time.
- **SSA lost-copy fix**: Parallel-copy insertion in SSA destruction prevents overwritten phi sources — eliminates a class of wrong-code bugs.
- **SSA liveness**: Liveness analysis now crosses basic-block boundaries — correct register pressure and allocation.
- **GVN hash table**: Upgraded from fixed 1024 slots to dynamic resizing — no missed optimizations on large functions.
- **Unsigned comparisons**: Signed condition codes replaced with unsigned variants (`ja`/`jb` instead of `jg`/`jl`) — correct behavior for large unsigned values.
- **64-bit IMUL**: `emit_imul_ri3()` now emits REX.W for 8-byte operands — correct results for `i64` multiplication.
- **Enum type resolution**: `irgen_type_size()` resolves enum type names to underlying integer type before size calculation.
- **Field defaults**: Constructor calls with fewer arguments than field members now emit default values for remaining members.
- **LICM back-edges**: Loop body detection scans all back-edges to the same header — correct invariance analysis after jump threading.
- **Enum bounds**: Out-of-range enum values are now rejected at compile time.
- **Import table**: Off-by-one in PE import entry count corrected.
- **String dedup**: Duplicate string literals deduplicated in `.rdata`/`.rodata` — smaller binaries.

### Code Quality

- **opt.c split**: Monolithic 2000-line optimizer split into 8 focused sub-files (`opt_fold`, `opt_elim`, `opt_flow`, `opt_loop`, `opt_mem`, `opt_inline`, `opt_promote`, `opt_backend`).
- **gen_instr() refactor**: Single 500-line codegen function refactored into 5 category helpers.
- **Dead code removal**: 5 unused 32-bit-only x64 emit helpers removed (superseded by width-aware `_w` variants).
- **Error output**: ANSI color escapes now use TTY detection — clean output when piped to files.
- **`--dump-ast` flag**: New diagnostic flag prints the full AST after parsing via dedicated `ast_dump.c` module.

### New Tests

7 new edge-case test programs added: division edge cases, integer bounds, string escapes, field access, and error-path validation.

---

## 🎉 Summary

AXIS v1.3.0 makes the language safer and more expressive:

- **`const`** eliminates accidental mutation — the compiler catches it at every assignment path, every index write, every field write, and every `update` parameter.
- **Labeled loops** solve the nested-loop escape problem cleanly with `stop @name` and `skip @name`.
- **`copy...as` / `update...as`** give explicit control over integer type conversions — no implicit casts, clear warnings on narrowing.
- **Automatic aliasing** — `x: i32 = y` shares storage, `x: i32 = copy y` creates an independent copy.
- **Array index `copy`** — `x = arr[0]` is now an error, `x = copy arr[0]` is required. Makes data extraction explicit.
- **`input()` system** — type-safe user input with per-variable error handling. Replaces `read`/`readln`/`readchar`.
- **String operations** — concatenation (`+`) and comparison (`==`, `!=`) in compile mode.
- **7 bug fixes** — negative integer display, negative literal coercion, `.rdata` access violation, 64-bit IMUL encoding, enum type size resolution, field default values, and LICM multi-back-edge handling.
- **49-item internal audit** — OOM safety, SSA correctness, unsigned codegen, GVN scaling, optimizer modularization, dead-code removal, and 7 new edge-case tests.

The compiler remains minimal — binaries are still in the 2–4 KB range with zero runtime dependencies. Next up: a new SSA-based IR architecture.

*Think in values, compile to metal.*
