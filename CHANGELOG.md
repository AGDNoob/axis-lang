# Changelog

All notable changes to the AXIS programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
