# AXIS v1.1.0 - Major Update

**Philosophy:** "Think in values, compile to metal"

---

## 💭 Developer Note

This release took a while. I originally planned to add pointers to AXIS - you know, the classic C-style `&x` and `*p` stuff. But the more I worked on it, the more it felt wrong. Pointers gave me a really hard time with my whole thought process about what AXIS should be.

AXIS isn't meant to be another C. It's meant to feel like Python while compiling to native code. Pointers are powerful, but they're also confusing, error-prone, and honestly... not fun. So I scrapped them entirely and came up with something better: **value-oriented programming**.

Instead of thinking about memory addresses, you think about values. Instead of passing pointers to modify variables, you use `update`. Instead of wondering if two arrays share memory, you explicitly `copy`. It's simpler, safer, and more AXIS.

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
- **No pointers** - we have something better

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

- **`copy` / `copy.runtime`**: Uses optimized CPU instructions (REP MOVSB). Best runtime performance, slightly longer compile time.
- **`copy.compile`**: Uses simpler code generation. Faster to compile, good for quick iteration during development.

In script mode, both behave identically.

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

### Semantic Analysis for Script Mode

Script mode now runs full semantic analysis:

- Type checking for all expressions
- `copy` keyword enforcement
- Field type checking and access validation
- Better error messages

---

## 🔧 Technical Changes

### Tokenizer

- Added: `UPDATE`, `COPY`, `RETURN`, `FIELD`, `DOT`, `ENUM`, `MATCH` tokens

### Parser

- Added: `Parameter` dataclass with `modifier` field
- Added: `CopyExpr` AST node for `copy <expr>`
- Added: `ArrayType`, `ArrayLiteral`, `IndexAccess` AST nodes
- Added: `FieldDef`, `FieldMember`, `FieldAccess` AST nodes
- Added: `EnumDef`, `EnumVariant`, `EnumAccess` AST nodes
- Added: `Match`, `MatchArm` AST nodes for pattern matching
- Updated: Function parsing for new return type syntax
- Added: Field definition parsing with inline and nested support
- Added: Enum definition parsing with optional underlying type

### Semantic Analyzer

- Added: `analyze_copy_expr` for copy expressions
- Added: Array assignment check (requires `copy` keyword)
- Added: Top-level statement analysis for script mode
- Added: Array type checking and size validation
- Added: Field type tracking and validation
- Added: Field access chain analysis with nested/inline support
- Added: Field size calculation for arrays of fields
- Added: Enum type tracking with configurable underlying types
- Added: Match statement analysis with pattern validation

### Code Generator

- Added: Array initialization and dynamic index access
- Added: Parameter copying from registers to stack
- Added: Full function parameter support

### Transpiler

- Added: `update` modifier handling with tuple return/unpacking
- Added: `CopyExpr` transpilation using `list()` for arrays, `deepcopy` for fields
- Added: Semantic analysis integration
- Added: Field type transpilation to Python classes
- Added: Field instantiation and member initialization
- Added: Enum transpilation to Python classes with class attributes
- Added: Match statement transpilation to if-elif-else chains

---

## 📦 Dependencies

**Optional:**

- `keystone-engine>=0.9.2` - Full x86-64 assembler support

```bash
pip install keystone-engine
```

---

## 🧪 Examples

### Function Parameters

```axis
mode script

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
mode script

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
mode script

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
mode script

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

AXIS v1.1.0 is a major update that establishes AXIS's unique identity: **value-oriented programming that compiles to native code**.

New features:

- **Arrays** with dynamic indexing
- **Fields** - custom data types with unlimited nesting
- **Enums** - named constants with configurable underlying types
- **Match statements** - clean pattern matching on values
- **`update` modifier** for modifying caller's variables
- **`copy` keyword** with runtime/compile modes for explicit array/field copies  
- **New function syntax** with `return` keyword

No pointers. No methods. No memory headaches. Just values.

*Think in values, compile to metal.*
