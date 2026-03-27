# Changelog

All notable changes to the AXIS programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.3.0] - 2026-03-26

### Added

- `@name` syntax to tag loops for targeted `stop @name` / `skip @name` from nested scopes; syntax: `@outer while condition:`, `@search for i in range(0, n):`, `@main repeat:`
- `stop @name` breaks the named loop; `skip @name` continues to the next iteration of the named loop
- Loop flags supported on `while`, `repeat`, and `for`; unflagged loops are unaffected
- Duplicate flag names and unknown flag references are rejected at compile time
- Loop flags supported in both script mode (`axis run`) and compile mode (`axis build`)
- `const` keyword for immutable variables; values are read-only after initialization
- Scalars: `const x: i32 = 42`
- Arrays: `const arr: (i32; 3) = [1, 2, 3]`
- Enums: `const Color: enum:` makes the entire enum constant; individual variants can also be `const`
- Fields: `const Player: field:` makes the entire field including members and subfields constant; individual members can also be `const`
- `const` prevents reassignment, compound assignment (`+=`, `-=`, …), index mutation, and field mutation
- Passing a `const` variable as an `update` parameter is a compile error
- New example: `21_constants.axis`
- String concatenation with `+` operator in compile mode: `greeting: str = "Hello, " + name`
- String comparison with `==` and `!=` in compile mode: `when name == "Alice":`
- Runtime stubs `__axis_str_concat` and `__axis_str_eq` added to PE and ELF backends
- `copy ... as` and `update ... as` for explicit type casting between integer types
- Widening cast: `copy x as i64` — sign-extension for signed types, zero-extension for unsigned
- Narrowing cast: `update x as i8` — truncation with a compiler warning
- `bool ↔ integer` casting is a compile error
- No-op casts between equal-size types are eliminated by the compiler
- Variable assignment `x: i32 = y` creates an alias (shared storage) — no keyword required
- `x: i32 = copy y` creates an independent copy instead of an alias
- Aliases inherit the constness of their source; a `const` alias of a mutable variable is a compile error
- Array index access in declarations and assignments requires `copy`: `x: i32 = copy arr[0]`; `x: i32 = arr[0]` is a compile error
- Array elements cannot be aliased, only copied
- `for` loops copy array elements implicitly — no `copy` keyword needed in loop body
- Direct array access in expressions (`writeln(arr[0])`, `if arr[1] > 0:`) does not require `copy`
- `input()` function for explicit, type-safe user input
- `x: i32 = input("Prompt: ")` — reads an integer and displays the prompt
- `s: str = input()` — reads a line with no prompt; prompt argument can be a string literal or variable
- `{var}_input_failed()` returns `bool` to check if the preceding `input()` call failed — e.g. `x_input_failed()`
- `const` variables with `input()` are a compile error
- `input()` supported in PE and ELF backends (scanf/printf runtime stubs)

### Removed

- `read()`, `readln()`, `readchar()`, and `read_failed()` removed — replaced by `input()` and `{var}_input_failed()`
- `give` keyword removed — use `return` instead

### Fixed

- **pe.c**: `.rdata` section now has WRITE flag — `input()` runtime stubs write to `.rdata` (scanf buffer, input_failed flag), causing ACCESS_VIOLATION when read-only

---

## [1.2.1] - 2026-03-16

### Added

- 32-bit native arithmetic: all integer operations encoded as native 32-bit x86 instructions (matching `i32` width)
- Peephole optimizer pass (redundant instruction elimination)
- Copy propagation pass
- Function inlining (small leaf functions)
- Loop-Invariant Code Motion (LICM)
- Loop unrolling for small fixed-count loops

### Changed

- Improved codegen for Prime Count benchmark (~GCC `-O0` parity) and GCD Stress benchmark
- Improved Fibonacci benchmark runtime (~1.11× vs GCC `-O0`, previously ~1.3×)
- Binary sizes reduced (Fibonacci: 2.5 KB → 2.0 KB) via shorter 32-bit instruction encoding

### Fixed

- **x64.c**: Parameter offset for argument 5+ was wrong (`16 + i*8` instead of `16 + (i-4)*8`) — functions with 5+ args read garbage from stack
- **x64.c**: Division by zero caused CPU exception #DE without error message — now caught
- **x64.c**: MEMCPY loop used hardcoded jump offsets — fragile, replaced with label-based jumps
- **lexer.c**: Indent stack overflow only protected by `assert()` — buffer overflow in release builds
- **lexer.c**: `strtoull(buf, NULL, 0)` didn't recognize `0b` prefix — all binary literals evaluated to 0
- **lexer.c**: Mixed tabs/spaces in indentation not detected — unpredictable indent behavior
- **lexer.c**: Tab character incremented column by 1 only — wrong error positions with tabs
- **irgen.c**: Logical NOT returned operand size instead of 1 byte — garbage in upper bytes of bool result
- **irgen.c**: Comparisons (`==`, `!=`, `<`, ...) returned operand size instead of 1 byte — wrong result size
- **irgen.c**: Store size derived from value instead of variable — potential size mismatch
- **irgen.c**: For-loop variable size derived from expression instead of type — potential size mismatch
- **parser.c**: `copy` used `parse_expression()` instead of `parse_unary()` — wrong operator precedence
- **semantic.c**: `update` parameter accepted immutable variables — violated mutability guarantee
- **semantic.c**: Duplicate enum variant names/values not detected — invalid code accepted
- **semantic.c**: Built-in `read`/`readln`/`readchar` didn't check argument count — e.g. `read(1,2,3)` accepted
- **semantic.c**: `match` on enum didn't check for exhaustiveness — missing variants not detected
- **semantic.c**: Array element type fell back to `i32` silently — hid type errors
- **semantic.c**: Copy-parameter mutability not enforced — feature incomplete
- **pe.c**: `SizeOfCode` used file-aligned instead of virtual sizes — PE header technically incorrect
- **pe.c**: `.rdata` section had WRITE flag — W^X violation (security)

---

## [1.2.0] - 2026-03-13

### Added

- AXCC optimization pipeline (constant folding, DCE, strength reduction, load-store elimination, CMP+branch fusion)
- Linear-scan register allocation across all 16 x86-64 GP registers
- Differentiated `copy.compile` (inline loop) vs `copy.runtime` (`REP MOVSB`) codegen
- `axis check` command with `--dead`, `--unused`, `--all` flags
- Script mode skips optimizer passes for faster compile times

### Changed

- Benchmark performance improved 1.1–1.7× over v1.1.0 (within 10–30% of GCC `-O0`)
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
