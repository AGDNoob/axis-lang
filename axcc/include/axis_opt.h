/*
 * axis_opt.h – Flat-IR optimization passes for the AXIS compiler.
 *
 * Provides 20 passes operating on the flat three-address-code IR.
 * These complement the SSA-level passes declared in axis_ssa.h;
 * together they form the 37-pass optimization pipeline.
 *
 * Pass                    Min Level   Description
 * ─────────────────────── ───────── ─────────────────────────────────
 *  1. opt_dce              O0        Dead Code Elimination
 *  2. opt_constfold         O0        Constant Folding & Propagation
 *  3. opt_strength_reduce   O1        Strength Reduction (MUL→SHL etc.)
 *  4. opt_peephole          O1        Algebraic Simplifications
 *  5. opt_loadstore_elim    O1        Load-Store Elimination
 *  6. opt_copyprop          O1        Copy Propagation
 *  7. opt_inline            O1        Function Inlining (leaf, <48 instrs)
 *  8. opt_licm              O2        Loop-Invariant Code Motion
 *  9. opt_loop_invert       O2        Loop Inversion (top→bottom test)
 * 10. opt_unroll            O2        Loop Unrolling (2× unroll)
 * 11. opt_regpromote        O2        Register Promotion for loops
 * 12. opt_ivsr              O2        Induction Variable Strength Red.
 * 13. opt_rie               O1        Redundant Instruction Elimination
 * 14. opt_dead_store        O1        Dead Store Elimination
 * 15. opt_regalloc          always    Linear-scan Register Allocation
 * 16. opt_tail_call         O1        Tail Call Optimization
 * 17. opt_jump_thread       O1        Jump Threading
 * 18. opt_if_convert        O1        If-Conversion (CMOV)
 * 19. opt_simplify_cfg      O1        CFG Simplification
 * 20. opt_reassociate       O1        Reassociation
 */
#ifndef AXIS_OPT_H
#define AXIS_OPT_H

#include "axis_ir.h"

/* ═════════════════════════════════════════════════════════════
 * Dead Code Elimination
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_dce – Remove unreachable instructions from every function.
 *
 * Instructions following an unconditional IR_JMP, IR_RET, or
 * IR_RET_VOID are eliminated until the next IR_LABEL is reached
 * (since a label may be a jump target from elsewhere).
 */
void opt_dce(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Constant Folding & Propagation
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_constfold – Fold compile-time constant arithmetic and propagate
 * known values through temporaries.
 *
 * Repeatedly scans each function:
 *  - Binary ops (ADD, SUB, MUL, …) on two immediates → IR_LOAD_IMM
 *  - Unary ops (NEG, LOG_NOT) on an immediate       → IR_LOAD_IMM
 *  - Comparisons on two immediates                   → IR_LOAD_IMM 0/1
 *  - Conditional jumps (JZ/JNZ) on immediates        → NOP or IR_JMP
 *  - Temps assigned a single constant are propagated to their uses
 */
void opt_constfold(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Strength Reduction
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_strength_reduce – Replace expensive operations with cheaper ones.
 *
 *   • MUL by power-of-2  → SHL
 *   • unsigned DIV by power-of-2 → SHR (logical)
 *   • unsigned MOD by power-of-2 → AND with (n-1)
 */
void opt_strength_reduce(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Load-Store Elimination
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_loadstore_elim – Eliminate redundant LOAD_VAR instructions.
 *
 * Tracks which stack slots are cached in temps after STORE_VAR.
 * When a LOAD_VAR loads a slot that is already cached, it is
 * replaced with IR_MOV from the cached temp.
 */
void opt_loadstore_elim(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Peephole Optimizations
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_peephole – IR-level algebraic simplifications.
 *
 * Eliminates identity operations (x+0, x*1, x-x, self-MOV, etc.)
 * and replaces them with cheaper MOV or LOAD_IMM instructions.
 */
void opt_peephole(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Copy Propagation
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_copyprop – Eliminate redundant temp-to-temp copies.
 *
 * For each temp with a single definition that is a MOV from another
 * single-definition temp, replace all uses with the source temp.
 */
void opt_copyprop(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Function Inlining
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_inline – Inline small leaf functions at their call sites.
 *
 * Criteria: callee < 32 instrs, no side-effects (CALL/WRITE/READ/
 * SYSCALL), not self-recursive, no update/field params.
 */
void opt_inline(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Loop-Invariant Code Motion
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_licm – Move loop-invariant pure computations before the loop.
 *
 * Detects natural loops via back-edges and hoists instructions whose
 * source operands are all defined outside the loop or by other
 * already-hoisted instructions.
 */
void opt_licm(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Loop Inversion
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_loop_invert – Convert top-test loops to bottom-test form.
 *
 * Transforms: LABEL top → [cond] → JZ/JNZ exit → [body] → JMP top
 * Into:       [guard_cond] → JZ/JNZ exit → LABEL top → [body] → [cond] → inv_branch top
 *
 * Eliminates the unconditional JMP from the hot loop path.
 */
void opt_loop_invert(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Loop Unrolling
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_unroll – Duplicate small loop bodies (2× unrolling).
 *
 * For loops with unconditional back-edges and < 24 body instructions,
 * the body is duplicated once to halve back-edge overhead.
 */
void opt_unroll(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Redundant Instruction Elimination
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_rie – Remove instructions whose dest temp is overwritten
 * before it is ever read.
 *
 * For each IR instruction that writes to a temp, scan forward to find
 * either a use (read) of that temp, a label/branch (basic-block boundary),
 * or another write to the same temp.  If another write is found first,
 * the original instruction is dead and can be eliminated.
 */
void opt_rie(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Dead Store Elimination
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_dead_store – Remove STORE_VAR instructions to stack slots that
 * are never read by any LOAD_VAR, and eliminate redundant stores
 * within the same basic block.
 */
void opt_dead_store(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Tail Call Optimization
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_tail_call – Convert self-recursive tail calls into jumps.
 *
 * Detects pattern: IR_ARG... IR_CALL self IR_RET (returning call
 * result) and replaces with parameter stores + JMP to entry label.
 */
void opt_tail_call(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Jump Threading
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_jump_thread – Thread jumps through intermediate blocks.
 *
 * When a JMP/JZ/JNZ targets a label followed only by another JMP,
 * redirect to the final target. Follows chains up to 8 hops.
 */
void opt_jump_thread(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * If-Conversion (CMOV)
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_if_convert – Replace simple if/else diamonds with CMOV.
 *
 * Pattern: CMP → JZ/JNZ → MOV/LOAD_IMM → JMP → LABEL → MOV/LOAD_IMM → LABEL
 * Converted to: CMP → MOV default → CMOV alternate, cond
 */
void opt_if_convert(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * CFG Simplification
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_simplify_cfg – Simplify the control-flow graph.
 *
 * Removes redundant JMP-to-next-label, merges consecutive labels,
 * and eliminates empty basic blocks.
 */
void opt_simplify_cfg(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Reassociation
 * ═════════════════════════════════════════════════════════════ */

/*
 * opt_reassociate – Reassociate chains of associative/commutative ops.
 *
 * Combines: op t1, t0, imm1; op t2, t1, imm2 (t1 single-use)
 * Into:     op t2, t0, (imm1 ⊕ imm2)
 * For ADD, MUL, AND, OR, XOR.
 */
void opt_reassociate(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * opt_regpromote – Register promotion for innermost loops.
 * Replaces in-loop load_var with MOV from a home register,
 * keeping stores for correctness.
 */
void opt_regpromote(IRProgram *ir);

/*
 * opt_ivsr – Induction Variable Strength Reduction.
 * Replaces MUL/ADD on loop induction variables with accumulated
 * additions, eliminating expensive multiply instructions.
 */
void opt_ivsr(IRProgram *ir);

/*
 * opt_ive – Induction Variable Elimination.
 * Eliminates IVs used only for the loop exit test by rewriting the
 * test to use another IV with the same step and an adjusted bound.
 */
void opt_ive(IRProgram *ir);

/* ═════════════════════════════════════════════════════════════
 * Register Allocation – Linear Scan
 * ═════════════════════════════════════════════════════════════ */

/* Physical register or spill sentinel */
#define REG_SPILLED (-1)

/*
 * Per-temp allocation result: either a physical register id
 * (RAX=0 .. R15=15) or REG_SPILLED (-1) meaning it lives on the stack.
 */
typedef struct {
    int *temp_reg;       /* array[temp_count]: physical reg or REG_SPILLED */
    int  temp_count;
    int  spill_count;    /* number of temps that were spilled */
    bool callee_used[16]; /* which callee-saved regs are actually used */
} RegAlloc;

/*
 * opt_regalloc – Perform linear-scan register allocation for a function.
 *
 * Allocates from the callee-saved pool (RBX, RSI, RDI, R12–R15) first,
 * then caller-saved scratch (R10, R11).  Temps that don't fit are spilled.
 *
 * The caller must free ra->temp_reg when done (or use the arena).
 */
void opt_regalloc(RegAlloc *ra, const IRFunc *fn, Arena *arena);

/* ── New GCC -O1 equivalent passes ────────────────────────── */
void opt_fwdprop(IRProgram *ir);
void opt_minmaxabs(IRProgram *ir);
void opt_sink(IRProgram *ir);
void opt_vrp(IRProgram *ir);
void opt_tail_merge(IRProgram *ir);
void opt_sra(IRProgram *ir);
void opt_switch_lower(IRProgram *ir);

/* Shared helpers (used across opt_*.c files) */
bool is_pure_op(IROpcode op);
bool has_complex_params(const IRFunc *fn);
int  func_max_label(const IRFunc *fn);

#endif /* AXIS_OPT_H */
