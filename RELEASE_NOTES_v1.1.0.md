# AXIS v1.1.0 - Major Update

**Philosophy:** "Think in values, compile to metal"

---

## 💭 Developer Note

This release took a while. I originally planned to add pointers to AXIS - you know, the classic C-style `&x` and `*p` stuff. But the more I worked on it, the more it felt wrong. Pointers gave me a really hard time with my whole thought process about what AXIS should be.

AXIS isn't meant to be another C. It's meant to feel like Python while compiling to native code. Pointers are powerful, but they're also confusing, error-prone, and honestly... not fun. So I scrapped them entirely and came up with something better: **value-oriented programming**.

Instead of thinking about memory addresses, you think about values. Instead of passing pointers to modify variables, you use `update`. Instead of wondering if two arrays share memory, you explicitly `copy`. It's simpler, safer, and more AXIS.

Oh, and one more thing: **Python is gone as the compiler language.** The entire compiler (AXCC) has been rewritten from scratch in C. Why? Because a language that compiles to native code shouldn't depend on an interpreter to get there. Lexer, parser, semantic analysis, IR generation, x64 codegen, PE and ELF output — all in one fast binary. No pip, no Python runtime, no dependencies. Just `axis.exe` (or `./axis` on Linux) and you get a native executable. The old Python pipeline has been retired.

---

## 🚫 Why No Methods?

You might notice that fields don't have methods. This was a deliberate choice.

I actually implemented methods at one point - you could do things like `player.move(10, 20)` and it worked. But the more I looked at it, the more it bothered me. Methods make AXIS look like an object-oriented language, and **AXIS is not object-oriented**.

AXIS is **value-oriented**. Data and functions are separate. Functions operate on values passed via parameters.

**Instead of this (OOP style):**

```text
player.move(10, 20)
```

**AXIS does this (value-oriented):**

```axis
move(player, 10, 20)            # Read-only
move(update player, 10, 20)     # Modifies player
```

This is more explicit, more consistent, and keeps the language focused on its core philosophy: *values, not objects*.

If you want something modified, you say `update`. If you just want to read it, you pass it normally. No hidden `this` pointer, no method dispatch, no confusion about what's being modified.

---

## 🎯 Design Philosophy

AXIS v1.1.0 introduces a **value-oriented programming paradigm**:

- **Values are first-class citizens** - you work with values, not memory locations
- **Memory is an implementation detail** - the compiler handles it
- **Explicit is better than implicit** - array copies require the `copy` keyword
- **No pointers** - there's something better

---

## 🚀 New Features

### Arrays

Fixed-size arrays with full dynamic indexing support:

```axis
# Declaration with type and size
arr: (i32; 5) = [1, 2, 3, 4, 5]

# Access (constant or dynamic index)
x: i32 = arr[0]
x = arr[i]

# Assignment
arr[0] = 42
arr[i] = x + 1
```

**Supported element types:** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `str`

---

### Compound Assignment Operators

All arithmetic and bitwise compound assignment operators are now supported:

```axis
x: i32 = 10

// Arithmetic
x += 5      // x = x + 5  → 15
x -= 3      // x = x - 3  → 12
x *= 2      // x = x * 2  → 24
x /= 4      // x = x / 4  → 6
x %= 4      // x = x % 4  → 2

// Bitwise
y: i32 = 12
y &= 10     // y = y & 10  (AND)
y |= 3      // y = y | 3   (OR)
y ^= 5      // y = y ^ 5   (XOR)

// Shift
z: i32 = 4
z <<= 2     // z = z << 2  → 16
z >>= 1     // z = z >> 1  → 8
```

**Works with arrays and fields too:**

```axis
arr: (i32; 3) = [10, 20, 30]
arr[0] += 5     // arr[0] = 15

Counter: field:
    value: i32 = 0

c: Counter
c.value = 100
c.value += 50   // c.value = 150
```

---

### For Loops

Iterate over ranges or arrays with the new `for...in` syntax:

```axis
// Range iteration
for i in range(0, 10):
    writeln(i)              // 0 to 9

for i in range(0, 10, 2):
    writeln(i)              // 0, 2, 4, 6, 8 (step by 2)

for i in range(5, 0, -1):
    writeln(i)              // 5, 4, 3, 2, 1 (countdown)

// Array iteration (script mode)
arr: (i32; 5) = [1, 2, 3, 4, 5]
for x in arr:
    writeln(x)
```

**Features:**

- `range(start, end)` - iterate from start to end-1
- `range(start, end, step)` - with custom step (positive or negative)
- `break` and `continue` work inside for loops
- Nested for loops supported
- Works in both script and compile mode

---

### Windows Support for Compile Mode

In v1.0.2, compile mode was **Linux-only** (ELF64). With v1.1.0, AXCC now also generates **native Windows PE executables**. You can compile AXIS programs to standalone `.exe` files — no GCC, no NASM, no linker required.

```bash
# On Windows — produces a native .exe
axis hello.axis -o hello.exe

# Cross-compilation flags
axis hello.axis -o hello.exe --pe     # Force PE output (Windows)
axis hello.axis -o hello --elf        # Force ELF64 output (Linux)
```

---

### AXCC — Native Compiler in C

The compile mode backend has been completely rewritten from Python to C. AXCC is a single-pass native compiler that takes `.axis` source files and produces **Windows PE** or **Linux ELF64** executables directly — no assembler, no linker, no external tools.

**What changed:**

- Compiler implementation language: **Python → C**
- Output formats: **PE (Windows)** and **ELF64 (Linux)** — both from the same codebase
- Calling convention: Windows x64 ABI (RCX, RDX, R8, R9) internally, with Linux syscall remapping for ELF
- Build: `mingw32-make` on Windows, `make` on Linux — produces a single `axis.exe` / `axis` binary
- Zero runtime dependencies — no Python, no pip, no `.dll` files needed

**Architecture:**

```
.axis source → Lexer → Parser → AST → Semantic Analysis → IR → x64 Codegen → PE/ELF
```

All nine stages in ~3000 lines of C. Compilation is near-instant — most files compile in under 50ms.

---

### Improved Error Messages

Error messages now include file name, line number, column, and source context:

```text
tests/example.axis:4:10: error: Undefined variable: undefined_var
  4 | y: i32 = undefined_var + 5
               ^
```

**Features:**

- Precise location: file name, line number, column number
- Source line shown with visual caret pointing to error
- Clean output without stack traces (use `-v` for verbose debugging)
- Works for: undefined variables, type mismatches, undefined functions, etc.

---

### Logical Operators

Boolean logic with `and`, `or`, and `not`:

```axis
// Logical AND - both conditions must be true
when x > 0 and y < 100:
    writeln("In range")

// Logical OR - at least one condition must be true
when is_admin or has_permission:
    writeln("Access granted")

// Logical NOT - inverts the condition
when not is_locked:
    writeln("Unlocked")

// Combined expressions
when (a > 0 and b > 0) or force_allow:
    writeln("Allowed")
```

**Features:**

- Short-circuit evaluation (right side not evaluated if result is known)
- Both `not` and `!` work for logical NOT
- Works in both script and compile modes
- Proper operator precedence: `not` > `and` > `or`

---

### New Function Syntax

Functions now use a cleaner syntax with `return` as an alias for `give`:

```axis
# Return type after parameters (no arrow)
func add(a: i32, b: i32) i32:
    return a + b

# No return type for void functions
func greet():
    writeln("Hello!")

# 'give' still works
func multiply(x: i32, y: i32) i32:
    give x * y
```

---

### `update` Modifier - Copy-In/Copy-Out Semantics

The `update` modifier lets functions modify caller's variables - no pointers needed:

```axis
# Function modifies caller's variable
func double(update x: i32):
    x = x * 2

num: i32 = 25
double(num)
writeln(num)  # Output: 50
```

```axis
# Update with return value
func increment_and_square(update x: i32) i32:
    x = x + 1
    return x * x

val: i32 = 5
result: i32 = increment_and_square(val)
# val is now 6, result is 36
```

```axis
# Multiple update parameters - easy swap!
func swap(update a: i32, update b: i32):
    temp: i32 = a
    a = b
    b = temp

x: i32 = 100
y: i32 = 200
swap(x, y)
# x is now 200, y is now 100
```

**How it works:**

- On function entry: values are copied in
- On function return: values are copied back to caller
- No pointers, no references - just values!

---

### `copy` Keyword - Explicit Array Copies

Array assignment requires the `copy` keyword to make copies explicit:

```axis
original: (i32; 5) = [10, 20, 30, 40, 50]
copied: (i32; 5) = [0, 0, 0, 0, 0]

# Explicit copy
copied = copy original

# Modify copy - original unchanged
copied[0] = 999

writeln(original[0])  # Output: 10 (unchanged)
writeln(copied[0])    # Output: 999
```

**Without `copy`:**

```axis
arr2 = arr1  # Error: Array assignment requires 'copy' keyword
```

This prevents accidental aliasing bugs and makes your intent clear.

**Copy Modes (for compile mode):**

You can choose between runtime-optimized or compile-time-optimized copying:

```axis
# Default - optimized for fast execution
arr2: (i32; 5) = copy arr1
arr2 = copy.runtime arr1   # Same as above

# Optimized for faster compilation
arr3: (i32; 5) = copy.compile arr1
```

- **`copy` / `copy.runtime`**: Default copy mode.
- **`copy.compile`**: Alternative copy mode for future optimization.

> **Note:** In v1.1.0, both modes produce identical code. The syntax is fully supported and parsed, but differentiated code generation (REP MOVSB for `copy.runtime`, simpler instructions for `copy.compile`) is planned for v1.2.0.

---

### Fields - Custom Data Types

Fields let you define custom data types that group related values:

```axis
# Define a field type
Vec2: field:
    x: i32 = 0
    y: i32 = 0

# Create and use
pos: Vec2
pos.x = 100
pos.y = 200
writeln(pos.x)  # 100
```

**Nested Fields:**

```axis
Player: field:
    name: str = ""
    position: Vec2

p: Player
p.name = "Alice"
p.position.x = 50
p.position.y = 100
```

**Inline Anonymous Fields:**

```axis
Game: field:
    home: field:
        name: str = ""
        score: i32 = 0
    away: field:
        name: str = ""
        score: i32 = 0

g: Game
g.home.name = "Team A"
g.home.score = 3
```

**Arrays of Fields:**

```axis
# Array of named field type
players: (Player; 5)
players[0].name = "Bob"
players[0].position.x = 10

# Array of inline fields
Team: field:
    members: (field; 11): [
        name: str = ""
        number: i32 = 0
    ]

t: Team
t.members[0].name = "Alice"
t.members[0].number = 10
```

**Fields work with `update` and `copy`:**

```axis
func move(update pos: Vec2, dx: i32, dy: i32):
    pos.x = pos.x + dx
    pos.y = pos.y + dy

myPos: Vec2
move(myPos, 10, 20)  # myPos is modified

# Copy a field
original: Vec2
original.x = 100
duplicate: Vec2 = copy original
duplicate.x = 999  # original unchanged
```

Fields can be nested to unlimited depth - perfect for game entities, data structures, and more!

---

### Enums - Named Constants

Enums let you define a set of named constants with a configurable underlying type:

```axis
# Default underlying type (i32)
Color: enum:
    Red      # 0
    Green    # 1
    Blue     # 2

# Explicit underlying type (u8)
Status: enum u8:
    Pending = 0
    Active = 1
    Completed = 100

# Large values with i64
BigValues: enum i64:
    Small = 0
    Large = 1000000
```

**Usage:**

```axis
c: Color = Color.Red
s: Status = Status.Active

when c == Color.Green:
    writeln("It's green!")
```

**Supported underlying types:** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64` (default: `i32`)

---

### Match Statements - Pattern Matching

Match statements provide clean pattern matching on values:

```axis
x: i32 = 2
match x:
    1:
        writeln("one")
    2:
        writeln("two")
    3:
        writeln("three")
    _:
        writeln("other")

# Output: two
```

**Match with enums:**

```axis
c: Color = Color.Green
match c:
    Color.Red:
        writeln("Red")
    Color.Green:
        writeln("Green")
    Color.Blue:
        writeln("Blue")
```

The `_` pattern is the wildcard/default case that matches any value.

---


## � Benchmarks vs GCC

AXCC produces unoptimized code (no optimization passes yet), so this is a baseline comparison.
Four benchmarks, identical algorithms in AXIS and C, measured on the same machine (5 runs, median):

| Benchmark | AXIS | GCC -O0 | GCC -O2 | vs -O0 | vs -O2 |
|---|---:|---:|---:|---:|---:|
| Recursive Fibonacci (63M calls) | 554ms | 256ms | 78ms | 2.2x | 7.1x |
| Prime Counting (500K) | 161ms | 78ms | 77ms | 2.1x | 2.1x |
| Nested Loops (100M iterations) | 686ms | 347ms | 146ms | 2.0x | 4.7x |
| GCD Stress (2M calls) | 73ms | 52ms | 51ms | 1.4x | 1.4x |

**~1.4–2.2x vs GCC without optimizations.** For a compiler with no register allocator, no constant folding, and no inlining, that's a solid starting point. The GCD benchmark (1.4x vs GCC -O2) shows that pure arithmetic workloads are already close to optimized C.

All four benchmarks produce identical results between AXIS and GCC (verified via exit codes).

---

## 🔧 Technical Changes

### AXCC (Native Compiler — C)

- **New**: Complete rewrite of compile mode backend in C
- **New**: PE (Windows) executable generation
- **New**: ELF64 (Linux) executable generation
- **New**: Windows x64 calling convention (RCX, RDX, R8, R9 + stack for 5+ args)
- **New**: Linux syscall remapping stubs (write, read, exit) for ELF
- **New**: IR-based code generation pipeline
- **New**: 26 automated tests — all passing on both Windows and Linux

### Removed

- **Python Pipeline**: The old Python-based compilation pipeline (`compilation_pipeline.py`, `code_generator.py`, `assembler.py`, `executable_format_generator.py`, `transpiler.py`, `tokenization_engine.py`, `syntactic_analyzer.py`, `semantic_analyzer.py`, `error_handler.py`) has been retired. AXCC replaces it entirely.
- **keystone-engine dependency**: No longer needed — AXCC generates machine code directly.

---

## 🧪 Examples

### Function Parameters

```axis
func add(a: i32, b: i32) i32:
    return a + b

func double(update x: i32):
    x = x * 2

result: i32 = add(5, 10)
writeln(result)  # 15

num: i32 = 25
double(num)
writeln(num)  # 50
```

### Array Operations

```axis
arr: (i32; 5) = [10, 20, 30, 40, 50]

# Dynamic indexing
i: i32 = 2
writeln(arr[i])  # 30

# Copy array
backup: (i32; 5) = [0, 0, 0, 0, 0]
backup = copy arr
arr[0] = 999
writeln(backup[0])  # 10 (unaffected)
```

### Swap Without Pointers

```axis
func swap(update a: i32, update b: i32):
    temp: i32 = a
    a = b
    b = temp

x: i32 = 100
y: i32 = 200
swap(x, y)
writeln(x)  # 200
writeln(y)  # 100
```

### Fields

```axis
Vec2: field:
    x: i32 = 0
    y: i32 = 0

Player: field:
    name: str = ""
    pos: Vec2

func move(update p: Player, dx: i32, dy: i32):
    p.pos.x = p.pos.x + dx
    p.pos.y = p.pos.y + dy

hero: Player

hero.name = "Alice"
hero.pos.x = 10
hero.pos.y = 20

move(hero, 5, 10)

writeln(hero.name)   # Alice
writeln(hero.pos.x)  # 15
writeln(hero.pos.y)  # 30
```

---

## 🎉 Summary

AXIS v1.1.0 is a major update that establishes AXIS's identity: **value-oriented programming that compiles to native code** — and now the compiler itself is native too.

What's new:

- **AXCC** — the compiler backend rewritten from Python to C
- **Windows PE + Linux ELF64** — compile to native executables on both platforms
- **Arrays** with dynamic indexing
- **Fields** — custom data types with unlimited nesting
- **Enums** — named constants with configurable underlying types
- **Match statements** — clean pattern matching on values
- **`update` modifier** for modifying caller's variables
- **`copy` keyword** with runtime/compile modes for explicit array/field copies
- **New function syntax** with `return` keyword
- **26 automated tests** passing on Windows and Linux
- **Benchmarks**: ~1.4–2.2x vs GCC -O0 without any optimization passes

No pointers. No methods. No memory headaches. Just values.

*Think in values, compile to metal.*
