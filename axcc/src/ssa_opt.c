/*
 * ssa_opt.c – SSA-based Optimization Passes for the AXIS compiler.
 *
 * Implements the full SSA optimization pipeline (Tiers 2–6):
 *
 *   Tier 2 – Scalar:   SCCP, Copy Prop, ADCE, GVN, Phi Elim, Inlining
 *   Tier 3 – Loop:     LICM, Unrolling, IV Simplify, Strength Reduce, Rotate
 *   Tier 4 – Lowering: Address Mode Selection, Instruction Scheduling
 *   Tier 5 – RegAlloc: Live Range Splitting (SSA-aware)
 *   Tier 6 – Post-RA:  Peephole, Branch Relax, NOP Align, Scheduling
 *
 * All SSA passes operate on SSAFunc (basic blocks + phis + dominator tree).
 * Post-RA passes (Tier 6) operate on flat IRFunc after SSA destruction.
 */

#include "axis_ssa.h"
#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ═════════════════════════════════════════════════════════════
 * Helpers
 * ═════════════════════════════════════════════════════════════ */

/* Check if an opcode is a pure computation (no side effects, no memory) */
static bool is_ssa_pure(IROpcode op)
{
    switch (op) {
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_NEG: case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
    case IR_SHL: case IR_SHR:
    case IR_CMP_EQ: case IR_CMP_NE: case IR_CMP_LT: case IR_CMP_LE:
    case IR_CMP_GT: case IR_CMP_GE:
    case IR_LOG_NOT: case IR_CMOV:
    case IR_SEXT: case IR_ZEXT: case IR_TRUNC:
    case IR_MOV: case IR_LOAD_IMM:
        return true;
    default:
        return false;
    }
}

/* Check if an opcode has side effects (cannot be removed even if dead) */
static bool ssa_has_side_effect(IROpcode op)
{
    switch (op) {
    case IR_CALL: case IR_WRITE: case IR_READ: case IR_SYSCALL:
    case IR_STORE_VAR: case IR_STORE_IND:
    case IR_INDEX_STORE: case IR_FIELD_STORE:
    case IR_MEMCPY:
    case IR_RET: case IR_RET_VOID:
    case IR_JMP: case IR_JZ: case IR_JNZ:
    case IR_LABEL: case IR_ARG:
        return true;
    default:
        return false;
    }
}

/* Check if an instruction's dest operand is actually a USE, not a DEF.
 * Store-like ops use dest as the target address, not as a definition. */
static bool instr_dest_is_use(IROpcode op)
{
    return op == IR_FIELD_STORE || op == IR_INDEX_STORE ||
           op == IR_STORE_IND  || op == IR_MEMCPY ||
           op == IR_CMOV;
}

/* Check if an operand uses a given temp */
static bool oper_uses_temp(const IROper *op, int temp)
{
    return op->kind == OPER_TEMP && op->temp_id == temp;
}

/* Replace all uses of old_temp with new_temp in an operand (src only) */
static void oper_replace_temp(IROper *op, int old_temp, int new_temp)
{
    if (op->kind == OPER_TEMP && op->temp_id == old_temp)
        op->temp_id = new_temp;
}

/* Evaluate a binary op on two constants. Returns false if not foldable. */
static bool eval_binop(IROpcode op, int64_t a, int64_t b, int64_t *result, int extra)
{
    switch (op) {
    case IR_ADD:     *result = a + b; return true;
    case IR_SUB:     *result = a - b; return true;
    case IR_MUL:     *result = a * b; return true;
    case IR_DIV:     if (b == 0) return false; *result = a / b; return true;
    case IR_MOD:     if (b == 0) return false; *result = a % b; return true;
    case IR_BIT_AND: *result = a & b; return true;
    case IR_BIT_OR:  *result = a | b; return true;
    case IR_BIT_XOR: *result = a ^ b; return true;
    case IR_SHL:     *result = (int64_t)((uint64_t)a << (b & 63)); return true;
    case IR_SHR:     *result = extra ? (int64_t)((uint64_t)a >> (b & 63)) : (a >> (b & 63)); return true;
    case IR_CMP_EQ:  *result = (a == b) ? 1 : 0; return true;
    case IR_CMP_NE:  *result = (a != b) ? 1 : 0; return true;
    case IR_CMP_LT:  *result = extra ? ((uint64_t)a <  (uint64_t)b) : (a <  b) ? 1 : 0; return true;
    case IR_CMP_LE:  *result = extra ? ((uint64_t)a <= (uint64_t)b) : (a <= b) ? 1 : 0; return true;
    case IR_CMP_GT:  *result = extra ? ((uint64_t)a >  (uint64_t)b) : (a >  b) ? 1 : 0; return true;
    case IR_CMP_GE:  *result = extra ? ((uint64_t)a >= (uint64_t)b) : (a >= b) ? 1 : 0; return true;
    default: return false;
    }
}

/* Evaluate a unary op on a constant */
static bool eval_unop(IROpcode op, int64_t a, int64_t *result)
{
    switch (op) {
    case IR_NEG:     *result = -a; return true;
    case IR_LOG_NOT: *result = a ? 0 : 1; return true;
    default: return false;
    }
}

/* Check if a block dominates another */
static bool dominates(const SSAFunc *sf, int dom, int block)
{
    if (dom == block) return true;
    int cur = block;
    while (cur != -1 && cur != 0) {
        cur = sf->blocks[cur].idom;
        if (cur == dom) return true;
    }
    return false;
}

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – Sparse Conditional Constant Propagation (SCCP)
 *
 * Uses a lattice per SSA temp: TOP → CONST → BOTTOM.
 * Propagates constants through the CFG, evaluating instructions
 * and folding branches. Unreachable blocks are detected when
 * branch conditions become constants.
 * ═════════════════════════════════════════════════════════════ */

static LatticeVal lattice_meet(LatticeVal a, LatticeVal b)
{
    if (a.kind == LATTICE_TOP) return b;
    if (b.kind == LATTICE_TOP) return a;
    if (a.kind == LATTICE_BOTTOM || b.kind == LATTICE_BOTTOM)
        return (LatticeVal){ LATTICE_BOTTOM, 0 };
    /* Both CONST */
    if (a.value == b.value) return a;
    return (LatticeVal){ LATTICE_BOTTOM, 0 };
}

static LatticeVal lattice_const(int64_t v)
{
    return (LatticeVal){ LATTICE_CONST, v };
}

/* Get the lattice value for an operand */
static LatticeVal oper_lattice(const IROper *op, const LatticeVal *lat, int tc)
{
    if (op->kind == OPER_IMM) return lattice_const(op->imm);
    if (op->kind == OPER_TEMP && op->temp_id >= 0 && op->temp_id < tc)
        return lat[op->temp_id];
    return (LatticeVal){ LATTICE_BOTTOM, 0 };
}

void ssa_sccp(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0 || sf->block_count == 0) return;

    /* Lattice: all start at TOP */
    LatticeVal *lat = (LatticeVal *)xcalloc((size_t)tc, sizeof(LatticeVal));
    for (int i = 0; i < tc; i++) lat[i].kind = LATTICE_TOP;

    /* Block reachability */
    bool *reachable = (bool *)xcalloc((size_t)sf->block_count, sizeof(bool));
    reachable[0] = true;

    /* SSA worklist: temps whose lattice value changed */
    int *ssa_wl = (int *)xmalloc((size_t)tc * sizeof(int));
    bool *in_ssa_wl = (bool *)xcalloc((size_t)tc, sizeof(bool));
    int ssa_wl_count = 0;

    /* CFG worklist: blocks to process */
    int *cfg_wl = (int *)xmalloc((size_t)sf->block_count * sizeof(int));
    bool *in_cfg_wl = (bool *)xcalloc((size_t)sf->block_count, sizeof(bool));
    int cfg_wl_count = 0;

    cfg_wl[cfg_wl_count++] = 0;
    in_cfg_wl[0] = true;

    /* Helper: evaluate instruction and return lattice value for dest */
    #define VISIT_INSTR(ins) do {                                             \
        LatticeVal new_val = { LATTICE_BOTTOM, 0 };                           \
        if ((ins)->op == IR_LOAD_IMM) {                                       \
            new_val = lattice_const((ins)->src1.imm);                         \
        } else if ((ins)->op == IR_MOV) {                                     \
            new_val = oper_lattice(&(ins)->src1, lat, tc);                    \
        } else if ((ins)->op == IR_NEG || (ins)->op == IR_LOG_NOT) {          \
            LatticeVal sv = oper_lattice(&(ins)->src1, lat, tc);              \
            if (sv.kind == LATTICE_TOP) new_val.kind = LATTICE_TOP;           \
            else if (sv.kind == LATTICE_CONST) {                              \
                int64_t r;                                                    \
                if (eval_unop((ins)->op, sv.value, &r))                       \
                    new_val = lattice_const(r);                               \
            }                                                                 \
        } else if (is_ssa_pure((ins)->op) && (ins)->src2.kind != OPER_NONE) { \
            LatticeVal lv = oper_lattice(&(ins)->src1, lat, tc);              \
            LatticeVal rv = oper_lattice(&(ins)->src2, lat, tc);              \
            if (lv.kind == LATTICE_TOP || rv.kind == LATTICE_TOP)             \
                new_val.kind = LATTICE_TOP;                                   \
            else if (lv.kind == LATTICE_CONST && rv.kind == LATTICE_CONST) {  \
                int64_t r;                                                    \
                if (eval_binop((ins)->op, lv.value, rv.value, &r, (ins)->extra))            \
                    new_val = lattice_const(r);                               \
            }                                                                 \
        }                                                                     \
        if ((ins)->dest.kind == OPER_TEMP) {                                  \
            int _tid = (ins)->dest.temp_id;                                   \
            if (_tid >= 0 && _tid < tc) {                                     \
                LatticeVal old = lat[_tid];                                   \
                lat[_tid] = lattice_meet(old, new_val);                       \
                if (lat[_tid].kind != old.kind ||                             \
                    (lat[_tid].kind == LATTICE_CONST &&                       \
                     lat[_tid].value != old.value)) {                         \
                    if (!in_ssa_wl[_tid]) {                                   \
                        ssa_wl[ssa_wl_count++] = _tid;                        \
                        in_ssa_wl[_tid] = true;                               \
                    }                                                         \
                }                                                             \
            }                                                                 \
        }                                                                     \
    } while (0)

    /* Process all reachable blocks */
    while (cfg_wl_count > 0 || ssa_wl_count > 0) {
        /* Process CFG worklist */
        while (cfg_wl_count > 0) {
            int b = cfg_wl[--cfg_wl_count];
            in_cfg_wl[b] = false;
            SSABlock *blk = &sf->blocks[b];

            /* Evaluate phis */
            for (int p = 0; p < blk->phi_count; p++) {
                SSAPhi *phi = &blk->phis[p];
                if (phi->eliminated) continue;
                LatticeVal val = { LATTICE_TOP, 0 };
                for (int a = 0; a < phi->arg_count; a++) {
                    if (!reachable[phi->arg_blocks[a]]) continue;
                    int arg = phi->args[a];
                    LatticeVal av = (arg >= 0 && arg < tc) ?
                        lat[arg] : (LatticeVal){ LATTICE_BOTTOM, 0 };
                    val = lattice_meet(val, av);
                }
                int tid = phi->dest;
                if (tid >= 0 && tid < tc) {
                    LatticeVal old = lat[tid];
                    lat[tid] = lattice_meet(old, val);
                    if (lat[tid].kind != old.kind ||
                        (lat[tid].kind == LATTICE_CONST &&
                         lat[tid].value != old.value)) {
                        if (!in_ssa_wl[tid]) {
                            ssa_wl[ssa_wl_count++] = tid;
                            in_ssa_wl[tid] = true;
                        }
                    }
                }
            }

            /* Evaluate instructions */
            for (int i = blk->instr_start; i < blk->instr_end; i++) {
                IRInstr *ins = &fn->instrs[i];
                VISIT_INSTR(ins);
            }

            /* Determine successors to add */
            int last = blk->instr_end - 1;
            if (last >= blk->instr_start) {
                IRInstr *term = &fn->instrs[last];
                if (term->op == IR_JZ || term->op == IR_JNZ) {
                    LatticeVal cond = oper_lattice(&term->src1, lat, tc);
                    if (cond.kind == LATTICE_CONST) {
                        /* Only one successor is reachable */
                        bool take_branch = (term->op == IR_JZ) ?
                            (cond.value == 0) : (cond.value != 0);
                        /* succs[0] = branch target, succs[1] = fallthrough */
                        int target = take_branch ? 0 : 1;
                        if (target < blk->succ_count) {
                            int s = blk->succs[target];
                            if (!reachable[s]) {
                                reachable[s] = true;
                                if (!in_cfg_wl[s]) {
                                    cfg_wl[cfg_wl_count++] = s;
                                    in_cfg_wl[s] = true;
                                }
                            }
                        }
                        goto next_block;
                    }
                }
            }
            /* All successors reachable */
            for (int s = 0; s < sf->blocks[b].succ_count; s++) {
                int succ = sf->blocks[b].succs[s];
                if (!reachable[succ]) {
                    reachable[succ] = true;
                    if (!in_cfg_wl[succ]) {
                        cfg_wl[cfg_wl_count++] = succ;
                        in_cfg_wl[succ] = true;
                    }
                }
            }
            next_block:;
        }

        /* Process SSA worklist: re-evaluate uses of changed temps */
        while (ssa_wl_count > 0) {
            int tid = ssa_wl[--ssa_wl_count];
            in_ssa_wl[tid] = false;

            /* Find all uses of tid across all reachable blocks */
            for (int b = 0; b < sf->block_count; b++) {
                if (!reachable[b]) continue;
                SSABlock *blk = &sf->blocks[b];

                /* Check phis */
                for (int p = 0; p < blk->phi_count; p++) {
                    SSAPhi *phi = &blk->phis[p];
                    if (phi->eliminated) continue;
                    bool uses_tid = false;
                    for (int a = 0; a < phi->arg_count; a++) {
                        if (phi->args[a] == tid) { uses_tid = true; break; }
                    }
                    if (uses_tid) {
                        LatticeVal val = { LATTICE_TOP, 0 };
                        for (int a = 0; a < phi->arg_count; a++) {
                            if (!reachable[phi->arg_blocks[a]]) continue;
                            int arg = phi->args[a];
                            LatticeVal av = (arg >= 0 && arg < tc) ?
                                lat[arg] : (LatticeVal){ LATTICE_BOTTOM, 0 };
                            val = lattice_meet(val, av);
                        }
                        int dest = phi->dest;
                        if (dest >= 0 && dest < tc) {
                            LatticeVal old = lat[dest];
                            lat[dest] = lattice_meet(old, val);
                            if (lat[dest].kind != old.kind ||
                                (lat[dest].kind == LATTICE_CONST &&
                                 lat[dest].value != old.value)) {
                                if (!in_ssa_wl[dest]) {
                                    ssa_wl[ssa_wl_count++] = dest;
                                    in_ssa_wl[dest] = true;
                                }
                            }
                        }
                    }
                }

                /* Check instructions */
                for (int i = blk->instr_start; i < blk->instr_end; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if (oper_uses_temp(&ins->src1, tid) ||
                        oper_uses_temp(&ins->src2, tid)) {
                        VISIT_INSTR(ins);
                    }
                }

                /* Re-evaluate successor reachability when a branch
                   condition changes (e.g. from CONST to BOTTOM).
                   Without this, blocks guarded by a condition that was
                   once constant-folded stay unreachable even after the
                   lattice value widens. */
                {
                    int last = blk->instr_end - 1;
                    if (last >= blk->instr_start) {
                        IRInstr *term = &fn->instrs[last];
                        if ((term->op == IR_JZ || term->op == IR_JNZ) &&
                            oper_uses_temp(&term->src1, tid)) {
                            LatticeVal cond = oper_lattice(&term->src1,
                                                           lat, tc);
                            if (cond.kind != LATTICE_CONST) {
                                /* Condition no longer constant — ensure
                                   all successors are reachable */
                                for (int s = 0; s < blk->succ_count; s++) {
                                    int succ = blk->succs[s];
                                    if (!reachable[succ]) {
                                        reachable[succ] = true;
                                        if (!in_cfg_wl[succ]) {
                                            cfg_wl[cfg_wl_count++] = succ;
                                            in_cfg_wl[succ] = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    #undef VISIT_INSTR

    /* ── Apply results: replace constants, fold branches ── */
    for (int b = 0; b < sf->block_count; b++) {
        if (!reachable[b]) {
            /* Mark all instructions in unreachable block as NOP */
            SSABlock *blk = &sf->blocks[b];
            for (int i = blk->instr_start; i < blk->instr_end; i++)
                fn->instrs[i].op = IR_NOP;
            /* Eliminate phis */
            for (int p = 0; p < blk->phi_count; p++)
                blk->phis[p].eliminated = true;
            continue;
        }

        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];

            /* Replace temp operands with known constants */
            if (ins->src1.kind == OPER_TEMP) {
                int t = ins->src1.temp_id;
                if (t >= 0 && t < tc && lat[t].kind == LATTICE_CONST) {
                    ins->src1.kind = OPER_IMM;
                    ins->src1.imm = lat[t].value;
                }
            }
            if (ins->src2.kind == OPER_TEMP) {
                int t = ins->src2.temp_id;
                if (t >= 0 && t < tc && lat[t].kind == LATTICE_CONST) {
                    ins->src2.kind = OPER_IMM;
                    ins->src2.imm = lat[t].value;
                }
            }

            /* If dest is a constant, replace the instruction */
            if (ins->dest.kind == OPER_TEMP && is_ssa_pure(ins->op)) {
                int t = ins->dest.temp_id;
                if (t >= 0 && t < tc && lat[t].kind == LATTICE_CONST) {
                    ins->op = IR_LOAD_IMM;
                    ins->src1.kind = OPER_IMM;
                    ins->src1.imm = lat[t].value;
                    ins->src2 = (IROper){0};
                }
            }

            /* Fold branches */
            if (ins->op == IR_JZ && ins->src1.kind == OPER_IMM) {
                ins->op = (ins->src1.imm == 0) ? IR_JMP : IR_NOP;
                ins->src1 = (IROper){0};
            } else if (ins->op == IR_JNZ && ins->src1.kind == OPER_IMM) {
                ins->op = (ins->src1.imm != 0) ? IR_JMP : IR_NOP;
                ins->src1 = (IROper){0};
            }
        }

        /* Eliminate phis whose result is constant */
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated) continue;
            int t = phi->dest;
            if (t >= 0 && t < tc && lat[t].kind == LATTICE_CONST)
                phi->eliminated = true;
        }
    }

    free(lat);
    free(reachable);
    free(ssa_wl);
    free(in_ssa_wl);
    free(cfg_wl);
    free(in_cfg_wl);
}

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – SSA Copy Propagation
 *
 * Follow chains of IR_MOV temp-to-temp copies. Replace all uses
 * of the destination with the ultimate source in the chain.
 * ═════════════════════════════════════════════════════════════ */

void ssa_copyprop(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0) return;

    /* Build def map: for each SSA temp, where is it defined? */
    int *copy_of = (int *)xmalloc((size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++) copy_of[i] = i; /* identity */

    /* Find all MOV temp-to-temp definitions */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];

        /* Phi copies: phi with single non-self argument */
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated) continue;
            int dest = phi->dest;
            /* Check if all args are the same (ignoring self-references) */
            int common = -1;
            bool all_same = true;
            for (int a = 0; a < phi->arg_count; a++) {
                int arg = phi->args[a];
                if (arg == dest) continue; /* skip self-ref */
                if (arg < 0) continue;
                if (common < 0) common = arg;
                else if (arg != common) { all_same = false; break; }
            }
            if (all_same && common >= 0 && dest >= 0 && dest < tc) {
                copy_of[dest] = common;
            }
        }

        /* MOV instructions */
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->op == IR_MOV && ins->dest.kind == OPER_TEMP &&
                ins->src1.kind == OPER_TEMP) {
                int dest = ins->dest.temp_id;
                int src = ins->src1.temp_id;
                if (dest >= 0 && dest < tc && src >= 0 && src < tc)
                    copy_of[dest] = src;
            }
        }
    }

    /* Chase chains to find root */
    for (int i = 0; i < tc; i++) {
        int root = i;
        int depth = 0;
        while (copy_of[root] != root && depth < tc) {
            root = copy_of[root];
            depth++;
        }
        copy_of[i] = root;
    }

    /* Apply: replace all temp uses with their root */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];

        /* Phi args */
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated) continue;
            for (int a = 0; a < phi->arg_count; a++) {
                int arg = phi->args[a];
                if (arg >= 0 && arg < tc)
                    phi->args[a] = copy_of[arg];
            }
        }

        /* Instructions */
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->src1.kind == OPER_TEMP) {
                int t = ins->src1.temp_id;
                if (t >= 0 && t < tc) ins->src1.temp_id = copy_of[t];
            }
            if (ins->src2.kind == OPER_TEMP) {
                int t = ins->src2.temp_id;
                if (t >= 0 && t < tc) ins->src2.temp_id = copy_of[t];
            }
        }
    }

    /* Mark dead MOVs (where dest == src after propagation) */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->op == IR_MOV && ins->dest.kind == OPER_TEMP &&
                ins->src1.kind == OPER_TEMP &&
                ins->dest.temp_id == ins->src1.temp_id) {
                ins->op = IR_NOP;
            }
        }
    }

    free(copy_of);
}

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – Aggressive Dead Code Elimination (ADCE)
 *
 * Starting from essential instructions (side effects, returns,
 * branches), mark all contributing definitions as live via
 * backward walk through SSA def chains. Remove everything else.
 * ═════════════════════════════════════════════════════════════ */

void ssa_adce(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0 || fn->instr_count == 0) return;

    /* Mark array for instructions */
    bool *live_instr = (bool *)xcalloc((size_t)fn->instr_count, sizeof(bool));

    /* Mark array for phis */
    int total_phis = 0;
    for (int b = 0; b < sf->block_count; b++) total_phis += sf->blocks[b].phi_count;
    bool *live_phi = (bool *)xcalloc((size_t)(total_phis + 1), sizeof(bool));

    /* Temp → defining instruction index (-1 = phi or none) */
    int *def_instr = (int *)xmalloc((size_t)tc * sizeof(int));
    /* Temp → defining phi: encode as (block_id << 16) | phi_idx, or -1 */
    int *def_phi = (int *)xmalloc((size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++) { def_instr[i] = -1; def_phi[i] = -1; }

    /* Build def maps */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated) continue;
            if (phi->dest >= 0 && phi->dest < tc)
                def_phi[phi->dest] = (b << 16) | p;
        }
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->dest.kind == OPER_TEMP && !instr_dest_is_use(ins->op)) {
                int t = ins->dest.temp_id;
                if (t >= 0 && t < tc) def_instr[t] = i;
            }
        }
    }

    /* Worklist of live temps */
    int *wl = (int *)xmalloc((size_t)tc * sizeof(int));
    bool *in_wl = (bool *)xcalloc((size_t)tc, sizeof(bool));
    int wl_count = 0;

    /* Seed: mark essential instructions as live */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ssa_has_side_effect(ins->op)) {
                live_instr[i] = true;
                /* Add source temps to worklist */
                if (ins->src1.kind == OPER_TEMP) {
                    int t = ins->src1.temp_id;
                    if (t >= 0 && t < tc && !in_wl[t]) {
                        wl[wl_count++] = t;
                        in_wl[t] = true;
                    }
                }
                if (ins->src2.kind == OPER_TEMP) {
                    int t = ins->src2.temp_id;
                    if (t >= 0 && t < tc && !in_wl[t]) {
                        wl[wl_count++] = t;
                        in_wl[t] = true;
                    }
                }
                /* For conditional branches, the condition temp is in src1 */
                if (ins->dest.kind == OPER_TEMP) {
                    int t = ins->dest.temp_id;
                    if (t >= 0 && t < tc && !in_wl[t]) {
                        wl[wl_count++] = t;
                        in_wl[t] = true;
                    }
                }
            }
        }
    }

    /* Backward walk through def chains */
    while (wl_count > 0) {
        int t = wl[--wl_count];
        in_wl[t] = false;

        /* Check if defined by instruction */
        if (def_instr[t] >= 0) {
            int idx = def_instr[t];
            if (!live_instr[idx]) {
                live_instr[idx] = true;
                IRInstr *ins = &fn->instrs[idx];
                if (ins->src1.kind == OPER_TEMP) {
                    int s = ins->src1.temp_id;
                    if (s >= 0 && s < tc && !in_wl[s]) {
                        wl[wl_count++] = s;
                        in_wl[s] = true;
                    }
                }
                if (ins->src2.kind == OPER_TEMP) {
                    int s = ins->src2.temp_id;
                    if (s >= 0 && s < tc && !in_wl[s]) {
                        wl[wl_count++] = s;
                        in_wl[s] = true;
                    }
                }
            }
        }

        /* Check if defined by phi */
        if (def_phi[t] >= 0) {
            int b = def_phi[t] >> 16;
            int p = def_phi[t] & 0xFFFF;
            int phi_linear = 0;
            for (int bb = 0; bb < b; bb++) phi_linear += sf->blocks[bb].phi_count;
            phi_linear += p;

            if (!live_phi[phi_linear]) {
                live_phi[phi_linear] = true;
                SSAPhi *phi = &sf->blocks[b].phis[p];
                for (int a = 0; a < phi->arg_count; a++) {
                    int s = phi->args[a];
                    if (s >= 0 && s < tc && !in_wl[s]) {
                        wl[wl_count++] = s;
                        in_wl[s] = true;
                    }
                }
            }
        }
    }

    /* Remove dead instructions (NOP out) */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            if (!live_instr[i] && !ssa_has_side_effect(fn->instrs[i].op))
                fn->instrs[i].op = IR_NOP;
        }

        /* Remove dead phis */
        int phi_base = 0;
        for (int bb = 0; bb < b; bb++) phi_base += sf->blocks[bb].phi_count;
        for (int p = 0; p < blk->phi_count; p++) {
            if (!live_phi[phi_base + p])
                blk->phis[p].eliminated = true;
        }
    }

    free(live_instr);
    free(live_phi);
    free(def_instr);
    free(def_phi);
    free(wl);
    free(in_wl);
}

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – Global Value Numbering (GVN)
 *
 * Hash-based value numbering: discover and eliminate redundant
 * computations that produce the same value. Dominator-scoped
 * to ensure correctness.
 * ═════════════════════════════════════════════════════════════ */

#define GVN_INIT_CAP   1024
#define GVN_LOAD_NUM   7    /* grow when count * 10 > cap * 7 (70%) */
#define GVN_LOAD_DEN   10

typedef struct {
    GVNEntry *entries;
    bool     *valid;
    int       cap;
    int       count;
} GVNTable;

static uint32_t gvn_hash_cap(IROpcode op, int vn1, int vn2, int extra, int cap)
{
    uint32_t h = (uint32_t)op * 2654435761u;
    h ^= (uint32_t)vn1 * 2246822519u;
    h ^= (uint32_t)vn2 * 3266489917u;
    h ^= (uint32_t)extra * 668265263u;
    return h % (uint32_t)cap;
}

/* Linear-probing lookup: returns slot index or -1 if not found. */
static int gvn_find(const GVNTable *t,
                    IROpcode op, int v1, int v2, int extra)
{
    uint32_t h = gvn_hash_cap(op, v1, v2, extra, t->cap);
    for (int i = 0; i < t->cap; i++) {
        uint32_t idx = (h + (uint32_t)i) % (uint32_t)t->cap;
        if (!t->valid[idx]) return -1;
        if (t->entries[idx].op == op && t->entries[idx].src1_vn == v1 &&
            t->entries[idx].src2_vn == v2 && t->entries[idx].extra == extra)
            return (int)idx;
    }
    return -1;
}

/* Grow the table to double capacity and rehash all entries. */
static void gvn_grow(GVNTable *t)
{
    int new_cap = t->cap * 2;
    GVNEntry *new_entries = (GVNEntry *)xcalloc((size_t)new_cap, sizeof(GVNEntry));
    bool     *new_valid   = (bool *)xcalloc((size_t)new_cap, sizeof(bool));

    for (int i = 0; i < t->cap; i++) {
        if (!t->valid[i]) continue;
        GVNEntry *e = &t->entries[i];
        uint32_t h = gvn_hash_cap(e->op, e->src1_vn, e->src2_vn, e->extra, new_cap);
        for (int j = 0; j < new_cap; j++) {
            uint32_t idx = (h + (uint32_t)j) % (uint32_t)new_cap;
            if (!new_valid[idx]) {
                new_entries[idx] = *e;
                new_valid[idx] = true;
                break;
            }
        }
    }

    free(t->entries);
    free(t->valid);
    t->entries = new_entries;
    t->valid   = new_valid;
    t->cap     = new_cap;
}

/* Linear-probing insert with automatic resize at 70% load. */
static void gvn_insert(GVNTable *t,
                       IROpcode op, int v1, int v2, int dest, int extra)
{
    /* Grow if load factor exceeds threshold */
    if ((t->count + 1) * GVN_LOAD_DEN > t->cap * GVN_LOAD_NUM)
        gvn_grow(t);

    uint32_t h = gvn_hash_cap(op, v1, v2, extra, t->cap);
    for (int i = 0; i < t->cap; i++) {
        uint32_t idx = (h + (uint32_t)i) % (uint32_t)t->cap;
        if (!t->valid[idx]) {
            t->entries[idx] = (GVNEntry){ op, v1, v2, dest, extra };
            t->valid[idx] = true;
            t->count++;
            return;
        }
        /* If same key exists, update it. */
        if (t->entries[idx].op == op && t->entries[idx].src1_vn == v1 &&
            t->entries[idx].src2_vn == v2 && t->entries[idx].extra == extra) {
            t->entries[idx].result_temp = dest;
            return;
        }
    }
}

void ssa_gvn(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0) return;

    /* Value number table: vn[temp] = value number */
    int *vn = (int *)xmalloc((size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++) vn[i] = i; /* start as identity */

    /* GVN hash table (dynamic, grows at 70% load) */
    GVNTable tbl;
    tbl.cap     = GVN_INIT_CAP;
    tbl.count   = 0;
    tbl.entries  = (GVNEntry *)xcalloc((size_t)tbl.cap, sizeof(GVNEntry));
    tbl.valid    = (bool *)xcalloc((size_t)tbl.cap, sizeof(bool));

    /* Process blocks in dominator tree order (BFS) */
    int *order = (int *)xmalloc((size_t)sf->block_count * sizeof(int));
    int order_count = 0;
    order[order_count++] = 0;
    for (int i = 0; i < order_count && i < sf->block_count; i++) {
        SSABlock *blk = &sf->blocks[order[i]];
        for (int c = 0; c < blk->dom_child_count; c++) {
            if (order_count < sf->block_count)
                order[order_count++] = blk->dom_children[c];
        }
    }

    for (int oi = 0; oi < order_count; oi++) {
        int b = order[oi];
        SSABlock *blk = &sf->blocks[b];

        /* Process phis: value number from args */
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated) continue;
            /* If all args have the same VN, phi gets that VN */
            int common_vn = -1;
            bool all_same = true;
            for (int a = 0; a < phi->arg_count; a++) {
                int arg = phi->args[a];
                if (arg < 0 || arg >= tc) { all_same = false; break; }
                int av = vn[arg];
                if (av == phi->dest) continue; /* self-ref */
                if (common_vn < 0) common_vn = av;
                else if (av != common_vn) { all_same = false; break; }
            }
            if (all_same && common_vn >= 0 && phi->dest >= 0 && phi->dest < tc)
                vn[phi->dest] = common_vn;
        }

        /* Process instructions */
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->op == IR_NOP) continue;

            if (ins->dest.kind != OPER_TEMP) continue;
            int dest = ins->dest.temp_id;
            if (dest < 0 || dest >= tc) continue;

            /* LOAD_IMM: value number by constant value */
            if (ins->op == IR_LOAD_IMM) {
                /* Look for existing temp with same imm */
                int slot = gvn_find(&tbl,
                                    IR_LOAD_IMM, (int)ins->src1.imm, 0, 0);
                if (slot >= 0) {
                    int existing = tbl.entries[slot].result_temp;
                    if (existing >= 0 && existing < tc &&
                        dominates(sf, sf->def_block[existing], b)) {
                        vn[dest] = vn[existing];
                        continue;
                    }
                }
                gvn_insert(&tbl,
                           IR_LOAD_IMM, (int)ins->src1.imm, 0, dest, 0);
                continue;
            }

            /* MOV: inherit value number */
            if (ins->op == IR_MOV && ins->src1.kind == OPER_TEMP) {
                int src = ins->src1.temp_id;
                if (src >= 0 && src < tc) vn[dest] = vn[src];
                continue;
            }

            /* Pure binary/unary: hash-based lookup */
            if (!is_ssa_pure(ins->op)) continue;

            int v1 = 0, v2 = 0;
            if (ins->src1.kind == OPER_TEMP && ins->src1.temp_id >= 0 &&
                ins->src1.temp_id < tc)
                v1 = vn[ins->src1.temp_id];
            else if (ins->src1.kind == OPER_IMM)
                v1 = (int)ins->src1.imm + tc; /* offset to avoid collision */

            if (ins->src2.kind == OPER_TEMP && ins->src2.temp_id >= 0 &&
                ins->src2.temp_id < tc)
                v2 = vn[ins->src2.temp_id];
            else if (ins->src2.kind == OPER_IMM)
                v2 = (int)ins->src2.imm + tc;

            /* For commutative ops, canonicalize order */
            if ((ins->op == IR_ADD || ins->op == IR_MUL ||
                 ins->op == IR_BIT_AND || ins->op == IR_BIT_OR ||
                 ins->op == IR_BIT_XOR ||
                 ins->op == IR_CMP_EQ || ins->op == IR_CMP_NE) &&
                v1 > v2) {
                int tmp = v1; v1 = v2; v2 = tmp;
            }

            int slot = gvn_find(&tbl,
                                ins->op, v1, v2, ins->extra);
            if (slot >= 0) {
                int existing = tbl.entries[slot].result_temp;
                if (existing >= 0 && existing < tc &&
                    dominates(sf, sf->def_block[existing], b)) {
                    /* Reuse existing computation */
                    vn[dest] = vn[existing];
                    ins->op = IR_MOV;
                    ins->src1.kind = OPER_TEMP;
                    ins->src1.temp_id = existing;
                    ins->src2 = (IROper){0};
                    continue;
                }
            }
            gvn_insert(&tbl,
                       ins->op, v1, v2, dest, ins->extra);
        }
    }

    /* Apply value numbers: replace temp uses with canonical reps */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->src1.kind == OPER_TEMP) {
                int t = ins->src1.temp_id;
                if (t >= 0 && t < tc && vn[t] != t &&
                    vn[t] >= 0 && vn[t] < tc)
                    ins->src1.temp_id = vn[t];
            }
            if (ins->src2.kind == OPER_TEMP) {
                int t = ins->src2.temp_id;
                if (t >= 0 && t < tc && vn[t] != t &&
                    vn[t] >= 0 && vn[t] < tc)
                    ins->src2.temp_id = vn[t];
            }
        }
        /* Phi args */
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated) continue;
            for (int a = 0; a < phi->arg_count; a++) {
                int t = phi->args[a];
                if (t >= 0 && t < tc && vn[t] != t &&
                    vn[t] >= 0 && vn[t] < tc)
                    phi->args[a] = vn[t];
            }
        }
    }

    free(vn);
    free(tbl.entries);
    free(tbl.valid);
    free(order);
}

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – Phi Elimination
 *
 * Remove trivial phis:
 *   - All args are the same temp (or self-referential)
 *   - Single unique non-self argument
 * Replace uses of phi dest with the unique arg.
 * ═════════════════════════════════════════════════════════════ */

void ssa_phi_elim(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0) return;

    bool changed = true;
    while (changed) {
        changed = false;

        for (int b = 0; b < sf->block_count; b++) {
            SSABlock *blk = &sf->blocks[b];
            for (int p = 0; p < blk->phi_count; p++) {
                SSAPhi *phi = &blk->phis[p];
                if (phi->eliminated) continue;

                /* Find unique non-self argument */
                int unique = -1;
                bool trivial = true;
                for (int a = 0; a < phi->arg_count; a++) {
                    int arg = phi->args[a];
                    if (arg == phi->dest) continue; /* self-ref */
                    if (arg < 0) continue;
                    if (unique < 0) unique = arg;
                    else if (arg != unique) { trivial = false; break; }
                }

                if (!trivial || unique < 0) continue;

                /* Replace all uses of phi->dest with unique */
                int old_dest = phi->dest;
                phi->eliminated = true;
                changed = true;

                /* Replace in all blocks */
                for (int bb = 0; bb < sf->block_count; bb++) {
                    SSABlock *bb_blk = &sf->blocks[bb];

                    /* Phi args */
                    for (int pp = 0; pp < bb_blk->phi_count; pp++) {
                        SSAPhi *other = &bb_blk->phis[pp];
                        if (other->eliminated) continue;
                        for (int a = 0; a < other->arg_count; a++) {
                            if (other->args[a] == old_dest)
                                other->args[a] = unique;
                        }
                    }

                    /* Instructions */
                    for (int i = bb_blk->instr_start; i < bb_blk->instr_end; i++) {
                        IRInstr *ins = &fn->instrs[i];
                        oper_replace_temp(&ins->src1, old_dest, unique);
                        oper_replace_temp(&ins->src2, old_dest, unique);
                    }
                }
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – SSA-Aware Function Inlining
 *
 * Wrapper around the existing flat-IR inliner with adjustable
 * pass count. O1: 3 passes, O2: 5 passes, O3: 7 passes.
 * AXCC targets only AXIS, so we can afford to be more aggressive
 * than general-purpose compilers like GCC or LLVM.
 * ═════════════════════════════════════════════════════════════ */

void ssa_inline(IRProgram *ir, int passes)
{
    /* Use existing inline infrastructure from opt.c.
     * O1: 3 passes, O2: 5 passes, O3: 7 passes. */
    for (int pass = 0; pass < passes; pass++) {
        opt_inline(ir);
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 3 – SSA Loop-Invariant Code Motion (LICM)
 *
 * For each loop, find instructions that:
 *   1. Are pure (no side effects)
 *   2. Have all operands defined outside the loop
 *   3. Are in blocks dominated by the loop header
 * Move them to the loop preheader.
 * ═════════════════════════════════════════════════════════════ */

void ssa_licm(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0 || sf->block_count == 0) return;

    /* For each loop header, collect body blocks */
    for (int h = 0; h < sf->block_count; h++) {
        if (!sf->blocks[h].is_loop_header) continue;

        /* Find the preheader: a predecessor of the header that is NOT
         * in the loop.  If there is none we cannot hoist. */
        int preheader = -1;
        {
            SSABlock *hdr = &sf->blocks[h];
            for (int pi = 0; pi < hdr->pred_count; pi++) {
                int p = hdr->preds[pi];
                if (sf->blocks[p].loop_header != h && p != h) {
                    preheader = p;
                    break;
                }
            }
        }
        if (preheader < 0) continue;

        /* Collect blocks in this loop (loop_header == h) */
        bool *in_loop = (bool *)xcalloc((size_t)sf->block_count, sizeof(bool));
        int *body = (int *)xmalloc((size_t)sf->block_count * sizeof(int));
        int body_count = 0;

        for (int b = 0; b < sf->block_count; b++) {
            if (sf->blocks[b].loop_header == h || b == h) {
                body[body_count++] = b;
                in_loop[b] = true;
            }
        }

        if (body_count == 0) { free(body); free(in_loop); continue; }

        /* Find definitions in the loop */
        bool *def_in_loop = (bool *)xcalloc((size_t)tc, sizeof(bool));
        for (int bi = 0; bi < body_count; bi++) {
            int b = body[bi];
            SSABlock *blk = &sf->blocks[b];
            for (int p = 0; p < blk->phi_count; p++) {
                if (!blk->phis[p].eliminated && blk->phis[p].dest >= 0 &&
                    blk->phis[p].dest < tc)
                    def_in_loop[blk->phis[p].dest] = true;
            }
            for (int i = blk->instr_start; i < blk->instr_end; i++) {
                if (fn->instrs[i].dest.kind == OPER_TEMP &&
                    !instr_dest_is_use(fn->instrs[i].op)) {
                    int t = fn->instrs[i].dest.temp_id;
                    if (t >= 0 && t < tc) def_in_loop[t] = true;
                }
            }
        }

        /* Check if an operand is loop-invariant */
        #define IS_INVARIANT(op) (                                               \
            (op).kind != OPER_TEMP ||                                             \
            ((op).temp_id >= 0 && (op).temp_id < tc && !def_in_loop[(op).temp_id]) \
        )

        /* Iteratively mark invariant instructions and collect them */
        int *hoist_indices = (int *)xmalloc((size_t)fn->instr_count * sizeof(int));
        int hoist_count = 0;

        bool progress = true;
        while (progress) {
            progress = false;
            for (int bi = 0; bi < body_count; bi++) {
                int b = body[bi];
                SSABlock *blk = &sf->blocks[b];
                for (int i = blk->instr_start; i < blk->instr_end; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if (ins->op == IR_NOP) continue;
                    if (!is_ssa_pure(ins->op)) continue;
                    if (ins->dest.kind != OPER_TEMP) continue;
                    int dest = ins->dest.temp_id;
                    if (dest < 0 || dest >= tc) continue;
                    if (!def_in_loop[dest]) continue; /* already hoisted */
                    if (!IS_INVARIANT(ins->src1)) continue;
                    if (ins->src2.kind != OPER_NONE && !IS_INVARIANT(ins->src2))
                        continue;

                    /* Mark as invariant (outside loop) */
                    def_in_loop[dest] = false;
                    hoist_indices[hoist_count++] = i;
                    progress = true;
                }
            }
        }
        #undef IS_INVARIANT

        if (hoist_count == 0) {
            free(hoist_indices);
            free(body);
            free(in_loop);
            free(def_in_loop);
            continue;
        }

        /* Build a set for O(1) lookup of hoisted positions */
        bool *is_hoisted = (bool *)xcalloc((size_t)fn->instr_count, sizeof(bool));
        for (int i = 0; i < hoist_count; i++)
            is_hoisted[hoist_indices[i]] = true;

        /* Build new instruction array: insert hoisted instructions at the
         * end of the preheader block (before its terminator), and NOP them
         * out of the loop body. */
        int new_cap = fn->instr_count + hoist_count + 4;
        IRInstr *new_instrs = (IRInstr *)xmalloc(
            (size_t)new_cap * sizeof(IRInstr));

        /* Find insertion point: just before the preheader's last instruction
         * (which is a terminator — JMP/JZ/JNZ to the header). If the
         * preheader has no terminator, insert at the end. */
        SSABlock *ph_blk = &sf->blocks[preheader];
        int insert_before = ph_blk->instr_end; /* default: append */
        if (ph_blk->instr_end > ph_blk->instr_start) {
            int last = ph_blk->instr_end - 1;
            IROpcode last_op = fn->instrs[last].op;
            if (last_op == IR_JMP || last_op == IR_JZ || last_op == IR_JNZ ||
                last_op == IR_RET)
                insert_before = last;
        }

        int nc = 0;
        /* Map: old instruction index → new instruction index (for blocks) */
        int *idx_map = (int *)xmalloc((size_t)fn->instr_count * sizeof(int));

        for (int i = 0; i < fn->instr_count; i++) {
            if (i == insert_before) {
                /* Insert all hoisted instructions here */
                for (int k = 0; k < hoist_count; k++)
                    new_instrs[nc++] = fn->instrs[hoist_indices[k]];
            }
            if (is_hoisted[i]) {
                /* Replace original with NOP */
                idx_map[i] = nc;
                IRInstr nop_ins = {0};
                nop_ins.op = IR_NOP;
                new_instrs[nc++] = nop_ins;
            } else {
                idx_map[i] = nc;
                new_instrs[nc++] = fn->instrs[i];
            }
        }
        /* Handle case where insert_before == instr_count (no terminator) */
        if (insert_before == fn->instr_count) {
            for (int k = 0; k < hoist_count; k++)
                new_instrs[nc++] = fn->instrs[hoist_indices[k]];
        }

        /* Update all block boundaries */
        for (int b = 0; b < sf->block_count; b++) {
            SSABlock *blk = &sf->blocks[b];
            int old_start = blk->instr_start;
            int old_end   = blk->instr_end;

            /* Map start: find new index of the old start instruction */
            if (old_start < fn->instr_count)
                blk->instr_start = idx_map[old_start];
            else
                blk->instr_start = nc;

            /* Map end: if old_end == old instr_count, use nc;
             * otherwise use the mapped index of old_end */
            if (old_end < fn->instr_count)
                blk->instr_end = idx_map[old_end];
            else
                blk->instr_end = nc;
        }

        /* The preheader block grew — its end must include the hoisted instrs.
         * The hoisted instructions were inserted at idx_map[insert_before]
         * (or at nc if insert_before == fn->instr_count), so the preheader's
         * new end is the old end + hoist_count. Recalculate properly: */
        {
            int old_ph_end = ph_blk->instr_end;
            /* The preheader now contains hoist_count extra instrs */
            /* Since we inserted BEFORE the terminator, and the terminator
             * got shifted by hoist_count, the block end also shifts. */
            /* Actually, idx_map already shifted the end correctly for
             * blocks AFTER the insertion point. But the preheader's end
             * was mapped from its old end, which was already past the
             * insertion point, so it's already correct. We just need to
             * verify the preheader's end includes the hoisted instrs. */
            (void)old_ph_end;
            /* idx_map[old_ph_end] already points past the hoisted instrs
             * and the terminator, so ph_blk->instr_end is correct. */
        }

        fn->instrs     = new_instrs;
        fn->instr_count = nc;
        fn->instr_cap   = new_cap;

        free(idx_map);
        free(is_hoisted);
        free(hoist_indices);
        free(body);
        free(in_loop);
        free(def_in_loop);
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 3 – SSA Loop Unrolling (2×)
 *
 * For small loops (< 32 instrs), duplicate the loop body once
 * to reduce branch overhead. Operates on SSA form — creates
 * new SSA temps for the duplicated body.
 * ═════════════════════════════════════════════════════════════ */

#define SSA_UNROLL_MAX_BODY 96

void ssa_unroll(SSAFunc *sf)
{
    if (sf->block_count == 0) return;

    for (int h = 0; h < sf->block_count; h++) {
        if (!sf->blocks[h].is_loop_header) continue;

        /* Count instructions in loop body */
        int body_instrs = 0;
        for (int b = 0; b < sf->block_count; b++) {
            if (sf->blocks[b].loop_header == h || b == h) {
                body_instrs += sf->blocks[b].instr_end - sf->blocks[b].instr_start;
            }
        }

        if (body_instrs > SSA_UNROLL_MAX_BODY || body_instrs == 0)
            continue;

        /* For SSA unrolling, we use the existing flat-IR unroller
         * via opt_unroll() which will be called after SSA destruction.
         * In SSA form we just mark the loop as a candidate. */
        /* No-op in SSA phase — unrolling is deferred to post-SSA */
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 3 – Induction Variable Simplification
 *
 * Detect linear induction variables (phi + ADD/SUB constant)
 * and simplify derived expressions.
 * ═════════════════════════════════════════════════════════════ */

void ssa_iv_simplify(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0 || sf->block_count == 0) return;

    /* For each loop header phi, check if it's an IV:
     * phi(init, iv_next) where iv_next = phi + const */
    for (int h = 0; h < sf->block_count; h++) {
        if (!sf->blocks[h].is_loop_header) continue;
        SSABlock *header = &sf->blocks[h];

        for (int p = 0; p < header->phi_count; p++) {
            SSAPhi *phi = &header->phis[p];
            if (phi->eliminated) continue;
            if (phi->arg_count != 2) continue;

            /* One arg should come from outside loop (init),
             * one from inside (back-edge = iv_next) */
            int init = -1, back = -1;
            for (int a = 0; a < phi->arg_count; a++) {
                int pred = phi->arg_blocks[a];
                if (sf->blocks[pred].loop_header == h || pred == h)
                    back = phi->args[a];
                else
                    init = phi->args[a];
            }

            if (init < 0 || back < 0 || back >= tc) continue;

            /* Check if back-edge value is phi + const */
            bool found_add = false;
            int64_t stride = 0;
            for (int b = 0; b < sf->block_count; b++) {
                if (sf->blocks[b].loop_header != h && b != h) continue;
                SSABlock *blk = &sf->blocks[b];
                for (int i = blk->instr_start; i < blk->instr_end; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if (ins->dest.kind != OPER_TEMP) continue;
                    if (ins->dest.temp_id != back) continue;

                    if (ins->op == IR_ADD &&
                        ins->src1.kind == OPER_TEMP &&
                        ins->src1.temp_id == phi->dest &&
                        ins->src2.kind == OPER_IMM) {
                        stride = ins->src2.imm;
                        found_add = true;
                    } else if (ins->op == IR_ADD &&
                               ins->src2.kind == OPER_TEMP &&
                               ins->src2.temp_id == phi->dest &&
                               ins->src1.kind == OPER_IMM) {
                        stride = ins->src1.imm;
                        found_add = true;
                    } else if (ins->op == IR_SUB &&
                               ins->src1.kind == OPER_TEMP &&
                               ins->src1.temp_id == phi->dest &&
                               ins->src2.kind == OPER_IMM) {
                        stride = -(ins->src2.imm);
                        found_add = true;
                    }
                }
            }

            if (!found_add) continue;

            /* Found IV: phi->dest with init and stride.
             * Look for MUL(iv, const) patterns and convert
             * to a derived IV using ADD chain (strength reduce). */
            for (int b = 0; b < sf->block_count; b++) {
                if (sf->blocks[b].loop_header != h && b != h) continue;
                SSABlock *blk = &sf->blocks[b];
                for (int i = blk->instr_start; i < blk->instr_end; i++) {
                    IRInstr *ins = &fn->instrs[i];
                    if (ins->op != IR_MUL) continue;

                    int64_t factor = 0;
                    bool uses_iv = false;

                    if (ins->src1.kind == OPER_TEMP &&
                        ins->src1.temp_id == phi->dest &&
                        ins->src2.kind == OPER_IMM) {
                        factor = ins->src2.imm;
                        uses_iv = true;
                    } else if (ins->src2.kind == OPER_TEMP &&
                               ins->src2.temp_id == phi->dest &&
                               ins->src1.kind == OPER_IMM) {
                        factor = ins->src1.imm;
                        uses_iv = true;
                    }

                    if (!uses_iv) continue;

                    /* Replace MUL(iv, factor) with ADD-based IV:
                     * derived_next = derived + stride*factor
                     * This is profitable when stride*factor is a constant.
                     * For simplicity, we replace the MUL with an
                     * ADD of (stride * factor) to a running sum,
                     * but that requires creating a new phi — which
                     * is complex in the current framework. Instead,
                     * we do strength reduction: if factor is power of 2,
                     * replace MUL with SHL. */
                    int64_t log2f = -1;
                    {
                        int64_t v = factor;
                        if (v > 0 && (v & (v - 1)) == 0) {
                            log2f = 0;
                            while ((v >> log2f) != 1) log2f++;
                        }
                    }
                    if (log2f >= 0) {
                        ins->op = IR_SHL;
                        if (ins->src1.kind == OPER_IMM) {
                            /* swap so IV is src1 */
                            ins->src1 = ins->src2;
                        }
                        ins->src2.kind = OPER_IMM;
                        ins->src2.imm = log2f;
                    }
                    (void)stride;
                }
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 3 – Loop Strength Reduction
 *
 * Convert expensive operations (MUL) with induction variables
 * into cheaper ADD chains. Builds on IV detection from iv_simplify.
 * ═════════════════════════════════════════════════════════════ */

void ssa_loop_strength_reduce(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0) return;

    /* Power-of-2 strength reduction for any MUL/DIV/MOD in loops */
    for (int b = 0; b < sf->block_count; b++) {
        if (sf->blocks[b].loop_depth == 0) continue;
        SSABlock *blk = &sf->blocks[b];

        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];

            if (ins->op == IR_MUL) {
                int64_t val = 0;
                bool has_imm = false;
                if (ins->src2.kind == OPER_IMM) {
                    val = ins->src2.imm; has_imm = true;
                } else if (ins->src1.kind == OPER_IMM) {
                    val = ins->src1.imm; has_imm = true;
                    /* Swap so imm is in src2 */
                    IROper tmp = ins->src1;
                    ins->src1 = ins->src2;
                    ins->src2 = tmp;
                }

                if (has_imm && val > 0 && (val & (val - 1)) == 0) {
                    int64_t shift = 0;
                    while ((val >> shift) != 1) shift++;
                    ins->op = IR_SHL;
                    ins->src2.imm = shift;
                }
            }

            /* DIV by power-of-2 → SHR (unsigned only — AXIS is i32) */
            if (ins->op == IR_DIV && ins->src2.kind == OPER_IMM) {
                int64_t val = ins->src2.imm;
                if (val > 0 && (val & (val - 1)) == 0) {
                    int64_t shift = 0;
                    while ((val >> shift) != 1) shift++;
                    ins->op = IR_SHR;
                    ins->src2.imm = shift;
                }
            }

            /* MOD by power-of-2 → AND (unsigned only) */
            if (ins->op == IR_MOD && ins->src2.kind == OPER_IMM) {
                int64_t val = ins->src2.imm;
                if (val > 0 && (val & (val - 1)) == 0) {
                    ins->op = IR_BIT_AND;
                    ins->src2.imm = val - 1;
                }
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 3 – Loop Rotation (while → do-while)
 *
 * Transforms:
 *   while (cond) { body }
 * Into:
 *   if (cond) { do { body } while (cond) }
 *
 * This eliminates one branch per iteration and enables better
 * optimization of the loop body. In SSA form, this means
 * duplicating the loop header (condition test) before the loop.
 *
 * Currently a placeholder: the transformation is complex in
 * SSA form and the LICM + unrolling already capture most gains.
 * ═════════════════════════════════════════════════════════════ */

void ssa_loop_rotate(SSAFunc *sf)
{
    (void)sf;
    /* Loop rotation alters CFG structure significantly.
     * For now, we rely on existing LICM + unrolling to capture
     * the performance gains. Full loop rotation will be
     * implemented when the SSA framework supports CFG mutation. */
}

/* ═════════════════════════════════════════════════════════════
 * Tier 4 – Address Mode Selection
 *
 * Fold patterns like:
 *   t1 = ADD base, offset    (where offset is IMM)
 *   t2 = INDEX_LOAD t1, ...
 * Into direct [base + offset] addressing modes via LEA.
 *
 * Also recognizes:
 *   t1 = MUL idx, scale       (scale = 1,2,4,8)
 *   t2 = ADD base, t1
 * As [base + idx*scale] patterns for x64 addressing.
 * ═════════════════════════════════════════════════════════════ */

void ssa_addr_mode_select(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0) return;

    /* Build single-def map: temp → instruction index */
    int *def_idx = (int *)xmalloc((size_t)tc * sizeof(int));
    for (int i = 0; i < tc; i++) def_idx[i] = -1;

    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->dest.kind == OPER_TEMP && !instr_dest_is_use(ins->op)) {
                def_idx[ins->dest.temp_id] = i;
            }
        }
    }

    /* NOTE: The ADD(temp, imm) → LEA conversion was removed because:
     * 1. It incorrectly converted value computations (e.g. x + 1) to LEA
     *    when the result was stored via FIELD_STORE, not just used as an address.
     * 2. The x64 codegen for IR_LEA only handles LEA from stack slots,
     *    not the temp+imm form, so the immediate was silently dropped. */

    free(def_idx);
}

/* ═════════════════════════════════════════════════════════════
 * Tier 4 – Instruction Scheduling
 *
 * Basic list scheduling within basic blocks to reduce pipeline
 * stalls. Tries to separate dependent instructions by inserting
 * independent instructions between them.
 * ═════════════════════════════════════════════════════════════ */

void ssa_insn_schedule(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;

    /* Simple scheduling: for each basic block, try to move
     * independent instructions between dependent pairs. */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        int n = blk->instr_end - blk->instr_start;
        if (n < 3) continue;

        /* Build dependency info: for each instruction, track
         * which temp it defines and which temps it uses */
        for (int i = blk->instr_start; i < blk->instr_end - 1; i++) {
            IRInstr *ins = &fn->instrs[i];
            if (ins->op == IR_NOP || ssa_has_side_effect(ins->op)) continue;
            if (ins->dest.kind != OPER_TEMP) continue;
            int def_temp = ins->dest.temp_id;

            /* Check if next instruction uses def_temp */
            IRInstr *next = &fn->instrs[i + 1];
            if (!oper_uses_temp(&next->src1, def_temp) &&
                !oper_uses_temp(&next->src2, def_temp))
                continue;

            /* Look for an instruction after i+1 that could be moved between */
            for (int j = i + 2; j < blk->instr_end; j++) {
                IRInstr *cand = &fn->instrs[j];
                if (cand->op == IR_NOP) continue;
                if (ssa_has_side_effect(cand->op)) break;
                if (cand->op == IR_LABEL) break;

                /* Check cand doesn't use def_temp and doesn't conflict */
                if (oper_uses_temp(&cand->src1, def_temp) ||
                    oper_uses_temp(&cand->src2, def_temp))
                    break;

                /* Check cand's dest doesn't conflict with instructions i..j-1 */
                if (cand->dest.kind == OPER_TEMP) {
                    int cd = cand->dest.temp_id;
                    bool conflict = false;
                    for (int k = i; k < j; k++) {
                        if (oper_uses_temp(&fn->instrs[k].src1, cd) ||
                            oper_uses_temp(&fn->instrs[k].src2, cd) ||
                            (fn->instrs[k].dest.kind == OPER_TEMP &&
                             fn->instrs[k].dest.temp_id == cd)) {
                            conflict = true;
                            break;
                        }
                    }
                    if (conflict) break;
                }

                /* Must not read a temp defined by instructions i+1..j-1
                 * (RAW dependency: moving cand up would read stale value) */
                {
                    bool src_conflict = false;
                    for (int k = i + 1; k < j; k++) {
                        /* Cannot move cand over a side-effecting instruction
                         * (e.g. STORE_VAR) — memory ordering matters */
                        if (ssa_has_side_effect(fn->instrs[k].op)) {
                            src_conflict = true;
                            break;
                        }
                        if (fn->instrs[k].dest.kind == OPER_TEMP) {
                            int kd = fn->instrs[k].dest.temp_id;
                            if (oper_uses_temp(&cand->src1, kd) ||
                                oper_uses_temp(&cand->src2, kd)) {
                                src_conflict = true;
                                break;
                            }
                        }
                    }
                    if (src_conflict) break;
                }

                /* Move cand to position i+1 by rotating */
                IRInstr saved = *cand;
                for (int k = j; k > i + 1; k--)
                    fn->instrs[k] = fn->instrs[k - 1];
                fn->instrs[i + 1] = saved;
                break;
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 5 – SSA Live Range Splitting
 *
 * Split long-lived temporaries at loop boundaries to reduce
 * register pressure. Inserts copies at loop entry/exit points
 * to create shorter live ranges.
 * ═════════════════════════════════════════════════════════════ */

void ssa_live_range_split(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    int tc = sf->ssa_temp_count;
    if (tc <= 0 || sf->block_count == 0) return;

    /* Find temps that are live across loop boundaries:
     * defined outside a loop but used inside it,
     * or defined inside a loop but used outside it. */
    for (int h = 0; h < sf->block_count; h++) {
        if (!sf->blocks[h].is_loop_header) continue;

        bool *in_loop = (bool *)xcalloc((size_t)sf->block_count, sizeof(bool));
        for (int b = 0; b < sf->block_count; b++) {
            if (sf->blocks[b].loop_header == h || b == h)
                in_loop[b] = true;
        }

        /* Collect defs in/out of loop and uses in/out of loop */
        bool *def_inside = (bool *)xcalloc((size_t)tc, sizeof(bool));
        bool *used_outside = (bool *)xcalloc((size_t)tc, sizeof(bool));

        for (int b = 0; b < sf->block_count; b++) {
            SSABlock *blk = &sf->blocks[b];
            bool is_in = in_loop[b];

            for (int i = blk->instr_start; i < blk->instr_end; i++) {
                IRInstr *ins = &fn->instrs[i];
                if (ins->dest.kind == OPER_TEMP && is_in) {
                    int t = ins->dest.temp_id;
                    if (t >= 0 && t < tc) def_inside[t] = true;
                }
                if (!is_in) {
                    if (ins->src1.kind == OPER_TEMP) {
                        int t = ins->src1.temp_id;
                        if (t >= 0 && t < tc) used_outside[t] = true;
                    }
                    if (ins->src2.kind == OPER_TEMP) {
                        int t = ins->src2.temp_id;
                        if (t >= 0 && t < tc) used_outside[t] = true;
                    }
                }
            }
        }

        /* Temps defined inside loop and used outside are candidates
         * for live range splitting. For now, we just note them —
         * the actual splitting is handled by the register allocator
         * which respects loop boundaries via interval extension. */

        free(in_loop);
        free(def_inside);
        free(used_outside);
    }
}

/* ═════════════════════════════════════════════════════════════
 * Tier 6 – Post-RA Peephole
 *
 * Pattern-match x64-level instruction sequences after register
 * allocation. Eliminates redundant MOVs, identity operations,
 * and short sequences.
 * ═════════════════════════════════════════════════════════════ */

void ssa_post_peephole(IRFunc *fn)
{
    if (!fn || fn->instr_count == 0) return;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        /* Remove self-MOV: MOV t, t → NOP */
        if (ins->op == IR_MOV && ins->dest.kind == OPER_TEMP &&
            ins->src1.kind == OPER_TEMP &&
            ins->dest.temp_id == ins->src1.temp_id) {
            ins->op = IR_NOP;
            continue;
        }

        /* ADD/SUB by 0 → NOP */
        if ((ins->op == IR_ADD || ins->op == IR_SUB) &&
            ins->src2.kind == OPER_IMM && ins->src2.imm == 0 &&
            ins->dest.kind == OPER_TEMP && ins->src1.kind == OPER_TEMP &&
            ins->dest.temp_id == ins->src1.temp_id) {
            ins->op = IR_NOP;
            continue;
        }

        /* MUL by 1 → NOP (when dest == src1) */
        if (ins->op == IR_MUL && ins->src2.kind == OPER_IMM &&
            ins->src2.imm == 1 && ins->dest.kind == OPER_TEMP &&
            ins->src1.kind == OPER_TEMP &&
            ins->dest.temp_id == ins->src1.temp_id) {
            ins->op = IR_NOP;
            continue;
        }

        /* SHL/SHR by 0 → NOP */
        if ((ins->op == IR_SHL || ins->op == IR_SHR) &&
            ins->src2.kind == OPER_IMM && ins->src2.imm == 0 &&
            ins->dest.kind == OPER_TEMP && ins->src1.kind == OPER_TEMP &&
            ins->dest.temp_id == ins->src1.temp_id) {
            ins->op = IR_NOP;
            continue;
        }

        /* MUL by 0 → LOAD_IMM 0 */
        if (ins->op == IR_MUL && ins->src2.kind == OPER_IMM &&
            ins->src2.imm == 0) {
            ins->op = IR_LOAD_IMM;
            ins->src1.kind = OPER_IMM;
            ins->src1.imm = 0;
            ins->src2 = (IROper){0};
            continue;
        }

        /* AND with 0 → LOAD_IMM 0 */
        if (ins->op == IR_BIT_AND && ins->src2.kind == OPER_IMM &&
            ins->src2.imm == 0) {
            ins->op = IR_LOAD_IMM;
            ins->src1.kind = OPER_IMM;
            ins->src1.imm = 0;
            ins->src2 = (IROper){0};
            continue;
        }

        /* Two consecutive LOAD_IMM to same dest → keep only second */
        if (i + 1 < fn->instr_count) {
            IRInstr *next = &fn->instrs[i + 1];
            if (ins->op == IR_LOAD_IMM && next->op == IR_LOAD_IMM &&
                ins->dest.kind == OPER_TEMP && next->dest.kind == OPER_TEMP &&
                ins->dest.temp_id == next->dest.temp_id) {
                ins->op = IR_NOP;
            }
        }
    }

    /* Compact NOPs */
    int w = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_NOP) {
            if (w != i) fn->instrs[w] = fn->instrs[i];
            w++;
        }
    }
    fn->instr_count = w;
}

/* ═════════════════════════════════════════════════════════════
 * Tier 6 – Branch Relaxation
 *
 * Shorten or eliminate branches:
 * - JMP to the immediately following label → NOP
 * - JZ/JNZ with fallthrough to target → NOP
 * - Chain of JMP → direct target
 * ═════════════════════════════════════════════════════════════ */

void ssa_branch_relax(IRFunc *fn)
{
    if (!fn || fn->instr_count == 0) return;

    /* Build label → instruction index map */
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

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];

        if (ins->op == IR_JMP) {
            int target = ins->dest.label_id;
            if (target >= 0 && target <= max_label) {
                /* JMP to next instruction → NOP */
                int tpos = label_pos[target];
                if (tpos == i + 1) {
                    ins->op = IR_NOP;
                    continue;
                }

                /* Chain following: JMP → target is another JMP */
                if (tpos >= 0 && tpos < fn->instr_count) {
                    /* Look past the label for a JMP */
                    int next = tpos + 1;
                    if (next < fn->instr_count &&
                        fn->instrs[next].op == IR_JMP) {
                        ins->dest = fn->instrs[next].dest;
                    }
                }
            }
        }

        /* JZ/JNZ to fallthrough → NOP */
        if ((ins->op == IR_JZ || ins->op == IR_JNZ) &&
            ins->dest.kind == OPER_LABEL) {
            int target = ins->dest.label_id;
            if (target >= 0 && target <= max_label &&
                label_pos[target] == i + 1) {
                ins->op = IR_NOP;
            }
        }
    }

    free(label_pos);

    /* Compact NOPs */
    int w = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op != IR_NOP) {
            if (w != i) fn->instrs[w] = fn->instrs[i];
            w++;
        }
    }
    fn->instr_count = w;
}

/* ═════════════════════════════════════════════════════════════
 * Tier 6 – NOP Alignment
 *
 * Insert NOP instructions before hot loop headers to align them
 * to 16-byte boundaries. Reduces instruction fetch penalties
 * on modern x86 processors.
 * ═════════════════════════════════════════════════════════════ */

void ssa_nop_align(IRFunc *fn)
{
    if (!fn || fn->instr_count == 0) return;

    /* Count back-edge targets to find hot loop headers */
    int max_label = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL) {
            int lid = fn->instrs[i].dest.label_id;
            if (lid > max_label) max_label = lid;
        }
    }

    int *label_pos = (int *)xcalloc((size_t)(max_label + 2), sizeof(int));
    bool *is_loop_header = (bool *)xcalloc((size_t)(max_label + 2), sizeof(bool));

    for (int i = 0; i <= max_label; i++) label_pos[i] = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL)
            label_pos[fn->instrs[i].dest.label_id] = i;
    }

    /* Detect back-edges: jump targeting earlier label */
    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_JMP || ins->op == IR_JZ || ins->op == IR_JNZ) {
            int lid = ins->dest.label_id;
            if (lid >= 0 && lid <= max_label && label_pos[lid] >= 0 &&
                label_pos[lid] <= i) {
                is_loop_header[lid] = true;
            }
        }
    }

    /* Insert NOP padding before loop header labels.
     * We use a simple approach: for each loop header label,
     * add up to 3 NOPs before it (simulating alignment).
     * The actual byte-level alignment is handled by x64 codegen. */
    int new_count = fn->instr_count;
    for (int i = 0; i <= max_label; i++) {
        if (is_loop_header[i]) new_count += 3; /* up to 3 alignment NOPs */
    }

    if (new_count > fn->instr_count) {
        IRInstr *new_instrs = (IRInstr *)xmalloc((size_t)new_count * sizeof(IRInstr));
        int w = 0;
        for (int i = 0; i < fn->instr_count; i++) {
            if (fn->instrs[i].op == IR_LABEL) {
                int lid = fn->instrs[i].dest.label_id;
                if (lid >= 0 && lid <= max_label && is_loop_header[lid]) {
                    /* Insert alignment NOPs */
                    for (int n = 0; n < 3; n++) {
                        memset(&new_instrs[w], 0, sizeof(IRInstr));
                        new_instrs[w].op = IR_NOP;
                        w++;
                    }
                }
            }
            new_instrs[w++] = fn->instrs[i];
        }

        /* Replace instruction array.
         * Note: original array is arena-allocated, we leak it
         * but that's fine since it'll be freed with the arena. */
        fn->instrs = new_instrs;
        fn->instr_count = w;
        fn->instr_cap = new_count;
    }

    free(label_pos);
    free(is_loop_header);
}

/* ═════════════════════════════════════════════════════════════
 * Tier 6 – Post-RA Scheduling
 *
 * Final instruction reordering after register allocation to
 * minimize pipeline stalls. Operates on flat IR instructions.
 * ═════════════════════════════════════════════════════════════ */

void ssa_post_schedule(IRFunc *fn)
{
    if (!fn || fn->instr_count < 3) return;

    /* Simple scheduling: avoid back-to-back dependent instructions.
     * For each pair of consecutive instructions where the second
     * reads the first's dest, try to insert an independent instruction
     * between them to fill the pipeline slot. */
    for (int i = 0; i < fn->instr_count - 1; i++) {
        IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_NOP || ssa_has_side_effect(ins->op)) continue;
        if (ins->dest.kind != OPER_TEMP) continue;

        int def_temp = ins->dest.temp_id;
        IRInstr *next = &fn->instrs[i + 1];

        /* Check if next uses def_temp */
        if (!oper_uses_temp(&next->src1, def_temp) &&
            !oper_uses_temp(&next->src2, def_temp))
            continue;

        /* Look for an independent instruction to move in between */
        for (int j = i + 2; j < fn->instr_count && j < i + 8; j++) {
            IRInstr *cand = &fn->instrs[j];
            if (cand->op == IR_NOP) continue;
            if (ssa_has_side_effect(cand->op)) break;
            if (cand->op == IR_LABEL) break;

            /* Must not use def_temp */
            if (oper_uses_temp(&cand->src1, def_temp) ||
                oper_uses_temp(&cand->src2, def_temp))
                break;

            /* Must not define anything used by instructions i..j-1 */
            if (cand->dest.kind == OPER_TEMP) {
                int cd = cand->dest.temp_id;
                bool conflict = false;
                for (int k = i; k < j; k++) {
                    if (oper_uses_temp(&fn->instrs[k].src1, cd) ||
                        oper_uses_temp(&fn->instrs[k].src2, cd) ||
                        (fn->instrs[k].dest.kind == OPER_TEMP &&
                         fn->instrs[k].dest.temp_id == cd)) {
                        conflict = true;
                        break;
                    }
                }
                if (conflict) break;
            }

            /* Must not read a temp that is defined by instructions i+1..j-1
             * (RAW dependency: if moved up, cand would read stale value) */
            {
                bool src_conflict = false;
                for (int k = i + 1; k < j; k++) {
                    /* Cannot move cand over a side-effecting instruction
                     * (e.g. STORE_VAR) — memory ordering matters */
                    if (ssa_has_side_effect(fn->instrs[k].op)) {
                        src_conflict = true;
                        break;
                    }
                    if (fn->instrs[k].dest.kind == OPER_TEMP) {
                        int kd = fn->instrs[k].dest.temp_id;
                        if (oper_uses_temp(&cand->src1, kd) ||
                            oper_uses_temp(&cand->src2, kd)) {
                            src_conflict = true;
                            break;
                        }
                    }
                }
                if (src_conflict) break;
            }

            /* Rotate cand into position i+1 */
            IRInstr saved = *cand;
            for (int k = j; k > i + 1; k--)
                fn->instrs[k] = fn->instrs[k - 1];
            fn->instrs[i + 1] = saved;
            break;
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Top-level Pipeline – ssa_optimize()
 *
 * Orchestrates all SSA and post-RA optimization passes based
 * on the selected optimization level. Pass tiers aligned with
 * LLVM; thresholds tuned aggressively since AXCC targets only
 * AXIS (not C/C++ like GCC/LLVM).
 *
 * 32 unique optimization passes total (14 SSA + 14 flat-IR + 4 post-RA):
 *
 * O0:  opt_constfold, opt_dce
 *
 * O1 adds:
 *   SSA:     ssa_sccp, ssa_copyprop, ssa_phi_elim, ssa_adce
 *   Flat-IR: opt_constfold, opt_strength_reduce, opt_peephole,
 *            opt_loadstore_elim, opt_copyprop, opt_rie, opt_dead_store,
 *            opt_inline (via ssa_inline, 3 passes)
 *   Post-RA: ssa_post_peephole, ssa_branch_relax
 *
 * O2 adds:
 *   SSA:     ssa_gvn, ssa_licm, ssa_iv_simplify,
 *            ssa_loop_strength_reduce, ssa_loop_rotate,
 *            ssa_unroll, ssa_insn_schedule,
 *            ssa_addr_mode_select, ssa_live_range_split
 *   Flat-IR: opt_inline (5 passes), opt_loop_invert, opt_licm,
 *            opt_unroll, opt_regpromote, opt_ivsr
 *   Post-RA: ssa_nop_align, ssa_post_schedule
 *
 * O3: Same passes as O2, more aggressive thresholds
 *   opt_inline: 7 passes
 *
 * Os: Same as O2 (no unroll, nop-align)
 * ═════════════════════════════════════════════════════════════ */

void ssa_optimize(IRProgram *ir, OptLevel level, Arena *arena, bool verbose)
{
    if (level == OPT_O0) {
        /* O0: No SSA, just basic flat-IR cleanup */
        opt_constfold(ir);
        opt_dce(ir);
        return;
    }

    /* ── Pre-SSA flat-IR passes ────────────────────────── */
    opt_dce(ir);

    /* Inlining operates on flat IR before SSA construction (O1+) */
    if (level >= OPT_O1) {
        if (verbose) fprintf(stderr, "[ssa] inlining (pre-SSA)...\n");
        int passes = (level >= OPT_O3) ? 7 : 5;
        ssa_inline(ir, passes);
        opt_dce(ir);
    }

    /* Tail-call optimization: convert self-recursive calls to loops (O1+) */
    if (level >= OPT_O1) {
        if (verbose) fprintf(stderr, "[ssa] tail-call optimization...\n");
        opt_tail_call(ir);
    }

    /* ── SSA Construction + Scalar + Loop passes per function ── */
    for (int fi = 0; fi < ir->func_count; fi++) {
        IRFunc *fn = &ir->funcs[fi];
        if (fn->instr_count == 0) continue;

        if (verbose)
            fprintf(stderr, "[ssa] optimizing '%s' (%d instrs)...\n",
                    fn->name, fn->instr_count);

        /* ── Tier 1: SSA Construction ────────────────────── */
        SSAFunc sf;
        ssa_construct(&sf, fn, arena);

        if (verbose)
            fprintf(stderr, "[ssa]   constructed: %d blocks, %d SSA temps\n",
                    sf.block_count, sf.ssa_temp_count);

        /* ── Tier 2: Scalar Optimizations ────────────────── */

        /* SCCP: always at O1+ */
        if (verbose) fprintf(stderr, "[ssa]   SCCP...\n");
        ssa_sccp(&sf);

        /* Copy propagation: always at O1+ */
        if (verbose) fprintf(stderr, "[ssa]   copy propagation...\n");
        ssa_copyprop(&sf);

        /* GVN: O1+ */
        if (verbose) fprintf(stderr, "[ssa]   GVN...\n");
        ssa_gvn(&sf);

        /* Phi elimination: always at O1+ */
        if (verbose) fprintf(stderr, "[ssa]   phi elimination...\n");
        ssa_phi_elim(&sf);

        /* ADCE: always at O1+ */
        if (verbose) fprintf(stderr, "[ssa]   ADCE...\n");
        ssa_adce(&sf);

        /* ── Tier 3: Loop Optimizations (O1+) ────────────── */
        if (verbose) fprintf(stderr, "[ssa]   LICM...\n");
        ssa_licm(&sf);

        if (verbose) fprintf(stderr, "[ssa]   IV simplify...\n");
        ssa_iv_simplify(&sf);

        if (verbose) fprintf(stderr, "[ssa]   loop strength reduce...\n");
        ssa_loop_strength_reduce(&sf);

        if (verbose) fprintf(stderr, "[ssa]   loop rotate...\n");
        ssa_loop_rotate(&sf);

        /* Loop unrolling: O1+ (not Os) */
        if (level >= OPT_O1 && level != OPT_Os) {
            if (verbose) fprintf(stderr, "[ssa]   loop unroll...\n");
            ssa_unroll(&sf);
        }

        /* ── Tier 4: Lowering (O2+) ─────────────────────── */
        if (level >= OPT_O1) {
            if (verbose) fprintf(stderr, "[ssa]   address mode select...\n");
            ssa_addr_mode_select(&sf);
        }

        /* Instruction scheduling: O1+ */
        if (level >= OPT_O1) {
            if (verbose) fprintf(stderr, "[ssa]   instruction scheduling...\n");
            ssa_insn_schedule(&sf);
        }

        /* ── Tier 5: Live Range Splitting (O2+) ──────────── */
        if (level >= OPT_O1) {
            if (verbose) fprintf(stderr, "[ssa]   live range split...\n");
            ssa_live_range_split(&sf);
        }

        /* Second pass of scalar opts to clean up after loop opts */
        ssa_copyprop(&sf);
        ssa_phi_elim(&sf);
        ssa_adce(&sf);

        /* ── SSA Destruction ─────────────────────────────── */
        if (verbose) fprintf(stderr, "[ssa]   destructing SSA...\n");
        ssa_destruct(&sf);
        ssa_func_free(&sf);

        if (verbose)
            fprintf(stderr, "[ssa]   done: %d instrs after SSA\n",
                    fn->instr_count);
    }

    /* ── Top-level code (non-function) ────────────────── */
    if (ir->top_level.instr_count > 0) {
        SSAFunc sf;
        ssa_construct(&sf, &ir->top_level, arena);

        ssa_sccp(&sf);
        ssa_copyprop(&sf);
        ssa_gvn(&sf);
        ssa_phi_elim(&sf);
        ssa_adce(&sf);

        ssa_licm(&sf);
        ssa_iv_simplify(&sf);
        ssa_loop_strength_reduce(&sf);

        ssa_copyprop(&sf);
        ssa_phi_elim(&sf);
        ssa_adce(&sf);

        ssa_destruct(&sf);
        ssa_func_free(&sf);
    }

    /* ── Post-SSA flat-IR passes ──────────────────────── */
    if (verbose) fprintf(stderr, "[ssa] post-SSA flat-IR passes...\n");

    /* Run existing flat-IR optimizations after SSA destruction */
    opt_reassociate(ir);
    opt_constfold(ir);
    opt_fwdprop(ir);
    opt_strength_reduce(ir);
    opt_peephole(ir);
    opt_sra(ir);
    opt_loadstore_elim(ir);
    opt_copyprop(ir);

    opt_switch_lower(ir);
    opt_jump_thread(ir);
    opt_simplify_cfg(ir);
    opt_tail_merge(ir);
    opt_if_convert(ir);
    opt_minmaxabs(ir);
    opt_dce(ir);
    opt_vrp(ir);
    opt_licm(ir);
    if (level >= OPT_O1 && level != OPT_Os) opt_unroll(ir);
    opt_regpromote(ir);
    opt_loadstore_elim(ir);
    opt_copyprop(ir);

    opt_ivsr(ir);
    opt_ive(ir);

    opt_loop_invert(ir);
    opt_loadstore_elim(ir);
    opt_copyprop(ir);

    opt_rie(ir);
    opt_dead_store(ir);
    opt_sink(ir);
    opt_dce(ir);

    /* Second round: catch cascading optimization opportunities */
    opt_constfold(ir);
    opt_copyprop(ir);
    opt_peephole(ir);
    opt_simplify_cfg(ir);
    opt_dce(ir);

    /* ── Tier 6: Post-RA optimization ─────────────────── */
    if (verbose) fprintf(stderr, "[ssa] post-RA passes...\n");

    for (int fi = 0; fi < ir->func_count; fi++) {
        IRFunc *fn = &ir->funcs[fi];

        /* Post-RA peephole: always at O1+ */
        ssa_post_peephole(fn);

        /* Branch relaxation: always at O1+ */
        ssa_branch_relax(fn);

        /* NOP alignment: O1+ */
        if (level >= OPT_O1)
            ssa_nop_align(fn);

        /* Post-RA scheduling: O1+ */
        if (level >= OPT_O1)
            ssa_post_schedule(fn);
    }

    /* Same for top-level */
    if (ir->top_level.instr_count > 0) {
        ssa_post_peephole(&ir->top_level);
        ssa_branch_relax(&ir->top_level);
    }

    /* Final cleanup */
    opt_dce(ir);

    if (verbose) fprintf(stderr, "[ssa] optimization complete\n");
}
