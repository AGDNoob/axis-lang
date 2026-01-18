# AXIS

![Version](https://img.shields.io/badge/version-1.0.2--beta-blue) ![Platform](https://img.shields.io/badge/platform-Linux%20x86--64-lightgrey) ![License](https://img.shields.io/badge/license-MIT-green)

**A minimalist programming language with Python-like syntax and dual execution modes.**

AXIS can run as an interpreted scripting language OR compile directly to x86-64 machine code.

---

## 🚀 Quick Start

### Installation

**All Platforms (Windows, macOS, Linux):**
```bash
git clone https://github.com/AGDNoob/axis-lang
cd axis-lang
```

No dependencies required beyond Python 3.7+.

**Linux only (optional CLI):**
```bash
cd installer && ./install.sh --user
```

### Hello World (Script Mode)

Works on **Windows**, **macOS**, and **Linux**:

```bash
# Windows (PowerShell)
python compilation_pipeline.py run hello.axis

# macOS/Linux
python3 compilation_pipeline.py run hello.axis
```

### Hello World (Compile Mode)

**Linux x86-64 only** - creates native ELF executable:
```bash
cat > hello.axis << 'EOF'
mode compile

func main() -> i32:
    give 42
EOF

python3 compilation_pipeline.py hello.axis -o hello --elf
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
python compilation_pipeline.py run script.axis
```

### Compile Mode

Compile mode generates native Linux x86-64 executables with zero runtime dependencies.

```python
mode compile

func main() -> i32:
    x: i32 = 10
    y: i32 = 20
    give x + y
```

Build with:
```bash
python compilation_pipeline.py program.axis -o program --elf
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

**Types:** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `bool`, `str`, `ptr`

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
python compilation_pipeline.py run examples/01_hello_world.axis

# Compile mode (example 20)
python compilation_pipeline.py examples/20_compile_mode.axis -o demo --elf
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
├── assembler.py               # Machine code
├── executable_format_generator.py  # ELF64
├── transpiler.py              # Python transpiler
├── examples/                  # 20 example programs
├── axis-vscode/               # VS Code extension
└── installer/                 # Linux installer
```

---

## 🛠️ Usage

### Commands

```bash
# Run script mode
python compilation_pipeline.py run program.axis

# Compile to ELF64 (Linux)
python compilation_pipeline.py program.axis -o output --elf

# Compile with verbose output
python compilation_pipeline.py program.axis -o output --elf -v

# Show hex dump only
python compilation_pipeline.py program.axis
```

### VS Code Extension

The `axis-vscode/` folder contains syntax highlighting for `.axis` files.

---

## ⚠️ Platform Requirements

**Compile mode:**
- Linux x86-64 only (Ubuntu, Debian, Fedora, Arch, etc.)
- Generated binaries are native ELF64 executables

**Script mode:**
- Any platform with Python 3.7+
- Windows, macOS, Linux all supported

---

## 📊 Performance

| Mode | Overhead | Binary Size | Dependencies |
|------|----------|-------------|--------------|
| Script | ~30% vs Python | N/A | Python 3.7+ |
| Compile | Native speed | ~4KB | None |

---

## 🗺️ Roadmap

### Implemented ✓
- [x] Dual-mode execution (script/compile)
- [x] Python transpiler for script mode
- [x] ELF64 native compilation
- [x] All integer types (i8-i64, u8-u64)
- [x] Control flow (when, while, repeat, stop, skip)
- [x] Functions
- [x] Arithmetic and bitwise operators
- [x] I/O (write, writeln, read, readln, readchar)
- [x] VS Code syntax highlighting

### Planned
- [ ] Function parameters in compile mode
- [ ] Structs and arrays
- [ ] Pointer arithmetic
- [ ] Standard library
- [ ] Language Server Protocol (LSP)

---

## 📜 License

MIT License

---

**AXIS – Python syntax. Native performance. Your choice.**
