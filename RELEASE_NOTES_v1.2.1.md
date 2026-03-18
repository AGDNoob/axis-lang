# AXIS v1.2.1 — Optimization Update

**Philosophy:** "Think in values, compile to metal"

---

## 💭 Developer Note

v1.2.0 introduced the AXCC optimization pipeline — register allocation, constant folding, dead code elimination, and more. That was the foundation. v1.2.1 builds on it with six additional optimization passes and a fundamental change to how the compiler encodes arithmetic.

The headline: AXCC now encodes all integer operations as native 32-bit x86 instructions, matching the `i32` type width. Combined with the new optimizer passes — peephole optimization, copy propagation, function inlining, loop-invariant code motion, and loop unrolling — this brings Fibonacci from 1.3× slower than GCC `-O0` down to 1.11×. On GCD Stress, AXCC now **beats GCC** at `-O0`.

No new language features in this release. Just making the existing ones faster.

---

## 🚀 What's New

### 32-Bit Native Arithmetic

AXCC now generates native 32-bit x86 instructions for all integer operations. Since AXIS integers are `i32`, there's no reason to use 64-bit encoding — the 32-bit form is shorter, faster to decode, and implicitly zero-extends the upper 32 bits.

**What changed:**
- **`mov`** — 5-byte encoding (`mov r32, imm32`) instead of 10-byte (`movabs r64, imm64`)
- **`add`, `sub`, `imul`, `idiv`, `cmp`** — 32-bit operand size, no REX.W prefix
- **`cdq`** replaces `cqo` — sign-extends EAX → EDX:EAX (32-bit) instead of RAX → RDX:RAX (128-bit)
- **Pointer and stack operations** remain 64-bit (addresses are always 64-bit on x86-64)

**Impact on binary size:**

| Benchmark          | v1.2.0   | v1.2.1  | Savings |
|--------------------|----------|---------|---------|
| Recursive Fibonacci| 2.5 KB   | 2.0 KB  | -20%    |
| Prime Count        | 3.5 KB   | 3.0 KB  | -14%    |

Shorter instructions also mean better instruction cache utilization and faster decoding.

---

### New Optimizer Passes

Six new optimization passes have been added to the AXCC pipeline, bringing the total to 14:

- **Peephole Optimization** — Scans the emitted instruction stream and eliminates redundant patterns: `mov rax, rax` (self-moves), `add rax, 0` (identity arithmetic), back-to-back complementary operations.
- **Copy Propagation** — When a value is copied between registers (`mov rbx, rax`), subsequent uses of `rbx` are replaced with `rax` where possible, enabling further dead code elimination.
- **Function Inlining** — Small leaf functions (no calls, small body) are inlined at the call site, eliminating call/return overhead and enabling cross-function optimization.
- **Loop-Invariant Code Motion (LICM)** — Expressions whose operands don't change inside a loop are hoisted before the loop header. Avoids redundant recomputation on every iteration.
- **Loop Unrolling** — Small fixed-count loops are unrolled to reduce branch overhead and enable further peephole optimization across iterations.
- **Redundant Instruction Elimination** — Post-emission pass that removes instructions whose results are immediately overwritten or never used.

The full AXCC optimizer pipeline (in order):
1. Dead Code Elimination
2. Constant Folding & Propagation
3. Copy Propagation *(new)*
4. Function Inlining *(new)*
5. Loop-Invariant Code Motion *(new)*
6. Loop Unrolling *(new)*
7. Linear-Scan Register Allocation
8. Strength Reduction (including LEA-Multiply)
9. Register-Aware Instruction Selection
10. CMP+Branch Fusion
11. IR Load-Store Elimination
12. x64 Spill-Reload Cache
13. Peephole Optimization *(new)*
14. Redundant Instruction Elimination *(new)*

---

### Updated Benchmarks

All benchmarks re-run with the same methodology (7 interleaved runs, best taken).

#### AXCC v1.2.1 vs GCC `-O0`

| Benchmark                      | AXCC    | GCC `-O0` | Ratio            |
|--------------------------------|---------|-----------|------------------|
| Recursive Fibonacci `fib(38)`  | 295 ms  | 266 ms    | 1.11× slower     |
| Prime Count (0–500K)           | 77 ms   | 77 ms     | **~parity**      |
| Nested Loops (100M iterations) | 370 ms  | 338 ms    | 1.09× slower     |
| GCD Stress (2M calls)          | 48 ms   | 50 ms     | **0.96× faster** |

Three of four benchmarks are within 11% of GCC `-O0`. GCD Stress now **beats GCC**.

#### Improvement over v1.2.0

| Benchmark                      | v1.2.0  | v1.2.1  | Speedup          |
|--------------------------------|---------|---------|------------------|
| Recursive Fibonacci `fib(38)`  | 425 ms  | 295 ms  | **1.44× faster** |
| Prime Count (0–500K)           | 96 ms   | 77 ms   | **1.25× faster** |
| Nested Loops (100M iterations) | 491 ms  | 370 ms  | **1.33× faster** |
| GCD Stress (2M calls)          | 68 ms   | 48 ms   | **1.42× faster** |

#### Improvement over v1.1.0

| Benchmark                      | v1.1.0  | v1.2.1  | Speedup          |
|--------------------------------|---------|---------|------------------|
| Recursive Fibonacci `fib(38)`  | 554 ms  | 295 ms  | **1.88× faster** |
| Prime Count (0–500K)           | 161 ms  | 77 ms   | **2.09× faster** |
| Nested Loops (100M iterations) | 686 ms  | 370 ms  | **1.85× faster** |
| GCD Stress (2M calls)          | 73 ms   | 48 ms   | **1.52× faster** |

#### Binary Size

| Benchmark          | AXCC    | GCC `-O0` | Ratio               |
|--------------------|---------|-----------|----------------------|
| Recursive Fibonacci| 2.0 KB  | 59.6 KB   | **30× smaller**      |
| Prime Count        | 3.0 KB  | 59.6 KB   | **20× smaller**      |
| Nested Loops       | 2.5 KB  | 59.6 KB   | **24× smaller**      |
| GCD Stress         | 3.0 KB  | 59.6 KB   | **20× smaller**      |

---

## 🐛 Bug Fixes

20 bugs fixed across 6 source files. Grouped by severity.

### Critical

| ID | File | Description |
|----|------|-------------|
| XU-01 | `x64.c` | **Parameter offset for argument 5+ was wrong** — `16 + i*8` instead of `16 + (i-4)*8`. Functions with 5+ arguments read garbage from the stack. |
| L1 | `lexer.c` | **Indent stack overflow only protected by `assert()`** — in release builds (NDEBUG), no bounds check → buffer overflow. |

### High

| ID | File | Description |
|----|------|-------------|
| IR1 | `irgen.c` | Logical NOT returned operand size instead of 1 byte — garbage in upper bytes of boolean result. |
| IR2 | `irgen.c` | Comparisons (`==`, `!=`, `<`, `>`, `<=`, `>=`) returned operand size instead of 1 byte — wrong result size. |
| L6 | `lexer.c` | `strtoull(buf, NULL, 0)` didn't recognize `0b` prefix — all binary literals evaluated to 0. |
| S3 | `semantic.c` | `update` parameter accepted immutable variables — violated mutability guarantee. |
| S1/S2 | `semantic.c` | Duplicate enum variant names/values not detected — invalid code silently accepted. |
| S4 | `semantic.c` | Built-in `read`/`readln`/`readchar` didn't check argument count — e.g. `read(1,2,3)` accepted. |
| S5 | `semantic.c` | `match` on enum didn't check for exhaustiveness — missing variants not detected. |
| P2 | `parser.c` | `copy` used `parse_expression()` instead of `parse_unary()` — wrong operator precedence. |
| XU-04 | `x64.c` | Division by zero caused CPU exception #DE without error message — now caught with runtime check. |
| PE1 | `pe.c` | `SizeOfCode` used file-aligned instead of virtual sizes — technically incorrect PE header. |

### Medium

| ID | File | Description |
|----|------|-------------|
| PE2 | `pe.c` | `.rdata` section had WRITE flag — W^X violation (security). |
| L8 | `lexer.c` | Mixed tabs/spaces in indentation not detected — unpredictable indent behavior. |
| L9 | `lexer.c` | Tab character incremented column by 1 only — wrong error positions when using tabs. |
| XU-05 | `x64.c` | MEMCPY loop used hardcoded jump offsets — fragile, replaced with label-based jumps. |
| IR4 | `irgen.c` | Store size derived from value instead of variable — potential size mismatch. |
| IR5 | `irgen.c` | For-loop variable size derived from expression instead of type — potential size mismatch. |
| S6 | `semantic.c` | Array element type fell back to `i32` silently — hid type errors. |
| S8 | `semantic.c` | Copy-parameter mutability not enforced — feature was incomplete. |

---

## 🎉 Summary

AXIS v1.2.1 is an optimization and stability release. The AXCC compiler now includes 14 optimization passes and generates native 32-bit arithmetic — bringing performance to within 11% of GCC `-O0` across all benchmarks, with GCD Stress now faster than GCC. Binaries are up to 20% smaller thanks to shorter instruction encoding. Compared to v1.1.0, all benchmarks improved by 1.5–2.1×. Additionally, 20 bugs were fixed across the lexer, parser, semantic analyzer, IR generator, x64 code generator, and PE generator — including 2 critical issues (stack corruption with 5+ function arguments, indent stack overflow).

*Think in values, compile to metal.*
