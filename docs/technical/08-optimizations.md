# Optimizations

AXCC v1.2.0 includes a multi-pass optimization pipeline. Passes run after IR generation and before (or during) x64 code emission.

## Pipeline Overview

```
IR Generation → Constant Folding → Dead Code Elimination → Load-Store Elimination → x64 Codegen
                                                                                      ↓
                                                                         Register Allocation
                                                                         Strength Reduction
                                                                         CMP+Branch Fusion
                                                                         Spill-Reload Cache
                                                                         Register-Aware Selection
```

Some passes operate on the IR (constant folding, DCE, load-store elimination). Others are integrated into the x64 code generator (register allocation, strength reduction, CMP+Branch fusion, spill-reload cache).

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

; After (v1.2.0):
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

## Impact

See [Benchmarks](../Benchmarks.md) for measurements. In summary:

| Benchmark | v1.1.0 → v1.2.0 | vs GCC `-O0` |
|-----------|-----------------|---------------|
| Fibonacci | 1.3× faster | 1.3× slower |
| Primes | 1.7× faster | 1.1× slower |
| Nested Loops | 1.4× faster | 1.1× slower |
| GCD Stress | 1.1× faster | 1.1× slower |
