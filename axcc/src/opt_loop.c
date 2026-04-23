#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

/* ═════════════════════════════════════════════════════════════
 * Loop-Invariant Code Motion (LICM)
 *
 * Detects natural loops via back-edges and moves invariant pure
 * computations out of the loop body into the pre-header.
 * ═════════════════════════════════════════════════════════════ */

bool is_pure_op(IROpcode op)
{
    switch (op) {
    case IR_MOV: case IR_LOAD_IMM: case IR_LOAD_STR:
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_NEG: case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
    case IR_SHL: case IR_SHR:
    case IR_CMP_EQ: case IR_CMP_NE: case IR_CMP_LT: case IR_CMP_LE:
    case IR_CMP_GT: case IR_CMP_GE:
    case IR_LOG_NOT: case IR_LEA: case IR_CMOV:
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

    int *label_pos = (int *)xmalloc((size_t)(max_label + 2) * sizeof(int));
    for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            label_pos[fn->instrs[i].dest.label_id] = i;
    }

    bool did_something = true;
    int licm_iter = 0;
    while (did_something && licm_iter++ < 4) {
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

            /* Expand loop_end to cover ALL back-edges to the same
             * header.  After jump-threading there can be multiple
             * back-edges; using only the first would miss stores
             * that sit between the two edges. */
            for (int k = i + 1; k < fn->instr_count; k++) {
                IROpcode op2 = fn->instrs[k].op;
                if (op2 != IR_JMP && op2 != IR_JZ && op2 != IR_JNZ)
                    continue;
                int tl2 = fn->instrs[k].dest.label_id;
                if (tl2 == target_label)
                    loop_end = k;   /* widen to latest back-edge */
            }

            /* Identify invariant instructions */
            bool *is_inv = (bool *)xcalloc((size_t)fn->instr_count, sizeof(bool));

            bool changed = true;
            while (changed) {
                changed = false;
                for (int j = loop_start + 1; j < loop_end; j++) {
                    if (is_inv[j]) continue;
                    IRInstr *ins = &fn->instrs[j];

                    /* LOAD_VAR is invariant if the slot is never
                     * written inside the loop body. */
                    if (ins->op == IR_LOAD_VAR &&
                        ins->dest.kind == OPER_TEMP) {
                        int slot = ins->src1.stack_off;
                        bool written = false;
                        for (int k = loop_start + 1; k < loop_end; k++) {
                            if (fn->instrs[k].op == IR_STORE_VAR &&
                                fn->instrs[k].dest.stack_off == slot) {
                                written = true;
                                break;
                            }
                        }
                        if (!written) {
                            /* Also check single-def for the dest temp */
                            int def_count = 0;
                            for (int k = loop_start; k <= loop_end; k++) {
                                if (fn->instrs[k].dest.kind == OPER_TEMP &&
                                    fn->instrs[k].dest.temp_id ==
                                        ins->dest.temp_id)
                                    def_count++;
                            }
                            if (def_count == 1) {
                                is_inv[j] = true;
                                changed = true;
                            }
                        }
                        continue;
                    }

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
                IRInstr *new_instrs = (IRInstr *)xmalloc(
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
 * Loop Inversion
 *
 * Transforms top-test loops into bottom-test form:
 *   LABEL top → [cond] → JZ/JNZ exit → [body] → JMP top → LABEL exit
 * becomes:
 *   [cond_guard] → JZ/JNZ exit → LABEL top → [body] → [cond] → inverted_branch top → LABEL exit
 *
 * This eliminates the unconditional JMP from the hot loop.
 * ═════════════════════════════════════════════════════════════ */

static void loop_invert_func(IRFunc *fn)
{
    if (fn->instr_count < 4) return;

    int max_label = func_max_label(fn);
    if (max_label < 0) return;

    int label_cap = max_label + 2;
    int *label_pos = (int *)xmalloc((size_t)label_cap * sizeof(int));
    for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            label_pos[fn->instrs[i].dest.label_id] = i;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        /* Rebuild label_pos after each transformation */
        if (max_label + 2 > label_cap) {
            label_cap = max_label + 2;
            label_pos = (int *)xrealloc(label_pos, (size_t)label_cap * sizeof(int));
        }
        for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op == IR_LABEL) {
                int lid = fn->instrs[i].dest.label_id;
                if (lid >= 0 && lid <= max_label)
                    label_pos[lid] = i;
            }
        }

        for (int i = fn->instr_count - 1; i >= 0; i--) {
            /* Find unconditional back-edge: JMP to earlier label */
            if (fn->instrs[i].op != IR_JMP) continue;
            int target_lid = fn->instrs[i].dest.label_id;
            if (target_lid < 0 || target_lid > max_label) continue;
            int header = label_pos[target_lid];
            if (header < 0 || header >= i) continue;

            /* Find condition block: instrs from header+1 to first JZ/JNZ */
            int cond_start = header + 1;
            int cond_branch = -1;
            for (int j = cond_start; j < i; j++) {
                if (fn->instrs[j].op == IR_JZ || fn->instrs[j].op == IR_JNZ) {
                    cond_branch = j;
                    break;
                }
                /* If we hit a label or unconditional jump before
                 * finding the condition branch, this isn't a simple loop */
                if (fn->instrs[j].op == IR_LABEL || fn->instrs[j].op == IR_JMP)
                    break;
            }
            if (cond_branch < 0) continue;

            /* Only invert top-tested loops with JZ exit branch.
             * JNZ means "continue" (not "exit"), e.g. outer-latch jnz L0
             * which is not a loop header pattern we can safely invert. */
            if (fn->instrs[cond_branch].op != IR_JZ) continue;

            int exit_lid = fn->instrs[cond_branch].dest.label_id;

            /* Exit label must be OUTSIDE the loop body [header..i].
             * If it is inside (e.g. a 'when' conditional in a repeat loop),
             * this is not a while-loop pattern — skip it. */
            if (exit_lid >= 0 && exit_lid <= max_label) {
                int exit_pos = label_pos[exit_lid];
                if (exit_pos >= header && exit_pos <= i) continue;
            }

            /* Check if exit label is directly after the back-edge JMP */
            bool need_exit_jmp = true;
            if (i + 1 < fn->instr_count && fn->instrs[i + 1].op == IR_LABEL
                && fn->instrs[i + 1].dest.label_id == exit_lid)
                need_exit_jmp = false;

            int cond_len = cond_branch - cond_start + 1; /* includes the JZ/JNZ */
            if (cond_len <= 0 || cond_len > 16) continue; /* sanity limit */

            /* ── Build the guard copy of condition instructions ── */
            /* Temps defined in the condition block need fresh IDs in the guard */
            int tremap_cap = fn->temp_count + cond_len;
            int *temp_remap = (int *)xcalloc((size_t)tremap_cap, sizeof(int));
            for (int k = 0; k < tremap_cap; k++) temp_remap[k] = -1;
            for (int j = cond_start; j <= cond_branch; j++) {
                if (fn->instrs[j].dest.kind == OPER_TEMP) {
                    int tid = fn->instrs[j].dest.temp_id;
                    if (tid >= 0 && tid < tremap_cap && temp_remap[tid] < 0)
                        temp_remap[tid] = fn->temp_count++;
                }
            }

            IRInstr *guard = (IRInstr *)xmalloc((size_t)cond_len * sizeof(IRInstr));
            for (int j = 0; j < cond_len; j++) {
                guard[j] = fn->instrs[cond_start + j];
                /* Remap temps */
                if (guard[j].dest.kind == OPER_TEMP) {
                    int tid = guard[j].dest.temp_id;
                    if (tid >= 0 && tid < tremap_cap && temp_remap[tid] >= 0)
                        guard[j].dest.temp_id = temp_remap[tid];
                }
                if (guard[j].src1.kind == OPER_TEMP) {
                    int tid = guard[j].src1.temp_id;
                    if (tid >= 0 && tid < tremap_cap && temp_remap[tid] >= 0)
                        guard[j].src1.temp_id = temp_remap[tid];
                }
                if (guard[j].src2.kind == OPER_TEMP) {
                    int tid = guard[j].src2.temp_id;
                    if (tid >= 0 && tid < tremap_cap && temp_remap[tid] >= 0)
                        guard[j].src2.temp_id = temp_remap[tid];
                }
            }
            /* ── Bridge MOVs: seed original temps for first iteration ──
             * When the condition block defines a temp that the body uses,
             * the guard peels a renamed copy.  On the first iteration the
             * original temp is undefined → emit MOV orig ← guard_copy. */
            int body_start = cond_branch + 1;
            int body_end   = i - 1;
            int body_len   = body_end - body_start + 1;

            struct { int orig, guard, sz; } li_bridge[16];
            int li_bridge_n = 0;

            for (int j = cond_start; j <= cond_branch
                                  && li_bridge_n < 16; j++) {
                if (fn->instrs[j].dest.kind != OPER_TEMP) continue;
                int tid = fn->instrs[j].dest.temp_id;
                if (tid < 0 || tid >= tremap_cap || temp_remap[tid] < 0)
                    continue;
                bool used = false;
                for (int k = body_start; k <= body_end && !used; k++) {
                    if (fn->instrs[k].src1.kind == OPER_TEMP
                        && fn->instrs[k].src1.temp_id == tid)
                        used = true;
                    if (fn->instrs[k].src2.kind == OPER_TEMP
                        && fn->instrs[k].src2.temp_id == tid)
                        used = true;
                }
                if (used) {
                    li_bridge[li_bridge_n].orig  = tid;
                    li_bridge[li_bridge_n].guard = temp_remap[tid];
                    li_bridge[li_bridge_n].sz    = fn->instrs[j].dest.size;
                    li_bridge_n++;
                }
            }

            free(temp_remap);

            /* Check for inner jumps to header (continue/skip) */
            bool has_inner_continue = false;
            for (int j = body_start; j <= body_end; j++) {
                IRInstr *ins = &fn->instrs[j];
                if ((ins->op == IR_JMP || ins->op == IR_JZ || ins->op == IR_JNZ)
                    && ins->dest.label_id == target_lid) {
                    has_inner_continue = true;
                    break;
                }
            }

            /* ── Build the bottom condition (original temps, inverted branch) ── */
            IRInstr *bottom = (IRInstr *)xmalloc((size_t)cond_len * sizeof(IRInstr));
            for (int j = 0; j < cond_len; j++)
                bottom[j] = fn->instrs[cond_start + j];
            /* Invert branch and retarget to loop header */
            IRInstr *bot_branch = &bottom[cond_len - 1];
            bot_branch->op = (bot_branch->op == IR_JZ) ? IR_JNZ : IR_JZ;
            bot_branch->dest.label_id = target_lid; /* jump back to top */

            /* ── Reassemble instruction stream ──
             * Original: [pre] LABEL_top [cond] JZ/JNZ_exit [body] JMP_top [...]
             * New:      [pre] [guard] [bridge] LABEL_top [body] [bottom_cond] inv_branch_top [jmp_exit?] [...]
             */

            int new_count = fn->instr_count + cond_len - 1
                            + li_bridge_n
                            + (need_exit_jmp ? 1 : 0)
                            + (has_inner_continue ? 1 : 0);

            int new_cap = new_count + 4;
            IRInstr *out = (IRInstr *)xmalloc((size_t)new_cap * sizeof(IRInstr));
            int nc = 0;

            /* [pre]: everything before header label */
            for (int j = 0; j < header; j++)
                out[nc++] = fn->instrs[j];

            /* [guard]: duplicated condition with fresh temps */
            memcpy(&out[nc], guard, (size_t)cond_len * sizeof(IRInstr));
            nc += cond_len;

            /* [bridge MOVs]: seed original temps from guard copies */
            for (int b = 0; b < li_bridge_n; b++) {
                memset(&out[nc], 0, sizeof(IRInstr));
                out[nc].op           = IR_MOV;
                out[nc].dest.kind    = OPER_TEMP;
                out[nc].dest.temp_id = li_bridge[b].orig;
                out[nc].dest.size    = li_bridge[b].sz;
                out[nc].src1.kind    = OPER_TEMP;
                out[nc].src1.temp_id = li_bridge[b].guard;
                out[nc].src1.size    = li_bridge[b].sz;
                nc++;
            }

            /* LABEL_top (header label, kept) */
            out[nc++] = fn->instrs[header];

            /* [body]: instructions from after original cond to before JMP */
            if (body_len > 0) {
                memcpy(&out[nc], &fn->instrs[body_start],
                       (size_t)body_len * sizeof(IRInstr));
                /* Retarget inner continues (jumps to header) to latch */
                if (has_inner_continue) {
                    int latch_lid = ++max_label;
                    for (int j = nc; j < nc + body_len; j++) {
                        if ((out[j].op == IR_JMP || out[j].op == IR_JZ
                             || out[j].op == IR_JNZ)
                            && out[j].dest.label_id == target_lid) {
                            out[j].dest.label_id = latch_lid;
                        }
                    }
                    nc += body_len;
                    /* Insert latch label before bottom condition */
                    memset(&out[nc], 0, sizeof(IRInstr));
                    out[nc].op = IR_LABEL;
                    out[nc].dest.kind = OPER_LABEL;
                    out[nc].dest.label_id = latch_lid;
                    nc++;
                } else {
                    nc += body_len;
                }
            }

            /* [bottom condition]: cond instrs + inverted branch */
            memcpy(&out[nc], bottom, (size_t)cond_len * sizeof(IRInstr));
            nc += cond_len;

            /* If exit label not right after JMP, add JMP to exit */
            if (need_exit_jmp) {
                memset(&out[nc], 0, sizeof(IRInstr));
                out[nc].op = IR_JMP;
                out[nc].dest.kind = OPER_LABEL;
                out[nc].dest.label_id = exit_lid;
                out[nc].loc = fn->instrs[i].loc;
                nc++;
            }

            /* Everything after the back-edge JMP */
            for (int j = i + 1; j < fn->instr_count; j++)
                out[nc++] = fn->instrs[j];

            free(guard);
            free(bottom);
            /* Note: fn->instrs may be arena-allocated, so don't free it.
             * The arena will reclaim the memory at program exit. */

            fn->instrs      = out;
            fn->instr_count  = nc;
            fn->instr_cap    = new_cap;

            changed = true;
            break; /* restart scan with updated instruction stream */
        }
    }

    free(label_pos);
}

void opt_loop_invert(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        loop_invert_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        loop_invert_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Loop Unrolling (2×)
 *
 * For small loops (body < UNROLL_MAX_BODY instructions), the loop
 * body is duplicated once per iteration to halve back-edge overhead.
 * Internal labels are remapped in the copy; exit jumps keep their
 * original targets so the loop can exit at any point.
 * ═════════════════════════════════════════════════════════════ */

#define UNROLL_MAX_BODY 80

static void unroll_func(IRFunc *fn)
{
    if (fn->instr_count < 4) return;

    int max_label = func_max_label(fn);
    if (max_label < 0) max_label = 0;

    int *label_pos = (int *)xmalloc((size_t)(max_label + 2) * sizeof(int));
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
        int *label_remap = (int *)xmalloc((size_t)remap_cap * sizeof(int));
        for (int k = 0; k < remap_cap; k++) label_remap[k] = k;

        for (int j = body_start; j <= body_end; j++) {
            if (fn->instrs[j].op == IR_LABEL) {
                int lid = fn->instrs[j].dest.label_id;
                if (lid >= 0 && lid < remap_cap)
                    label_remap[lid] = next_label++;
            }
        }

        /* Build temp remap: temps DEFined in body get fresh IDs */
        int tremap_cap = fn->temp_count;
        int *temp_remap = (int *)xmalloc((size_t)tremap_cap * sizeof(int));
        for (int k = 0; k < tremap_cap; k++) temp_remap[k] = -1;
        for (int j = body_start; j <= body_end; j++) {
            if (fn->instrs[j].dest.kind == OPER_TEMP) {
                int tid = fn->instrs[j].dest.temp_id;
                if (tid >= 0 && tid < tremap_cap && temp_remap[tid] < 0)
                    temp_remap[tid] = fn->temp_count++;
            }
        }

        /* Build duplicated body with remapped labels and temps */
        IRInstr *dup = (IRInstr *)xcalloc((size_t)body_len, sizeof(IRInstr));
        for (int j = 0; j < body_len; j++) {
            dup[j] = fn->instrs[body_start + j];
            /* Remap labels */
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
            /* Remap temps */
            if (dup[j].dest.kind == OPER_TEMP) {
                int tid = dup[j].dest.temp_id;
                if (tid >= 0 && tid < tremap_cap && temp_remap[tid] >= 0)
                    dup[j].dest.temp_id = temp_remap[tid];
            }
            if (dup[j].src1.kind == OPER_TEMP) {
                int tid = dup[j].src1.temp_id;
                if (tid >= 0 && tid < tremap_cap && temp_remap[tid] >= 0)
                    dup[j].src1.temp_id = temp_remap[tid];
            }
            if (dup[j].src2.kind == OPER_TEMP) {
                int tid = dup[j].src2.temp_id;
                if (tid >= 0 && tid < tremap_cap && temp_remap[tid] >= 0)
                    dup[j].src2.temp_id = temp_remap[tid];
            }
        }

        free(label_remap);
        free(temp_remap);

        /* Insert duplicated body before the back-edge JMP */
        int new_cap = fn->instr_count + body_len + 4;
        IRInstr *new_instrs = (IRInstr *)xmalloc(
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

