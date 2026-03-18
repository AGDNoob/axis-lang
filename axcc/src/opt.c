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
 * Peephole Optimizations
 *
 * IR-level algebraic simplifications and identity elimination:
 *   • x + 0, x - 0, x * 1, x / 1       → MOV (identity)
 *   • x * 0, x & 0                       → LOAD_IMM 0
 *   • x | 0, x ^ 0, x << 0, x >> 0      → MOV (identity)
 *   • x & -1                             → MOV (identity)
 *   • x | -1                             → LOAD_IMM -1
 *   • x - x, x ^ x                       → LOAD_IMM 0
 *   • x + x                              → SHL x, 1
 *   • MOV x, x (self-move)               → NOP
 * ═════════════════════════════════════════════════════════════ */

/* Convert instruction to MOV dest = src1 (preserving dest) */
static void make_mov(IRInstr *ins)
{
    ins->op = IR_MOV;
    ins->src2 = (IROper){0};
}

/* Convert instruction to LOAD_IMM dest = val */
static void make_load_imm(IRInstr *ins, int64_t val)
{
    ins->op = IR_LOAD_IMM;
    ins->src1.kind = OPER_IMM;
    ins->src1.imm  = val;
    ins->src2 = (IROper){0};
}

static bool peephole_identity(IRInstr *ins)
{
    /* ── src2 is immediate 0 ──────────────────────────── */
    if (ins->src2.kind == OPER_IMM && ins->src2.imm == 0) {
        switch (ins->op) {
        case IR_ADD: case IR_SUB: case IR_BIT_OR: case IR_BIT_XOR:
        case IR_SHL: case IR_SHR:
            make_mov(ins); return true;
        case IR_MUL: case IR_BIT_AND:
            make_load_imm(ins, 0); return true;
        default: break;
        }
    }

    /* ── src1 is immediate 0 (commutative ops) ───────── */
    if (ins->src1.kind == OPER_IMM && ins->src1.imm == 0) {
        switch (ins->op) {
        case IR_ADD: case IR_BIT_OR: case IR_BIT_XOR:
            ins->src1 = ins->src2;
            make_mov(ins); return true;
        case IR_MUL: case IR_BIT_AND:
            make_load_imm(ins, 0); return true;
        default: break;
        }
    }

    /* ── src2 is immediate 1 ──────────────────────────── */
    if (ins->src2.kind == OPER_IMM && ins->src2.imm == 1) {
        switch (ins->op) {
        case IR_MUL: case IR_DIV:
            make_mov(ins); return true;
        case IR_MOD:
            make_load_imm(ins, 0); return true;
        default: break;
        }
    }

    /* ── src1 is immediate 1 (commutative MUL) ────────── */
    if (ins->src1.kind == OPER_IMM && ins->src1.imm == 1) {
        if (ins->op == IR_MUL) {
            ins->src1 = ins->src2;
            make_mov(ins); return true;
        }
    }

    /* ── src2 is immediate -1 (all bits set) ─────────── */
    if (ins->src2.kind == OPER_IMM && ins->src2.imm == -1) {
        switch (ins->op) {
        case IR_BIT_AND:
            make_mov(ins); return true;
        case IR_BIT_OR:
            make_load_imm(ins, -1); return true;
        default: break;
        }
    }

    /* ── Same temp on both sources ────────────────────── */
    if (ins->src1.kind == OPER_TEMP && ins->src2.kind == OPER_TEMP &&
        ins->src1.temp_id == ins->src2.temp_id) {
        switch (ins->op) {
        case IR_SUB: case IR_BIT_XOR:
            make_load_imm(ins, 0); return true;
        case IR_ADD:
            ins->op = IR_SHL;
            ins->src2.kind = OPER_IMM;
            ins->src2.imm  = 1;
            return true;
        case IR_BIT_AND: case IR_BIT_OR:
            make_mov(ins); return true;
        default: break;
        }
    }

    /* ── Self-MOV → NOP ───────────────────────────────── */
    if (ins->op == IR_MOV &&
        ins->dest.kind == OPER_TEMP && ins->src1.kind == OPER_TEMP &&
        ins->dest.temp_id == ins->src1.temp_id) {
        ins->op = IR_NOP;
        return true;
    }

    return false;
}

static void peephole_func(IRFunc *fn)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < fn->instr_count; i++) {
            if (peephole_identity(&fn->instrs[i]))
                changed = true;
        }
        /* Compact out NOPs */
        int w = 0;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op != IR_NOP) {
                if (w != i) fn->instrs[w] = fn->instrs[i];
                w++;
            }
        }
        fn->instr_count = w;
    }
}

void opt_peephole(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        peephole_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        peephole_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Copy Propagation
 *
 * When a temp has exactly one definition, and that definition is
 * a MOV from another single-definition temp, replace all uses of
 * the destination with the source and eliminate the MOV.
 * ═════════════════════════════════════════════════════════════ */

static void copyprop_func(IRFunc *fn)
{
    int tc = fn->temp_count;
    if (tc <= 0) return;

    bool changed = true;
    while (changed) {
        changed = false;

        /* Count definitions per temp and track MOV-from-temp sources */
        int *def_count = (int *)calloc((size_t)tc, sizeof(int));
        int *mov_src   = (int *)malloc((size_t)tc * sizeof(int));
        for (int i = 0; i < tc; i++) mov_src[i] = -1;

        for (int i = 0; i < fn->instr_count; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->dest.kind == OPER_TEMP) {
                int id = ins->dest.temp_id;
                if (id >= 0 && id < tc) {
                    def_count[id]++;
                    if (ins->op == IR_MOV && ins->src1.kind == OPER_TEMP &&
                        ins->src1.temp_id >= 0 && ins->src1.temp_id < tc)
                        mov_src[id] = ins->src1.temp_id;
                    else
                        mov_src[id] = -1;
                }
            }
        }

        for (int t = 0; t < tc; t++) {
            if (def_count[t] != 1 || mov_src[t] < 0) continue;
            int src = mov_src[t];
            if (def_count[src] != 1) continue;

            /* Replace all uses of t with src */
            for (int i = 0; i < fn->instr_count; i++) {
                IRInstr *ins = &fn->instrs[i];
                /* Skip the defining MOV itself */
                if (ins->op == IR_MOV && ins->dest.kind == OPER_TEMP &&
                    ins->dest.temp_id == t)
                    continue;
                if (ins->src1.kind == OPER_TEMP && ins->src1.temp_id == t) {
                    ins->src1.temp_id = src;
                    changed = true;
                }
                if (ins->src2.kind == OPER_TEMP && ins->src2.temp_id == t) {
                    ins->src2.temp_id = src;
                    changed = true;
                }
            }

            /* Mark the MOV as dead */
            for (int i = 0; i < fn->instr_count; i++) {
                IRInstr *ins = &fn->instrs[i];
                if (ins->op == IR_MOV && ins->dest.kind == OPER_TEMP &&
                    ins->dest.temp_id == t) {
                    ins->op = IR_NOP;
                    break;
                }
            }
        }

        free(def_count);
        free(mov_src);
    }

    /* Compact out NOPs */
    int w = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_NOP) {
            if (w != i) fn->instrs[w] = fn->instrs[i];
            w++;
        }
    }
    fn->instr_count = w;
}

void opt_copyprop(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        copyprop_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        copyprop_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Function Inlining
 *
 * Inline small leaf functions at their call sites to eliminate
 * call overhead and expose further optimization opportunities.
 *
 * Criteria:
 *   • Callee has fewer than INLINE_THRESHOLD instructions
 *   • Callee is a leaf (no CALL, WRITE, READ, or SYSCALL)
 *   • Callee is not recursive
 *   • No update or field parameters
 * ═════════════════════════════════════════════════════════════ */

#define INLINE_THRESHOLD 32

static IRFunc *find_func(const IRProgram *ir, const char *name)
{
    for (int i = 0; i < ir->func_count; i++) {
        if (ir->funcs[i].name && strcmp(ir->funcs[i].name, name) == 0)
            return &ir->funcs[i];
    }
    return NULL;
}

static bool is_leaf_func(const IRFunc *fn)
{
    for (int i = 0; i < fn->instr_count; i++) {
        IROpcode op = fn->instrs[i].op;
        if (op == IR_CALL || op == IR_WRITE || op == IR_READ || op == IR_SYSCALL)
            return false;
    }
    return true;
}

static bool has_complex_params(const IRFunc *fn)
{
    for (int i = 0; i < fn->param_count; i++) {
        if (fn->param_info[i].is_update || fn->param_info[i].is_field)
            return true;
    }
    return false;
}

static int func_max_label(const IRFunc *fn)
{
    int mx = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        const IRInstr *ins = &fn->instrs[i];
        if (ins->dest.kind == OPER_LABEL && ins->dest.label_id > mx)
            mx = ins->dest.label_id;
        if (ins->src1.kind == OPER_LABEL && ins->src1.label_id > mx)
            mx = ins->src1.label_id;
    }
    return mx;
}

/* Remap an operand for the inlined callee body */
static IROper inline_remap_oper(IROper op, int temp_base, int label_base,
                                 int stack_shift)
{
    IROper r = op;
    switch (op.kind) {
    case OPER_TEMP:   r.temp_id   += temp_base;  break;
    case OPER_LABEL:  r.label_id  += label_base; break;
    case OPER_STACK:  r.stack_off -= stack_shift; break;
    default: break;
    }
    return r;
}

/* Ensure instruction buffer has room for at least one more entry */
static void ensure_instr_cap(IRInstr **buf, int count, int *cap)
{
    if (count >= *cap) {
        *cap = (*cap) * 2;
        *buf = (IRInstr *)realloc(*buf, (size_t)(*cap) * sizeof(IRInstr));
    }
}

/* Try to inline calls within a single function.  Returns true if any
 * inlining occurred. */
static bool inline_in_func(IRFunc *fn, const IRProgram *ir)
{
    bool any_inlined = false;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *call_ins = &fn->instrs[i];
        if (call_ins->op != IR_CALL) continue;
        if (call_ins->src1.kind != OPER_FUNC) continue;

        const char *callee_name = call_ins->src1.func_name;

        /* Don't inline self-recursive calls */
        if (fn->name && strcmp(callee_name, fn->name) == 0) continue;

        IRFunc *callee = find_func(ir, callee_name);
        if (!callee) continue;
        if (callee->instr_count == 0) continue;
        if (callee->instr_count >= INLINE_THRESHOLD) continue;
        if (!is_leaf_func(callee)) continue;
        if (has_complex_params(callee)) continue;

        /* Collect IR_ARG instructions preceding this CALL */
        int nargs = (call_ins->src2.kind == OPER_IMM)
                    ? (int)call_ins->src2.imm : 0;
        IROper *arg_vals = NULL;
        int    *arg_positions = NULL;
        if (nargs > 0) {
            arg_vals     = (IROper *)calloc((size_t)nargs, sizeof(IROper));
            arg_positions = (int *)malloc((size_t)nargs * sizeof(int));
            for (int k = 0; k < nargs; k++) arg_positions[k] = -1;

            int found = 0;
            for (int j = i - 1; j >= 0 && found < nargs; j--) {
                if (fn->instrs[j].op == IR_ARG) {
                    int idx = (int)fn->instrs[j].dest.imm;
                    if (idx >= 0 && idx < nargs) {
                        arg_vals[idx]     = fn->instrs[j].src1;
                        arg_positions[idx] = j;
                        found++;
                    }
                }
                /* Stop at labels or other calls */
                if (fn->instrs[j].op == IR_LABEL || fn->instrs[j].op == IR_CALL)
                    break;
            }
        }

        /* Compute remapping bases */
        int temp_base   = fn->temp_count;
        int label_base  = func_max_label(fn) + 1;
        int stack_shift = fn->stack_size;
        int exit_label  = label_base + func_max_label(callee) + 1;

        /* Build the inlined instruction sequence */
        int inline_cap = callee->instr_count + nargs + 4;
        IRInstr *inline_buf = (IRInstr *)malloc(
            (size_t)inline_cap * sizeof(IRInstr));
        int inline_count = 0;

        /* a) Store arguments into callee parameter stack slots */
        for (int p = 0; p < nargs && p < callee->param_count; p++) {
            ensure_instr_cap(&inline_buf, inline_count, &inline_cap);
            IRInstr *ins = &inline_buf[inline_count++];
            memset(ins, 0, sizeof(*ins));
            ins->op        = IR_STORE_VAR;
            ins->dest.kind = OPER_STACK;
            ins->dest.stack_off = callee->param_info[p].offset - stack_shift;
            ins->dest.size = callee->param_info[p].size;
            ins->src1      = arg_vals[p];
        }

        /* b) Copy callee instructions with operand remapping */
        IROper call_dest = call_ins->dest;
        for (int j = 0; j < callee->instr_count; j++) {
            IRInstr src = callee->instrs[j];

            if (src.op == IR_RET) {
                /* Return with value → MOV + JMP to exit */
                if (call_dest.kind == OPER_TEMP) {
                    ensure_instr_cap(&inline_buf, inline_count, &inline_cap);
                    IRInstr *mov = &inline_buf[inline_count++];
                    memset(mov, 0, sizeof(*mov));
                    mov->op   = IR_MOV;
                    mov->dest = call_dest;
                    mov->src1 = inline_remap_oper(src.src1, temp_base,
                                                  label_base, stack_shift);
                    mov->loc  = src.loc;
                }
                ensure_instr_cap(&inline_buf, inline_count, &inline_cap);
                IRInstr *jmp = &inline_buf[inline_count++];
                memset(jmp, 0, sizeof(*jmp));
                jmp->op             = IR_JMP;
                jmp->dest.kind      = OPER_LABEL;
                jmp->dest.label_id  = exit_label;
                continue;
            }

            if (src.op == IR_RET_VOID) {
                ensure_instr_cap(&inline_buf, inline_count, &inline_cap);
                IRInstr *jmp = &inline_buf[inline_count++];
                memset(jmp, 0, sizeof(*jmp));
                jmp->op             = IR_JMP;
                jmp->dest.kind      = OPER_LABEL;
                jmp->dest.label_id  = exit_label;
                continue;
            }

            /* Regular instruction: remap all operands */
            ensure_instr_cap(&inline_buf, inline_count, &inline_cap);
            IRInstr *dst = &inline_buf[inline_count++];
            *dst = src;
            dst->dest = inline_remap_oper(src.dest, temp_base,
                                          label_base, stack_shift);
            dst->src1 = inline_remap_oper(src.src1, temp_base,
                                          label_base, stack_shift);
            dst->src2 = inline_remap_oper(src.src2, temp_base,
                                          label_base, stack_shift);
        }

        /* c) Append exit label */
        ensure_instr_cap(&inline_buf, inline_count, &inline_cap);
        IRInstr *exitlbl = &inline_buf[inline_count++];
        memset(exitlbl, 0, sizeof(*exitlbl));
        exitlbl->op             = IR_LABEL;
        exitlbl->dest.kind      = OPER_LABEL;
        exitlbl->dest.label_id  = exit_label;

        /* Build the new instruction array for the function:
         * remove the ARG instructions and CALL, insert inlined body */
        int new_cap = fn->instr_count + inline_count + 16;
        IRInstr *new_instrs = (IRInstr *)malloc(
            (size_t)new_cap * sizeof(IRInstr));
        int new_count = 0;

        for (int j = 0; j < fn->instr_count; j++) {
            /* Skip ARG instructions that belong to this call */
            if (arg_positions) {
                bool skip = false;
                for (int p = 0; p < nargs; p++) {
                    if (arg_positions[p] == j) { skip = true; break; }
                }
                if (skip) continue;
            }

            if (j == i) {
                /* Replace CALL with inlined body */
                memcpy(&new_instrs[new_count], inline_buf,
                       (size_t)inline_count * sizeof(IRInstr));
                new_count += inline_count;
                continue;
            }

            new_instrs[new_count++] = fn->instrs[j];
        }

        free(inline_buf);
        free(arg_vals);
        free(arg_positions);

        fn->instrs      = new_instrs;
        fn->instr_count  = new_count;
        fn->instr_cap    = new_cap;
        fn->temp_count  += callee->temp_count;

        /* Extend caller frame to accommodate callee locals */
        int callee_stack = (callee->stack_size + 15) & ~15;
        fn->stack_size  += callee_stack;

        any_inlined = true;
        i = -1; /* restart scan (indices changed) */
    }

    return any_inlined;
}

void opt_inline(IRProgram *ir)
{
    /* Run up to 3 inlining passes to allow cascading */
    for (int pass = 0; pass < 3; pass++) {
        bool any = false;
        for (int i = 0; i < ir->func_count; i++) {
            if (inline_in_func(&ir->funcs[i], ir))
                any = true;
        }
        if (ir->top_level.instr_count > 0) {
            if (inline_in_func(&ir->top_level, ir))
                any = true;
        }
        if (!any) break;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Loop-Invariant Code Motion (LICM)
 *
 * Detects natural loops via back-edges and moves invariant pure
 * computations out of the loop body into the pre-header.
 * ═════════════════════════════════════════════════════════════ */

static bool is_pure_op(IROpcode op)
{
    switch (op) {
    case IR_MOV: case IR_LOAD_IMM: case IR_LOAD_STR:
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_NEG: case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
    case IR_SHL: case IR_SHR:
    case IR_CMP_EQ: case IR_CMP_NE: case IR_CMP_LT: case IR_CMP_LE:
    case IR_CMP_GT: case IR_CMP_GE:
    case IR_LOG_NOT: case IR_LEA:
    case IR_SEXT: case IR_ZEXT: case IR_TRUNC:
        return true;
    default:
        return false;
    }
}

/* Check whether an operand is loop-invariant. */
static bool oper_is_invariant(const IROper *op, const IRFunc *fn,
                              int loop_start, int loop_end,
                              const bool *is_inv)
{
    if (op->kind != OPER_TEMP)
        return true; /* immediates, labels, etc. are always invariant */

    int tid = op->temp_id;

    /* Defined outside the loop → invariant */
    bool def_inside = false;
    for (int i = loop_start; i <= loop_end; i++) {
        if (fn->instrs[i].dest.kind == OPER_TEMP &&
            fn->instrs[i].dest.temp_id == tid) {
            def_inside = true;
            break;
        }
    }
    if (!def_inside) return true;

    /* Defined by an instruction already marked invariant → invariant */
    for (int i = loop_start; i <= loop_end; i++) {
        if (fn->instrs[i].dest.kind == OPER_TEMP &&
            fn->instrs[i].dest.temp_id == tid && is_inv[i])
            return true;
    }

    return false;
}

static void licm_func(IRFunc *fn)
{
    if (fn->instr_count < 3) return;

    /* Build label → position map */
    int max_label = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL) {
            int lid = fn->instrs[i].dest.label_id;
            if (lid > max_label) max_label = lid;
        }
    }

    int *label_pos = (int *)malloc((size_t)(max_label + 2) * sizeof(int));
    for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            label_pos[fn->instrs[i].dest.label_id] = i;
    }

    bool did_something = true;
    while (did_something) {
        did_something = false;

        /* Find back-edges: jumps to an earlier label */
        for (int i = 0; i < fn->instr_count; i++) {
            IROpcode op = fn->instrs[i].op;
            if (op != IR_JMP && op != IR_JZ && op != IR_JNZ) continue;

            int target_label = fn->instrs[i].dest.label_id;
            if (target_label < 0 || target_label > max_label) continue;
            int header = label_pos[target_label];
            if (header < 0 || header >= i) continue; /* not a back-edge */

            int loop_start = header;
            int loop_end   = i;

            /* Identify invariant instructions */
            bool *is_inv = (bool *)calloc((size_t)fn->instr_count, sizeof(bool));

            bool changed = true;
            while (changed) {
                changed = false;
                for (int j = loop_start + 1; j < loop_end; j++) {
                    if (is_inv[j]) continue;
                    IRInstr *ins = &fn->instrs[j];
                    if (!is_pure_op(ins->op)) continue;
                    if (ins->dest.kind != OPER_TEMP) continue;

                    /* Dest temp must have exactly one def in the loop */
                    int def_count = 0;
                    for (int k = loop_start; k <= loop_end; k++) {
                        if (fn->instrs[k].dest.kind == OPER_TEMP &&
                            fn->instrs[k].dest.temp_id == ins->dest.temp_id)
                            def_count++;
                    }
                    if (def_count != 1) continue;

                    if (oper_is_invariant(&ins->src1, fn, loop_start,
                                          loop_end, is_inv) &&
                        oper_is_invariant(&ins->src2, fn, loop_start,
                                          loop_end, is_inv)) {
                        is_inv[j] = true;
                        changed = true;
                    }
                }
            }

            /* Count invariant instructions */
            int inv_count = 0;
            for (int j = loop_start + 1; j < loop_end; j++) {
                if (is_inv[j]) inv_count++;
            }

            if (inv_count > 0) {
                /* Build new instruction array with invariant code hoisted
                 * before the loop header label */
                int new_cap = fn->instr_count + 4;
                IRInstr *new_instrs = (IRInstr *)malloc(
                    (size_t)new_cap * sizeof(IRInstr));
                int nc = 0;

                for (int j = 0; j < fn->instr_count; j++) {
                    if (j == loop_start) {
                        /* Insert invariant instructions before header */
                        for (int k = loop_start + 1; k < loop_end; k++) {
                            if (is_inv[k])
                                new_instrs[nc++] = fn->instrs[k];
                        }
                    }
                    /* Skip invariant from original positions */
                    if (j > loop_start && j < loop_end && is_inv[j])
                        continue;
                    new_instrs[nc++] = fn->instrs[j];
                }

                fn->instrs     = new_instrs;
                fn->instr_count = nc;
                fn->instr_cap   = new_cap;

                /* Rebuild label positions */
                for (int k = 0; k <= max_label; k++) label_pos[k] = -1;
                for (int k = 0; k < fn->instr_count; k++) {
                    if (fn->instrs[k].op == IR_LABEL)
                        label_pos[fn->instrs[k].dest.label_id] = k;
                }

                did_something = true;
            }

            free(is_inv);
            if (did_something) break; /* restart loop detection */
        }
    }

    free(label_pos);
}

void opt_licm(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        licm_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        licm_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Loop Unrolling (2×)
 *
 * For small loops (body < UNROLL_MAX_BODY instructions), the loop
 * body is duplicated once per iteration to halve back-edge overhead.
 * Internal labels are remapped in the copy; exit jumps keep their
 * original targets so the loop can exit at any point.
 * ═════════════════════════════════════════════════════════════ */

#define UNROLL_MAX_BODY 24

static void unroll_func(IRFunc *fn)
{
    if (fn->instr_count < 4) return;

    int max_label = func_max_label(fn);
    if (max_label < 0) max_label = 0;

    int *label_pos = (int *)malloc((size_t)(max_label + 2) * sizeof(int));
    for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            label_pos[fn->instrs[i].dest.label_id] = i;
    }

    for (int i = fn->instr_count - 1; i >= 0; i--) {
        /* Look for unconditional back-edge */
        if (fn->instrs[i].op != IR_JMP) continue;
        int target_lid = fn->instrs[i].dest.label_id;
        if (target_lid < 0 || target_lid > max_label) continue;
        int header = label_pos[target_lid];
        if (header < 0 || header >= i) continue;

        int body_start = header + 1;
        int body_end   = i - 1;
        int body_len   = body_end - body_start + 1;
        if (body_len <= 0 || body_len > UNROLL_MAX_BODY) continue;

        /* Build label remap for internal labels in the copy */
        int next_label = max_label + 1;
        int remap_cap  = max_label + 2;
        int *label_remap = (int *)malloc((size_t)remap_cap * sizeof(int));
        for (int k = 0; k < remap_cap; k++) label_remap[k] = k;

        for (int j = body_start; j <= body_end; j++) {
            if (fn->instrs[j].op == IR_LABEL) {
                int lid = fn->instrs[j].dest.label_id;
                if (lid >= 0 && lid < remap_cap)
                    label_remap[lid] = next_label++;
            }
        }

        /* Build duplicated body with remapped internal labels */
        IRInstr *dup = (IRInstr *)malloc((size_t)body_len * sizeof(IRInstr));
        for (int j = 0; j < body_len; j++) {
            dup[j] = fn->instrs[body_start + j];
            if (dup[j].dest.kind == OPER_LABEL) {
                int lid = dup[j].dest.label_id;
                if (lid >= 0 && lid < remap_cap)
                    dup[j].dest.label_id = label_remap[lid];
            }
            if (dup[j].src1.kind == OPER_LABEL) {
                int lid = dup[j].src1.label_id;
                if (lid >= 0 && lid < remap_cap)
                    dup[j].src1.label_id = label_remap[lid];
            }
        }

        free(label_remap);

        /* Insert duplicated body before the back-edge JMP */
        int new_cap = fn->instr_count + body_len + 4;
        IRInstr *new_instrs = (IRInstr *)malloc(
            (size_t)new_cap * sizeof(IRInstr));
        int nc = 0;

        for (int j = 0; j < fn->instr_count; j++) {
            if (j == i) {
                /* Insert duplicate body before the back-edge JMP */
                memcpy(&new_instrs[nc], dup, (size_t)body_len * sizeof(IRInstr));
                nc += body_len;
            }
            new_instrs[nc++] = fn->instrs[j];
        }

        free(dup);

        fn->instrs     = new_instrs;
        fn->instr_count = nc;
        fn->instr_cap   = new_cap;

        break; /* unroll at most one loop per function */
    }

    free(label_pos);
}

void opt_unroll(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        unroll_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        unroll_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Redundant Instruction Elimination
 *
 * For each instruction that writes a temp, scan forward within the
 * same basic block.  If the temp is re-defined before any read,
 * the original write is dead.  Side-effectful opcodes (CALL, WRITE,
 * READ, SYSCALL, stores, index/field stores) are never eliminated.
 * ═════════════════════════════════════════════════════════════ */

static bool has_side_effect(IROpcode op)
{
    switch (op) {
    case IR_CALL:
    case IR_WRITE:
    case IR_READ:
    case IR_SYSCALL:
    case IR_STORE_VAR:
    case IR_INDEX_STORE:
    case IR_FIELD_STORE:
    case IR_STORE_IND:
    case IR_MEMCPY:
    case IR_ARG:
    case IR_JMP:
    case IR_JZ:
    case IR_JNZ:
    case IR_RET:
    case IR_RET_VOID:
    case IR_LABEL:
    case IR_DIV:
    case IR_MOD:
        return true;
    default:
        return false;
    }
}

static bool oper_reads_temp(const IROper *o, int tid)
{
    return o->kind == OPER_TEMP && o->temp_id == tid;
}

static bool instr_reads_temp(const IRInstr *ins, int tid)
{
    return oper_reads_temp(&ins->src1, tid) ||
           oper_reads_temp(&ins->src2, tid) ||
           /* Some opcodes read from dest (e.g. INDEX_STORE uses dest as base) */
           (ins->op == IR_INDEX_STORE && oper_reads_temp(&ins->dest, tid)) ||
           (ins->op == IR_FIELD_STORE && oper_reads_temp(&ins->dest, tid)) ||
           (ins->op == IR_STORE_IND   && oper_reads_temp(&ins->dest, tid)) ||
           (ins->op == IR_MEMCPY      && oper_reads_temp(&ins->dest, tid));
}

static void rie_func(IRFunc *fn)
{
    if (fn->instr_count == 0) return;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_NOP) continue;
        if (ins->dest.kind != OPER_TEMP) continue;
        if (has_side_effect(ins->op)) continue;

        int tid = ins->dest.temp_id;

        /* Scan forward within the basic block */
        bool dead = false;
        for (int j = i + 1; j < fn->instr_count; j++) {
            IRInstr *next = &fn->instrs[j];

            /* Basic-block boundary — stop scanning */
            if (next->op == IR_LABEL || next->op == IR_JMP ||
                next->op == IR_JZ   || next->op == IR_JNZ ||
                next->op == IR_RET  || next->op == IR_RET_VOID)
                break;

            /* temp is read — this instruction is needed */
            if (instr_reads_temp(next, tid))
                break;

            /* temp is re-defined — original write is dead */
            if (next->dest.kind == OPER_TEMP && next->dest.temp_id == tid) {
                dead = true;
                break;
            }
        }

        if (dead) ins->op = IR_NOP;
    }

    /* Compact: remove NOPs */
    int w = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_NOP) {
            if (w != i) fn->instrs[w] = fn->instrs[i];
            w++;
        }
    }
    fn->instr_count = w;
}

void opt_rie(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        rie_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        rie_func(&ir->top_level);
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
