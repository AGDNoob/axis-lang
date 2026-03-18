# Optimizations

AXCC v1.2.1 includes a 14-pass optimization pipeline. Passes run after IR generation and before (or during) x64 code emission.

## Pipeline Overview

```
IR Generation → DCE → Constant Folding → Copy Propagation → Function Inlining
                                                                    ↓
              LICM → Loop Unrolling → Register Allocation → x64 Codegen
                                                                ↓
                                                   Strength Reduction
                                                   Register-Aware Selection
                                                   CMP+Branch Fusion
                                                   IR Load-Store Elimination
                                                   Spill-Reload Cache
                                                   Peephole Optimization
                                                   Redundant Instruction Elimination
```

Some passes operate on the IR (DCE, constant folding, copy propagation, function inlining, LICM, loop unrolling, load-store elimination). Others are integrated into the x64 code generator (register allocation, strength reduction, register-aware selection, CMP+Branch fusion, spill-reload cache, peephole optimization, RIE).

## 32-Bit Native Arithmetic

Since AXIS integers are `i32`, AXCC generates native 32-bit x86 instructions for all integer operations. The 32-bit form is shorter, faster to decode, and implicitly zero-extends the upper 32 bits.

- **`mov`** — 5-byte encoding (`mov r32, imm32`) instead of 10-byte (`movabs r64, imm64`)
- **`add`, `sub`, `imul`, `idiv`, `cmp`** — 32-bit operand size, no REX.W prefix
- **`cdq`** replaces `cqo` — sign-extends EAX → EDX:EAX (32-bit) instead of RAX → RDX:RAX (128-bit)
- **Pointer and stack operations** remain 64-bit (addresses are always 64-bit on x86-64)

**Impact on binary size:**

| Benchmark           | v1.2.0 | v1.2.1 | Savings |
|---------------------|--------|--------|---------|
| Recursive Fibonacci | 2.5 KB | 2.0 KB | -20%    |
| Prime Count         | 3.5 KB | 3.0 KB | -14%    |

## IR-Level Passes

### Constant Folding & Propagation

Arithmetic expressions with known values are evaluated at compile time:

```
# Before:
t1 = 3
t2 = 5
t3 = t1 + t2
store_var x, t3

# After:
t3 = 8
store_var x, t3
```

Applies to `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>` and comparison operators.

### Dead Code Elimination

Unreachable code after `return`, `stop`, or `break` is removed from the IR. Constant conditions (`if true:`, `if false:`) are resolved — the dead branch is eliminated entirely.

### Copy Propagation

When a value is copied between temporaries or registers (`mov rbx, rax`), subsequent uses of `rbx` are replaced with `rax` where possible. This exposes more opportunities for dead code elimination.

### Function Inlining

Small leaf functions (no calls, small body) are inlined at the call site, eliminating call/return overhead and enabling cross-function optimization.

### Loop-Invariant Code Motion (LICM)

Expressions whose operands don't change inside a loop are hoisted before the loop header. Avoids redundant recomputation on every iteration.

### Loop Unrolling

Small fixed-count loops are unrolled to reduce branch overhead and enable further peephole optimization across iterations.

### Load-Store Elimination

Tracks which values are stored in stack variables. If a value is stored and re-loaded without an intervening modification, the load is replaced by a reference to the already-known temporary:

```
store_var [rbp-8], t5
...                       # no store to [rbp-8]
load_var  t10, [rbp-8]   → replaced by: mov t10, t5
```

The cache is flushed at labels, function calls, and indirect stores to ensure correctness.

## x64-Level Passes

### Register Allocation

Linear-scan allocation over all 16 general-purpose x86-64 registers. Virtual IR temporaries are assigned to physical registers based on live ranges. When all registers are occupied, the least-recently-used value is spilled to the stack.

### Strength Reduction

Expensive operations are replaced by cheaper equivalents when one operand is a known constant:

| Pattern | Replacement | Condition |
|---------|-------------|-----------|
| `x * 2^n` | `x << n` | Power-of-two multiplier |
| `x / 2^n` (unsigned) | `x >> n` | Power-of-two divisor |
| `x % 2^n` (unsigned) | `x & (2^n - 1)` | Power-of-two modulus |
| `x * 3` | `lea r, [r + r*2]` | — |
| `x * 5` | `lea r, [r + r*4]` | — |
| `x * 9` | `lea r, [r + r*8]` | — |

LEA-multiply uses 1 cycle latency vs 3 cycles for `IMUL`.

### CMP+Branch Fusion

Comparison and conditional jump are fused into a single `CMP` + `Jcc` sequence. Without fusion, a conditional branch materializes a boolean:

```asm
; Before (v1.1.0):
cmp  eax, ebx
setl al
movzx eax, al
mov  [rbp-X], eax
mov  eax, [rbp-X]
test eax, eax
jz   .else

; After (v1.2.1):
cmp  eax, ebx
jge  .else
```

This eliminates ~5 instructions per branch.

### Spill-Reload Cache

When a spilled temporary is loaded back into a register, the code generator records the mapping. On subsequent accesses to the same spill slot, it emits a `MOV reg, reg` instead of `MOV reg, [rbp-offset]`. The cache is invalidated at register clobbers, branch targets, and complex instructions (`INDEX_LOAD`, etc.).

### Register-Aware Instruction Selection

The x64 backend uses physical register assignments directly:

- Values already in the target register are not reloaded
- Redundant `MOV` instructions are suppressed
- For commutative operations (ADD, MUL, AND, OR, XOR), operands are swapped to match the destination register, avoiding an extra move

### Peephole Optimization

Scans the emitted instruction stream and eliminates redundant patterns: `mov rax, rax` (self-moves), `add rax, 0` (identity arithmetic), back-to-back complementary operations.

### Redundant Instruction Elimination

Post-emission pass that removes instructions whose results are immediately overwritten or never used.

## Impact

See [Benchmarks](../Benchmarks.md) for measurements. In summary:

| Benchmark | v1.1.0 → v1.2.1 | vs GCC `-O0` |
|-----------|-----------------|---------------|
| Fibonacci | 1.88× faster | 1.11× slower |
| Primes | 2.09× faster | ~parity |
| Nested Loops | 1.85× faster | 1.09× slower |
| GCD Stress | 1.52× faster | **0.96× faster** |
