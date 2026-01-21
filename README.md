# AXIS

![Version](https://img.shields.io/badge/version-1.1.0--beta-blue) ![Platform](https://img.shields.io/badge/platform-Linux%20x86--64-lightgrey) ![License](https://img.shields.io/badge/license-MIT-green)

**A minimalist programming language with Python-like syntax and dual execution modes.**

AXIS can run as an interpreted scripting language OR compile directly to x86-64 machine code.

---

## 🚀 Quick Start

### Installation

Download and run the one-click GUI installer for your platform:

| Platform | Installer | One-Liner |
|----------|-----------|----------|
| **Windows** | [install-windows.ps1](https://github.com/AGDNoob/axis-lang/raw/main/installer/install-windows.ps1) | Right-click → Run with PowerShell |
| **Linux** | [install-linux.sh](https://github.com/AGDNoob/axis-lang/raw/main/installer/install-linux.sh) | `curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-linux.sh \| bash` |
| **macOS** | [install-macos.sh](https://github.com/AGDNoob/axis-lang/raw/main/installer/install-macos.sh) | `curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-macos.sh \| bash` |

The installer will:
- ✅ Check/install Python 3.7+
- ✅ Download all AXIS files
- ✅ Set up the `axis` command
- ✅ Optionally install VS Code extension

### Hello World (Script Mode)

Works on **Windows**, **macOS**, and **Linux**:

```bash
axis run hello.axis
```

### Hello World (Compile Mode)

**Linux x86-64 only** - creates native ELF executable:
```bash
cat > hello.axis << 'EOF'
mode compile

func main() -> i32:
    give 42
EOF

axis build hello.axis -o hello --elf
./hello && echo $?  # Output: 42
```

---

## 📖 Dual-Mode Execution

AXIS supports two execution modes:

| Mode | Declaration | Execution | Speed | Use Case |
|------|-------------|-----------|-------|----------|
| **Script** | `mode script` | Transpiled to Python | Fast startup | Scripting, prototyping |
| **Compile** | `mode compile` | Native x86-64 ELF | Maximum performance | Systems programming |

### Script Mode

Script mode transpiles AXIS to Python and executes it. ~30% overhead vs native Python.

```python
mode script

writeln("Script mode example")
x: i32 = 10
writeln(x)
```

Run with:
```bash
axis run script.axis
```

### Compile Mode

Compile mode generates native Linux x86-64 executables. The output binary has no runtime dependencies.

```python
mode compile

func main() -> i32:
    x: i32 = 10
    y: i32 = 20
    give x + y
```

Build with:
```bash
axis build program.axis -o program --elf
./program
```

---

## 📚 Language Reference

### Variables

```python
x: i32 = 42          # 32-bit signed integer
y: i64 = 1000000     # 64-bit signed integer
small: i8 = 127      # 8-bit signed integer
flag: bool = True    # Boolean
```

**Types:** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `bool`, `str`

### Output

```python
write("Hello ")      # Output without newline
writeln("World!")    # Output with newline
writeln(42)          # Works with numbers
```

### Conditionals

```python
when x > 0:
    writeln("positive")

when x < 0:
    writeln("negative")
```

### Loops

```python
# Infinite loop with break
i: i32 = 0
repeat:
    writeln(i)
    i = i + 1
    when i >= 10:
        stop

# While loop
while i < 20:
    i = i + 1
```

**Keywords:**
- `repeat:` – Infinite loop
- `while condition:` – Conditional loop  
- `stop` – Break out of loop
- `skip` – Continue to next iteration

### Functions (Script Mode)

```python
mode script

func greet():
    writeln("Hello!")

func add(a: i32, b: i32) -> i32:
    give a + b

greet()
result: i32 = add(10, 20)
writeln(result)
```

### Functions (Compile Mode)

```python
mode compile

func main() -> i32:
    x: i32 = 42
    give x
```

### Fields

Fields are custom data types that group related values together.

```python
# Define a field type
Vec2: field:
    x: i32 = 0
    y: i32 = 0

# Create and use a field
pos: Vec2
pos.x = 100
pos.y = 200
writeln(pos.x)   # 100
```

**Nested Fields:**

```python
Player: field:
    name: str = ""
    position: Vec2    # Use another field type

p: Player
p.name = "Alice"
p.position.x = 50
```

**Inline Anonymous Fields:**

```python
Game: field:
    home: field:          # Inline field (no separate type)
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

```python
# Array of a named field type
players: (Player; 5)
players[0].name = "Bob"

# Array of inline fields
team: field:
    members: (field; 11): [
        name: str = ""
        number: i32 = 0
    ]

t: team
t.members[0].name = "Alice"
t.members[0].number = 10
```

**Fields with Update Modifier:**

```python
func move(update pos: Vec2, dx: i32, dy: i32):
    pos.x = pos.x + dx
    pos.y = pos.y + dy

myPos: Vec2
move(myPos, 10, 20)    # myPos is modified
```

**Copying Fields:**

```python
original: Vec2
original.x = 100

duplicate: Vec2 = copy original    # Deep copy (default: runtime optimized)
duplicate.x = 999                  # Doesn't affect original

# Copy modes for compile mode:
# copy.runtime - optimized for fast execution (default)
# copy.compile - optimized for fast compilation
fast_copy: Vec2 = copy.runtime original   # Best runtime performance
quick_build: Vec2 = copy.compile original # Faster to compile
```

### Enums

Enums define a set of named constants with a configurable underlying integer type:

```python
# Default underlying type (i32)
Color: enum:
    Red
    Green
    Blue

# Explicit underlying type
Status: enum u8:
    Pending = 0
    Active = 1
    Completed = 100

Priority: enum i64:
    Low
    Medium
    High

c: Color = Color.Red
s: Status = Status.Active

when c == Color.Green:
    writeln("It's green!")
```

Enum features:
- **Underlying type**: Specify `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, or `u64` (default: `i32`)
- **Auto-increment**: Variants without explicit values auto-increment from 0
- **Explicit values**: Use `= value` to assign specific integer values

### Match Statements

Match statements provide pattern matching on values:

```python
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

# Prints: two
```

Match with enums:

```python
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

### Operators

**Arithmetic:** `+`, `-`, `*`, `/`, `%`

**Comparison:** `==`, `!=`, `<`, `<=`, `>`, `>=`

**Bitwise:** `&`, `|`, `^`, `<<`, `>>`

### Comments

```python
// C-style comment
# Python-style comment
```

---

## 📁 Examples

The `examples/` folder contains 20 example programs:

| # | Example | Description |
|---|---------|-------------|
| 01 | `hello_world.axis` | Basic output |
| 02 | `variables.axis` | Variable types |
| 03 | `arithmetic.axis` | Math operations |
| 04 | `conditionals.axis` | `when` branching |
| 05 | `loops.axis` | `repeat` loops |
| 06 | `while_loops.axis` | `while` loops |
| 07 | `break_continue.axis` | `stop` and `skip` |
| 08 | `nested_loops.axis` | Multiplication table |
| 09 | `boolean_logic.axis` | Bitwise logic |
| 10 | `comparison.axis` | Comparison operators |
| 11 | `bitwise.axis` | Bit manipulation |
| 12 | `functions.axis` | Function definitions |
| 13 | `fibonacci.axis` | Fibonacci sequence |
| 14 | `prime_numbers.axis` | Prime checker |
| 15 | `factorial.axis` | Factorial calculation |
| 16 | `guessing_game.axis` | Binary search |
| 17 | `ascii_art.axis` | Pattern drawing |
| 18 | `gcd.axis` | Euclidean algorithm |
| 19 | `fizzbuzz.axis` | Classic challenge |
| 20 | `compile_mode.axis` | Native compilation |

Run examples:
```bash
# Script mode (examples 01-19)
axis run examples/01_hello_world.axis

# Compile mode (example 20)
axis build examples/20_compile_mode.axis -o demo --elf
```

---

## 🏗️ Architecture

### Compilation Pipeline

```
Source (.axis)
     │
     ▼
┌─────────────┐
│   Lexer     │  tokenization_engine.py
└──────┬──────┘
       ▼
┌─────────────┐
│   Parser    │  syntactic_analyzer.py
└──────┬──────┘
       │
       ├──────────────────┐
       ▼                  ▼
┌─────────────┐    ┌─────────────┐
│ Transpiler  │    │  Semantic   │
│ (Script)    │    │  Analyzer   │
└──────┬──────┘    └──────┬──────┘
       ▼                  ▼
┌─────────────┐    ┌─────────────┐
│   Python    │    │  Code Gen   │
│   exec()    │    │  (x86-64)   │
└─────────────┘    └──────┬──────┘
                          ▼
                   ┌─────────────┐
                   │  Assembler  │
                   └──────┬──────┘
                          ▼
                   ┌─────────────┐
                   │  ELF64      │
                   │  Executable │
                   └─────────────┘
```

### Project Structure

```
axis-lang/
├── compilation_pipeline.py    # Main driver
├── tokenization_engine.py     # Lexer
├── syntactic_analyzer.py      # Parser + AST
├── semantic_analyzer.py       # Type checker
├── code_generator.py          # x86-64 codegen
├── executable_format_generator.py  # ELF64
├── examples/                  # 20 example programs
├── axis-vscode/               # VS Code extension
└── installer/                 # GUI installers (Windows/Linux/macOS)
```

---

## 🛠️ Usage

### Commands

After installation, the `axis` command is available on all platforms:

```bash
axis run script.axis        # Run in script mode
axis build prog.axis        # Compile to native binary (Linux only)
axis check prog.axis        # Validate syntax without running
axis info                   # Show installation info
axis version                # Show version
axis help                   # Show all commands
```

### VS Code Extension

The installer can optionally install the VS Code extension for syntax highlighting.
Or manually: `axis-vscode/` folder contains the extension source.

### Uninstall

Run the same installer again and select **Uninstall**.

---

## ⚠️ Platform Requirements

**All modes require Python 3.7+** to run the AXIS compiler.

**Compile mode:**
- Linux x86-64 only (Ubuntu, Debian, Fedora, Arch, etc.)
- Generated binaries are native ELF64 executables (no runtime dependencies)

**Script mode:**
- Any platform with Python 3.7+
- Windows, macOS, Linux all supported

---

## 📊 Performance

| Mode | Overhead | Binary Size | Compiler Requires | Output Requires |
|------|----------|-------------|-------------------|----------------|
| Script | ~30% vs Python | N/A | Python 3.7+ | Python 3.7+ |
| Compile | Native speed | ~4KB | Python 3.7+ | Nothing (standalone) |

---

## 🗺️ Roadmap

### Implemented ✓
- [x] Dual-mode execution (script/compile)
- [x] Python transpiler for script mode
- [x] ELF64 native compilation
- [x] All integer types (i8-i64, u8-u64)
- [x] Control flow (when, while, repeat, stop, skip)
- [x] Functions with update/copy modifiers
- [x] Fields (custom data types with nesting)
- [x] Arrays (including arrays of fields)
- [x] Arithmetic and bitwise operators
- [x] I/O (write, writeln, read, readln, readchar)
- [x] VS Code syntax highlighting

### Planned
- [ ] Function parameters in compile mode
- [ ] Standard library
- [ ] Language Server Protocol (LSP)

---

## 📜 License

MIT License

---

**AXIS – Python syntax. Native performance. Your choice.**
