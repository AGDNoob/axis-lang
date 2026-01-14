# Changelog

All notable changes to the AXIS programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

### Planned for 1.0.2-beta
- Function parameter passing
- Array types
- Struct definitions
- Standard library basics

---

**Note**: This is a BETA release. The language and compiler are under active development. 
Breaking changes may occur between versions. Not recommended for production use.
