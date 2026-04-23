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
    case IR_SHL:    r = (int64_t)((uint64_t)a << (b & 63)); break;
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
    int cf_iter = 0;
    while (changed && cf_iter++ < 100) {
        changed = false;

        /* ── Pass A: Constant Propagation ────────────────── *
         *                                                     *
         * A temp is propagatable if it is defined exactly once *
         * by IR_LOAD_IMM.  We count defs and track the value. */
        int *def_count = NULL;
        int64_t *def_val = NULL;
        if (tc > 0) {
            def_count = (int *)xcalloc((size_t)tc, sizeof(int));
            def_val   = (int64_t *)xcalloc((size_t)tc, sizeof(int64_t));

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
        bool *used = (bool *)xcalloc((size_t)tc, sizeof(bool));
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

/* Decompose MUL by constants of form (2^n ± 1) into SHL + ADD/SUB.
 * Runs after the power-of-2 pass which already converted exact powers. */
static void strength_reduce_composite(IRFunc *fn)
{
    /* Count how many MULs will expand so we can pre-size the buffer */
    int expand = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op != IR_MUL) continue;
        int64_t c = 0;
        if (ins->src2.kind == OPER_IMM)      c = ins->src2.imm;
        else if (ins->src1.kind == OPER_IMM) c = ins->src1.imm;
        else continue;
        /* Skip 3,5,7,9: code generator handles them directly.
         * 3,5,9 use single LEA; 7 uses mov+shl+sub (2c latency). */
        if (c > 2 && c != 3 && c != 5 && c != 7 && c != 9 &&
            (exact_log2(c - 1) > 0 || exact_log2(c + 1) > 0))
            expand++;
    }
    if (expand == 0) return;

    int new_cap = fn->instr_count + expand;
    IRInstr *out = (IRInstr *)xmalloc((size_t)new_cap * sizeof(IRInstr));
    int oc = 0;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        if (ins->op == IR_MUL) {
            int64_t c = 0;
            IROper var_op = {0};
            if (ins->src2.kind == OPER_IMM) {
                c = ins->src2.imm;
                var_op = ins->src1;
            } else if (ins->src1.kind == OPER_IMM) {
                c = ins->src1.imm;
                var_op = ins->src2;
            }

            if (c > 2 && c != 3 && c != 5 && c != 7 && c != 9) {
                int sp = exact_log2(c - 1);   /* c == 2^sp + 1 */
                int sm = exact_log2(c + 1);   /* c == 2^sm - 1 */

                if (sp > 0) {
                    /* x * (2^n+1) → t = x << n; dest = t + x */
                    int t = fn->temp_count++;
                    out[oc++] = (IRInstr){
                        .op   = IR_SHL,
                        .dest = {.kind = OPER_TEMP, .temp_id = t, .size = ins->dest.size},
                        .src1 = var_op,
                        .src2 = {.kind = OPER_IMM, .imm = sp},
                        .loc  = ins->loc
                    };
                    out[oc++] = (IRInstr){
                        .op   = IR_ADD,
                        .dest = ins->dest,
                        .src1 = {.kind = OPER_TEMP, .temp_id = t, .size = ins->dest.size},
                        .src2 = var_op,
                        .loc  = ins->loc
                    };
                    continue;
                }
                if (sm > 0) {
                    /* x * (2^n-1) → t = x << n; dest = t - x */
                    int t = fn->temp_count++;
                    out[oc++] = (IRInstr){
                        .op   = IR_SHL,
                        .dest = {.kind = OPER_TEMP, .temp_id = t, .size = ins->dest.size},
                        .src1 = var_op,
                        .src2 = {.kind = OPER_IMM, .imm = sm},
                        .loc  = ins->loc
                    };
                    out[oc++] = (IRInstr){
                        .op   = IR_SUB,
                        .dest = ins->dest,
                        .src1 = {.kind = OPER_TEMP, .temp_id = t, .size = ins->dest.size},
                        .src2 = var_op,
                        .loc  = ins->loc
                    };
                    continue;
                }
            }
        }

        out[oc++] = *ins;
    }

    /* Don't free fn->instrs: it may be arena-allocated after SSA destruction */
    fn->instrs = out;
    fn->instr_count = oc;
    fn->instr_cap = new_cap;
}

/* ═════════════════════════════════════════════════════════════
 * Magic Number Division
 *
 * Replaces constant DIV/MOD with multiply-shift sequences.
 * Handles signed pow2, unsigned non-pow2, signed non-pow2, and MOD
 * variants by computing quotient then remainder = n - q*d.
 * ═════════════════════════════════════════════════════════════ */

/* Unsigned 32-bit magic: returns multiplier M, shift s, and whether
 * the "add" fixup is needed (M overflows 32 bits). */
static void compute_umagic32(uint32_t d, uint32_t *M, int *s, int *need_add)
{
    uint64_t two32 = (uint64_t)1 << 32;
    uint64_t nc = two32 - 1 - (two32 % d);
    int p = 31;
    uint64_t q1 = 0x80000000ULL / nc;
    uint64_t r1 = 0x80000000ULL - q1 * nc;
    uint64_t q2 = 0x7FFFFFFFULL / d;
    uint64_t r2 = 0x7FFFFFFFULL - q2 * d;
    uint64_t delta;

    do {
        p++;
        q1 = 2 * q1;  r1 = 2 * r1;
        if (r1 >= nc) { q1++; r1 -= nc; }
        q2 = 2 * q2;  r2 = 2 * r2 + 1;
        if (r2 >= d)  { q2++; r2 -= d; }
        delta = d - 1 - r2;
    } while (p < 64 && (q1 < delta || (q1 == delta && r1 == 0)));

    uint64_t mval = q2 + 1;
    *s = p - 32;
    if (mval >= two32) {
        *need_add = 1;
        *M = (uint32_t)(mval - two32);
    } else {
        *need_add = 0;
        *M = (uint32_t)mval;
    }
}

/* Signed 32-bit magic for positive d >= 3. */
static void compute_smagic32(int32_t d, int32_t *M, int *s)
{
    uint32_t ad = (uint32_t)d;
    uint64_t two31 = (uint64_t)1 << 31;
    uint64_t nc = two31 - 1 - (two31 % ad);
    int p = 31;
    uint64_t q1 = two31 / nc;
    uint64_t r1 = two31 - q1 * nc;
    uint64_t q2 = two31 / ad;
    uint64_t r2 = two31 - q2 * ad;
    uint64_t delta;

    do {
        p++;
        q1 = 2 * q1;  r1 = 2 * r1;
        if (r1 >= nc) { q1++; r1 -= nc; }
        q2 = 2 * q2;  r2 = 2 * r2;
        if (r2 >= ad) { q2++; r2 -= ad; }
        delta = ad - r2 - 1;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    *M = (int32_t)(q2 + 1);
    *s = p - 32;
}

#define MAGICDIV_MAX_EXPAND 14

static void magic_div_func(IRFunc *fn)
{
    int expand = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op != IR_DIV && ins->op != IR_MOD) continue;
        if (ins->src2.kind != OPER_IMM) continue;
        int64_t d = ins->src2.imm;
        if (d == 0 || d == 1 || d == -1) continue;
        int is_unsigned = ins->extra;
        /* Skip cases already converted by strength_reduce_func */
        if (is_unsigned && exact_log2(d) > 0) continue;
        expand += MAGICDIV_MAX_EXPAND;
    }
    if (expand == 0) return;

    int new_cap = fn->instr_count + expand;
    IRInstr *out = (IRInstr *)xmalloc((size_t)new_cap * sizeof(IRInstr));
    int oc = 0;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        if ((ins->op == IR_DIV || ins->op == IR_MOD) &&
            ins->src2.kind == OPER_IMM)
        {
            int64_t d = ins->src2.imm;
            int is_unsigned = ins->extra;
            int is_mod = (ins->op == IR_MOD);
            int sz = ins->dest.size;
            SrcLoc loc = ins->loc;
            IROper n_op = ins->src1;
            IROper final_dest = ins->dest;

            if (d == 0 || d == 1 || d == -1) { out[oc++] = *ins; continue; }
            if (is_unsigned && exact_log2(d) > 0) { out[oc++] = *ins; continue; }

            /* Macro helpers for emitting instructions */
            #define MK_T(id) ((IROper){.kind = OPER_TEMP, .size = sz, .temp_id = (id)})
            #define MK_TW(id) ((IROper){.kind = OPER_TEMP, .size = 8, .temp_id = (id)})
            #define MK_I(v)  ((IROper){.kind = OPER_IMM,  .size = 0, .imm = (v)})
            #define MK_NONE  ((IROper){0})
            #define EMIT_I(o, d_, s1_, s2_, ex) out[oc++] = (IRInstr){ \
                .op = (o), .dest = (d_), .src1 = (s1_), .src2 = (s2_), \
                .extra = (ex), .loc = loc }

            int t_q = -1; /* will hold quotient temp */

            /* ── Signed power-of-2 DIV ───────────────────── */
            if (!is_unsigned && d > 0 && exact_log2(d) > 0) {
                int k = exact_log2(d);
                int t1 = fn->temp_count++, t2 = fn->temp_count++;
                int t3 = fn->temp_count++;
                t_q = fn->temp_count++;
                EMIT_I(IR_SHR, MK_T(t1), n_op, MK_I(63), 0);
                EMIT_I(IR_SHR, MK_T(t2), MK_T(t1), MK_I(64 - k), 1);
                EMIT_I(IR_ADD, MK_T(t3), n_op, MK_T(t2), 0);
                EMIT_I(IR_SHR, MK_T(t_q), MK_T(t3), MK_I(k), 0);
            }
            /* ── Signed pow2 DIV by negative ─────────────── */
            else if (!is_unsigned && d < 0 && exact_log2(-d) > 0) {
                int64_t ad = -d;
                int k = exact_log2(ad);
                int t1 = fn->temp_count++, t2 = fn->temp_count++;
                int t3 = fn->temp_count++, t4 = fn->temp_count++;
                t_q = fn->temp_count++;
                EMIT_I(IR_SHR, MK_T(t1), n_op, MK_I(63), 0);
                EMIT_I(IR_SHR, MK_T(t2), MK_T(t1), MK_I(64 - k), 1);
                EMIT_I(IR_ADD, MK_T(t3), n_op, MK_T(t2), 0);
                EMIT_I(IR_SHR, MK_T(t4), MK_T(t3), MK_I(k), 0);
                EMIT_I(IR_NEG, MK_T(t_q), MK_T(t4), MK_NONE, 0);
            }
            /* ── Unsigned non-pow2 ───────────────────────── */
            else if (is_unsigned && d >= 3 && exact_log2(d) < 0
                     && d <= (int64_t)UINT32_MAX) {
                uint32_t magic; int shift, add;
                compute_umagic32((uint32_t)d, &magic, &shift, &add);

                int t_nw = fn->temp_count++;
                int t_m = fn->temp_count++, t_p = fn->temp_count++;
                int t_hw = fn->temp_count++, t_h = fn->temp_count++;
                EMIT_I(IR_ZEXT, MK_TW(t_nw), n_op, MK_NONE, 0);
                EMIT_I(IR_LOAD_IMM, MK_TW(t_m), MK_I((int64_t)(uint64_t)magic), MK_NONE, 0);
                EMIT_I(IR_MUL, MK_TW(t_p), MK_TW(t_nw), MK_TW(t_m), 0);
                EMIT_I(IR_SHR, MK_TW(t_hw), MK_TW(t_p), MK_I(32), 1);
                EMIT_I(IR_TRUNC, MK_T(t_h), MK_TW(t_hw), MK_NONE, 0);

                if (!add) {
                    if (shift > 0) {
                        t_q = fn->temp_count++;
                        EMIT_I(IR_SHR, MK_T(t_q), MK_T(t_h), MK_I(shift), 1);
                    } else {
                        t_q = t_h;
                    }
                } else {
                    int td = fn->temp_count++, tf = fn->temp_count++;
                    int ts = fn->temp_count++;
                    EMIT_I(IR_SUB, MK_T(td), n_op, MK_T(t_h), 0);
                    EMIT_I(IR_SHR, MK_T(tf), MK_T(td), MK_I(1), 1);
                    EMIT_I(IR_ADD, MK_T(ts), MK_T(tf), MK_T(t_h), 0);
                    if (shift > 1) {
                        t_q = fn->temp_count++;
                        EMIT_I(IR_SHR, MK_T(t_q), MK_T(ts), MK_I(shift - 1), 1);
                    } else {
                        t_q = ts;
                    }
                }
            }
            /* ── Signed non-pow2 ─────────────────────────── */
            else if (!is_unsigned && (d >= 3 || d <= -3)
                     && d >= (int64_t)INT32_MIN && d <= (int64_t)INT32_MAX) {
                int64_t abs_d = d > 0 ? d : -d;
                int32_t ad = (int32_t)abs_d;
                int32_t magic; int shift;
                compute_smagic32(ad, &magic, &shift);

                int t_nw = fn->temp_count++;
                int t_m = fn->temp_count++, t_p = fn->temp_count++;
                int t_hw = fn->temp_count++, t_h = fn->temp_count++;
                EMIT_I(IR_SEXT, MK_TW(t_nw), n_op, MK_NONE, 0);
                EMIT_I(IR_LOAD_IMM, MK_TW(t_m), MK_I((int64_t)magic), MK_NONE, 0);
                EMIT_I(IR_MUL, MK_TW(t_p), MK_TW(t_nw), MK_TW(t_m), 0);
                EMIT_I(IR_SHR, MK_TW(t_hw), MK_TW(t_p), MK_I(32), 0);
                EMIT_I(IR_TRUNC, MK_T(t_h), MK_TW(t_hw), MK_NONE, 0);

                int cur = t_h;
                if (magic < 0) {
                    int t_adj = fn->temp_count++;
                    EMIT_I(IR_ADD, MK_T(t_adj), MK_T(cur), n_op, 0);
                    cur = t_adj;
                }
                if (shift > 0) {
                    int t_sh = fn->temp_count++;
                    EMIT_I(IR_SHR, MK_T(t_sh), MK_T(cur), MK_I(shift), 0);
                    cur = t_sh;
                }
                int t_sign = fn->temp_count++;
                t_q = fn->temp_count++;
                EMIT_I(IR_SHR, MK_T(t_sign), MK_T(cur), MK_I(63), 1);
                EMIT_I(IR_ADD, MK_T(t_q), MK_T(cur), MK_T(t_sign), 0);

                if (d < 0) {
                    int t_neg = fn->temp_count++;
                    EMIT_I(IR_NEG, MK_T(t_neg), MK_T(t_q), MK_NONE, 0);
                    t_q = t_neg;
                }
            }
            else {
                /* Unhandled case — keep original instruction */
                out[oc++] = *ins;
                goto next_instr;
            }

            /* Emit final result: DIV → quotient, MOD → n - q*d */
            if (!is_mod) {
                EMIT_I(IR_MOV, final_dest, MK_T(t_q), MK_NONE, 0);
            } else {
                int t_qd = fn->temp_count++;
                EMIT_I(IR_MUL, MK_T(t_qd), MK_T(t_q), MK_I(d), 0);
                EMIT_I(IR_SUB, final_dest, n_op, MK_T(t_qd), 0);
            }

            #undef MK_TW
            #undef MK_T
            #undef MK_I
            #undef MK_NONE
            #undef EMIT_I
            continue;
        }
next_instr:
        out[oc++] = *ins;
    }

    fn->instrs = out;
    fn->instr_count = oc;
    fn->instr_cap = new_cap;
}

void opt_strength_reduce(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++) {
        strength_reduce_func(&ir->funcs[i]);
        strength_reduce_composite(&ir->funcs[i]);
        magic_div_func(&ir->funcs[i]);
    }

    if (ir->top_level.instr_count > 0) {
        strength_reduce_func(&ir->top_level);
        strength_reduce_composite(&ir->top_level);
        magic_div_func(&ir->top_level);
    }
}

