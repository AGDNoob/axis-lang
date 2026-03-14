/*
 * opt.c – Optimization passes for the AXIS compiler.
 *
 * 1. Dead Code Elimination – removes unreachable instructions
 * 2. Constant Folding / Propagation – evaluates compile-time constants
 * 3. Linear-scan register allocation – maps temps to physical regs
 */

#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

/* ═════════════════════════════════════════════════════════════
 * Dead Code Elimination
 *
 * Walk each function's instruction list.  When we encounter an
 * unconditional terminator (IR_JMP, IR_RET, IR_RET_VOID), mark
 * all subsequent instructions as dead until we see an IR_LABEL
 * (which could be a branch target).  Then compact the array.
 * ═════════════════════════════════════════════════════════════ */

static void dce_func(IRFunc *fn)
{
    if (fn->instr_count == 0) return;

    /* Mark dead instructions by setting op = IR_NOP */
    bool dead = false;
    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        if (dead) {
            if (ins->op == IR_LABEL) {
                dead = false;       /* label is reachable from a branch */
            } else {
                ins->op = IR_NOP;   /* unreachable — kill it */
            }
        }

        /* Check if this instruction is an unconditional terminator */
        if (ins->op == IR_JMP || ins->op == IR_RET || ins->op == IR_RET_VOID) {
            dead = true;
        }
    }

    /* Compact: remove NOP instructions */
    int w = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_NOP) {
            if (w != i) fn->instrs[w] = fn->instrs[i];
            w++;
        }
    }
    fn->instr_count = w;
}

void opt_dce(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        dce_func(&ir->funcs[i]);

    if (ir->top_level.instr_count > 0)
        dce_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Constant Folding & Propagation
 *
 * Two interlocking passes run in a loop until convergence:
 *
 *  A) Constant Propagation – for each temp that has exactly one
 *     definition and that definition is IR_LOAD_IMM or IR_MOV of
 *     an immediate, replace all uses of that temp with the immediate.
 *
 *  B) Constant Folding – for instructions whose source operands are
 *     both immediates, evaluate at compile time and replace the
 *     instruction with IR_LOAD_IMM (or IR_JMP / IR_NOP for branches).
 *
 * After convergence, dead IR_LOAD_IMM instructions (whose dest temp
 * is no longer used) are turned into IR_NOP and compacted out.
 * ═════════════════════════════════════════════════════════════ */

/* Try to fold a binary or unary IR instruction whose operands are
 * both immediate.  Returns true if the instruction was replaced. */
static bool fold_instr(IRInstr *ins)
{
    /* ── Unary: NEG, LOG_NOT ─────────────────────────────── */
    if (ins->op == IR_NEG && ins->src1.kind == OPER_IMM) {
        ins->op = IR_LOAD_IMM;
        ins->src1.imm = -ins->src1.imm;
        ins->src2 = (IROper){0};
        return true;
    }
    if (ins->op == IR_LOG_NOT && ins->src1.kind == OPER_IMM) {
        ins->op = IR_LOAD_IMM;
        ins->src1.imm = ins->src1.imm ? 0 : 1;
        ins->src2 = (IROper){0};
        return true;
    }

    /* ── Binary: both sources must be immediate ──────────── */
    if (ins->src1.kind != OPER_IMM || ins->src2.kind != OPER_IMM)
        return false;

    int64_t a = ins->src1.imm;
    int64_t b = ins->src2.imm;
    int64_t r;

    switch (ins->op) {
    case IR_ADD:    r = a + b; break;
    case IR_SUB:    r = a - b; break;
    case IR_MUL:    r = a * b; break;
    case IR_DIV:    if (b == 0) return false; r = a / b; break;
    case IR_MOD:    if (b == 0) return false; r = a % b; break;
    case IR_BIT_AND: r = a & b; break;
    case IR_BIT_OR:  r = a | b; break;
    case IR_BIT_XOR: r = a ^ b; break;
    case IR_SHL:    r = a << (b & 63); break;
    case IR_SHR:    r = a >> (b & 63); break;
    case IR_CMP_EQ: r = (a == b) ? 1 : 0; break;
    case IR_CMP_NE: r = (a != b) ? 1 : 0; break;
    case IR_CMP_LT: r = (a <  b) ? 1 : 0; break;
    case IR_CMP_LE: r = (a <= b) ? 1 : 0; break;
    case IR_CMP_GT: r = (a >  b) ? 1 : 0; break;
    case IR_CMP_GE: r = (a >= b) ? 1 : 0; break;
    default:
        return false;
    }

    ins->op = IR_LOAD_IMM;
    ins->src1.kind = OPER_IMM;
    ins->src1.imm  = r;
    ins->src2 = (IROper){0};
    return true;
}

/* Try to fold a conditional jump on an immediate condition. */
static bool fold_branch(IRInstr *ins)
{
    if (ins->op == IR_JZ && ins->src1.kind == OPER_IMM) {
        if (ins->src1.imm == 0) {
            /* Condition is zero → always jump */
            ins->op  = IR_JMP;
            ins->src1 = (IROper){0};
        } else {
            /* Condition is nonzero → never jump */
            ins->op = IR_NOP;
        }
        return true;
    }
    if (ins->op == IR_JNZ && ins->src1.kind == OPER_IMM) {
        if (ins->src1.imm != 0) {
            ins->op  = IR_JMP;
            ins->src1 = (IROper){0};
        } else {
            ins->op = IR_NOP;
        }
        return true;
    }
    return false;
}

/* Replace OPER_TEMP references with a known constant value. */
static void replace_oper(IROper *op, int temp_id, int64_t val)
{
    if (op->kind == OPER_TEMP && op->temp_id == temp_id) {
        op->kind = OPER_IMM;
        op->imm  = val;
    }
}

static void constfold_func(IRFunc *fn)
{
    if (fn->instr_count == 0) return;

    int tc = fn->temp_count;
    if (tc <= 0) tc = 0;

    /* Iterate until no more changes */
    bool changed = true;
    while (changed) {
        changed = false;

        /* ── Pass A: Constant Propagation ────────────────── *
         *                                                     *
         * A temp is propagatable if it is defined exactly once *
         * by IR_LOAD_IMM.  We count defs and track the value. */
        int *def_count = NULL;
        int64_t *def_val = NULL;
        if (tc > 0) {
            def_count = (int *)calloc((size_t)tc, sizeof(int));
            def_val   = (int64_t *)calloc((size_t)tc, sizeof(int64_t));

            for (int i = 0; i < fn->instr_count; i++) {
                IRInstr *ins = &fn->instrs[i];
                if (ins->dest.kind == OPER_TEMP) {
                    int id = ins->dest.temp_id;
                    if (id >= 0 && id < tc) {
                        def_count[id]++;
                        if (ins->op == IR_LOAD_IMM)
                            def_val[id] = ins->src1.imm;
                        else if (ins->op == IR_MOV && ins->src1.kind == OPER_IMM)
                            def_val[id] = ins->src1.imm;
                        else
                            def_val[id] = 0; /* not a constant def */
                    }
                }
            }

            /* Propagation: replace uses of single-def constant temps */
            for (int i = 0; i < fn->instr_count; i++) {
                IRInstr *ins = &fn->instrs[i];
                for (int t = 0; t < tc; t++) {
                    if (def_count[t] != 1) continue;
                    /* Check the single def is IR_LOAD_IMM or IR_MOV imm */
                    bool is_const = false;
                    for (int k = 0; k < fn->instr_count; k++) {
                        IRInstr *d = &fn->instrs[k];
                        if (d->dest.kind == OPER_TEMP && d->dest.temp_id == t) {
                            is_const = (d->op == IR_LOAD_IMM) ||
                                       (d->op == IR_MOV && d->src1.kind == OPER_IMM);
                            break;
                        }
                    }
                    if (!is_const) continue;

                    /* Don't replace the definition itself */
                    if (ins->dest.kind == OPER_TEMP && ins->dest.temp_id == t)
                        continue;

                    int64_t v = def_val[t];
                    IROper old_src1 = ins->src1;
                    IROper old_src2 = ins->src2;
                    replace_oper(&ins->src1, t, v);
                    replace_oper(&ins->src2, t, v);
                    if (ins->src1.kind != old_src1.kind || ins->src1.imm != old_src1.imm ||
                        ins->src2.kind != old_src2.kind || ins->src2.imm != old_src2.imm)
                        changed = true;
                }
            }

            free(def_count);
            free(def_val);
        }

        /* ── Pass B: Constant Folding ────────────────────── */
        for (int i = 0; i < fn->instr_count; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (fold_instr(ins))  changed = true;
            if (fold_branch(ins)) changed = true;
        }
    }

    /* ── Dead-instruction cleanup ────────────────────────── *
     * After propagation, some IR_LOAD_IMM / IR_MOV defs may  *
     * have no remaining uses — turn them into NOP.            */
    if (tc > 0) {
        bool *used = (bool *)calloc((size_t)tc, sizeof(bool));
        for (int i = 0; i < fn->instr_count; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->src1.kind == OPER_TEMP && ins->src1.temp_id < tc)
                used[ins->src1.temp_id] = true;
            if (ins->src2.kind == OPER_TEMP && ins->src2.temp_id < tc)
                used[ins->src2.temp_id] = true;
        }
        for (int i = 0; i < fn->instr_count; i++) {
            IRInstr *ins = &fn->instrs[i];
            if ((ins->op == IR_LOAD_IMM || ins->op == IR_MOV) &&
                ins->dest.kind == OPER_TEMP &&
                ins->dest.temp_id < tc &&
                !used[ins->dest.temp_id]) {
                ins->op = IR_NOP;
            }
        }
        free(used);
    }

    /* Compact out NOP instructions */
    int w = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_NOP) {
            if (w != i) fn->instrs[w] = fn->instrs[i];
            w++;
        }
    }
    fn->instr_count = w;
}

void opt_constfold(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        constfold_func(&ir->funcs[i]);

    if (ir->top_level.instr_count > 0)
        constfold_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Strength Reduction
 *
 * Replaces expensive operations with cheaper equivalents:
 *   • MUL by power-of-2  → SHL
 *   • unsigned DIV by power-of-2 → SHR (logical)
 *   • unsigned MOD by power-of-2 → AND with (n-1)
 * ═════════════════════════════════════════════════════════════ */

/* Return log2(v) if v is a power of 2, otherwise -1. */
static int exact_log2(int64_t v)
{
    if (v <= 0 || (v & (v - 1)) != 0) return -1;
    int n = 0;
    while (v > 1) { v >>= 1; n++; }
    return n;
}

static void strength_reduce_func(IRFunc *fn)
{
    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        /* MUL by power-of-2 → SHL (works for both signed and unsigned) */
        if (ins->op == IR_MUL && ins->src2.kind == OPER_IMM) {
            int shift = exact_log2(ins->src2.imm);
            if (shift > 0) {
                ins->op = IR_SHL;
                ins->src2.imm = shift;
            }
        }
        /* MUL with src1 as immediate power-of-2 (commutative) */
        if (ins->op == IR_MUL && ins->src1.kind == OPER_IMM) {
            int shift = exact_log2(ins->src1.imm);
            if (shift > 0) {
                ins->op = IR_SHL;
                ins->src1 = ins->src2;
                ins->src2.kind = OPER_IMM;
                ins->src2.imm = shift;
            }
        }

        /* Unsigned DIV by power-of-2 → SHR (logical) */
        if (ins->op == IR_DIV && ins->extra && ins->src2.kind == OPER_IMM) {
            int shift = exact_log2(ins->src2.imm);
            if (shift > 0) {
                ins->op = IR_SHR;
                ins->src2.imm = shift;
                /* extra already set = unsigned → SHR will emit logical shift */
            }
        }

        /* Unsigned MOD by power-of-2 → AND with (n-1) */
        if (ins->op == IR_MOD && ins->extra && ins->src2.kind == OPER_IMM) {
            int shift = exact_log2(ins->src2.imm);
            if (shift > 0) {
                ins->op = IR_BIT_AND;
                ins->src2.imm = ins->src2.imm - 1;
                ins->extra = 0;
            }
        }
    }
}

void opt_strength_reduce(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        strength_reduce_func(&ir->funcs[i]);

    if (ir->top_level.instr_count > 0)
        strength_reduce_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Load-Store Elimination
 *
 * Tracks which stack slots (variables) are currently "cached" in a
 * temp and eliminates redundant IR_LOAD_VAR instructions.
 *
 * When IR_STORE_VAR writes temp T to slot S, we record cache[S] = T.
 * When IR_LOAD_VAR loads slot S into dest D, and cache[S] is valid,
 * we replace the LOAD_VAR with IR_MOV dest = cached_temp.
 *
 * Cache is invalidated on:
 *   - IR_LABEL (branch target — incoming state unknown)
 *   - IR_CALL  (callee may modify memory)
 *   - IR_STORE_VAR to the same slot (update cache)
 *   - IR_STORE_IND, IR_INDEX_STORE, IR_FIELD_STORE, IR_MEMCPY
 *     (may alias any slot — flush entire cache)
 * ═════════════════════════════════════════════════════════════ */

#define LSE_MAX_SLOTS 256

typedef struct {
    int   offset;     /* stack_off of the cached slot */
    int   temp_id;    /* the temp that holds the value */
    bool  valid;
} LSEEntry;

static void loadstore_elim_func(IRFunc *fn)
{
    LSEEntry cache[LSE_MAX_SLOTS];
    int cache_count = 0;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        /* Flush entire cache on control-flow merge or memory-aliasing ops */
        if (ins->op == IR_LABEL || ins->op == IR_CALL ||
            ins->op == IR_STORE_IND || ins->op == IR_INDEX_STORE ||
            ins->op == IR_FIELD_STORE || ins->op == IR_MEMCPY) {
            cache_count = 0;
            continue;
        }

        /* Also flush on jumps (the target label will flush too,
         * but flush here so fall-through after a conditional branch
         * doesn't carry stale state from before the branch target) */
        if (ins->op == IR_JMP || ins->op == IR_JZ || ins->op == IR_JNZ) {
            cache_count = 0;
            continue;
        }

        /* STORE_VAR: [slot] = src1  → update cache */
        if (ins->op == IR_STORE_VAR && ins->src1.kind == OPER_TEMP) {
            int off = ins->dest.stack_off;
            /* Update existing entry or add new one */
            bool found = false;
            for (int c = 0; c < cache_count; c++) {
                if (cache[c].valid && cache[c].offset == off) {
                    cache[c].temp_id = ins->src1.temp_id;
                    found = true;
                    break;
                }
            }
            if (!found && cache_count < LSE_MAX_SLOTS) {
                cache[cache_count].offset  = off;
                cache[cache_count].temp_id = ins->src1.temp_id;
                cache[cache_count].valid   = true;
                cache_count++;
            }
            continue;
        }

        /* STORE_VAR with non-temp src: invalidate cached slot */
        if (ins->op == IR_STORE_VAR) {
            int off = ins->dest.stack_off;
            for (int c = 0; c < cache_count; c++) {
                if (cache[c].valid && cache[c].offset == off) {
                    cache[c].valid = false;
                    break;
                }
            }
            continue;
        }

        /* LOAD_VAR: dest = [slot]  → check cache for hit */
        if (ins->op == IR_LOAD_VAR && ins->dest.kind == OPER_TEMP) {
            int off = ins->src1.stack_off;
            for (int c = 0; c < cache_count; c++) {
                if (cache[c].valid && cache[c].offset == off) {
                    /* Cache hit — replace with MOV dest = cached_temp */
                    ins->op = IR_MOV;
                    ins->src1.kind    = OPER_TEMP;
                    ins->src1.temp_id = cache[c].temp_id;
                    ins->src1.size    = 8;
                    /* Also cache the new dest temp for this slot */
                    cache[c].temp_id = ins->dest.temp_id;
                    goto next_instr;
                }
            }
            /* Cache miss — add (dest temp is now the cached copy) */
            if (cache_count < LSE_MAX_SLOTS) {
                cache[cache_count].offset  = off;
                cache[cache_count].temp_id = ins->dest.temp_id;
                cache[cache_count].valid   = true;
                cache_count++;
            }
        }
next_instr:;
    }
}

void opt_loadstore_elim(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        loadstore_elim_func(&ir->funcs[i]);

    if (ir->top_level.instr_count > 0)
        loadstore_elim_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Register Allocation – Linear Scan
 *
 * 1. Compute liveness intervals [first_def, last_use] for each temp.
 * 2. Sort intervals by start point.
 * 3. Walk intervals, assigning from a pool of physical registers.
 *    When the pool is empty, spill the interval that ends latest.
 *
 * Register pool (for Windows x64):
 *   Callee-saved (preferred – survive across calls):
 *     RBX(3), RSI(6), RDI(7), R12(12), R13(13), R14(14), R15(15)
 *   Caller-saved scratch (only useful between calls):
 *     R10(10), R11(11)
 *
 * RAX, RCX, RDX, R8, R9 are reserved for:
 *   - return values (RAX)
 *   - calling convention arguments (RCX, RDX, R8, R9)
 *   - scratch within individual instruction patterns
 *
 * RSP, RBP are frame pointers – never allocatable.
 * ═════════════════════════════════════════════════════════════ */

/* Allocatable register pool ordered by preference */
static const int reg_pool[] = {
    3,   /* RBX  – callee-saved */
    6,   /* RSI  – callee-saved */
    7,   /* RDI  – callee-saved */
    12,  /* R12  – callee-saved */
    13,  /* R13  – callee-saved */
    14,  /* R14  – callee-saved */
    15,  /* R15  – callee-saved */
    10,  /* R10  – caller-saved */
    11,  /* R11  – caller-saved */
};

#define REG_POOL_SIZE ((int)(sizeof(reg_pool) / sizeof(reg_pool[0])))

/* Callee-saved register set (must be pushed/popped in prologue/epilogue) */
static bool is_callee_saved(int phys)
{
    return phys == 3 || phys == 6 || phys == 7 ||
           (phys >= 12 && phys <= 15);
}

/* Caller-saved registers that get clobbered by CALLs */
static bool is_caller_saved_alloc(int phys)
{
    return phys == 10 || phys == 11;
}

typedef struct {
    int temp_id;
    int start;      /* first instruction index */
    int end;        /* last instruction index */
} LiveInterval;

/* Scan an operand and update interval bounds */
static void scan_oper(const IROper *op, int pos, int *starts, int *ends, int tc)
{
    if (op->kind == OPER_TEMP && op->temp_id >= 0 && op->temp_id < tc) {
        int id = op->temp_id;
        if (starts[id] < 0 || pos < starts[id]) starts[id] = pos;
        if (pos > ends[id]) ends[id] = pos;
    }
}

void opt_regalloc(RegAlloc *ra, const IRFunc *fn, Arena *arena)
{
    int tc = fn->temp_count;
    memset(ra, 0, sizeof(*ra));
    ra->temp_count = tc;

    if (tc <= 0) {
        ra->temp_reg = NULL;
        return;
    }

    /* Allocate result array */
    ra->temp_reg = (int *)arena_alloc(arena, (size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++)
        ra->temp_reg[i] = REG_SPILLED;

    /* ── Step 1: Compute liveness intervals ─────────────── */
    int *starts = (int *)malloc((size_t)tc * sizeof(int));
    int *ends   = (int *)malloc((size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++) {
        starts[i] = -1;
        ends[i]   = -1;
    }

    /* Track which temps are live across a CALL instruction */
    bool *crosses_call = (bool *)calloc((size_t)tc, sizeof(bool));

    for (int i = 0; i < fn->instr_count; i++) {
        const IRInstr *ins = &fn->instrs[i];
        scan_oper(&ins->dest, i, starts, ends, tc);
        scan_oper(&ins->src1, i, starts, ends, tc);
        scan_oper(&ins->src2, i, starts, ends, tc);
    }

    /* ── Step 1b: Extend intervals for loop back-edges ──── *
     *                                                        *
     * A backward jump (IR_JMP/IR_JZ/IR_JNZ targeting a label *
     * at a lower instruction index) creates a loop.  Any temp *
     * whose interval intersects the loop range [label, jump]  *
     * must have its interval extended to cover the entire loop *
     * so the register stays allocated across iterations.       *
     * Repeat until no more extensions (handles nested loops).  */

    /* Build label_id → instruction index map */
    int max_label = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL) {
            int lid = fn->instrs[i].dest.label_id;
            if (lid > max_label) max_label = lid;
        }
    }
    int *label_pos = (int *)malloc((size_t)(max_label + 1) * sizeof(int));
    for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            label_pos[fn->instrs[i].dest.label_id] = i;
    }

    /* Iteratively extend intervals across loop back-edges */
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < fn->instr_count; i++) {
            const IRInstr *ins = &fn->instrs[i];
            int target = -1;
            if (ins->op == IR_JMP || ins->op == IR_JZ || ins->op == IR_JNZ) {
                int lid = ins->dest.label_id;
                if (lid >= 0 && lid <= max_label)
                    target = label_pos[lid];
            }
            if (target < 0 || target >= i) continue; /* not a back-edge */

            /* Loop range: [target, i] */
            int loop_start = target;
            int loop_end   = i;

            for (int t = 0; t < tc; t++) {
                if (starts[t] < 0) continue; /* unused temp */
                /* Does this temp's interval overlap [loop_start, loop_end]? */
                if (starts[t] <= loop_end && ends[t] >= loop_start) {
                    /* Extend to cover the full loop range */
                    if (starts[t] > loop_start) {
                        starts[t] = loop_start;
                        changed = true;
                    }
                    if (ends[t] < loop_end) {
                        ends[t] = loop_end;
                        changed = true;
                    }
                }
            }
        }
    }

    free(label_pos);

    /* Determine which temps cross a CALL */
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_CALL || fn->instrs[i].op == IR_WRITE ||
            fn->instrs[i].op == IR_READ || fn->instrs[i].op == IR_MEMCPY ||
            fn->instrs[i].op == IR_SYSCALL) {
            /* Any temp whose interval spans this call crosses it */
            for (int t = 0; t < tc; t++) {
                if (starts[t] >= 0 && starts[t] < i && ends[t] > i)
                    crosses_call[t] = true;
            }
        }
    }

    /* Build sorted interval list (only temps that are actually used) */
    LiveInterval *intervals = (LiveInterval *)malloc((size_t)tc * sizeof(LiveInterval));
    int nintervals = 0;
    for (int i = 0; i < tc; i++) {
        if (starts[i] >= 0) {
            intervals[nintervals].temp_id = i;
            intervals[nintervals].start   = starts[i];
            intervals[nintervals].end     = ends[i];
            nintervals++;
        }
    }

    /* Sort by start point (insertion sort – typically small) */
    for (int i = 1; i < nintervals; i++) {
        LiveInterval tmp = intervals[i];
        int j = i - 1;
        while (j >= 0 && intervals[j].start > tmp.start) {
            intervals[j + 1] = intervals[j];
            j--;
        }
        intervals[j + 1] = tmp;
    }

    /* ── Step 2: Linear scan allocation ─────────────────── */

    /* Active list: intervals currently occupying a register */
    typedef struct { int temp_id; int end; int phys; } Active;
    Active *active = (Active *)malloc((size_t)tc * sizeof(Active));
    int nactive = 0;

    /* Free register pool – use a simple boolean array */
    bool reg_free[16];
    memset(reg_free, 0, sizeof(reg_free));
    for (int i = 0; i < REG_POOL_SIZE; i++)
        reg_free[reg_pool[i]] = true;

    for (int i = 0; i < nintervals; i++) {
        LiveInterval *cur = &intervals[i];

        /* Expire old intervals whose end < cur->start */
        int w = 0;
        for (int j = 0; j < nactive; j++) {
            if (active[j].end < cur->start) {
                /* Return register to pool */
                reg_free[active[j].phys] = true;
            } else {
                active[w++] = active[j];
            }
        }
        nactive = w;

        /* Try to allocate a register */
        int assigned = -1;

        if (crosses_call[cur->temp_id]) {
            /* Must use callee-saved register (survives calls) */
            for (int r = 0; r < REG_POOL_SIZE; r++) {
                int phys = reg_pool[r];
                if (reg_free[phys] && is_callee_saved(phys)) {
                    assigned = phys;
                    break;
                }
            }
        } else {
            /* Prefer caller-saved first (avoids push/pop overhead) */
            for (int r = 0; r < REG_POOL_SIZE; r++) {
                int phys = reg_pool[r];
                if (reg_free[phys] && is_caller_saved_alloc(phys)) {
                    assigned = phys;
                    break;
                }
            }
            if (assigned < 0) {
                /* Fall back to callee-saved */
                for (int r = 0; r < REG_POOL_SIZE; r++) {
                    int phys = reg_pool[r];
                    if (reg_free[phys]) {
                        assigned = phys;
                        break;
                    }
                }
            }
        }

        if (assigned >= 0) {
            /* Assign register */
            reg_free[assigned] = false;
            ra->temp_reg[cur->temp_id] = assigned;
            if (is_callee_saved(assigned))
                ra->callee_used[assigned] = true;

            /* Add to active list (sorted by end point for fast expire) */
            int pos = nactive;
            for (int j = nactive - 1; j >= 0; j--) {
                if (active[j].end > cur->end) {
                    active[j + 1] = active[j];
                    pos = j;
                } else break;
            }
            active[pos].temp_id = cur->temp_id;
            active[pos].end     = cur->end;
            active[pos].phys    = assigned;
            nactive++;
        } else {
            /* Spill: no register available.
             * Try to spill the active interval that ends latest
             * (if it ends later than current, it's better to spill that one). */
            int spill_idx = -1;
            int latest_end = cur->end;
            for (int j = 0; j < nactive; j++) {
                if (active[j].end > latest_end) {
                    /* Only spill if the active reg is compatible */
                    if (crosses_call[cur->temp_id] &&
                        !is_callee_saved(active[j].phys))
                        continue;  /* can't steal a caller-saved for a call-crossing temp */
                    spill_idx = j;
                    latest_end = active[j].end;
                }
            }

            if (spill_idx >= 0) {
                /* Steal register from the longer-lived interval */
                int stolen_phys = active[spill_idx].phys;
                int stolen_temp = active[spill_idx].temp_id;

                ra->temp_reg[stolen_temp] = REG_SPILLED;
                ra->temp_reg[cur->temp_id] = stolen_phys;
                ra->spill_count++;

                /* Replace in active list */
                active[spill_idx].temp_id = cur->temp_id;
                active[spill_idx].end     = cur->end;
                /* phys stays the same */
            } else {
                /* No register to steal – spill current */
                ra->temp_reg[cur->temp_id] = REG_SPILLED;
                ra->spill_count++;
            }
        }
    }

    free(starts);
    free(ends);
    free(crosses_call);
    free(intervals);
    free(active);
}
