# Changelog

All notable changes to the AXIS programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.2.0] - 2026-03-13

### Added

- AXCC optimization pipeline (constant folding, DCE, strength reduction, load-store elimination, CMP+branch fusion)
- Linear-scan register allocation across all 16 x86-64 GP registers
- Differentiated `copy.compile` (inline loop) vs `copy.runtime` (`REP MOVSB`) codegen
- `axis check` command with `--dead`, `--unused`, `--all` flags
- Script mode skips optimizer passes for faster compile times

### Changed

- Benchmarks 1.1–1.7× faster than v1.1.0; within 10–30% of GCC `-O0`
- New docs: `08-optimizations.md`, `12-check-command.md`
- Updated docs: `05-arrays.md`, `06-x64-codegen.md`, `11-compile-mode.md`

---

## [1.1.0] - 2026-03-12

### Added

- Enums with configurable underlying type (`enum u8:`, etc.), auto-increment and explicit values
- Match statements with wildcard `_` pattern
- Function parameters in compile mode (Windows x64 calling convention, 5+ args on stack, `update` modifier)
- Windows PE executable generation for compile mode (`--pe` / `--elf` flags)
- AXCC — complete rewrite of compile mode backend in C (IR-based pipeline, zero dependencies)
- Copy modes: `copy.runtime` (fast execution) vs `copy.compile` (fast compilation)
- Compound assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`)
- For loops with `range()` and array iteration
- Logical operators `and`, `or`, `not` with short-circuit evaluation
- Improved error messages with file, line, column, and source context

### Changed

- Updated README and documentation

---

## [1.0.2-beta] - 2026-01-18

### Added

- Dual-mode execution: `mode script` (Python transpilation) and `mode compile` (native x86-64 ELF64)
- Source transpiler (`transpiler.py`) for fast cross-platform execution
- CLI commands: `axis check`, `axis info`, `axis update`
- 20 example programs in `examples/`
- I/O functions: `write()`, `writeln()`, `read()`, `readln()`, `readchar()`, `read_failed()`
- String type with escape sequences, boolean type with `True`/`False`
- Unary `-` operator, `//` and `#` comments
- Full integer support (`i8`–`i64`, `u8`–`u64`), bitwise operators
- Windows/macOS uninstallers

### Changed

- Simplified architecture: removed interpreter/bytecode VM, replaced with transpiler
- Renamed `tets.py` → `assembler.py`
- Reorganized installers into `installer/` folder

### Removed

- `ast_compiler.py`, `interpreter.py`, `bytecode_vm.py`
- Old `tests/` folder (replaced by `examples/`)
- Cache system

### Fixed

- `ExprStatement` handling in transpiler
- Arithmetic right shift (`sar`) for signed types
- Jump relaxation for large conditional jumps
- Negative literal handling for `i8`/`i16`
- Installer file lists

---

## [1.0.1-beta] - 2026-01-14

### Added

- Type system: `i8`–`i64`, `u8`–`u64`, `ptr`, `bool`
- Variables: `let` (immutable), `let mut` (mutable)
- Control flow: `when`/`else`, `while`, `stop`, `skip`
- Functions with typed parameters and return values
- Arithmetic and comparison operators
- Compilation pipeline: Tokenizer → Parser → Semantic Analyzer → Code Generator → Assembler
- ELF64 and raw binary output formats
- VS Code extension for syntax highlighting
- Linux installer scripts

### Known Limitations

- Linux x86-64 only
- Limited function parameter support
- No standard library, optimization passes, or debug info

---

## [1.0.0-beta] - 2026-01-12

Initial beta release of the AXIS programming language.

---

> **Note**: This is beta software under active development. Breaking changes may occur.
