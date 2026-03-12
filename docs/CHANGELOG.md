# Changelog

All notable changes to the AXIS programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0-beta] - 2026-03-12

### Added

- **Enums**: Named constant types for improved code clarity
  - Configurable underlying type: `enum u8:`, `enum i64:`, etc. (default: i32)
  - Auto-incrementing values: `Red`, `Green`, `Blue` → 0, 1, 2
  - Explicit values: `Active = 1`, `Completed = 100`
  - Full type safety: `c: Color = Color.Red`

- **Match Statements**: Pattern matching on values
  - Match on integers, enums, or any comparable types
  - Wildcard pattern `_` for default case
  - Compile-time type checking for pattern compatibility

- **Function Parameters in Compile Mode**: Full support for function parameters
  - Windows x64 calling convention (RCX, RDX, R8, R9)
  - Additional args passed on stack (5+ parameters supported)
  - `update` modifier for in-out parameters (copy-in/copy-out semantics)

- **Windows Support for Compile Mode**: Compile mode now works on Windows
  - Previously Linux-only (ELF64) — now also generates native Windows PE executables
  - `axis hello.axis -o hello.exe` produces a standalone `.exe` — no GCC, no NASM, no linker needed
  - Cross-compilation: `--pe` flag (Windows) and `--elf` flag (Linux) from any platform

- **AXCC — Native Compiler in C**: Complete rewrite of compile mode backend
  - Compiler implementation language changed from Python to C
  - PE (Windows) executable generation
  - ELF64 (Linux) executable generation
  - IR-based code generation pipeline
  - Zero runtime dependencies
  - 26 automated tests passing on both platforms

- **Copy Modes**: Control runtime vs compile-time optimization
  - `copy` / `copy.runtime` - Optimized for fast execution (default)
  - `copy.compile` - Optimized for faster compilation
  - In script mode, both behave identically

- **Compound Assignment Operators**: All arithmetic and bitwise compound operators
  - Arithmetic: `+=`, `-=`, `*=`, `/=`, `%=`
  - Bitwise: `&=`, `|=`, `^=`
  - Shift: `<<=`, `>>=`
  - Works with variables, array elements, and field members

- **For Loops**: Iterate over ranges or arrays with `for...in` syntax
  - `range(start, end)` and `range(start, end, step)`
  - Array iteration in script mode
  - `break` and `continue` supported

- **Improved Error Messages**: Better error reporting with source context
  - File name, line number, and column in error messages
  - Source line shown with caret pointing to error location
  - Clean output without stack traces (use `-v` for verbose mode)

- **Logical Operators**: `and`, `or`, `not` for boolean logic
  - Short-circuit evaluation (right side not evaluated if result known)
  - Works in both script and compile modes
  - Example: `when x > 0 and y < 10:`

### Changed

- Updated README documentation with new features
- Improved semantic analyzer for enhanced type checking

---

## [1.0.2-beta] - 2026-01-18

### Added

- **Dual-Mode Execution**: Two execution modes for different use cases
  - `mode script` - Cross-platform Python transpilation (Windows, macOS, Linux)
  - `mode compile` - Native x86-64 ELF64 compilation (Linux only)
- **Source Transpiler**: New `transpiler.py` converts AXIS AST to Python for fast execution
- **CLI Commands**: `axis check`, `axis info`, `axis update`
- **20 Example Programs**: Comprehensive examples in `examples/` folder
- **I/O Functions**: `write()`, `writeln()`, `read()`, `readln()`, `readchar()`, `read_failed()`
- **String Support**: `str` type, double-quoted literals with escape sequences
- **Boolean Type**: `True`/`False` literals, `!` negation operator
- **Arithmetic Negation**: Unary `-` operator for all numeric types
- **Comments**: Both `//` and `#` style single-line comments
- **Full Integer Support**: `i8`/`u8`, `i16`/`u16`, `i32`/`u32`, `i64`/`u64`
- **Bitwise Operators**: `&`, `|`, `^`, `<<`, `>>`
- **Windows/macOS Uninstallers**: `uninstall.bat` and updated `uninstall.sh`

### Changed

- **Simplified Architecture**: Removed interpreter/bytecode VM in favor of direct transpilation
- **Renamed `tets.py` to `assembler.py`**: Cleaner naming for x86-64 assembler
- **Updated Documentation**: Complete README rewrite with dual-mode and cross-platform docs
- **Installer Reorganization**: All install files now in `installer/` folder

### Removed

- `ast_compiler.py`, `interpreter.py`, `bytecode_vm.py` (replaced by transpiler)
- `tests/` folder (replaced by `examples/`)
- Cache system (unnecessary complexity)

### Fixed

- `ExprStatement` handling in transpiler for function calls as statements
- Right shift now uses `sar` (arithmetic) for signed types
- Jump relaxation for large conditional jumps
- Negative literal handling for i8/i16 types
- Installer file lists now include all required modules

---

## [1.0.1-beta] - 2026-01-14

### Added

- **Type System**: `i8`-`i64`, `u8`-`u64`, `ptr`, `bool`
- **Variables**: `let` (immutable), `let mut` (mutable)
- **Control Flow**: `when`/`else`, `while`, `stop`, `skip`
- **Functions**: Typed parameters and return values
- **Operators**: `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `>`, `<=`, `>=`
- **Compilation Pipeline**: Tokenizer - Parser - Semantic Analyzer - Code Generator - Assembler
- **Output Formats**: ELF64 executables, raw binary
- **VS Code Extension**: Syntax highlighting for `.axis` files
- **Linux Installer**: User and system-wide installation scripts

### Known Limitations

- Linux x86-64 only (ELF64 format)
- Limited function parameter support
- No standard library
- No optimization passes
- No debug info generation

---

## [1.0.0-beta] - 2026-01-12

Initial beta release of the AXIS programming language.

---

> **Note**: This is beta software under active development. Breaking changes may occur.
