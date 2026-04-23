#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

/* ═════════════════════════════════════════════════════════════
 * Register Promotion for Loops (with Deferred Writeback)
 *
 * For each innermost loop, identifies stack slots that are both
 * loaded and stored inside the loop body.  Allocates a "home"
 * virtual register for each such slot:
 *   – inserts  load_var t_home, [slot]   before the loop header
 *   – renames in-loop  load_var tN, [slot]  by substituting tN → t_home
 *     everywhere (eliminating the load and the intermediate MOV)
 *   – removes in-loop  store_var [slot], tM  (deferred to loop exit)
 *   – inserts  mov t_home, tM  after each removed store
 *   – at each loop-exit conditional jump, redirects to a writeback
 *     block that stores all home temps to their slots
 *
 * Loops containing calls, indirect stores, or memcpy are skipped
 * because those operations may alias with stack slots.
 * ═════════════════════════════════════════════════════════════ */

#define RP_MAX_SLOTS 32

typedef struct {
    int stack_off;
    int size;
    int home_temp;
} RPSlot;

static void regpromote_func(IRFunc *fn)
{
    if (fn->instr_count < 4) return;

    /* ── Build label → position map ───────────────── */
    int max_label = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL) {
            int lid = fn->instrs[i].dest.label_id;
            if (lid > max_label) max_label = lid;
        }
    }

    int *lpos = (int *)xmalloc((size_t)(max_label + 2) * sizeof(int));
    if (!lpos) return;
    for (int i = 0; i <= max_label; i++) lpos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            lpos[fn->instrs[i].dest.label_id] = i;
    }

    /* ── Find innermost loops (backward unconditional JMP) ── */
    typedef struct { int hdr, be; } Loop;
    int loop_cap = 32;
    Loop *loops = (Loop *)xmalloc((size_t)loop_cap * sizeof(Loop));
    if (!loops) { free(lpos); return; }
    int lc = 0;

    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_JMP) continue;
        int tid = fn->instrs[i].dest.label_id;
        if (tid < 0 || tid > max_label) continue;
        int h = lpos[tid];
        if (h < 0 || h >= i) continue; /* not a back-edge */

        /* Skip non-innermost: if a nested back-edge exists in [h..i] */
        bool inner = true;
        for (int j = h + 1; j < i && inner; j++) {
            if (fn->instrs[j].op == IR_JMP) {
                int t2 = fn->instrs[j].dest.label_id;
                if (t2 >= 0 && t2 <= max_label &&
                    lpos[t2] >= h && lpos[t2] < j)
                    inner = false;
            }
        }
        if (!inner) continue;
        if (lc >= loop_cap) {
            loop_cap *= 2;
            loops = (Loop *)xrealloc(loops, (size_t)loop_cap * sizeof(Loop));
        }
        loops[lc].hdr = h; loops[lc].be = i; lc++;
    }

    if (lc == 0) { free(loops); free(lpos); return; }

    /* ── Per-loop: collect promotable slots ────────── */
    RPSlot *all_slots = (RPSlot *)xmalloc((size_t)lc * RP_MAX_SLOTS * sizeof(RPSlot));
    int total_slots = 0;
    int *loop_sstart = (int *)xmalloc((size_t)lc * sizeof(int));
    int *loop_scnt   = (int *)xmalloc((size_t)lc * sizeof(int));
    if (!all_slots || !loop_sstart || !loop_scnt) {
        free(all_slots); free(loop_sstart); free(loop_scnt);
        free(loops); free(lpos); return;
    }

    for (int li = 0; li < lc; li++) {
        loop_sstart[li] = total_slots;
        loop_scnt[li]   = 0;
        int H = loops[li].hdr, B = loops[li].be;

        /* Bail if loop body contains calls or indirect stores */
        bool unsafe = false;
        for (int i = H; i <= B && !unsafe; i++) {
            IROpcode op = fn->instrs[i].op;
            if (op == IR_CALL || op == IR_STORE_IND ||
                op == IR_INDEX_STORE || op == IR_FIELD_STORE ||
                op == IR_MEMCPY)
                unsafe = true;
        }
        if (unsafe) continue;

        /* Scan for stack slots that are loaded and stored */
        struct { int off, sz; bool ld, st; } found[RP_MAX_SLOTS];
        int nf = 0;

        for (int i = H + 1; i < B; i++) {
            IRInstr *ins = &fn->instrs[i];
            int off = 0; int sz = 0;
            bool is_ld = false, is_st = false;

            if (ins->op == IR_LOAD_VAR && ins->dest.kind == OPER_TEMP) {
                off = ins->src1.stack_off;
                sz  = ins->dest.size;
                is_ld = true;
            } else if (ins->op == IR_STORE_VAR) {
                off = ins->dest.stack_off;
                sz  = ins->dest.size;
                is_st = true;
            } else {
                continue;
            }

            int idx = -1;
            for (int s = 0; s < nf; s++)
                if (found[s].off == off) { idx = s; break; }
            if (idx < 0) {
                if (nf >= RP_MAX_SLOTS) continue;
                idx = nf++;
                found[idx].off = off;
                found[idx].sz  = sz;
                found[idx].ld  = false;
                found[idx].st  = false;
            }
            if (is_ld) found[idx].ld = true;
            if (is_st) found[idx].st = true;
        }

        /* Keep slots that are both loaded and stored */
        for (int s = 0; s < nf; s++) {
            if (found[s].ld && found[s].st &&
                total_slots < 32 * RP_MAX_SLOTS) {
                all_slots[total_slots].stack_off = found[s].off;
                all_slots[total_slots].size      = found[s].sz;
                all_slots[total_slots].home_temp = fn->temp_count++;
                total_slots++;
                loop_scnt[li]++;
            }
        }
    }

    if (total_slots == 0) { free(all_slots); free(loop_sstart); free(loop_scnt); free(loops); free(lpos); return; }

    /* ── Find loop-exit conditional jumps ──────────── */
    typedef struct { int loop_idx, instr_idx, orig_label, wb_label; } WBExit;
    int exit_cap = 64;
    WBExit *exits = (WBExit *)xmalloc((size_t)exit_cap * sizeof(WBExit));
    if (!exits) { free(all_slots); free(loop_sstart); free(loop_scnt); free(loops); free(lpos); return; }
    int exit_count = 0;
    int next_label = max_label + 1;

    for (int li = 0; li < lc; li++) {
        if (loop_scnt[li] == 0) continue;
        int H = loops[li].hdr, B = loops[li].be;
        for (int i = H + 1; i < B; i++) {
            IROpcode op = fn->instrs[i].op;
            if (op != IR_JZ && op != IR_JNZ && op != IR_JMP) continue;
            if (op == IR_JMP && i == B) continue; /* back-edge, not exit */
            int target = fn->instrs[i].dest.label_id;
            if (target < 0 || target > max_label) continue;
            int tpos = lpos[target];
            if (tpos >= H && tpos <= B) continue; /* inside loop */
            if (exit_count >= exit_cap) {
                exit_cap *= 2;
                exits = (WBExit *)xrealloc(exits, (size_t)exit_cap * sizeof(WBExit));
            }
            exits[exit_count].loop_idx   = li;
            exits[exit_count].instr_idx  = i;
            exits[exit_count].orig_label = target;
            exits[exit_count].wb_label   = next_label++;
            exit_count++;
        }
    }

    /* ── Rename table: load-dest temps → home temps ── */
    int rename_cap = fn->temp_count;
    int *rn = (int *)xmalloc((size_t)rename_cap * sizeof(int));
    if (!rn) { free(exits); free(all_slots); free(loop_sstart); free(loop_scnt); free(loops); free(lpos); return; }
    for (int k = 0; k < rename_cap; k++) rn[k] = k;

    /* ── Count extra instructions to insert ───────── */
    int extra = 0;
    for (int li = 0; li < lc; li++) {
        extra += loop_scnt[li]; /* pre-loop loads */
        int base = loop_sstart[li], cnt = loop_scnt[li];
        for (int i = loops[li].hdr + 1; i < loops[li].be; i++) {
            if (fn->instrs[i].op != IR_STORE_VAR) continue;
            for (int s = 0; s < cnt; s++) {
                if (all_slots[base + s].stack_off ==
                    fn->instrs[i].dest.stack_off)
                    { extra++; break; }
            }
        }
    }
    /* writeback blocks: label + stores + jmp per exit */
    for (int e = 0; e < exit_count; e++)
        extra += loop_scnt[exits[e].loop_idx] + 2;

    /* ── Build new instruction array ──────────────── */
    int cap = fn->instr_count + extra + 16;
    IRInstr *out = (IRInstr *)xmalloc((size_t)cap * sizeof(IRInstr));
    int nc = 0;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        /* ── Before loop header: insert pre-loop loads ── */
        for (int li = 0; li < lc; li++) {
            if (i != loops[li].hdr) continue;
            int base = loop_sstart[li], cnt = loop_scnt[li];
            for (int s = 0; s < cnt; s++) {
                IRInstr ld = {0};
                ld.op             = IR_LOAD_VAR;
                ld.dest.kind      = OPER_TEMP;
                ld.dest.temp_id   = all_slots[base + s].home_temp;
                ld.dest.size      = all_slots[base + s].size;
                ld.src1.kind      = OPER_STACK;
                ld.src1.stack_off = all_slots[base + s].stack_off;
                ld.src1.size      = all_slots[base + s].size;
                out[nc++] = ld;
            }
        }

        /* ── In-loop load_var → convert to MOV from home temp ─ */
        bool skip = false;
        if (ins->op == IR_LOAD_VAR && ins->dest.kind == OPER_TEMP) {
            for (int li = 0; li < lc; li++) {
                if (i <= loops[li].hdr || i >= loops[li].be) continue;
                int base = loop_sstart[li], cnt = loop_scnt[li];
                for (int s = 0; s < cnt; s++) {
                    if (all_slots[base + s].stack_off !=
                        ins->src1.stack_off)
                        continue;
                    /* Emit MOV origDest, homeTemp instead of
                       eliminating the load and renaming.  This
                       preserves the value at the load-point even
                       when the home temp is overwritten later
                       (parallel-copy / swap pattern). */
                    IRInstr mv = {0};
                    mv.op           = IR_MOV;
                    mv.dest         = ins->dest;
                    mv.src1.kind    = OPER_TEMP;
                    mv.src1.temp_id = all_slots[base + s].home_temp;
                    mv.src1.size    = all_slots[base + s].size;
                    mv.loc          = ins->loc;
                    out[nc++]       = mv;
                    skip = true;
                    break;
                }
                break;
            }
        }

        /* ── In-loop store_var for promoted slot → skip ── */
        if (ins->op == IR_STORE_VAR && !skip) {
            for (int li = 0; li < lc; li++) {
                if (i <= loops[li].hdr || i >= loops[li].be) continue;
                int base = loop_sstart[li], cnt = loop_scnt[li];
                for (int s = 0; s < cnt; s++) {
                    if (all_slots[base + s].stack_off ==
                        ins->dest.stack_off) {
                        skip = true;
                        break;
                    }
                }
                break;
            }
        }

        if (!skip) {
            IRInstr tmp = *ins;
            /* Redirect loop-exit jumps to writeback blocks */
            if (tmp.op == IR_JZ || tmp.op == IR_JNZ || tmp.op == IR_JMP) {
                for (int e = 0; e < exit_count; e++) {
                    if (exits[e].instr_idx == i) {
                        tmp.dest.label_id = exits[e].wb_label;
                        break;
                    }
                }
            }
            out[nc++] = tmp;
        }

        /* ── After in-loop store_var: insert mov t_home, src ── */
        if (ins->op == IR_STORE_VAR) {
            for (int li = 0; li < lc; li++) {
                if (i <= loops[li].hdr || i >= loops[li].be) continue;
                int base = loop_sstart[li], cnt = loop_scnt[li];
                for (int s = 0; s < cnt; s++) {
                    if (all_slots[base + s].stack_off !=
                        ins->dest.stack_off)
                        continue;
                    IRInstr up = {0};
                    up.loc = ins->loc;
                    if (ins->src1.kind == OPER_TEMP) {
                        up.op           = IR_MOV;
                        up.dest.kind    = OPER_TEMP;
                        up.dest.temp_id = all_slots[base + s].home_temp;
                        up.dest.size    = all_slots[base + s].size;
                        up.src1         = ins->src1;
                    } else if (ins->src1.kind == OPER_IMM) {
                        up.op           = IR_LOAD_IMM;
                        up.dest.kind    = OPER_TEMP;
                        up.dest.temp_id = all_slots[base + s].home_temp;
                        up.dest.size    = all_slots[base + s].size;
                        up.src1         = ins->src1;
                    }
                    if (up.op != 0) out[nc++] = up;
                    break;
                }
                break;
            }
        }
    }

    /* ── Post-pass: apply rename to all src operands ── */
    for (int j = 0; j < nc; j++) {
        if (out[j].src1.kind == OPER_TEMP) {
            int t = out[j].src1.temp_id;
            if (t >= 0 && t < rename_cap) out[j].src1.temp_id = rn[t];
        }
        if (out[j].src2.kind == OPER_TEMP) {
            int t = out[j].src2.temp_id;
            if (t >= 0 && t < rename_cap) out[j].src2.temp_id = rn[t];
        }
    }

    /* ── Emit writeback blocks (deferred stores at loop exits) ── */
    for (int e = 0; e < exit_count; e++) {
        int li = exits[e].loop_idx;
        int base = loop_sstart[li], cnt = loop_scnt[li];

        IRInstr lbl = {0};
        lbl.op            = IR_LABEL;
        lbl.dest.kind     = OPER_LABEL;
        lbl.dest.label_id = exits[e].wb_label;
        out[nc++] = lbl;

        for (int s = 0; s < cnt; s++) {
            IRInstr st = {0};
            st.op             = IR_STORE_VAR;
            st.dest.kind      = OPER_STACK;
            st.dest.stack_off = all_slots[base + s].stack_off;
            st.dest.size      = all_slots[base + s].size;
            st.src1.kind      = OPER_TEMP;
            st.src1.temp_id   = all_slots[base + s].home_temp;
            st.src1.size      = all_slots[base + s].size;
            out[nc++] = st;
        }

        IRInstr jm = {0};
        jm.op            = IR_JMP;
        jm.dest.kind     = OPER_LABEL;
        jm.dest.label_id = exits[e].orig_label;
        out[nc++] = jm;
    }

    fn->instrs      = out;
    fn->instr_count = nc;
    fn->instr_cap   = cap;

    free(exits);
    free(rn);
    free(all_slots);
    free(loop_sstart);
    free(loop_scnt);
    free(loops);
    free(lpos);
}

void opt_regpromote(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        regpromote_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        regpromote_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Induction Variable Strength Reduction (IVSR)
 *
 * Replaces expressions that depend linearly on a loop induction
 * variable with accumulated additions:
 *
 *   MUL result, invariant, IV  (IV step must be 1)
 *   → init:   MUL accum, invariant, IV_init   (before loop)
 *     body:   rename result → accum
 *     update: ADD accum, accum, invariant      (end of loop)
 *
 *   ADD result, invariant, IV
 *   → init:   ADD accum, invariant, IV_init   (before loop)
 *     body:   rename result → accum
 *     update: ADD accum, accum, #step          (end of loop)
 * ═════════════════════════════════════════════════════════════ */

static void ivsr_func(IRFunc *fn)
{
    if (fn->instr_count < 5) return;

    bool progress = true;
    while (progress) {
        progress = false;

        /* Build label → position map */
        int max_label = 0;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op == IR_LABEL) {
                int lid = fn->instrs[i].dest.label_id;
                if (lid > max_label) max_label = lid;
            }
        }
        int *label_pos = (int *)xcalloc((size_t)(max_label + 2), sizeof(int));
        for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op == IR_LABEL)
                label_pos[fn->instrs[i].dest.label_id] = i;
        }

        /* Find back-edges: unconditional JMP to earlier label */
        for (int i = 0; i < fn->instr_count && !progress; i++) {
            if (fn->instrs[i].op != IR_JMP) continue;
            int target_lid = fn->instrs[i].dest.label_id;
            if (target_lid < 0 || target_lid > max_label) continue;
            int header = label_pos[target_lid];
            if (header < 0 || header >= i) continue;

            int ls = header; /* loop start (label) */
            int le = i;      /* loop end   (jmp)   */

            /* Find induction variables: ADD IV, IV, #step */
            for (int j = ls + 1; j < le && !progress; j++) {
                IRInstr *iv_ins = &fn->instrs[j];
                if (iv_ins->op != IR_ADD) continue;
                if (iv_ins->dest.kind != OPER_TEMP) continue;
                if (iv_ins->src1.kind != OPER_TEMP) continue;
                if (iv_ins->src2.kind != OPER_IMM) continue;
                if (iv_ins->dest.temp_id != iv_ins->src1.temp_id) continue;

                int iv_tid = iv_ins->dest.temp_id;
                int64_t iv_step = iv_ins->src2.imm;

                /* IV must have exactly 1 def inside loop body */
                int iv_defs = 0;
                for (int k = ls + 1; k < le; k++) {
                    if (fn->instrs[k].dest.kind == OPER_TEMP &&
                        fn->instrs[k].dest.temp_id == iv_tid)
                        iv_defs++;
                }
                if (iv_defs != 1) continue;

                /* Search for MUL/ADD that uses this IV with an invariant */
                for (int k = ls + 1; k < le && !progress; k++) {
                    IRInstr *dep = &fn->instrs[k];
                    if (dep->dest.kind != OPER_TEMP) continue;
                    if (dep->dest.temp_id == iv_tid) continue;
                    if (dep->op != IR_MUL && dep->op != IR_ADD) continue;
                    if (dep->op == IR_MUL && iv_step != 1) continue;

                    /* Identify which operand is the IV */
                    IROper inv_op;
                    if (dep->src1.kind == OPER_TEMP &&
                        dep->src1.temp_id == iv_tid) {
                        inv_op = dep->src2;
                    } else if (dep->src2.kind == OPER_TEMP &&
                               dep->src2.temp_id == iv_tid) {
                        inv_op = dep->src1;
                    } else {
                        continue;
                    }

                    /* Invariant must not be defined inside the loop */
                    if (inv_op.kind == OPER_TEMP) {
                        bool def_in = false;
                        for (int m = ls + 1; m < le; m++) {
                            if (fn->instrs[m].dest.kind == OPER_TEMP &&
                                fn->instrs[m].dest.temp_id == inv_op.temp_id) {
                                def_in = true; break;
                            }
                        }
                        if (def_in) continue;
                    }

                    /* Result temp: exactly 1 def in loop, not used outside */
                    int res_tid = dep->dest.temp_id;
                    int res_defs = 0;
                    for (int m = ls + 1; m < le; m++) {
                        if (fn->instrs[m].dest.kind == OPER_TEMP &&
                            fn->instrs[m].dest.temp_id == res_tid)
                            res_defs++;
                    }
                    if (res_defs != 1) continue;

                    bool used_outside = false;
                    for (int m = 0; m < fn->instr_count; m++) {
                        if (m >= ls && m <= le) continue;
                        if ((fn->instrs[m].src1.kind == OPER_TEMP &&
                             fn->instrs[m].src1.temp_id == res_tid) ||
                            (fn->instrs[m].src2.kind == OPER_TEMP &&
                             fn->instrs[m].src2.temp_id == res_tid)) {
                            used_outside = true; break;
                        }
                    }
                    if (used_outside) continue;

                    /* === Transform === */
                    int accum = fn->temp_count++;
                    int sz = dep->dest.size;

                    int new_cap = fn->instr_count + 4;
                    IRInstr *nb = (IRInstr *)xmalloc(
                        (size_t)new_cap * sizeof(IRInstr));
                    int nc = 0;

                    for (int m = 0; m < fn->instr_count; m++) {
                        /* Insert init before loop header label */
                        if (m == ls) {
                            IRInstr init;
                            memset(&init, 0, sizeof(init));
                            init.op = dep->op; /* MUL or ADD */
                            init.dest.kind = OPER_TEMP;
                            init.dest.size = sz;
                            init.dest.temp_id = accum;
                            init.src1 = inv_op;
                            init.src2.kind = OPER_TEMP;
                            init.src2.size = sz;
                            init.src2.temp_id = iv_tid;
                            init.loc = dep->loc;
                            nb[nc++] = init;
                        }

                        if (m == k) {
                            /* NOP the original MUL/ADD */
                            IRInstr nop;
                            memset(&nop, 0, sizeof(nop));
                            nop.op = IR_NOP;
                            nb[nc++] = nop;
                        } else if (m == le) {
                            /* Insert accumulator update before JMP */
                            IRInstr upd;
                            memset(&upd, 0, sizeof(upd));
                            upd.op = IR_ADD;
                            upd.dest.kind = OPER_TEMP;
                            upd.dest.size = sz;
                            upd.dest.temp_id = accum;
                            upd.src1.kind = OPER_TEMP;
                            upd.src1.size = sz;
                            upd.src1.temp_id = accum;
                            if (dep->op == IR_MUL) {
                                upd.src2 = inv_op; /* add by invariant */
                            } else {
                                upd.src2.kind = OPER_IMM;
                                upd.src2.imm = iv_step; /* add by step */
                            }
                            upd.loc = dep->loc;
                            nb[nc++] = upd;
                            /* Copy the JMP itself */
                            nb[nc++] = fn->instrs[m];
                        } else {
                            IRInstr copy = fn->instrs[m];
                            /* Rename uses of res_tid → accum in loop */
                            if (m > ls && m < le) {
                                if (copy.src1.kind == OPER_TEMP &&
                                    copy.src1.temp_id == res_tid)
                                    copy.src1.temp_id = accum;
                                if (copy.src2.kind == OPER_TEMP &&
                                    copy.src2.temp_id == res_tid)
                                    copy.src2.temp_id = accum;
                            }
                            nb[nc++] = copy;
                        }
                    }

                    free(fn->instrs);
                    fn->instrs = nb;
                    fn->instr_count = nc;
                    fn->instr_cap = new_cap;
                    progress = true;
                }
            }
        }
        free(label_pos);
    }
}

void opt_ivsr(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        ivsr_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        ivsr_func(&ir->top_level);
}

/* ═════════════════════════════════════════════════════════════
 * Induction Variable Elimination (IVE)
 *
 * After IVSR, a loop may contain two IVs with the same step where
 * one is used only in the exit-test comparison and its own increment.
 * This pass eliminates the test-only IV by rewriting the exit test to
 * use the other IV with an adjusted bound:
 *
 *   Before:  cmp_lt cond, j, 10000    (j step +1)
 *            ...
 *            add j, j, 1
 *            add ipj, ipj, 1           (ipj step +1, used in body)
 *            jmp L_header
 *
 *   After:   new_limit = ADD 10000, (ipj - j)   (before loop)
 *            cmp_lt cond, ipj, new_limit
 *            ...
 *            NOP                                 (was: add j, j, 1)
 *            add ipj, ipj, 1
 *            jmp L_header
 * ═════════════════════════════════════════════════════════════ */

static void ive_func(IRFunc *fn)
{
    if (fn->instr_count < 5) return;

    bool progress = true;
    while (progress) {
        progress = false;

        /* Build label → position map */
        int max_label = 0;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op == IR_LABEL) {
                int lid = fn->instrs[i].dest.label_id;
                if (lid > max_label) max_label = lid;
            }
        }
        int *label_pos = (int *)xcalloc((size_t)(max_label + 2), sizeof(int));
        for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op == IR_LABEL)
                label_pos[fn->instrs[i].dest.label_id] = i;
        }

        /* Find back-edges: unconditional JMP to earlier label */
        for (int i = 0; i < fn->instr_count && !progress; i++) {
            if (fn->instrs[i].op != IR_JMP) continue;
            int target_lid = fn->instrs[i].dest.label_id;
            if (target_lid < 0 || target_lid > max_label) continue;
            int header = label_pos[target_lid];
            if (header < 0 || header >= i) continue;

            int ls = header; /* loop start (label) */
            int le = i;      /* loop end   (jmp)   */

            /* Collect IVs: ADD iv, iv, #step with exactly 1 def */
            typedef struct { int tid; int64_t step; int def_pos; } IV;
            IV ivs[32];
            int iv_count = 0;

            for (int j = ls + 1; j < le && iv_count < 32; j++) {
                IRInstr *ins = &fn->instrs[j];
                if (ins->op != IR_ADD) continue;
                if (ins->dest.kind != OPER_TEMP) continue;
                if (ins->src1.kind != OPER_TEMP) continue;
                if (ins->src2.kind != OPER_IMM) continue;
                if (ins->dest.temp_id != ins->src1.temp_id) continue;

                int tid = ins->dest.temp_id;
                int defs = 0;
                for (int k = ls + 1; k < le; k++) {
                    if (fn->instrs[k].dest.kind == OPER_TEMP &&
                        fn->instrs[k].dest.temp_id == tid)
                        defs++;
                }
                if (defs != 1) continue;

                ivs[iv_count++] = (IV){tid, ins->src2.imm, j};
            }

            if (iv_count < 2) goto next_loop;

            /* Try to find an eliminable IV (used only in exit test + increment) */
            for (int a = 0; a < iv_count && !progress; a++) {
                int elim_tid  = ivs[a].tid;
                int elim_pos  = ivs[a].def_pos;

                /* Scan for uses of elim_tid inside the loop body */
                int cmp_pos   = -1;
                bool eligible = true;

                for (int k = ls + 1; k < le; k++) {
                    IRInstr *ins = &fn->instrs[k];
                    bool uses = false;
                    if (ins->src1.kind == OPER_TEMP &&
                        ins->src1.temp_id == elim_tid) uses = true;
                    if (ins->src2.kind == OPER_TEMP &&
                        ins->src2.temp_id == elim_tid) uses = true;
                    if (!uses) continue;

                    if (k == elim_pos) continue; /* own increment */

                    if (ins->op >= IR_CMP_EQ && ins->op <= IR_CMP_GE) {
                        if (cmp_pos != -1) { eligible = false; break; }
                        cmp_pos = k;
                        continue;
                    }
                    eligible = false;
                    break;
                }
                if (!eligible || cmp_pos < 0) continue;

                /* Verify the CMP feeds a JZ/JNZ (exit test) */
                IRInstr *cmp = &fn->instrs[cmp_pos];
                if (cmp->dest.kind != OPER_TEMP) continue;
                int cmp_result = cmp->dest.temp_id;
                bool feeds_exit = false;
                for (int k = cmp_pos + 1; k <= le; k++) {
                    IRInstr *ins = &fn->instrs[k];
                    if ((ins->op == IR_JZ || ins->op == IR_JNZ) &&
                        ins->src1.kind == OPER_TEMP &&
                        ins->src1.temp_id == cmp_result) {
                        feeds_exit = true;
                        break;
                    }
                }
                if (!feeds_exit) continue;

                /* Determine which CMP operand is elim_tid and which is limit */
                int elim_is_src; /* 1 or 2 */
                IROper limit_op;
                if (cmp->src1.kind == OPER_TEMP &&
                    cmp->src1.temp_id == elim_tid) {
                    elim_is_src = 1;
                    limit_op = cmp->src2;
                } else if (cmp->src2.kind == OPER_TEMP &&
                           cmp->src2.temp_id == elim_tid) {
                    elim_is_src = 2;
                    limit_op = cmp->src1;
                } else {
                    continue;
                }

                /* Limit must be loop-invariant */
                if (limit_op.kind == OPER_TEMP) {
                    bool def_in = false;
                    for (int k = ls + 1; k < le; k++) {
                        if (fn->instrs[k].dest.kind == OPER_TEMP &&
                            fn->instrs[k].dest.temp_id == limit_op.temp_id) {
                            def_in = true; break;
                        }
                    }
                    if (def_in) continue;
                }

                /* Find a partner IV with the same step */
                int keep_tid = -1;
                for (int b = 0; b < iv_count; b++) {
                    if (b == a) continue;
                    if (ivs[b].step != ivs[a].step) continue;
                    keep_tid = ivs[b].tid;
                    break;
                }
                if (keep_tid < 0) continue;

                /* === Transform ===
                 * Insert before loop header:
                 *   t_diff      = SUB keep_tid, elim_tid
                 *   t_new_limit = ADD limit_op, t_diff
                 * Replace CMP to use keep_tid and t_new_limit.
                 * NOP the elim_tid increment.
                 */
                int t_diff = fn->temp_count++;
                int t_nlim = fn->temp_count++;
                int sz     = cmp->src1.size;

                int new_cap = fn->instr_count + 4;
                IRInstr *nb = (IRInstr *)xmalloc(
                    (size_t)new_cap * sizeof(IRInstr));
                int nc = 0;

                for (int m = 0; m < fn->instr_count; m++) {
                    /* Insert limit computation before loop header label */
                    if (m == ls) {
                        IRInstr sub_i;
                        memset(&sub_i, 0, sizeof(sub_i));
                        sub_i.op   = IR_SUB;
                        sub_i.dest = (IROper){
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = t_diff};
                        sub_i.src1 = (IROper){
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = keep_tid};
                        sub_i.src2 = (IROper){
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = elim_tid};
                        sub_i.loc  = cmp->loc;
                        nb[nc++] = sub_i;

                        IRInstr add_i;
                        memset(&add_i, 0, sizeof(add_i));
                        add_i.op   = IR_ADD;
                        add_i.dest = (IROper){
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = t_nlim};
                        add_i.src1 = (IROper){
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = t_diff};
                        add_i.src2 = limit_op;
                        add_i.loc  = cmp->loc;
                        nb[nc++] = add_i;
                    }

                    if (m == elim_pos) {
                        /* NOP the eliminated IV's increment */
                        IRInstr nop;
                        memset(&nop, 0, sizeof(nop));
                        nop.op = IR_NOP;
                        nb[nc++] = nop;
                    } else if (m == cmp_pos) {
                        /* Rewrite CMP: swap elim_tid → keep_tid,
                         *              limit_op  → t_new_limit */
                        IRInstr new_cmp = fn->instrs[m];
                        IROper keep_op = {
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = keep_tid};
                        IROper nlim_op = {
                            .kind = OPER_TEMP, .size = sz,
                            .temp_id = t_nlim};
                        if (elim_is_src == 1) {
                            new_cmp.src1 = keep_op;
                            new_cmp.src2 = nlim_op;
                        } else {
                            new_cmp.src1 = nlim_op;
                            new_cmp.src2 = keep_op;
                        }
                        nb[nc++] = new_cmp;
                    } else {
                        nb[nc++] = fn->instrs[m];
                    }
                }

                free(fn->instrs);
                fn->instrs   = nb;
                fn->instr_count = nc;
                fn->instr_cap   = new_cap;
                progress = true;
            }

        next_loop:
            ;
        }
        free(label_pos);
    }
}

void opt_ive(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        ive_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        ive_func(&ir->top_level);
}

