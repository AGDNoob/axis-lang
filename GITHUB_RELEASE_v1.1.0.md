# AXIS v1.1.0 — Native Compiler

**"Think in values, compile to metal"**

This is a major release. The compiler has been completely rewritten from Python to C, and AXIS now has a clear identity: **value-oriented programming that compiles to native code.**

---

## Highlights

- **AXCC — Native Compiler in C**: ~9000 lines of C. Lexer, parser, semantic analysis, IR generation, x64 codegen, PE/ELF output — all in one binary. No Python, no pip, no dependencies.
- **Windows PE + Linux ELF64**: Compile to native executables on both platforms. Cross-compilation via `--pe` and `--elf` flags.
- **Value-Oriented Programming**: No pointers. Instead: `update` for in-out parameters, `copy` for explicit array copies.

## New Language Features

- **Arrays** — fixed-size with dynamic indexing: `arr: (i32; 5) = [1, 2, 3, 4, 5]`
- **Fields** — custom data types with unlimited nesting
- **Enums** — named constants with configurable underlying type (`enum u8:`, `enum i64:`, etc.)
- **Match Statements** — pattern matching with wildcard `_`
- **For Loops** — `for i in range(0, 10):` and array iteration
- **Compound Assignment** — `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- **Logical Operators** — `and`, `or`, `not` with short-circuit evaluation
- **`copy` keyword** — explicit array copies, no accidental aliasing
- **`update` modifier** — modify caller's variables without pointers

## Benchmarks vs GCC

| Benchmark | AXIS | GCC -O0 | GCC -O2 | vs -O0 | vs -O2 |
|---|---:|---:|---:|---:|---:|
| Recursive Fibonacci (63M calls) | 554ms | 256ms | 78ms | 2.2x | 7.1x |
| Prime Counting (500K) | 161ms | 78ms | 77ms | 2.1x | 2.1x |
| Nested Loops (100M iterations) | 686ms | 347ms | 146ms | 2.0x | 4.7x |
| GCD Stress (2M calls) | 73ms | 52ms | 51ms | 1.4x | 1.4x |

No optimization passes yet — this is the baseline.

## Installation

**Windows:** Download `axis-installer.exe` below and run it.

**Linux:**
```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-linux.sh | bash
```

**Build from source:**
```bash
cd axcc
make        # Linux
mingw32-make CC=gcc   # Windows (MinGW)
```

## What's Removed

The old Python-based compilation pipeline (9 files, ~10K lines) has been retired. AXCC replaces it entirely.

---

For the full story — including design philosophy, code examples, and technical details — see [RELEASE_NOTES_v1.1.0.md](RELEASE_NOTES_v1.1.0.md).
