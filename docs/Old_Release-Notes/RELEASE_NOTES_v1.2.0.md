# AXIS v1.2.0 - Performance Update

**Philosophy:** "Think in values, compile to metal"

---

## ­ƒÆ¡ Developer Note

This release is all about making AXCC faster. In v1.1.0, the compiler produced correct code but left a lot of performance on the table ÔÇö no register allocation, no constant folding, everything went through the stack. AXCC v1.2.0 changes that with a full optimization pipeline.

The result: all four benchmark programs are now within 30% of GCC `-O0`, down from 1.4ÔÇô7.1├ù in v1.1.0. The nested loop benchmark improved the most ÔÇö from 4.7├ù slower to just 1.1├ù ÔÇö thanks to IR load-store elimination and LEA-based strength reduction.

Beyond performance, this release also delivers on the `copy.compile` vs `copy.runtime` promise from v1.1.0. Both copy modes now produce differentiated code: `copy.runtime` uses `REP MOVSB` for fast execution, `copy.compile` uses an inline byte-copy loop for smaller binaries.

To keep script mode fast, AXCC now skips all optimization passes when running `axis run` or executing `mode script` files. Optimizations only run in compile mode ÔÇö where runtime performance matters more than compile speed.

Finally, the new `axis check` command lets you validate source files without compiling ÔÇö useful for CI pipelines and editor integration.

---

## ­ƒÜÇ What's New

### AXCC Optimizer

AXCC v1.2.0 includes a multi-pass optimization pipeline that runs between IR generation and x64 code emission:

- **Register Allocation** ÔÇö Linear-scan allocation across all 16 x86-64 general-purpose registers. Local variables and temporaries are mapped to registers; stack spills only when registers are exhausted.
- **Constant Folding & Propagation** ÔÇö Arithmetic expressions with known values are evaluated at compile time. `x: i32 = 3 + 5` becomes a direct `8` ÔÇö no ADD instruction emitted.
- **Dead Code Elimination** ÔÇö Unreachable code after `return`, `stop`, `break` is removed. Constant conditions (`if true:`, `if false:`) are resolved at compile time.
- **Strength Reduction** ÔÇö Expensive operations are replaced with cheaper equivalents: `MUL` with power-of-two ÔåÆ `SHL`, `DIV` with power-of-two (unsigned) ÔåÆ `SHR`, `MOD` with power-of-two (unsigned) ÔåÆ `AND`. Multiplication by 3, 5, or 9 uses a single `LEA` instruction instead of `IMUL`.
- **CMP+Branch Fusion** ÔÇö Comparison and conditional jump are fused into a single `CMP` + `Jcc` sequence, eliminating ~5 instructions per branch (no more `SETCC` + `MOVZX` + store + load + `TEST`).
- **IR Load-Store Elimination** ÔÇö Redundant memory accesses are removed at the IR level. If a value is stored and re-loaded without modification, the load is replaced by a register-to-register move.
- **x64 Spill-Reload Cache** ÔÇö When a spilled value is loaded into a register, the code generator remembers the mapping. Subsequent accesses use a register-to-register `MOV` instead of a stack load.
- **Register-Aware Instruction Selection** ÔÇö The x64 backend directly uses physical registers from the allocator, eliminates redundant `MOV` instructions, and swaps operands for commutative operations (ADD, MUL, AND, OR, XOR) to avoid unnecessary moves.

---

### `copy.compile` vs `copy.runtime` ÔÇö Differentiated Code Generation

In v1.1.0, both copy modes produced identical code. Starting with v1.2.0, they generate different machine code:

```axis
arr2: (i32; 100) = copy.runtime arr1    # REP MOVSB ÔÇö fast at runtime
arr3: (i32; 100) = copy.compile arr1    # Inline byte-copy loop ÔÇö smaller binary
```

- **`copy` / `copy.runtime`** ÔÇö Uses `REP MOVSB` for memory-to-memory copy. The CPU's microcode handles the loop internally, making it fast for larger arrays.
- **`copy.compile`** ÔÇö Generates an inline byte-copy loop. Produces a smaller binary since it avoids the `REP` prefix setup, but may be slightly slower for large copies.

The default (`copy` without a suffix) remains `copy.runtime`.

---

### `axis check` ÔÇö Source Validation Without Compilation

The new `check` command runs the full frontend (lexer, parser, semantic analysis) without generating code:

```bash
axis check program.axis            # syntax + semantic errors
axis check program.axis --dead      # + unreachable code warnings
axis check program.axis --unused    # + unused variable warnings
axis check program.axis --all       # all warnings
```

Unlike normal compilation, check mode collects **all** errors instead of stopping at the first one ÔÇö making it ideal for CI and editor tooling.

---

### Updated Benchmarks

All benchmarks were re-run with the same methodology as v1.1.0 (7 interleaved runs, best taken).

#### AXCC v1.2.0 vs GCC `-O0`

| Benchmark                      | AXCC    | GCC `-O0` | Ratio       |
|--------------------------------|---------|-----------|-------------|
| Recursive Fibonacci `fib(38)`  | 425 ms  | 320 ms    | 1.3├ù slower |
| Prime Count (0ÔÇô500K)           | 96 ms   | 89 ms     | 1.1├ù slower |
| Nested Loops (100M iterations) | 491 ms  | 448 ms    | 1.1├ù slower |
| GCD Stress (2M calls)          | 68 ms   | 63 ms     | 1.1├ù slower |

Three of four benchmarks are within 10% of GCC `-O0`. Recursive Fibonacci ÔÇö the most register-pressure-heavy workload ÔÇö is within 30%.

#### Improvement over v1.1.0

| Benchmark                      | v1.1.0  | v1.2.0  | Speedup         |
|--------------------------------|---------|---------|-----------------|
| Recursive Fibonacci `fib(38)`  | 554 ms  | 425 ms  | **1.3├ù faster** |
| Prime Count (0ÔÇô500K)           | 161 ms  | 96 ms   | **1.7├ù faster** |
| Nested Loops (100M iterations) | 686 ms  | 491 ms  | **1.4├ù faster** |
| GCD Stress (2M calls)          | 73 ms   | 68 ms   | **1.1├ù faster** |

#### Binary Size (unchanged)

AXCC binaries remain minimal: 2.5ÔÇô3.0 KB with no C runtime, no standard library, and no linker bloat.

---

### Updated Documentation

The `docs/` directory has been updated for v1.2.0:

- **`docs/guide/05-arrays.md`** ÔÇö Copy modes section updated to reflect differentiated code generation
- **`docs/technical/08-optimizations.md`** ÔÇö New file documenting the full AXCC optimization pipeline
- **`docs/guide/12-check-command.md`** ÔÇö New guide for the `axis check` command
- **`docs/Benchmarks.md`** ÔÇö Updated with v1.2.0 numbers and v1.1.0 ÔåÆ v1.2.0 comparison

---

## ­ƒÄë Summary

AXIS v1.2.0 is a performance-focused release. The AXCC compiler now includes a full optimization pipeline ÔÇö register allocation, constant folding, dead code elimination, strength reduction, CMP+Branch fusion, load-store elimination, and more. All four benchmarks improved by 1.1ÔÇô1.7├ù over v1.1.0, bringing AXCC to within 10ÔÇô30% of GCC `-O0`.

The `copy.compile` vs `copy.runtime` distinction, promised in v1.1.0, is now fully implemented with differentiated code generation.

The new `axis check` command provides source validation without compilation, with optional warnings for dead code and unused variables.

*Think in values, compile to metal.*
