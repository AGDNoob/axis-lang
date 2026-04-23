#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

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
           (ins->op == IR_MEMCPY      && oper_reads_temp(&ins->dest, tid)) ||
           /* CMOV reads dest as the default value (conditional write) */
           (ins->op == IR_CMOV        && oper_reads_temp(&ins->dest, tid));
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
 * Dead Store Elimination
 *
 * Removes STORE_VAR instructions to stack slots that are never
 * read by any LOAD_VAR in the same function.  Also eliminates
 * a STORE_VAR if the same slot is stored again before any
 * LOAD_VAR/branch within the same basic block.
 * ═════════════════════════════════════════════════════════════ */

static void dse_func(IRFunc *fn)
{
    if (fn->instr_count == 0) return;

    /* Pass 1: collect the set of stack slots that are actually loaded */
    int max_off = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_STORE_VAR || fn->instrs[i].op == IR_LOAD_VAR) {
            int off = (fn->instrs[i].op == IR_STORE_VAR)
                      ? fn->instrs[i].dest.stack_off
                      : fn->instrs[i].src1.stack_off;
            if (off < 0) off = -off;
            if (off > max_off) max_off = off;
        }
    }
    if (max_off == 0) return;

    int slot_count = max_off / 8 + 2;
    bool *loaded = (bool *)xcalloc((size_t)slot_count, sizeof(bool));

    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LOAD_VAR) {
            int off = fn->instrs[i].src1.stack_off;
            if (off < 0) off = -off;
            int idx = off / 8;
            if (idx < slot_count) loaded[idx] = true;
            /* 8-byte load covers two 4-byte sub-slots */
            if (fn->instrs[i].src1.size >= 8 && off >= 4) {
                int idx2 = (off - 4) / 8;
                if (idx2 < slot_count) loaded[idx2] = true;
            }
        }
        /* LEA of a stack slot: address escapes to a temp, so any
         * memory reachable through that pointer may be read later.
         * Conservatively mark all slots as loaded. */
        if (fn->instrs[i].op == IR_LEA &&
            fn->instrs[i].src1.kind == OPER_STACK) {
            for (int s = 0; s < slot_count; s++)
                loaded[s] = true;
        }
        /* idx_load/idx_store access array elements via base + index*esz.
         * Individual elements were stored via store_var to consecutive
         * offsets; mark those slots as loaded so DSE doesn't kill them. */
        if (fn->instrs[i].op == IR_INDEX_LOAD ||
            fn->instrs[i].op == IR_FIELD_LOAD) {
            if (fn->instrs[i].src1.kind == OPER_STACK) {
                int off = fn->instrs[i].src1.stack_off;
                if (off < 0) off = -off;
                int top = off / 8;
                for (int s = 0; s <= top && s < slot_count; s++)
                    loaded[s] = true;
            }
        }
        if (fn->instrs[i].op == IR_INDEX_STORE ||
            fn->instrs[i].op == IR_FIELD_STORE) {
            if (fn->instrs[i].dest.kind == OPER_STACK) {
                int off = fn->instrs[i].dest.stack_off;
                if (off < 0) off = -off;
                int top = off / 8;
                for (int s = 0; s <= top && s < slot_count; s++)
                    loaded[s] = true;
            }
        }
        if (fn->instrs[i].op == IR_STORE_IND ||
            fn->instrs[i].op == IR_MEMCPY) {
            for (int s = 0; s < slot_count; s++)
                loaded[s] = true;
        }
    }

    /* Pass 2: kill STORE_VAR to never-loaded slots */
    bool changed = false;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_STORE_VAR) {
            int off = fn->instrs[i].dest.stack_off;
            if (off < 0) off = -off;
            int idx = off / 8;
            if (idx < slot_count && !loaded[idx]) {
                fn->instrs[i].op = IR_NOP;
                changed = true;
            }
        }
    }

    free(loaded);

    /* Pass 3: within each basic block, if the same slot is stored
     * twice with no intervening load, the earlier store is dead. */
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_STORE_VAR) continue;
        int off = fn->instrs[i].dest.stack_off;

        for (int j = i + 1; j < fn->instr_count; j++) {
            IROpcode op2 = fn->instrs[j].op;

            /* Basic-block boundary — stop */
            if (op2 == IR_LABEL || op2 == IR_JMP || op2 == IR_JZ ||
                op2 == IR_JNZ  || op2 == IR_RET || op2 == IR_RET_VOID)
                break;

            /* Slot is read — the store is needed.
             * Use range overlap: a load at loff with size lsz reads
             * bytes [loff, loff+lsz), the store wrote [off, off+ssz). */
            if (op2 == IR_LOAD_VAR) {
                int loff = fn->instrs[j].src1.stack_off;
                int lsz  = fn->instrs[j].src1.size;
                int ssz  = fn->instrs[i].dest.size;
                if (off < loff + lsz && loff < off + ssz)
                    break;
            }

            /* Indexed/field/indirect memory ops may alias this slot */
            if (op2 == IR_INDEX_LOAD  || op2 == IR_INDEX_STORE ||
                op2 == IR_FIELD_LOAD  || op2 == IR_FIELD_STORE ||
                op2 == IR_STORE_IND   || op2 == IR_MEMCPY)
                break;

            /* Same slot is stored again — first store is dead */
            if (op2 == IR_STORE_VAR && fn->instrs[j].dest.stack_off == off) {
                fn->instrs[i].op = IR_NOP;
                changed = true;
                break;
            }
        }
    }

    if (!changed) return;

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

void opt_dead_store(IRProgram *ir)
{
    for (int i = 0; i < ir->func_count; i++)
        dse_func(&ir->funcs[i]);
    if (ir->top_level.instr_count > 0)
        dse_func(&ir->top_level);
}

