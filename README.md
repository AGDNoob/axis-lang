# AXIS

![Version](https://img.shields.io/badge/version-1.0.2--beta-blue) ![Platform](https://img.shields.io/badge/platform-Linux%20x86--64-lightgrey) ![License](https://img.shields.io/badge/license-MIT-green)

**A minimalist system programming language with Python-like syntax and C-level performance.**

AXIS compiles directly to x86-64 machine code without requiring external linkers, assemblers, or runtime libraries.

**⚠️ Platform Requirements:** Linux x86-64 only (Ubuntu, Debian, Fedora, Arch, etc.)

---

## 🚀 Quick Start

### One-Line Install (Recommended)

```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/standalone_install.sh | bash
```

This will:
- ✅ Install AXIS compiler to `~/.local/bin`
- ✅ Set up the `axis` command
- ✅ Optionally install VS Code extension
- ✅ Configure your PATH automatically

### Or: Manual Installation

```bash
# Clone repository
git clone https://github.com/AGDNoob/axis-lang
cd axis-lang

# Install
cd installer
./install.sh --user
```

### Your First Program

```bash
# Write your first program
cat > hello.axis << 'EOF'
func main() -> i32:
    give 42
EOF

# Compile to executable
axis build hello.axis -o hello --elf

# Run
./hello
echo $?  # Output: 42
```

---

## 📖 Language Overview

### Syntax at a Glance

AXIS uses Python-like syntax with unique keywords:

| AXIS | Traditional | Description |
|------|-------------|-------------|
| `func` | `fn`/`function` | Define a function |
| `give` | `return` | Return a value |
| `when` | `if` | Conditional branch |
| `else` | `else` | Else branch |
| `while` | `while` | Conditional loop |
| `loop`/`repeat` | `loop` | Infinite loop |
| `True`/`False` | `true`/`false` | Boolean literals |

```python
func main() -> i32:
    x: i32 = 42
    when x > 0:
        give x
    else:
        give -x
```

### Philosophy

AXIS follows four core principles:

1. **Zero-Cost Abstractions** – You only pay for what you use
2. **Explicit Control** – Stack, memory, and OS interactions are visible
3. **Direct Mapping** – Source code maps predictably to assembly
4. **No Magic** – No hidden allocations, no garbage collector, no virtual machine

### Design Goals

- **Learnable in ≤1 week** – Small, focused language
- **Immediately productive** – Build real programs from day one
- **Predictable performance** – Performance ceiling = C/C++
- **Systems access** – Direct syscalls, full OS integration

---

## 📚 Language Reference

### Type System

AXIS provides hardware-native integer types with explicit sizing:

```python
# Signed integers
i8      # -128 to 127
i16     # -32,768 to 32,767
i32     # -2,147,483,648 to 2,147,483,647
i64     # -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807

# Unsigned integers
u8      # 0 to 255
u16     # 0 to 65,535
u32     # 0 to 4,294,967,295
u64     # 0 to 18,446,744,073,709,551,615

# Other types
bool    # True or False
ptr     # 64-bit pointer
```

**Type safety:** All variables must be explicitly typed. No implicit conversions.

### Variables

```python
# Variable declaration with type annotation
x: i32 = 10

# Variables can be reassigned
y: i32 = 20
y = y + 5  # y is now 25

# Different integer sizes
small: i8 = 127
medium: i16 = 32767
large: i64 = 9000000000000000000
```

### Functions

```python
# Basic function with return type
func add(a: i32, b: i32) -> i32:
    give a + b

# Entry point (must return i32)
func main() -> i32:
    result: i32 = add(10, 20)
    give result
```

**Keywords:**
- `func` – Define a function
- `give` – Return a value (like `return`)

**Calling convention:** System V AMD64 (Linux)
- First 6 arguments: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`
- Return value: `rax` (or `eax` for i32)

### Control Flow

#### Conditionals (when/else)

```python
func abs(x: i32) -> i32:
    when x < 0:
        give -x
    else:
        give x

# Without else
func positive_check(x: i32) -> bool:
    when x > 0:
        give True
    give False
```

**Keywords:**
- `when` – Conditional branch (like `if`)
- `else` – Else branch

#### Loops (while/loop/repeat)

```python
# while loop
func count() -> i32:
    i: i32 = 0
    while i < 10:
        i = i + 1
    give i  # 10

# repeat loop (infinite loop, use break to exit)
func find_value() -> i32:
    i: i32 = 0
    repeat:
        i = i + 1
        when i >= 10:
            break
    give i
```

**Keywords:**
- `while` – Conditional loop
- `loop` / `repeat` – Infinite loop (requires `break` to exit)

#### Break and Continue

```python
func find_value() -> i32:
    i: i32 = 0
    
    while i < 100:
        i = i + 1
        
        when i == 50:
            break      # Exit loop
        
        when i < 10:
            continue   # Skip to next iteration
    
    give i
```

### Operators

#### Arithmetic

```python
a: i32 = 10 + 5    # Addition: 15
b: i32 = 10 - 5    # Subtraction: 5
c: i32 = 10 * 5    # Multiplication: 50
d: i32 = 10 / 5    # Division: 2
e: i32 = 10 % 3    # Modulo: 1
f: i32 = -10       # Negation (unary minus)
```

#### Bitwise

```python
a: i32 = 5 & 3     # AND: 1
b: i32 = 5 | 3     # OR: 7
c: i32 = 5 ^ 3     # XOR: 6
d: i32 = 5 << 2    # Left shift: 20
e: i32 = 20 >> 2   # Right shift (arithmetic): 5
```

#### Comparison

```python
x == y    # Equal
x != y    # Not equal
x < y     # Less than
x <= y    # Less than or equal
x > y     # Greater than
x >= y    # Greater than or equal
```

All comparisons return `bool` (True or False).

#### Boolean

```python
a: bool = True     # Boolean true literal
b: bool = False    # Boolean false literal
c: bool = !a       # Boolean NOT: False
```

#### Assignment

```python
x = y       # Simple assignment
x = x + 1   # Compound expression
```

### Literals

```python
# Decimal
dec: i32 = 42

# Hexadecimal
hex_val: i32 = 0xFF       # 255
hex2: i32 = 0x1A2B        # 6699

# Binary
bin_val: i32 = 0b1010     # 10
bin2: i32 = 0b11111111    # 255

# Negative
neg: i32 = -100

# Boolean
flag: bool = True
done: bool = False
```

### Comments

```rust
// C-style single-line comments
# Python-style single-line comments

func example() -> i32:
    // This is a C-style comment
    x: i32 = 10  # This is a Python-style comment
    
    # Both styles work anywhere
    // Use whichever you prefer
    
    give x
```

**Note:** AXIS supports both `//` and `#` for comments. Multi-line comments use multiple single-line comments.

---

## 🏗️ Architecture

### Compilation Pipeline

```
┌─────────────────────┐
│  Source Code        │  .axis file
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  Lexer              │  tokenization_engine.py
│  (Tokenization)     │  → Token stream
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  Parser             │  syntactic_analyzer.py
│  (Syntax Analysis)  │  → Abstract Syntax Tree (AST)
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  Semantic Analyzer  │  semantic_analyzer.py
│  (Type Checking)    │  → Annotated AST
└──────────┬──────────┘  → Symbol table
           │              → Stack layout
           ▼
┌─────────────────────┐
│  Code Generator     │  code_generator.py
│  (x86-64)           │  → Assembly instructions
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  Assembler          │  tets.py
│  (Machine Code)     │  → Raw x86-64 machine code
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  ELF64 Generator    │  executable_format_generator.py
│  (Executable)       │  → Linux executable
└─────────────────────┘
```

### Runtime Model

**No runtime library.** AXIS executables contain only:

1. **ELF64 Header** (64 bytes)
2. **Program Header** (56 bytes)
3. **_start stub** (16 bytes) – Calls `main()` and invokes `exit` syscall
4. **User code** – Your compiled functions

**Entry point:**
```asm
_start:
    xor edi, edi        ; argc = 0
    call main           ; Call user's main()
    mov edi, eax        ; exit_code = main's return value
    mov eax, 60         ; syscall: exit
    syscall             ; exit(code)
```

### Memory Layout

**Virtual address space:**
- Base: `0x400000` (4MB, Linux standard)
- Code: `0x401000` (page-aligned at 4KB)
- Stack: Grows downward from high addresses

**Function frame:**
```
[rbp+0]    = saved rbp
[rbp-4]    = first local variable (i32)
[rbp-8]    = second local variable
...
```

Stack size is computed at compile-time and 16-byte aligned.

---

## 🛠️ Usage

### Installation

**Prerequisites:**
- **Operating System:** Linux x86-64 (Ubuntu, Debian, Fedora, Arch, openSUSE, etc.)
- **Python:** 3.7 or higher
- **Not supported:** Windows, macOS, ARM/ARM64

```bash
git clone https://github.com/AGDNoob/axis-lang
cd axis-lang
```

No additional dependencies required (Python 3.7+ only).

### Compiling Programs

**Generate ELF64 executable (Linux):**
```bash
python compilation_pipeline.py program.axis -o program --elf
chmod +x program
./program
```

**Generate raw machine code:**
```bash
python compilation_pipeline.py program.axis -o program.bin
```

**Verbose output (show assembly):**
```bash
python compilation_pipeline.py program.axis -o program --elf -v
```

**Hex dump only (no output file):**
```bash
python compilation_pipeline.py program.axis
```

### VS Code Integration

AXIS includes a VS Code extension for syntax highlighting.

**Install:**
```bash
# Extension is already installed in: axis-vscode/
# Reload VS Code (Ctrl+Shift+P → "Developer: Reload Window")
```

**Features:**
- Syntax highlighting for `.axis` files
- Auto-closing brackets
- Line comments via `Ctrl+/`
- Build task: `Ctrl+Shift+B`

---

## 📝 Examples

### Hello World (Exit Code)

```python
func main() -> i32:
    give 42
```

```bash
$ python compilation_pipeline.py hello.axis -o hello --elf
$ ./hello
$ echo $?
42
```

### Arithmetic

```python
func main() -> i32:
    x: i32 = 10
    y: i32 = 20
    z: i32 = x + y
    give z  # 30
```

### Loops

```python
func factorial(n: i32) -> i32:
    result: i32 = 1
    i: i32 = 1
    
    while i <= n:
        result = result * i
        i = i + 1
    
    give result

func main() -> i32:
    give factorial(5)  # 120
```

### Conditionals

```python
func max(a: i32, b: i32) -> i32:
    when a > b:
        give a
    give b

func clamp(x: i32, min_val: i32, max_val: i32) -> i32:
    when x < min_val:
        give min_val
    when x > max_val:
        give max_val
    give x
```

### Complex Example

```python
func is_prime(n: i32) -> bool:
    when n <= 1:
        give False
    
    i: i32 = 2
    while i < n:
        when n % i == 0:
            give False
        i = i + 1
    
    give True

func count_primes(limit: i32) -> i32:
    count: i32 = 0
    i: i32 = 2
    
    while i < limit:
        when is_prime(i):
            count = count + 1
        i = i + 1
    
    give count

func main() -> i32:
    give count_primes(100)
```

---

## 🔧 Technical Details

### Calling Convention (System V AMD64)

**Arguments:**
| Position | i32 Register | i64 Register |
|----------|-------------|--------------|
| 1st      | edi         | rdi          |
| 2nd      | esi         | rsi          |
| 3rd      | edx         | rdx          |
| 4th      | ecx         | rcx          |
| 5th      | r8d         | r8           |
| 6th      | r9d         | r9           |
| 7+       | Stack       | Stack        |

**Return value:** `eax` (i32) or `rax` (i64)

**Preserved registers:** `rbx`, `rbp`, `r12`-`r15`

### ELF64 Structure

```
Offset  | Size | Section
--------|------|-------------------
0x0000  | 64   | ELF Header
0x0040  | 56   | Program Header (PT_LOAD)
0x0078  | ...  | Padding (to 0x1000)
0x1000  | 16   | _start stub
0x1010  | ...  | User code
```

**Entry point:** `0x401000` (_start)

### Syscalls (Linux x86-64)

Currently implemented:
- `exit(code)`: rax=60, rdi=exit_code

Planned:
- `write(fd, buf, len)`: rax=1
- `read(fd, buf, len)`: rax=0

---

## ⚠️ Current Limitations (MVP)

### Not Yet Implemented

- [ ] Function parameters (only `main()` without args works)
- [ ] More than 6 function arguments
- [ ] Structs and arrays
- [ ] Pointer dereferencing
- [ ] Global variables
- [ ] Heap allocations
- [ ] String literals
- [ ] Type casting
- [ ] Floating-point types
- [ ] Standard library
- [ ] `write` syscall (stdout)

### Implemented ✓

- [x] ELF64 executable format
- [x] Stack-based local variables
- [x] Control flow (`when`/`else`, `while`, `loop`/`repeat`, `break`, `continue`)
- [x] Arithmetic (`+`, `-`, `*`, `/`, `%`)
- [x] Bitwise (`&`, `|`, `^`, `<<`, `>>`)
- [x] Comparisons (`==`, `!=`, `<`, `>`, `<=`, `>=`)
- [x] Boolean type with `True`/`False` literals
- [x] Unary operators (`-`, `!`)
- [x] Comments (`//` and `#`)
- [x] Function calls (basic)
- [x] All integer types (i8-i64, u8-u64)
- [x] VS Code syntax highlighting

---

## 🗺️ Roadmap

### Phase 6: I/O
- [ ] `write` syscall (stdout)
- [ ] `read` syscall (stdin)
- [ ] String literals and data section
- [ ] Basic string operations

### Phase 7: Advanced Features
- [ ] Function parameters (full System V ABI)
- [ ] Structs
- [ ] Arrays (fixed-size)
- [ ] Pointer arithmetic
- [ ] Type casting

### Phase 8: Standard Library
- [ ] Memory allocation (`mmap`, `brk`)
- [ ] File I/O
- [ ] Command-line arguments
- [ ] Environment variables

### Phase 9: Developer Experience
- [ ] Language Server Protocol (LSP)
- [ ] Error messages with source locations
- [ ] Debugger support (DWARF)
- [ ] REPL (interactive mode)

### Phase 10: Optimization
- [ ] Dead code elimination
- [ ] Constant folding
- [ ] Register allocation optimization
- [ ] Inline functions

---

## 📊 Performance

**Compilation:**
- ~100ms for small programs (< 100 lines)
- No external tools required

**Runtime:**
- **Zero overhead** – No runtime library
- **Direct syscalls** – No libc indirection
- **Native machine code** – Same performance as C/C++
- **Minimal binary size** – ~4KB + code size

**Comparison:**

| Metric          | AXIS     | C (gcc) | Python |
|-----------------|----------|---------|--------|
| Startup time    | <1ms     | <1ms    | ~20ms  |
| Binary size     | ~4KB     | ~15KB   | N/A    |
| Runtime deps    | None     | libc    | CPython|
| Compilation     | ~100ms   | ~200ms  | N/A    |

---

## 🏛️ Project Structure

```
axis-lang/
├── tokenization_engine.py          # Lexer
├── syntactic_analyzer.py           # Parser + AST
├── semantic_analyzer.py            # Type checker + Symbol table
├── code_generator.py               # x86-64 code generator
├── executable_format_generator.py  # ELF64 writer
├── compilation_pipeline.py         # Main compiler driver
├── tets.py                         # Assembler backend
├── axis-vscode/                    # VS Code extension
│   ├── package.json
│   ├── language-configuration.json
│   └── syntaxes/
│       └── axis.tmLanguage.json
├── tests/                          # Test programs
│   ├── test_arithmetic.axis
│   ├── test_control_flow.axis
│   └── syntax_test.axis
├── .vscode/
│   └── tasks.json                  # Build tasks
└── README.md
```

---

## 🤝 Contributing

AXIS is an experimental language project. Contributions welcome!

**Areas of interest:**
- Standard library design
- Optimization passes
- Language Server Protocol
- More architectures (ARM64, RISC-V)
- Windows support (PE format)

---

## 📜 License

MIT License

---

## 🎓 Learn More

**Recommended reading:**
- [x86-64 Assembly Programming](https://www.cs.cmu.edu/~fp/courses/15213-s07/misc/asm64-handout.pdf)
- [System V AMD64 ABI](https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf)
- [ELF Format Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [Crafting Interpreters](https://craftinginterpreters.com/)

**Similar projects:**
- [Zig](https://ziglang.org/) – Systems language with manual memory management
- [Odin](https://odin-lang.org/) – Simple, fast, modern alternative to C
- [V](https://vlang.io/) – Fast compilation, minimal dependencies

---

**Built with precision. No runtime overhead. Pure machine code.**

*AXIS – Where Python meets the metal.*
