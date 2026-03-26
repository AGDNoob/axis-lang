# SSA-Based IR Optimization Architecture

AXCC v2.0 introduces a 6-tier SSA-based optimization pipeline. The goal: beat GCC `-O3` and LLVM `-O3` on every benchmark — with zero external dependencies.

## Why AXIS Wins

AXIS has **structural advantages** over C/C++ that no amount of analysis can replicate in GCC or LLVM:

| AXIS Property | Compiler Advantage | What GCC/LLVM Must Do Instead |
| --- | --- | --- |
| **No pointers** | Perfect alias information — free | Points-to analysis, alias analysis (expensive, imprecise) |
| **Explicit aliases** (`x = y`) | Compiler knows ALL shared storage at parse time | Must conservatively assume aliasing |
| **Explicit copies** (`copy y`) | Compiler knows independence — guaranteed | Escape analysis, copy-on-write heuristics |
| **Stack-only** | All memory has known lifetime, no escape | Heap analysis, GC interaction, escape analysis |
| **`const` is absolute** | Values propagate everywhere, guaranteed | `const` in C can be cast away — not trustworthy |
| **No function pointers** | Every call target is known — inlining is exact | Devirtualization, indirect call profiling (PGO) |
| **Fixed `i32` width** | Native 32-bit encoding everywhere | Integer promotion rules, mixed-width arithmetic |
| **No undefined behavior** | Every optimization is sound — no UB exploitation needed | Entire optimization classes rely on UB (signed overflow, strict aliasing) |

**These advantages compound at every tier.** A simple constant propagation in AXIS is as powerful as SCCP + alias analysis + escape analysis combined in LLVM — because the language guarantees what LLVM must prove.

---

## Pipeline Overview

```text
Source → AST → Semantic Analysis → IR Generation
                                        ↓
                              ┌─────────────────────┐
                              │   TIER 1: SSA Build  │  Dominators, φ-functions, alias resolution
                              └──────────┬──────────┘
                                         ↓
                              ┌─────────────────────┐
                              │  TIER 2: Scalar Opt  │  SCCP, GVN, DCE, Inlining
                              └──────────┬──────────┘
                                         ↓
                              ┌─────────────────────┐
                              │  TIER 3: Loop Opt    │  LICM, Unrolling, IV Simplification
                              └──────────┬──────────┘
                                         ↓
                              ┌─────────────────────┐
                              │  TIER 4: Lowering    │  SSA → Machine IR, Instruction Selection
                              └──────────┬──────────┘
                                         ↓
                              ┌─────────────────────┐
                              │  TIER 5: RegAlloc    │  Graph Coloring on SSA, Spill Optimization
                              └──────────┬──────────┘
                                         ↓
                              ┌─────────────────────┐
                              │  TIER 6: Emission    │  Peephole, Scheduling, Binary Output
                              └──────────┬──────────┘
                                         ↓
                                   ELF / PE Binary
```

---

## Tier 1 — SSA Construction & Normalization

**Purpose:** Transform the linear IR into Static Single Assignment form. Every variable is defined exactly once. Control-flow merges are resolved with φ-functions. This is the foundation — every subsequent tier operates on SSA.

### Existing Passes

*None.* Current IR is linear three-address code without SSA properties.

### New Components

| Component | Description | Why It Beats GCC/LLVM |
| --- | --- | --- |
| **Dominator Tree** | Compute immediate dominators for all basic blocks (Lengauer-Tarjan) | Same as GCC/LLVM — but AXIS CFGs are simpler (no computed goto, no longjmp) |
| **Dominance Frontiers** | Identify φ-function insertion points | Standard SSA construction |
| **φ-Function Insertion** | Place φ-functions at dominance frontiers (Cytron's algorithm) | Same algorithm, but fewer φ-functions because AXIS has no pointer-induced defs |
| **SSA Renaming** | Rename all variable versions (def-use chains become trivial) | Standard — but AXIS aliases are resolved at parse time, no analysis needed |
| **Alias Resolution** | `x = y` → x and y share the same SSA name. `x = copy y` → x gets a fresh SSA name | **Free — language semantics give perfect alias info. GCC/LLVM can never achieve this.** |
| **Const Marking** | `const` variables tagged as SSA invariants — never redefined, globally propagable | `const` in AXIS is absolute. In C, `const` can be cast away — LLVM must verify. |

### AXIS Advantage at Tier 1

AXIS generates **fewer φ-functions** and **smaller SSA graphs** than equivalent C programs because:

- No pointer writes → no may-defs → no φ-functions from aliased stores
- Explicit aliases collapse to shared SSA names → fewer variables to track
- Stack-only → no heap-induced φ-functions at call boundaries

---

## Tier 2 — Scalar Optimizations (SSA-Based)

**Purpose:** High-level scalar transforms on the SSA IR. These are the "big wins" — they eliminate redundant computation, propagate constants, remove dead code, and inline functions.

### Existing Passes (Upgraded to SSA)

| Pass | Current Version | SSA Version | Improvement |
| --- | --- | --- | --- |
| **Constant Folding & Propagation** | Pattern-matching on IR triples | **Sparse Conditional Constant Propagation (SCCP)** — propagates constants through φ-functions AND resolves unreachable branches simultaneously | Folding + branch elimination in one pass instead of two |
| **Copy Propagation** | Tracks `mov` chains in x64 output | **SSA Copy Propagation** — follows use-def chains directly, eliminates φ-copies | O(n) instead of O(n²), catches cross-block propagation |
| **Dead Code Elimination** | Removes unreachable code after `return`/`stop` | **Aggressive DCE (ADCE)** — marks live instructions, deletes everything else. Works on SSA def-use | Catches dead computation, not just dead control flow |
| **Function Inlining** | Small leaf functions only | **SSA-Aware Inlining** — cost model considers SSA graph size, inlines through φ-functions, re-runs SCCP after inlining | Cross-function constant propagation after inlining |

### New Passes

| Pass | Description | Why It Beats GCC/LLVM |
| --- | --- | --- |
| **Global Value Numbering (GVN)** | Assigns identical values the same SSA number — detects and eliminates redundant computations across basic blocks | Common Subexpression Elimination on steroids. In AXIS: no pointer aliasing → GVN is always correct. In C: GVN must prove non-aliasing first. |
| **φ-Elimination** | Remove trivial φ-functions (φ(x, x) → x) and dead φ-nodes | Keeps SSA graph clean after other passes |

### AXIS Advantage at Tier 2

- **SCCP is maximally powerful:** No pointer-based control flow → all branches are analyzable. `const` variables propagate globally without proof obligations.
- **GVN is always sound:** No aliasing uncertainty → two expressions with same operands ALWAYS produce the same value. GCC/LLVM must insert `may-alias` checks.
- **Inlining cost model is exact:** Every call target is statically known. No indirect calls, no virtual dispatch. The compiler can make optimal inline decisions — GCC/LLVM must guess.
- **ADCE is more aggressive:** No side effects through pointers → more code is provably dead.

---

## Tier 3 — Loop Optimizations

**Purpose:** Transform loops for maximum throughput. AXIS loops are simple (no pointer iteration, no aliased loop counters) — every loop optimization is safe by construction.

### Tier 3 Existing Passes (Upgraded to SSA)

| Pass | Current Version | SSA Version | Improvement |
| --- | --- | --- | --- |
| **LICM** | Pattern-matching for invariant IR ops | **SSA LICM** — checks if operand SSA defs dominate loop header. Trivially correct on SSA. | Catches more invariants (cross-block), no kill-set tracking needed |
| **Loop Unrolling** | Small fixed-count loops | **SSA Unrolling** — unrolls with φ-function adjustment, enables cross-iteration SCCP | Constant propagation across unrolled iterations |

### Tier 3 New Passes

| Pass | Description | Why It Beats GCC/LLVM |
| --- | --- | --- |
| **Induction Variable Simplification** | Detect IVs (loop counters, derived values), canonicalize to `{base, step}` form | Enables strength reduction on IVs. AXIS for-loops have explicit IV — detection is trivial. |
| **Loop Strength Reduction** | Replace IV-dependent multiplications with additions: `i * 4` → `iv += 4` each iteration | Classic loop optimization. In AXIS: IV is always the only loop variable — no pointer-arithmetic IVs to analyze. |
| **Loop Rotation** | Convert `while` to `do-while` with guard — canonical form for all other loop opts | Ensures single back-edge, simplifies LICM and unrolling. AXIS already has `repeat` (do-while). |

### AXIS Advantage at Tier 3

- **No pointer-based loop iteration:** C loops often iterate through pointer arithmetic (`*p++`). AXIS loops use integer ranges → IV analysis is trivial.
- **No aliased loop state:** Loop body cannot modify external state through pointers → LICM can hoist more aggressively.
- **`for i in range(a, b)` is a canonical IV:** The compiler already knows the loop variable, start, end, and step. GCC/LLVM must reconstruct this from pointer arithmetic and integer comparisons.

---

## Tier 4 — Lowering & Instruction Selection

**Purpose:** Lower SSA IR to machine-level IR. Select x86-64 instructions, apply machine-specific strength reductions, and prepare for register allocation.

### Existing Passes (Integrated into Lowering)

| Pass | Current Version | In Tier 4 | Notes |
| --- | --- | --- | --- |
| **Strength Reduction** | Replace `imul` with LEA/shift | **Machine Strength Reduction** — applied during instruction selection | LEA-multiply (`*3`, `*5`, `*9`), shift for powers-of-2 |
| **CMP+Branch Fusion** | Fuse compare + conditional jump | **Flag-Aware Lowering** — CMP result feeds directly into Jcc, no boolean materialization | Eliminates ~5 instructions per branch |
| **Register-Aware Instruction Selection** | Suppress redundant MOVs, swap commutative operands | **Instruction Selection** — integrated into lowering, uses SSA def-use to pick optimal forms | SSA makes this trivial — each value has exactly one def |
| **Load-Store Elimination** | Track stack variable values, skip redundant loads | **SSA Load-Store Elimination** — stack slots are SSA-renamed, redundant loads impossible by construction | In SSA: loads and stores correspond to SSA defs/uses — elimination is structural |
| **32-bit Native Arithmetic** | Use 32-bit x86 encoding for i32 ops | **Encoding Selection** — all i32 ops use shorter 32-bit encoding | 20% smaller binaries, faster decode |

### Tier 4 New Passes

| Pass | Description | Why It Beats GCC/LLVM |
| --- | --- | --- |
| **Instruction Scheduling** | Reorder independent instructions to avoid pipeline stalls and utilize execution ports | Same concept as GCC/LLVM, but AXIS has no memory-ordering constraints (no pointers → no fence/barrier concerns) |
| **Address Mode Selection** | Choose optimal x86-64 addressing modes: `[base + index*scale + disp]` | AXIS array access patterns are simple and predictable — always `base + index*4` |

### AXIS Advantage at Tier 4

- **Load-Store elimination is structural:** In SSA, each variable has one definition. No need to track "which store last wrote this slot" — the SSA name IS the answer.
- **No memory ordering constraints:** No `volatile`, no atomics, no pointer-aliased stores → instruction scheduling is unconstrained.
- **Instruction selection is simpler:** Fixed `i32` type means no type-width decisions. Every integer op maps to exactly one x86 instruction form.

---

## Tier 5 — Register Allocation

**Purpose:** Map SSA values to physical x86-64 registers. AXIS uses all 16 general-purpose registers. Spills go to the stack with caching.

### Existing Passes (Upgraded)

| Pass | Current Version | SSA Version | Improvement |
| --- | --- | --- | --- |
| **Linear-Scan Register Allocation** | Live-range based, 16 GP registers | **SSA Linear Scan** — live ranges computed from SSA def-use chains (O(n)), φ-function copies inserted at block boundaries | Faster allocation, better spill decisions |
| **Spill-Reload Cache** | Track spilled values in registers, skip redundant loads | **SSA Spill Optimization** — spilled SSA names are tracked; reloads reuse registers if the SSA name is still valid | Fewer memory accesses, more cache-friendly |

### Tier 5 New Passes

| Pass | Description | Why It Beats GCC/LLVM |
| --- | --- | --- |
| **Live Range Splitting** | Split long live ranges at strategic points (loop boundaries, call sites) to reduce spill pressure | AXIS has no callee-saved register conventions to worry about — all 16 registers available |
| **φ-Function Deconstruction** | Convert φ-functions to parallel copies, coalesce where possible | Reduces copy overhead from SSA destruction |

### Future: Graph Coloring

The SSA form enables **chordal graph coloring** — a polynomial-time optimal register allocation algorithm. SSA interference graphs are always chordal (no odd cycles ≥ 5), which means:

- Optimal coloring in O(V + E) instead of NP-hard general graph coloring
- GCC uses graph coloring but on non-SSA IR → NP-hard heuristics
- LLVM uses greedy allocation → not optimal
- AXIS on SSA → **provably optimal register allocation in polynomial time**

### AXIS Advantage at Tier 5

- **All 16 GP registers available:** No registers reserved for special purposes (no frame pointer by default, no thread-local pointers).
- **Small live ranges:** Stack-only + explicit copies → values don't escape, live ranges are short.
- **Chordal interference graphs:** SSA guarantees this — enables optimal coloring that GCC/LLVM cannot achieve on their IRs.

---

## Tier 6 — Post-RA Machine Finalization

**Purpose:** Final machine code polishing. Runs after register allocation on the physical instruction stream.

### Existing Passes (Retained)

| Pass | Description | Status |
| --- | --- | --- |
| **Peephole Optimization** | Eliminate `mov rax, rax`, `add rax, 0`, complementary op pairs | Retained — runs on final instruction stream |
| **Redundant Instruction Elimination** | Remove instructions whose results are immediately overwritten or never used | Retained — final cleanup pass |

### Tier 6 New Passes

| Pass | Description | Why It Beats GCC/LLVM |
| --- | --- | --- |
| **Branch Relaxation** | Replace `jmp rel32` with `jmp rel8` where target is within ±127 bytes | Smaller binary, better I-cache utilization |
| **NOP Alignment** | Pad loop headers and function entries to 16-byte boundaries | Matches CPU fetch-block alignment — same technique as GCC/LLVM |
| **Post-RA Scheduling** | Final instruction reordering after physical register assignment, respecting true dependencies | Avoids pipeline stalls on the final instruction stream |

### AXIS Advantage at Tier 6

- **Shorter instruction stream:** Earlier tiers eliminate more code (no aliasing uncertainty) → fewer instructions to polish.
- **Simpler dependency tracking:** No memory-order dependencies → scheduling only needs to consider register dependencies.

---

## Complete Pass Mapping

### Existing 14 Passes → Tier Assignment

| # | Pass | Origin | Target Tier | SSA Upgrade |
| --- | --- | --- | --- | --- |
| 1 | Dead Code Elimination | v1.2.0 | **Tier 2** | → Aggressive DCE (ADCE) |
| 2 | Constant Folding & Propagation | v1.2.0 | **Tier 2** | → Sparse Conditional Constant Propagation (SCCP) |
| 3 | Copy Propagation | v1.2.1 | **Tier 2** | → SSA Copy Propagation (use-def chains) |
| 4 | Function Inlining | v1.2.1 | **Tier 2** | → SSA-Aware Inlining + cost model |
| 5 | LICM | v1.2.1 | **Tier 3** | → SSA LICM (dominator-based) |
| 6 | Loop Unrolling | v1.2.1 | **Tier 3** | → SSA Unrolling + cross-iteration opt |
| 7 | Load-Store Elimination | v1.2.0 | **Tier 4** | → Structural SSA elimination |
| 8 | Strength Reduction | v1.2.0 | **Tier 4** | → Machine Strength Reduction |
| 9 | Register-Aware Instruction Selection | v1.2.0 | **Tier 4** | → Integrated Instruction Selection |
| 10 | CMP+Branch Fusion | v1.2.0 | **Tier 4** | → Flag-Aware Lowering |
| 11 | 32-bit Native Arithmetic | v1.2.1 | **Tier 4** | → Encoding Selection |
| 12 | Linear-Scan Register Allocation | v1.2.0 | **Tier 5** | → SSA Linear Scan (→ Graph Coloring) |
| 13 | Spill-Reload Cache | v1.2.0 | **Tier 5** | → SSA Spill Optimization |
| 14 | Peephole Optimization | v1.2.1 | **Tier 6** | Retained |
| 15 | Redundant Instruction Elimination | v1.2.1 | **Tier 6** | Retained |

### New Passes per Tier

| Tier | New Passes | Total |
| --- | --- | --- |
| **Tier 1** | Dominator Tree, Dominance Frontiers, φ-Insertion, SSA Renaming, Alias Resolution, Const Marking | 6 new |
| **Tier 2** | Global Value Numbering (GVN), φ-Elimination | 2 new + 4 upgraded |
| **Tier 3** | Induction Variable Simplification, Loop Strength Reduction, Loop Rotation | 3 new + 2 upgraded |
| **Tier 4** | Instruction Scheduling, Address Mode Selection | 2 new + 5 integrated |
| **Tier 5** | Live Range Splitting, φ-Deconstruction | 2 new + 2 upgraded |
| **Tier 6** | Branch Relaxation, NOP Alignment, Post-RA Scheduling | 3 new + 2 retained |

**Total: 14 existing (all retained/upgraded) + 18 new = 32 passes across 6 tiers.**

---

## Optimization Levels

AXCC exposes 5 optimization levels via CLI flags. Each level enables a progressively larger subset of the 32-pass pipeline.

### `-O0` — Debug (No Optimization)

**Tiers active:** None (linear IR → direct codegen)

No SSA construction. IR is lowered directly to x64 with naive register usage (spill-everything). Maximum compile speed, maximum debuggability, worst runtime performance.

| What Runs | What Doesn't |
| --- | --- |
| IR generation | SSA construction |
| Direct x64 emission | All 32 optimization passes |
| 32-bit native encoding | Register allocation (spill-all) |

**Use case:** Development, debugging, rapid iteration.

---

### `-O1` — Fast Compile, Good Code

**Tiers active:** 1, 2, 4 (partial), 5, 6 (partial)

SSA is built, scalar optimizations run, basic lowering + register allocation. No loop optimizations, no instruction scheduling. Compile time stays low — code quality already beats unoptimized GCC.

| Tier | Passes Enabled |
| --- | --- |
| **Tier 1** | Dominator Tree, Dominance Frontiers, φ-Insertion, SSA Renaming, Alias Resolution, Const Marking |
| **Tier 2** | SCCP, SSA Copy Propagation, ADCE, φ-Elimination |
| **Tier 3** | — |
| **Tier 4** | Load-Store Elimination, Strength Reduction, CMP+Branch Fusion, Register-Aware ISel, 32-bit Encoding |
| **Tier 5** | SSA Linear Scan RegAlloc, Spill-Reload Cache |
| **Tier 6** | Peephole, Redundant Instruction Elimination |

**Inactive:** GVN, Inlining, all loop passes, Instruction Scheduling, Address Mode Selection, Live Range Splitting, Branch Relaxation, NOP Alignment, Post-RA Scheduling.

**Rationale:** SCCP + ADCE + Copy Prop alone already eliminate a large amount of dead code. Without inlining and loop opts, compile time stays low. Strength Reduction + CMP-Fusion come for free during lowering.

**Goal: GCC `-O0` parity or better.**

---

### `-O2` — Standard Optimization (Default)

**Tiers active:** 1, 2, 3, 4, 5, 6 (partial)

Full scalar and loop optimizations. Instruction selection with address modes. No aggressive unrolling, no post-RA scheduling, no NOP alignment.

| Tier | Passes Enabled (in addition to `-O1`) |
| --- | --- |
| **Tier 2** | + GVN, + SSA-Aware Inlining (conservative cost model) |
| **Tier 3** | + SSA LICM, + Loop Rotation, + Induction Variable Simplification, + Loop Strength Reduction |
| **Tier 4** | + Address Mode Selection |
| **Tier 5** | + Live Range Splitting, + φ-Deconstruction |
| **Tier 6** | + Branch Relaxation |

**Inactive:** Loop Unrolling (code size), NOP Alignment, Post-RA Scheduling, aggressive Inlining.

**Rationale:** GVN eliminates redundant computations across block boundaries. Inlining opens SCCP for cross-function propagation. LICM + IV Simplification + Loop Strength Reduction transform loops without code explosion. Live Range Splitting reduces spill pressure.

**Goal: beat GCC `-O2`.**

---

### `-O3` — Maximum Performance

**Tiers active:** 1–6 complete — all 32 passes

Everything enabled. Aggressive inlining, loop unrolling, instruction scheduling, NOP alignment. Compile time and binary size are secondary — only performance matters.

| Tier | Passes Enabled (in addition to `-O2`) |
| --- | --- |
| **Tier 2** | Inlining: aggressive cost model (larger functions, deeper call chains) |
| **Tier 3** | + SSA Loop Unrolling (mit cross-iteration SCCP) |
| **Tier 6** | + NOP Alignment (16-Byte Loop/Function Heads), + Post-RA Scheduling |

**Rationale:** Loop Unrolling eliminates branch overhead and enables cross-iteration constant propagation. NOP Alignment exploits CPU fetch-block sizes. Post-RA Scheduling avoids pipeline stalls. Aggressive inlining handles the rest.

**Goal: beat GCC `-O3` AND LLVM `-O3`.**

---

### `-Os` — Size Optimization

**Tiers active:** 1, 2, 3 (partial), 4, 5, 6 (partial)

Like `-O2`, but everything that bloats code is disabled. Minimal binary size at good performance.

| Tier | Difference from `-O2` |
| --- | --- |
| **Tier 2** | Inlining: only functions ≤ 3 IR ops (tiny leaf only) |
| **Tier 3** | Loop Unrolling: **off**, LICM/IV/Rotation: **on** |
| **Tier 6** | NOP Alignment: **off**, Branch Relaxation: **on** (saves bytes) |

**Rationale:** LICM and IV Simplification improve performance without code growth. Inlining tiny leaf functions saves call overhead without significant size cost. No unrolling, no NOP padding.

**Goal: Smallest binaries at >90% of `-O2` performance.**

---

### Level Comparison Matrix

| Pass | `-O0` | `-O1` | `-O2` | `-O3` | `-Os` |
| --- | :---: | :---: | :---: | :---: | :---: |
| **Tier 1: SSA Construction** | | | | | |
| Dominator Tree | — | ✓ | ✓ | ✓ | ✓ |
| Dominance Frontiers | — | ✓ | ✓ | ✓ | ✓ |
| φ-Insertion | — | ✓ | ✓ | ✓ | ✓ |
| SSA Renaming | — | ✓ | ✓ | ✓ | ✓ |
| Alias Resolution | — | ✓ | ✓ | ✓ | ✓ |
| Const Marking | — | ✓ | ✓ | ✓ | ✓ |
| **Tier 2: Scalar Optimizations** | | | | | |
| SCCP | — | ✓ | ✓ | ✓ | ✓ |
| SSA Copy Propagation | — | ✓ | ✓ | ✓ | ✓ |
| ADCE | — | ✓ | ✓ | ✓ | ✓ |
| φ-Elimination | — | ✓ | ✓ | ✓ | ✓ |
| GVN | — | — | ✓ | ✓ | ✓ |
| Function Inlining | — | — | conserv. | aggressive | tiny leaf |
| **Tier 3: Loop Optimizations** | | | | | |
| SSA LICM | — | — | ✓ | ✓ | ✓ |
| Loop Rotation | — | — | ✓ | ✓ | ✓ |
| IV Simplification | — | — | ✓ | ✓ | ✓ |
| Loop Strength Reduction | — | — | ✓ | ✓ | ✓ |
| Loop Unrolling | — | — | — | ✓ | — |
| **Tier 4: Lowering** | | | | | |
| Load-Store Elimination | — | ✓ | ✓ | ✓ | ✓ |
| Strength Reduction | — | ✓ | ✓ | ✓ | ✓ |
| CMP+Branch Fusion | — | ✓ | ✓ | ✓ | ✓ |
| Register-Aware ISel | — | ✓ | ✓ | ✓ | ✓ |
| 32-bit Encoding | ✓ | ✓ | ✓ | ✓ | ✓ |
| Address Mode Selection | — | — | ✓ | ✓ | ✓ |
| Instruction Scheduling | — | — | — | ✓ | — |
| **Tier 5: Register Allocation** | | | | | |
| SSA Linear Scan | — | ✓ | ✓ | ✓ | ✓ |
| Spill-Reload Cache | — | ✓ | ✓ | ✓ | ✓ |
| Live Range Splitting | — | — | ✓ | ✓ | ✓ |
| φ-Deconstruction | — | — | ✓ | ✓ | ✓ |
| **Tier 6: Emission** | | | | | |
| Peephole | — | ✓ | ✓ | ✓ | ✓ |
| Redundant Instruction Elim. | — | ✓ | ✓ | ✓ | ✓ |
| Branch Relaxation | — | — | ✓ | ✓ | ✓ |
| NOP Alignment | — | — | — | ✓ | — |
| Post-RA Scheduling | — | — | — | ✓ | — |
| **Active Passes** | **1** | **16** | **26** | **32** | **24** |

### Comparison with GCC/LLVM

| AXCC Level | GCC Equivalent | LLVM Equivalent | AXIS Advantage |
| --- | --- | --- | --- |
| `-O0` | `-O0` | `-O0` | Faster compilation (no optimizer overhead) |
| `-O1` | `-O1` (≈50 Passes) | `-O1` (≈70 Passes) | 16 passes achieve the same — AXIS semantics replace analysis |
| `-O2` | `-O2` (≈90 Passes) | `-O2` (≈120 Passes) | 26 passes vs 90–120. Perfect alias info = every pass is more aggressive |
| `-O3` | `-O3` (≈100 Passes) | `-O3` (≈140 Passes) | 32 passes vs 100–140. Optimal RegAlloc + no UB dependency |
| `-Os` | `-Os` | `-Oz` | Fewer trade-offs — AXIS opts grow code less |

> **GCC needs 100+ passes because the language is unsafe.**
> **AXIS needs 32 passes because the language is safe.**
> **Fewer passes, better results.**

---

## Why This Beats GCC and LLVM

### GCC `-O3` Weaknesses AXIS Exploits

1. **Alias analysis is imprecise.** GCC's alias oracle (`alias.c`, ~5000 LOC) still produces may-alias results that block optimizations. AXIS has zero aliasing ambiguity.
2. **Register allocation is heuristic.** GCC uses IRA (Integrated Register Allocator) on non-SSA IR — NP-hard graph coloring with heuristics. AXIS on SSA → optimal chordal coloring.
3. **Inlining is speculative.** GCC's inliner uses size heuristics and profile data. AXIS knows every call target statically — inlining decisions are exact.
4. **Loop analysis fights pointer arithmetic.** GCC must untangle `*p++` patterns into induction variables. AXIS has explicit `for i in range()`.

### LLVM `-O3` Weaknesses AXIS Exploits

1. **LLVM IR is too general.** LLVM must handle any language (C, C++, Rust, Swift) — its passes are conservative. AXIS has ONE language with simple semantics.
2. **Memory SSA is expensive.** LLVM uses MemorySSA to track memory state — heavyweight for alias analysis. AXIS doesn't need MemorySSA because there are no pointers.
3. **Register allocation is greedy, not optimal.** LLVM's regalloc is greedy graph coloring — fast but suboptimal. AXIS on chordal SSA → provably optimal.
4. **Phase ordering problem.** LLVM runs passes in a fixed order, missing cross-pass opportunities. AXIS's 6-tier pipeline is designed for AXIS-specific pass interactions.

### The Fundamental Argument

> GCC and LLVM must **prove** properties that AXIS **guarantees by language design**.
>
> Every cycle GCC spends on alias analysis, AXIS spends on optimization.
> Every conservative assumption LLVM makes, AXIS makes the aggressive (correct) decision.
>
> The language IS the optimizer.
