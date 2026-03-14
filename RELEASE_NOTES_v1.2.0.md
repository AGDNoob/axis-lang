# AXIS v1.2.0 - Performance Update

**Philosophy:** "Think in values, compile to metal"

---

## 💭 Developer Note

This release is all about making AXCC faster. In v1.1.0, the compiler produced correct code but left a lot of performance on the table — no register allocation, no constant folding, everything went through the stack. AXCC v1.2.0 changes that with a full optimization pipeline.

The result: all four benchmark programs are now within 30% of GCC `-O0`, down from 1.4–7.1× in v1.1.0. The nested loop benchmark improved the most — from 4.7× slower to just 1.1× — thanks to IR load-store elimination and LEA-based strength reduction.

Beyond performance, this release also delivers on the `copy.compile` vs `copy.runtime` promise from v1.1.0. Both copy modes now produce differentiated code: `copy.runtime` uses `REP MOVSB` for fast execution, `copy.compile` uses an inline byte-copy loop for smaller binaries.

To keep script mode fast, AXCC now skips all optimization passes when running `axis run` or executing `mode script` files. Optimizations only run in compile mode — where runtime performance matters more than compile speed.

Finally, the new `axis check` command lets you validate source files without compiling — useful for CI pipelines and editor integration.

---

## 🚀 What's New

### AXCC Optimizer

AXCC v1.2.0 includes a multi-pass optimization pipeline that runs between IR generation and x64 code emission:

- **Register Allocation** — Linear-scan allocation across all 16 x86-64 general-purpose registers. Local variables and temporaries are mapped to registers; stack spills only when registers are exhausted.
- **Constant Folding & Propagation** — Arithmetic expressions with known values are evaluated at compile time. `x: i32 = 3 + 5` becomes a direct `8` — no ADD instruction emitted.
- **Dead Code Elimination** — Unreachable code after `return`, `stop`, `break` is removed. Constant conditions (`if true:`, `if false:`) are resolved at compile time.
- **Strength Reduction** — Expensive operations are replaced with cheaper equivalents: `MUL` with power-of-two → `SHL`, `DIV` with power-of-two (unsigned) → `SHR`, `MOD` with power-of-two (unsigned) → `AND`. Multiplication by 3, 5, or 9 uses a single `LEA` instruction instead of `IMUL`.
- **CMP+Branch Fusion** — Comparison and conditional jump are fused into a single `CMP` + `Jcc` sequence, eliminating ~5 instructions per branch (no more `SETCC` + `MOVZX` + store + load + `TEST`).
- **IR Load-Store Elimination** — Redundant memory accesses are removed at the IR level. If a value is stored and re-loaded without modification, the load is replaced by a register-to-register move.
- **x64 Spill-Reload Cache** — When a spilled value is loaded into a register, the code generator remembers the mapping. Subsequent accesses use a register-to-register `MOV` instead of a stack load.
- **Register-Aware Instruction Selection** — The x64 backend directly uses physical registers from the allocator, eliminates redundant `MOV` instructions, and swaps operands for commutative operations (ADD, MUL, AND, OR, XOR) to avoid unnecessary moves.

---

### `copy.compile` vs `copy.runtime` — Differentiated Code Generation

In v1.1.0, both copy modes produced identical code. Starting with v1.2.0, they generate different machine code:

```axis
arr2: (i32; 100) = copy.runtime arr1    # REP MOVSB — fast at runtime
arr3: (i32; 100) = copy.compile arr1    # Inline byte-copy loop — smaller binary
```

- **`copy` / `copy.runtime`** — Uses `REP MOVSB` for memory-to-memory copy. The CPU's microcode handles the loop internally, making it fast for larger arrays.
- **`copy.compile`** — Generates an inline byte-copy loop. Produces a smaller binary since it avoids the `REP` prefix setup, but may be slightly slower for large copies.

The default (`copy` without a suffix) remains `copy.runtime`.

---

### `axis check` — Source Validation Without Compilation

The new `check` command runs the full frontend (lexer, parser, semantic analysis) without generating code:

```bash
axis check program.axis            # syntax + semantic errors
axis check program.axis --dead      # + unreachable code warnings
axis check program.axis --unused    # + unused variable warnings
axis check program.axis --all       # all warnings
```

Unlike normal compilation, check mode collects **all** errors instead of stopping at the first one — making it ideal for CI and editor tooling.

---

### Updated Benchmarks

All benchmarks were re-run with the same methodology as v1.1.0 (7 interleaved runs, best taken).

#### AXCC v1.2.0 vs GCC `-O0`

| Benchmark                      | AXCC    | GCC `-O0` | Ratio       |
|--------------------------------|---------|-----------|-------------|
| Recursive Fibonacci `fib(38)`  | 425 ms  | 320 ms    | 1.3× slower |
| Prime Count (0–500K)           | 96 ms   | 89 ms     | 1.1× slower |
| Nested Loops (100M iterations) | 491 ms  | 448 ms    | 1.1× slower |
| GCD Stress (2M calls)          | 68 ms   | 63 ms     | 1.1× slower |

Three of four benchmarks are within 10% of GCC `-O0`. Recursive Fibonacci — the most register-pressure-heavy workload — is within 30%.

#### Improvement over v1.1.0

| Benchmark                      | v1.1.0  | v1.2.0  | Speedup         |
|--------------------------------|---------|---------|-----------------|
| Recursive Fibonacci `fib(38)`  | 554 ms  | 425 ms  | **1.3× faster** |
| Prime Count (0–500K)           | 161 ms  | 96 ms   | **1.7× faster** |
| Nested Loops (100M iterations) | 686 ms  | 491 ms  | **1.4× faster** |
| GCD Stress (2M calls)          | 73 ms   | 68 ms   | **1.1× faster** |

#### Binary Size (unchanged)

AXCC binaries remain minimal: 2.5–3.0 KB with no C runtime, no standard library, and no linker bloat.

---

### Updated Documentation

The `docs/` directory has been updated for v1.2.0:

- **`docs/guide/05-arrays.md`** — Copy modes section updated to reflect differentiated code generation
- **`docs/technical/08-optimizations.md`** — New file documenting the full AXCC optimization pipeline
- **`docs/guide/12-check-command.md`** — New guide for the `axis check` command
- **`docs/Benchmarks.md`** — Updated with v1.2.0 numbers and v1.1.0 → v1.2.0 comparison

---

## 🎉 Summary

AXIS v1.2.0 is a performance-focused release. The AXCC compiler now includes a full optimization pipeline — register allocation, constant folding, dead code elimination, strength reduction, CMP+Branch fusion, load-store elimination, and more. All four benchmarks improved by 1.1–1.7× over v1.1.0, bringing AXCC to within 10–30% of GCC `-O0`.

The `copy.compile` vs `copy.runtime` distinction, promised in v1.1.0, is now fully implemented with differentiated code generation.

The new `axis check` command provides source validation without compilation, with optional warnings for dead code and unused variables.

*Think in values, compile to metal.*
