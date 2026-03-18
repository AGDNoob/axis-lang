/*
 * axis_opt.h – Optimization passes for the AXIS compiler IR.
 *
 * Currently provides:
 *   1. Dead Code Elimination (DCE) – removes unreachable instructions
 *      after unconditional jumps/returns until the next label.
 *   2. Constant Folding / Propagation – evaluates compile-time-known
 *      arithmetic and propagates constants through temps.
 *   3. Strength Reduction – replaces expensive ops (MUL, DIV, MOD)
 *      with cheaper equivalents (SHL, SHR, AND) when possible.
 *   4. Linear-scan register allocation – assigns physical x86-64
 *      registers to IR temporaries, spilling the rest to stack.
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

#endif /* AXIS_OPT_H */
