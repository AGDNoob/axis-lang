# AXCC Architecture

AXCC is the AXIS compiler. It is a single-pass-per-stage compiler written in C with zero external dependencies. It reads `.axis` source files and produces standalone native executables (Windows PE or Linux ELF64).

## Pipeline Overview

```
Source Code (.axis)
    │
    ▼
┌──────────┐
│  Lexer   │  lexer.c (~570 lines)
│          │  Source text → Token stream
└────┬─────┘
     │
     ▼
┌──────────┐
│  Parser  │  parser.c (~1800 lines)
│          │  Token stream → Abstract Syntax Tree (AST)
└────┬─────┘
     │
     ▼
┌──────────────────┐
│ Semantic Analyzer │  semantic.c (~1200 lines)
│                  │  Type checking, scope analysis, stack layout
└────┬─────────────┘
     │
     ▼
┌──────────────┐
│ IR Generator │  irgen.c (~1700 lines)
│              │  AST → Three-address intermediate representation
└────┬─────────┘
     │
     ▼
┌──────────────────┐
│ x64 Code Generator│  x64.c (~2000 lines)
│                   │  IR → x86-64 machine code + relocations
└────┬──────────────┘
     │
     ▼
┌─────────────┐
│ PE/ELF Writer│  pe.c (~900 lines) / elf.c (~600 lines)
│             │  Machine code → Executable binary
└─────────────┘
```

Total: approximately 9,000 lines of C.

## File Structure

| File | Purpose |
|------|---------|
| `main.c` | Driver, CLI parsing, script mode caching, build pipeline |
| `arena.c` | Arena (bump-pointer) memory allocator |
| `lexer.c` | Tokenizer with indentation tracking |
| `parser.c` | Recursive descent parser |
| `semantic.c` | Multi-pass type checker and scope analyzer |
| `irgen.c` | IR instruction generator |
| `x64.c` | x86-64 native code generator |
| `pe.c` | Windows PE32+ executable writer |
| `elf.c` | Linux ELF64 executable writer |
| `axis_token.h` | Token type definitions, keyword table |
| `axis_ast.h` | AST node definitions |
| `axis_ir.h` | IR opcode and operand definitions |
| `axis_common.h` | Shared type system, constants |
| `axis_arena.h` | Arena allocator header |

## Memory Management

AXCC uses an arena allocator (bump-pointer allocator) with 1 MiB blocks. All AST nodes, IR instructions, and temporary strings are allocated from the arena. At the end of compilation, a single `arena_free()` releases everything.

This avoids individual `malloc`/`free` calls and eliminates memory leak concerns. The allocator provides three operations:
- `arena_alloc(size)` — allocate a block of memory
- `arena_strdup(str)` — duplicate a string into the arena
- `arena_strndup(str, len)` — duplicate a string with length limit

## Driver (main.c)

The driver handles:

1. **Command-line parsing**: Source file, output path, `--pe`/`--elf` format flags
2. **Mode detection**: Scans the first non-comment line for `mode script` or `mode compile`
3. **Script mode caching**: If the source file is in script mode, AXCC checks `<dir>/__axcache__/<basename>.exe` (or no extension on Linux). If the cached binary's modification time is newer than the source file, it runs the cached binary directly. Otherwise it recompiles and updates the cache.
4. **Pipeline execution**: Calls each stage in sequence, passing the output of one stage as input to the next.

## Next

[Lexer](02-lexer.md)
