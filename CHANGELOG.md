# Changelog

All notable changes to the AXIS programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## GitHub Release Template (Copy & Paste for v1.0.2-beta)

```markdown
# AXIS v1.0.2-beta - Multi-Type Integer Support

Second beta release with full support for all integer sizes and new language features.

## 🚀 Installation

### Quick Install (Recommended)
```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/standalone_install.sh | bash
```

### Or: Download and Install Manually
```bash
# Download source code (see Assets below)
tar -xzf axis-lang-1.0.2-beta.tar.gz
cd axis-lang-*/installer
./install.sh --user
```

## ✨ What's New in v1.0.2-beta

### Boolean Type & Operators
- **Boolean Literals**: `True` and `False` keywords
- **Boolean Negation**: Unary `!` operator (`!condition`)
- **Strict Boolean Conditions**: `when`/`while` require boolean expressions

### Unary Negation
- **Arithmetic Negation**: Unary `-` operator for all numeric types (`-x`)

### Comments
- **Line Comments**: Both `//` and `#` style single-line comments

### Full Integer Type Support
- **i8/u8**: 8-bit integers (-128 to 127 / 0 to 255)
- **i16/u16**: 16-bit integers (-32768 to 32767 / 0 to 65535)
- **i32/u32**: 32-bit integers (full range)
- **i64/u64**: 64-bit integers (full range)
- Type-aware code generation with proper sign/zero extension

### Bitwise Operators
- **AND**: `&` operator
- **OR**: `|` operator
- **XOR**: `^` operator
- **Shifts**: `<<` and `>>` operators

### Bug Fixes
- Fixed right shift to use arithmetic shift (`sar`) for signed types
- Fixed jump relaxation for large conditional jumps
- Fixed negative literal handling for i8/i16 types

## 🧪 Quick Start

```bash
# Create your first program
cat > hello.axis << 'EOF'
func main() -> i32:
    x: i32 = 42
    y: i32 = -x
    when y == -42:
        give 42
    give 0
EOF

# Compile and run
axis build hello.axis -o hello --elf
./hello
echo $?  # Output: 42
```

## 📚 Integer Types Example

```python
func main() -> i32:
    # 8-bit integers
    small: i8 = 127
    tiny: u8 = 255
    
    # 16-bit integers
    medium: i16 = 32767
    word: u16 = 65535
    
    # 32-bit integers (default)
    normal: i32 = 2147483647
    
    # 64-bit integers
    large: i64 = 9000000000000000000
    
    give 42
```

## ⚠️ Known Limitations

- **Platform**: Linux x86-64 only
- **Function Parameters**: Limited implementation
- **Standard Library**: Not yet available
- **Unsigned Literals**: Values > signed max need special handling

## 🔧 Upgrade from v1.0.1-beta

No breaking changes. Simply reinstall:
```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/standalone_install.sh | bash
```

---

**Full Changelog**: https://github.com/AGDNoob/axis-lang/blob/main/CHANGELOG.md
```

---

## [Unreleased] - v1.0.2-beta

### ✨ New Features

#### Boolean Type & Operators
- **Boolean Literals**: `True` and `False` keywords for explicit boolean values
- **Boolean Negation**: Unary `!` operator for boolean NOT (e.g., `!condition`)
- **Strict Boolean Conditions**: `when` and `while` now require boolean expressions

#### Unary Negation
- **Arithmetic Negation**: Unary `-` operator for numeric negation (e.g., `-x`)
- **Type-Aware Negation**: Correct `neg` instruction for all integer sizes (i8 through i64)

#### Comments
- **Line Comments**: Support for both `//` and `#` style single-line comments
- **Mixed Styles**: Both comment styles can be used interchangeably in the same file

#### Multi-Size Integer Support
- **i8 Type**: Full support for 8-bit signed integers
  - Sign-extended loads via `movsx`
  - Byte-sized stores
  - Proper literal handling with sign extension
- **i16 Type**: Full support for 16-bit signed integers
  - Sign-extended loads via `movsx`
  - Word-sized stores with operand size prefix
- **i64 Type**: Full support for 64-bit signed integers
  - 64-bit register operations (rax, rbx, etc.)
  - Qword-sized stores with REX.W prefix
  - 64-bit immediate values
- **Unsigned Types**: u8, u16, u32, u64 with zero-extension via `movzx`

#### Assembler Enhancements
- **MOVSX Instruction**: Sign-extend byte/word to dword (`0F BE`, `0F BF`)
- **MOVZX Instruction**: Zero-extend byte/word to dword (`0F B6`, `0F B7`)
- **Byte Stores**: `mov byte [rbp-X], al` (opcode `88`)
- **Word Stores**: `mov word [rbp-X], ax` (prefix `66` + opcode `89`)
- **Qword Stores**: `mov qword [rbp-X], rax` (REX.W + opcode `89`)
- **Qword Loads**: `mov rax, qword [rbp-X]` (REX.W + opcode `8B`)
- **64-bit NEG**: `neg rax` with REX.W prefix

### 🐛 Bug Fixes
- **Right Shift Operator**: Fixed `>>` to use `sar` (arithmetic) instead of `shr` (logical) for signed integers
- **Jump Relaxation**: Fixed conditional jumps to fall back from short to near form when offset exceeds ±127 bytes
- **Negative Literals**: Fixed i8/i16 negative literal handling for proper sign extension

### 📝 Documentation
- **README Keyword Correction**: Fixed documentation to use actual AXIS keywords (`while`/`else` instead of fictional `whilst`/`otherwise`)
- **Loop Keywords**: Documented both `loop` and `repeat` as valid infinite loop keywords

### 🧪 New Test Suite
- `stress_test_bool.axis` - Boolean operations and True/False literals
- `stress_test_negation.axis` - Unary negation for all types
- `stress_test_comments.axis` - Comment syntax testing
- `stress_test_i8_limits.axis` - i8 boundary testing (-128 to 127)
- `stress_test_i16_limits.axis` - i16 boundary testing (-32768 to 32767)
- `stress_test_i32_limits.axis` - i32 boundary testing
- `stress_test_i64_limits.axis` - i64 large value testing
- `stress_test_u8_limits.axis` - u8 boundary testing (0 to 255)
- `stress_test_u16_limits.axis` - u16 boundary testing (0 to 65535)
- `stress_test_u32_limits.axis` - u32 large value testing
- `stress_test_u64_limits.axis` - u64 large value testing
- `stress_test_all_int_limits.axis` - Combined integer type testing
- Multiple comprehensive test files for each new feature

### 📋 Technical Improvements
- Type-aware code generation for all integer sizes
- Proper register selection (al/ax/eax/rax) based on operand type
- Sign extension for signed types, zero extension for unsigned types
- 64-bit immediate value support for i64/u64 types

---

## [1.0.1-beta] - 2026-01-14

### ✨ Features

#### Core Language
- **Type System**: Full support for hardware-native integer types
  - Signed: `i8`, `i16`, `i32`, `i64`
  - Unsigned: `u8`, `u16`, `u32`, `u64`
  - Pointer: `ptr`
  - Boolean: `bool`
- **Variables**: Immutable by default with `let`, mutable with `let mut`
- **Control Flow**: `if/else`, `while`, `break`, `continue`
- **Functions**: Function definitions with typed parameters and return values
- **Operators**: Arithmetic (`+`, `-`, `*`, `/`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`)

#### Compiler
- **Compilation Pipeline**: Complete source-to-machine-code compilation
  - Phase 1: Tokenization (Lexer)
  - Phase 2: Syntactic Analysis (Parser with AST)
  - Phase 3: Semantic Analysis (Type checking)
  - Phase 4: Code Generation (x86-64 assembly)
  - Phase 5: Assembly (Machine code generation)
- **Output Formats**:
  - ELF64 executables for Linux x86-64
  - Raw binary machine code
- **Command-line Interface**: Full-featured CLI with verbose mode

#### Tooling
- **VS Code Extension**: Syntax highlighting for `.axis` files
- **Build Tasks**: Integrated VS Code build tasks
- **Installer**: Linux installation scripts (user and system-wide)

### 📚 Documentation
- Comprehensive README with language reference
- Test suite documentation with example programs
- Installation guide
- MIT License

### 🧪 Test Programs
- `test_return42.axis` - Basic return value
- `test_arithmetic.axis` - Arithmetic operations
- `test_control_flow.axis` - While loops and conditionals
- `test_complex.axis` - Complex multi-feature program

### ⚠️ Known Limitations
- **Platform**: Linux x86-64 only (ELF64 format)
- **Windows/macOS**: Not supported in this release
- **Function Parameters**: Limited implementation
- **Standard Library**: Not yet available
- **Optimization**: No optimization passes yet
- **Debugging**: No DWARF debug info generation

### 🔧 Technical Details
- Compiler written in Python 3
- Direct x86-64 machine code generation
- No external assembler or linker required
- Zero-dependency runtime (no libc)
- Direct Linux syscalls for system interaction

### 📋 Future Roadmap
- Function parameters and multiple arguments
- Arrays and structs
- Pointers and references
- Memory operations
- Standard library
- Optimization passes
- More platforms (ARM64, Windows PE format)

---

## [Unreleased]

### Planned for Next Release
- Function parameter passing
- Array types
- Struct definitions
- Standard library basics

---

**Note**: This is a BETA release. The language and compiler are under active development. 
Breaking changes may occur between versions. Not recommended for production use.
