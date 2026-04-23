#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

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

/* Check whether offset 'off' is in a set of offsets. */
static bool lse_in_set(int off, const int *set, int cnt)
{
    for (int i = 0; i < cnt; i++)
        if (set[i] == off) return true;
    return false;
}

static void loadstore_elim_func(IRFunc *fn)
{
    LSEEntry cache[LSE_MAX_SLOTS];
    int cache_count = 0;

    /* ───────────────────────────────────────────────────────────
     * Pre-scan: classify stack offsets.
     *   written[]:  offsets that are targets of IR_STORE_VAR
     *   escaped[]:  offsets whose address is taken  (IR_LEA)
     *
     * • NOT written AND NOT escaped → "immutable": value never
     *   changes (e.g. parameters that are never reassigned).
     *   Cache survives IR_LABEL (control-flow merge).
     * • NOT escaped → "call-safe": callee cannot access the slot.
     *   Cache survives IR_CALL and indirect stores.
     * ──────────────────────────────────────────────────────────── */
    int written[LSE_MAX_SLOTS], wr_cnt = 0;
    int escaped[LSE_MAX_SLOTS], es_cnt = 0;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_STORE_VAR) {
            int off = ins->dest.stack_off;
            if (!lse_in_set(off, written, wr_cnt) && wr_cnt < LSE_MAX_SLOTS)
                written[wr_cnt++] = off;
        }
        if (ins->op == IR_LEA && ins->src1.kind == OPER_STACK) {
            int off = ins->src1.stack_off;
            if (!lse_in_set(off, escaped, es_cnt) && es_cnt < LSE_MAX_SLOTS)
                escaped[es_cnt++] = off;
        }
    }

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        /* IR_LABEL: control-flow merge.
         * Immutable (not written, not escaped) slots keep their
         * cached value — it can never change on any path.
         * Everything else is flushed. */
        if (ins->op == IR_LABEL) {
            for (int c = 0; c < cache_count; c++) {
                if (!cache[c].valid) continue;
                int off = cache[c].offset;
                if (lse_in_set(off, written, wr_cnt) ||
                    lse_in_set(off, escaped, es_cnt))
                    cache[c].valid = false;
            }
            continue;
        }

        /* IR_CALL / indirect stores: callee or pointer write may
         * modify escaped slots, but cannot touch non-escaped ones. */
        if (ins->op == IR_CALL ||
            ins->op == IR_STORE_IND || ins->op == IR_INDEX_STORE ||
            ins->op == IR_FIELD_STORE || ins->op == IR_MEMCPY) {
            for (int c = 0; c < cache_count; c++) {
                if (!cache[c].valid) continue;
                if (lse_in_set(cache[c].offset, escaped, es_cnt))
                    cache[c].valid = false;
            }
            continue;
        }

        /* Flush on unconditional jumps — code after JMP is unreachable
         * until a new label starts a new basic block.
         * Conditional branches (JZ/JNZ) do NOT flush: on the fall-through
         * path nothing changed in memory, so the cache remains valid.
         * The jump target label will flush the cache on its own. */
        if (ins->op == IR_JMP) {
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
        case IR_MUL:
            ins->op = IR_NEG;
            ins->src2 = (IROper){0};
            return true;
        default: break;
        }
    }

    /* ── src1 is immediate -1 (commutative MUL) ──────── */
    if (ins->src1.kind == OPER_IMM && ins->src1.imm == -1) {
        if (ins->op == IR_MUL) {
            ins->src1 = ins->src2;
            ins->op = IR_NEG;
            ins->src2 = (IROper){0};
            return true;
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
    int iter = 0;
    while (changed && iter++ < 8) {
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
    int cp_iter = 0;
    while (changed && cp_iter++ < 100) {
        changed = false;

        /* Count definitions per temp and track MOV-from-temp sources */
        int *def_count = (int *)xcalloc((size_t)tc, sizeof(int));
        int *mov_src   = (int *)xmalloc((size_t)tc * sizeof(int));
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

            /* Must re-scan: replacements above may have changed MOV
               sources for later temps, making mov_src[] stale. */
            if (changed) break;
        }

        /* Loop-carried coalescing: for MOV tDst=tSrc where tDst has
         * 2 defs (e.g. pre-loop init + this MOV) and tSrc has 1 def,
         * rename tSrc → tDst and delete the MOV.  This is safe when
         * tDst is not used between tSrc's definition and this MOV, so
         * its old value has already been consumed. */
        if (!changed) {
            for (int t = 0; t < tc; t++) {
                if (def_count[t] != 2 || mov_src[t] < 0) continue;
                int src = mov_src[t];
                if (def_count[src] != 1) continue;

                /* find src's definition and the MOV position */
                int src_def_pos = -1, mov_pos = -1;
                for (int i = 0; i < fn->instr_count; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if (ins->dest.kind == OPER_TEMP && ins->dest.temp_id == src
                        && ins->op != IR_NOP)
                        src_def_pos = i;
                    if (ins->op == IR_MOV && ins->dest.kind == OPER_TEMP &&
                        ins->dest.temp_id == t && ins->src1.kind == OPER_TEMP &&
                        ins->src1.temp_id == src)
                        mov_pos = i;
                }
                if (src_def_pos < 0 || mov_pos < 0 || src_def_pos >= mov_pos)
                    continue;

                /* Check: tDst is not used between src def and the MOV */
                bool safe = true;
                for (int i = src_def_pos + 1; i < mov_pos; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if ((ins->src1.kind == OPER_TEMP && ins->src1.temp_id == t) ||
                        (ins->src2.kind == OPER_TEMP && ins->src2.temp_id == t)) {
                        safe = false;
                        break;
                    }
                }
                if (!safe) continue;

                /* Rename src → t (all occurrences) */
                for (int i = 0; i < fn->instr_count; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if (ins->dest.kind == OPER_TEMP && ins->dest.temp_id == src)
                        ins->dest.temp_id = t;
                    if (ins->src1.kind == OPER_TEMP && ins->src1.temp_id == src)
                        ins->src1.temp_id = t;
                    if (ins->src2.kind == OPER_TEMP && ins->src2.temp_id == src)
                        ins->src2.temp_id = t;
                }
                /* MOV is now 'mov t, t' → NOP */
                fn->instrs[mov_pos].op = IR_NOP;
                changed = true;
                break;
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

