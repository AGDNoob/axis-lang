#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

/* ═════════════════════════════════════════════════════════════
 * Scalar Replacement of Aggregates (SRA)
 *
 * Replaces LEA + FIELD_LOAD/FIELD_STORE at constant offsets with
 * dedicated scalar temps, enabling further register promotion.
 * Only applies when the LEA address doesn't escape (not passed
 * to CALL, MEMCPY dest, or STORE_IND).
 * ═════════════════════════════════════════════════════════════ */
#define SRA_MAX_FIELDS 32

static void sra_func(IRFunc *fn)
{
    if (fn->instr_count == 0 || fn->temp_count == 0) return;

    int tc = fn->temp_count;
    /* For each temp defined by LEA, check if it escapes */
    bool *is_lea = (bool *)xcalloc((size_t)tc, sizeof(bool));
    bool *escapes = (bool *)xcalloc((size_t)tc, sizeof(bool));

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_LEA && ins->dest.kind == OPER_TEMP)
            is_lea[ins->dest.temp_id] = true;
    }

    /* Mark LEA temps that escape */
    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_NOP) continue;

        /* Passed as function argument? */
        if (ins->op == IR_ARG && ins->src1.kind == OPER_TEMP &&
            ins->src1.temp_id < tc && is_lea[ins->src1.temp_id])
            escapes[ins->src1.temp_id] = true;

        /* Used as MEMCPY dest or src? */
        if (ins->op == IR_MEMCPY) {
            if (ins->dest.kind == OPER_TEMP && ins->dest.temp_id < tc)
                escapes[ins->dest.temp_id] = true;
            if (ins->src1.kind == OPER_TEMP && ins->src1.temp_id < tc)
                escapes[ins->src1.temp_id] = true;
        }

        /* STORE_IND through the address? */
        if (ins->op == IR_STORE_IND && ins->dest.kind == OPER_TEMP &&
            ins->dest.temp_id < tc)
            escapes[ins->dest.temp_id] = true;

        /* Used in any op that's not FIELD_LOAD/FIELD_STORE as base? */
        if (ins->op != IR_FIELD_LOAD && ins->op != IR_FIELD_STORE &&
            ins->op != IR_LEA && ins->op != IR_NOP &&
            ins->op != IR_ARG && ins->op != IR_MEMCPY &&
            ins->op != IR_STORE_IND)
        {
            if (ins->src1.kind == OPER_TEMP && ins->src1.temp_id < tc &&
                is_lea[ins->src1.temp_id])
                escapes[ins->src1.temp_id] = true;
            if (ins->src2.kind == OPER_TEMP && ins->src2.temp_id < tc &&
                is_lea[ins->src2.temp_id])
                escapes[ins->src2.temp_id] = true;
        }
    }

    /* For each non-escaping LEA temp, collect all FIELD offsets used */
    for (int lea_tid = 0; lea_tid < tc; lea_tid++) {
        if (!is_lea[lea_tid] || escapes[lea_tid]) continue;

        /* Collect distinct offsets */
        int offsets[SRA_MAX_FIELDS];
        int field_temps[SRA_MAX_FIELDS]; /* scalar temp for each offset */
        int nfields = 0;
        bool too_many = false;

        for (int i = 0; i < fn->instr_count && !too_many; i++) {
            IRInstr *ins = &fn->instrs[i];
            int off = -1;
            if (ins->op == IR_FIELD_LOAD &&
                ins->src1.kind == OPER_TEMP && ins->src1.temp_id == lea_tid)
                off = ins->src2.imm;
            if (ins->op == IR_FIELD_STORE &&
                ins->dest.kind == OPER_TEMP && ins->dest.temp_id == lea_tid)
                off = ins->src2.imm;
            if (off < 0) continue;

            /* Find or add offset */
            int idx = -1;
            for (int f = 0; f < nfields; f++) {
                if (offsets[f] == off) { idx = f; break; }
            }
            if (idx < 0) {
                if (nfields >= SRA_MAX_FIELDS) { too_many = true; break; }
                idx = nfields;
                offsets[nfields] = off;
                field_temps[nfields] = fn->temp_count++;
                nfields++;
            }
        }

        if (too_many || nfields == 0) continue;

        /* Replace FIELD_LOAD/FIELD_STORE with MOV to/from scalar temps */
        for (int i = 0; i < fn->instr_count; i++) {
            IRInstr *ins = &fn->instrs[i];

            if (ins->op == IR_FIELD_LOAD &&
                ins->src1.kind == OPER_TEMP && ins->src1.temp_id == lea_tid)
            {
                int off = ins->src2.imm;
                for (int f = 0; f < nfields; f++) {
                    if (offsets[f] == off) {
                        ins->op = IR_MOV;
                        ins->src1 = (IROper){.kind = OPER_TEMP, .size = ins->dest.size,
                                             .temp_id = field_temps[f]};
                        ins->src2 = (IROper){0};
                        break;
                    }
                }
            }

            if (ins->op == IR_FIELD_STORE &&
                ins->dest.kind == OPER_TEMP && ins->dest.temp_id == lea_tid)
            {
                int off = ins->src2.imm;
                for (int f = 0; f < nfields; f++) {
                    if (offsets[f] == off) {
                        /* FIELD_STORE: dest=base, src1=value, src2=offset */
                        IROper val = ins->src1;
                        ins->op = IR_MOV;
                        ins->dest = (IROper){.kind = OPER_TEMP, .size = val.size,
                                             .temp_id = field_temps[f]};
                        ins->src1 = val;
                        ins->src2 = (IROper){0};
                        break;
                    }
                }
            }
        }
    }

    free(is_lea);
    free(escapes);
}

void opt_sra(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        sra_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        sra_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Switch Lowering – Binary Search
 *
 * Detects cascading CMP_EQ + JNZ sequences on the same variable
 * with dense-ish constant values, and reorganises them into a
 * balanced binary search tree, reducing worst-case comparisons
 * from O(n) to O(log n).
 * ═════════════════════════════════════════════════════════════ */
#define SW_MIN_CASES 4
#define SW_MAX_CASES 64

static void switch_lower_func(IRFunc *fn)
{
    if (fn->instr_count < SW_MIN_CASES * 2) return;

    int ic = fn->instr_count;
    for (int i = 0; i + 3 < ic; /* advanced inside */) {
        /* Look for a cascade: CMP_EQ t, var, const; JNZ label, t */
        IRInstr *cmp0 = &fn->instrs[i];
        if (cmp0->op != IR_CMP_EQ || cmp0->src2.kind != OPER_IMM ||
            cmp0->src1.kind != OPER_TEMP) { i++; continue; }

        IRInstr *jnz0 = &fn->instrs[i + 1];
        if (jnz0->op != IR_JNZ || jnz0->src1.kind != OPER_TEMP ||
            jnz0->src1.temp_id != cmp0->dest.temp_id) { i++; continue; }

        int var_tid = cmp0->src1.temp_id;

        /* Collect all consecutive CMP_EQ + JNZ on the same variable */
        int64_t  vals[SW_MAX_CASES];
        int      labels[SW_MAX_CASES];
        int      cmp_idx[SW_MAX_CASES]; /* instruction indices */
        int      jnz_idx[SW_MAX_CASES];
        int ncases = 0;
        int j = i;

        while (j + 1 < ic && ncases < SW_MAX_CASES) {
            IRInstr *c = &fn->instrs[j];
            IRInstr *jn = &fn->instrs[j + 1];
            if (c->op != IR_CMP_EQ || c->src1.kind != OPER_TEMP ||
                c->src1.temp_id != var_tid || c->src2.kind != OPER_IMM)
                break;
            if (jn->op != IR_JNZ || jn->src1.kind != OPER_TEMP ||
                jn->src1.temp_id != c->dest.temp_id)
                break;
            vals[ncases] = c->src2.imm;
            labels[ncases] = jn->dest.label_id;
            cmp_idx[ncases] = j;
            jnz_idx[ncases] = j + 1;
            ncases++;
            j += 2;
        }

        if (ncases < SW_MIN_CASES) { i += 2; continue; }

        /* Sort cases by value (simple insertion sort) */
        for (int a = 1; a < ncases; a++) {
            int64_t kv = vals[a]; int kl = labels[a];
            int ci = cmp_idx[a]; int ji = jnz_idx[a];
            int b = a - 1;
            while (b >= 0 && vals[b] > kv) {
                vals[b + 1] = vals[b]; labels[b + 1] = labels[b];
                cmp_idx[b + 1] = cmp_idx[b]; jnz_idx[b + 1] = jnz_idx[b];
                b--;
            }
            vals[b + 1] = kv; labels[b + 1] = kl;
            cmp_idx[b + 1] = ci; jnz_idx[b + 1] = ji;
        }

        /* Reorder the CMP_EQ+JNZ pairs so the median case comes first,
         * then recursively the sub-medians, producing a binary search
         * over the sorted values. We modify in-place. */
        /* Build order array: which case index goes at which position */
        int order[SW_MAX_CASES];
        int opos = 0;

        /* BFS-like traversal of the binary search tree */
        typedef struct { int lo, hi; } StkEntry;
        StkEntry stk[SW_MAX_CASES];
        int sp = 0;
        stk[sp++] = (StkEntry){0, ncases - 1};
        while (sp > 0) {
            StkEntry e = stk[--sp];
            if (e.lo > e.hi) continue;
            int mid = (e.lo + e.hi) / 2;
            order[opos++] = mid;
            /* Push right first so left is processed first (stack reversal) */
            if (mid + 1 <= e.hi) stk[sp++] = (StkEntry){mid + 1, e.hi};
            if (e.lo <= mid - 1) stk[sp++] = (StkEntry){e.lo, mid - 1};
        }

        /* Apply the reordering: write the CMP_EQ+JNZ pairs in binary search order */
        /* First, save originals */
        IRInstr *saved = (IRInstr *)xmalloc((size_t)(ncases * 2) * sizeof(IRInstr));
        for (int k = 0; k < ncases; k++) {
            saved[k * 2]     = fn->instrs[i + k * 2];
            saved[k * 2 + 1] = fn->instrs[i + k * 2 + 1];
        }
        for (int k = 0; k < ncases; k++) {
            int src = order[k];
            fn->instrs[i + k * 2]     = saved[src * 2];
            fn->instrs[i + k * 2 + 1] = saved[src * 2 + 1];
        }
        free(saved);

        i = j; /* skip past the cascade */
    }
}

void opt_switch_lower(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        switch_lower_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        switch_lower_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
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
 * RSP is the stack pointer – never allocatable.
 * RBP is callee-saved and available (frame-pointer-omit mode).
 * ═════════════════════════════════════════════════════════════ */

/* Allocatable register pool ordered by preference */
static const int reg_pool[] = {
    3,   /* RBX  – callee-saved */
    6,   /* RSI  – callee-saved */
    7,   /* RDI  – callee-saved */
    5,   /* RBP  – callee-saved (frame-pointer omitted) */
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
    return phys == 3 || phys == 5 || phys == 6 || phys == 7 ||
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
    int *starts = (int *)xmalloc((size_t)tc * sizeof(int));
    int *ends   = (int *)xmalloc((size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++) {
        starts[i] = -1;
        ends[i]   = -1;
    }

    /* Track which temps are live across a CALL instruction */
    bool *crosses_call = (bool *)xcalloc((size_t)tc, sizeof(bool));

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
    int *label_pos = (int *)xmalloc((size_t)(max_label + 1) * sizeof(int));
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
                    /* Check if temp is loop-local (defined before first
                     * use inside the loop body).  Such temps get a fresh
                     * value each iteration and do not need their interval
                     * extended across the back-edge. */
                    int first_def_in_loop = -1;
                    int first_use_in_loop = -1;
                    for (int p = loop_start; p <= loop_end; p++) {
                        const IRInstr *li = &fn->instrs[p];
                        if (first_def_in_loop < 0 &&
                            li->dest.kind == OPER_TEMP &&
                            li->dest.temp_id == t)
                            first_def_in_loop = p;
                        if (first_use_in_loop < 0 &&
                            ((li->src1.kind == OPER_TEMP &&
                              li->src1.temp_id == t) ||
                             (li->src2.kind == OPER_TEMP &&
                              li->src2.temp_id == t)))
                            first_use_in_loop = p;
                        if (first_def_in_loop >= 0 &&
                            first_use_in_loop >= 0)
                            break;
                    }
                    /* Loop-local: def strictly before any use in loop */
                    if (first_def_in_loop >= 0 &&
                        (first_use_in_loop < 0 ||
                         first_def_in_loop < first_use_in_loop))
                        continue;

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
    LiveInterval *intervals = (LiveInterval *)xcalloc((size_t)tc, sizeof(LiveInterval));
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
    Active *active = (Active *)xmalloc((size_t)tc * sizeof(Active));
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
