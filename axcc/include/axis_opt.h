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
