/*
 * ssa.c – SSA Construction and Destruction for the AXIS compiler.
 *
 * Transforms flat three-address-code IR into SSA form:
 *   1. Build basic blocks from linear instruction stream
 *   2. Build CFG (predecessor/successor edges)
 *   3. Compute dominator tree (iterative algorithm)
 *   4. Compute dominance frontiers
 *   5. Insert phi functions (Cytron's algorithm)
 *   6. Rename variables to SSA form (dominator-tree walk)
 *
 * SSA destruction:
 *   1. Deconstruct phi functions into parallel copies
 *   2. Coalesce copies where possible
 *   3. Flatten back to linear IR
 */

#include "axis_ssa.h"
#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

/* ═════════════════════════════════════════════════════════════
 * Helper: Does this opcode's dest operand represent a USE
 *         (an address being stored *through*) rather than
 *         a definition of a new value?
 *
 * For these opcodes dest holds the base/target address and
 * must be renamed as a USE during SSA construction, NOT as
 * a new definition.
 * ═════════════════════════════════════════════════════════════ */
static bool instr_dest_is_use(IROpcode op)
{
    switch (op) {
    case IR_FIELD_STORE:  /* *(base + off) = val  — dest = base addr   */
    case IR_INDEX_STORE:  /* base[idx] = val      — dest = base addr   */
    case IR_STORE_IND:    /* *ptr = val            — dest = pointer     */
    case IR_MEMCPY:       /* memcpy(dst, src, n)   — dest = dst addr   */
        return true;
    default:
        return false;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Helper: Dynamic array operations
 * ═════════════════════════════════════════════════════════════ */

static void int_array_push(int **arr, int *count, int *cap, int val, Arena *arena)
{
    if (*count >= *cap) {
        int new_cap = *cap == 0 ? 4 : *cap * 2;
        int *new_arr = (int *)arena_alloc(arena, (size_t)new_cap * sizeof(int));
        if (*arr && *count > 0) memcpy(new_arr, *arr, (size_t)*count * sizeof(int));
        *arr = new_arr;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = val;
}

static bool int_array_contains(const int *arr, int count, int val)
{
    for (int i = 0; i < count; i++) {
        if (arr[i] == val) return true;
    }
    return false;
}

static void phi_array_push(SSAPhi **arr, int *count, int *cap, SSAPhi phi, Arena *arena)
{
    if (*count >= *cap) {
        int new_cap = *cap == 0 ? 4 : *cap * 2;
        SSAPhi *new_arr = (SSAPhi *)arena_alloc(arena, (size_t)new_cap * sizeof(SSAPhi));
        if (*arr && *count > 0) memcpy(new_arr, *arr, (size_t)*count * sizeof(SSAPhi));
        *arr = new_arr;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = phi;
}

/* ═════════════════════════════════════════════════════════════
 * Step 1: Build Basic Blocks
 *
 * Split the flat instruction array at:
 *   - Function entry (block 0)
 *   - Each IR_LABEL (starts a new block)
 *   - After each terminator (JMP, JZ, JNZ, RET, RET_VOID)
 * ═════════════════════════════════════════════════════════════ */

static bool is_terminator(IROpcode op)
{
    return op == IR_JMP || op == IR_JZ || op == IR_JNZ
        || op == IR_RET || op == IR_RET_VOID;
}

/* Map label_id → block_id for CFG construction */
typedef struct {
    int *label_to_block;    /* label_id → block index */
    int  max_label;
} LabelMap;

static void build_basic_blocks(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;
    if (fn->instr_count == 0) {
        /* Empty function: create a single empty block */
        sf->block_count = 1;
        sf->block_cap = 4;
        sf->blocks = (SSABlock *)arena_alloc(sf->arena,
                                             (size_t)sf->block_cap * sizeof(SSABlock));
        memset(&sf->blocks[0], 0, sizeof(SSABlock));
        sf->blocks[0].id = 0;
        sf->blocks[0].idom = -1;
        sf->blocks[0].loop_header = -1;
        sf->blocks[0].instr_start = 0;
        sf->blocks[0].instr_end = 0;
        return;
    }

    /* First pass: find block boundaries.
     * A new block starts at:
     *   - index 0 (entry block)
     *   - every IR_LABEL instruction
     *   - instruction after a terminator */
    int max_blocks = fn->instr_count + 2;
    bool *block_start = (bool *)arena_alloc(sf->arena,
                                            (size_t)max_blocks * sizeof(bool));
    memset(block_start, 0, (size_t)max_blocks * sizeof(bool));

    block_start[0] = true;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL) {
            block_start[i] = true;
        }
        if (is_terminator(fn->instrs[i].op) && i + 1 < fn->instr_count) {
            block_start[i + 1] = true;
        }
    }

    /* Count blocks and allocate */
    int n_blocks = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (block_start[i]) n_blocks++;
    }

    sf->block_cap = n_blocks + 4;
    sf->blocks = (SSABlock *)arena_alloc(sf->arena,
                                         (size_t)sf->block_cap * sizeof(SSABlock));
    memset(sf->blocks, 0, (size_t)sf->block_cap * sizeof(SSABlock));
    sf->block_count = n_blocks;

    /* Second pass: fill in block ranges */
    int blk_idx = -1;
    for (int i = 0; i < fn->instr_count; i++) {
        if (block_start[i]) {
            if (blk_idx >= 0) {
                sf->blocks[blk_idx].instr_end = i;
            }
            blk_idx++;
            sf->blocks[blk_idx].id = blk_idx;
            sf->blocks[blk_idx].instr_start = i;
            sf->blocks[blk_idx].idom = -1;
            sf->blocks[blk_idx].loop_header = -1;
        }
    }
    if (blk_idx >= 0) {
        sf->blocks[blk_idx].instr_end = fn->instr_count;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Step 2: Build CFG
 *
 * For each block, determine successors based on the last
 * instruction (terminator). Predecessors are the reverse.
 * ═════════════════════════════════════════════════════════════ */

static void build_cfg(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;

    /* Build label → block mapping */
    int max_label = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        if (fn->instrs[i].op == IR_LABEL) {
            int lid = fn->instrs[i].dest.label_id;
            if (lid > max_label) max_label = lid;
        }
    }

    int *label_to_block = (int *)arena_alloc(sf->arena,
                                             (size_t)(max_label + 1) * sizeof(int));
    for (int i = 0; i <= max_label; i++) label_to_block[i] = -1;

    for (int b = 0; b < sf->block_count; b++) {
        int start = sf->blocks[b].instr_start;
        if (start < fn->instr_count && fn->instrs[start].op == IR_LABEL) {
            int lid = fn->instrs[start].dest.label_id;
            label_to_block[lid] = b;
        }
    }

    /* Build successor edges */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        int last = blk->instr_end - 1;

        if (last < blk->instr_start) continue; /* empty block */

        IRInstr *term = &fn->instrs[last];

        switch (term->op) {
        case IR_JMP: {
            int target = label_to_block[term->dest.label_id];
            if (target >= 0) {
                int_array_push(&blk->succs, &blk->succ_count,
                               &blk->succ_cap, target, sf->arena);
            }
            break;
        }
        case IR_JZ:
        case IR_JNZ: {
            /* Conditional: fall-through + branch target */
            int target = label_to_block[term->dest.label_id];
            if (target >= 0) {
                int_array_push(&blk->succs, &blk->succ_count,
                               &blk->succ_cap, target, sf->arena);
            }
            /* Fall-through to next block */
            if (b + 1 < sf->block_count) {
                int ft = b + 1;
                if (!int_array_contains(blk->succs, blk->succ_count, ft)) {
                    int_array_push(&blk->succs, &blk->succ_count,
                                   &blk->succ_cap, ft, sf->arena);
                }
            }
            break;
        }
        case IR_RET:
        case IR_RET_VOID:
            /* No successors */
            break;
        default:
            /* Non-terminator at end: fall through to next block */
            if (b + 1 < sf->block_count) {
                int_array_push(&blk->succs, &blk->succ_count,
                               &blk->succ_cap, b + 1, sf->arena);
            }
            break;
        }
    }

    /* Build predecessor edges from successors */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int s = 0; s < blk->succ_count; s++) {
            int succ_id = blk->succs[s];
            SSABlock *succ = &sf->blocks[succ_id];
            if (!int_array_contains(succ->preds, succ->pred_count, b)) {
                int_array_push(&succ->preds, &succ->pred_count,
                               &succ->pred_cap, b, sf->arena);
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Step 3: Compute Dominator Tree
 *
 * Iterative dominator algorithm (Cooper, Harvey, Kennedy 2001).
 * Simple and robust for our modestly-sized CFGs.
 * ═════════════════════════════════════════════════════════════ */

static int intersect_doms(const int *doms, int b1, int b2)
{
    int finger1 = b1, finger2 = b2;
    while (finger1 != finger2) {
        while (finger1 > finger2) finger1 = doms[finger1];
        while (finger2 > finger1) finger2 = doms[finger2];
    }
    return finger1;
}

static void compute_dominators(SSAFunc *sf)
{
    int n = sf->block_count;
    if (n == 0) return;

    int *doms = (int *)arena_alloc(sf->arena, (size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) doms[i] = -1;
    doms[0] = 0; /* entry block dominates itself */

    /* Iterative fixed-point computation */
    bool changed = true;
    while (changed) {
        changed = false;
        /* Process blocks in reverse postorder (ascending id is a good
         * approximation since blocks are already ordered) */
        for (int b = 1; b < n; b++) {
            SSABlock *blk = &sf->blocks[b];
            int new_idom = -1;

            /* Pick first processed predecessor */
            for (int p = 0; p < blk->pred_count; p++) {
                int pred = blk->preds[p];
                if (doms[pred] != -1) {
                    new_idom = pred;
                    break;
                }
            }

            if (new_idom == -1) continue; /* unreachable -- skip */

            /* Intersect with other processed predecessors */
            for (int p = 0; p < blk->pred_count; p++) {
                int pred = blk->preds[p];
                if (pred == new_idom) continue;
                if (doms[pred] != -1) {
                    new_idom = intersect_doms(doms, pred, new_idom);
                }
            }

            if (doms[b] != new_idom) {
                doms[b] = new_idom;
                changed = true;
            }
        }
    }

    /* Store idom in each block */
    for (int b = 0; b < n; b++) {
        sf->blocks[b].idom = doms[b];
    }
    sf->blocks[0].idom = -1; /* entry has no dominator */

    /* Compute dom_depth */
    for (int b = 0; b < n; b++) sf->blocks[b].dom_depth = 0;
    for (int b = 1; b < n; b++) {
        int depth = 0;
        int cur = b;
        while (cur != 0 && cur != -1 && depth < n) {
            cur = doms[cur];
            depth++;
        }
        sf->blocks[b].dom_depth = depth;
    }

    /* Build dominator tree children lists */
    for (int b = 1; b < n; b++) {
        int parent = sf->blocks[b].idom;
        if (parent >= 0 && parent < n) {
            int_array_push(&sf->blocks[parent].dom_children,
                           &sf->blocks[parent].dom_child_count,
                           &sf->blocks[parent].dom_child_cap,
                           b, sf->arena);
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Step 4: Compute Dominance Frontiers
 *
 * DF(b) = set of blocks where b's dominance ends.
 * Used for phi-function placement (Cytron's algorithm).
 * ═════════════════════════════════════════════════════════════ */

static void compute_dom_frontiers(SSAFunc *sf)
{
    int n = sf->block_count;
    for (int b = 0; b < n; b++) {
        SSABlock *blk = &sf->blocks[b];
        if (blk->pred_count < 2) continue;

        for (int p = 0; p < blk->pred_count; p++) {
            int runner = blk->preds[p];
            while (runner != -1 && runner != blk->idom) {
                /* Add b to DF(runner) */
                if (!int_array_contains(sf->blocks[runner].dom_frontier,
                                        sf->blocks[runner].df_count, b)) {
                    int_array_push(&sf->blocks[runner].dom_frontier,
                                   &sf->blocks[runner].df_count,
                                   &sf->blocks[runner].df_cap,
                                   b, sf->arena);
                }
                runner = sf->blocks[runner].idom;
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Step 5: Insert Phi Functions (Cytron's Algorithm)
 *
 * For each variable that is defined in multiple blocks,
 * insert a phi function at the dominance frontier of each
 * defining block. Iterate until no more phis are inserted.
 * ═════════════════════════════════════════════════════════════ */

/* Collect which temps are defined in which blocks */
static void find_defs_per_temp(SSAFunc *sf, int **def_blocks, int *def_counts,
                               int *def_caps, int n_temps)
{
    IRFunc *fn = sf->ir_func;

    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &fn->instrs[i];
            /* Skip instructions where dest is a USE (address), not a DEF */
            if (instr_dest_is_use(ins->op)) continue;
            if (ins->dest.kind == OPER_TEMP) {
                int t = ins->dest.temp_id;
                if (t >= 0 && t < n_temps) {
                    if (!int_array_contains(def_blocks[t], def_counts[t], b)) {
                        int_array_push(&def_blocks[t], &def_counts[t],
                                       &def_caps[t], b, sf->arena);
                    }
                }
            }
        }
    }
}

static void insert_phis(SSAFunc *sf)
{
    int n_temps = sf->ir_func->temp_count;
    if (n_temps == 0) return;

    /* Allocate per-temp definition lists */
    int **def_blocks = (int **)arena_alloc(sf->arena,
                                           (size_t)n_temps * sizeof(int *));
    int *def_counts = (int *)arena_alloc(sf->arena,
                                          (size_t)n_temps * sizeof(int));
    int *def_caps = (int *)arena_alloc(sf->arena,
                                        (size_t)n_temps * sizeof(int));
    memset(def_blocks, 0, (size_t)n_temps * sizeof(int *));
    memset(def_counts, 0, (size_t)n_temps * sizeof(int));
    memset(def_caps, 0, (size_t)n_temps * sizeof(int));

    find_defs_per_temp(sf, def_blocks, def_counts, def_caps, n_temps);

    /* Phi insertion worklist (Cytron's algorithm) */
    bool *has_phi = (bool *)arena_alloc(sf->arena,
                                        (size_t)sf->block_count * sizeof(bool));
    bool *in_worklist = (bool *)arena_alloc(sf->arena,
                                            (size_t)sf->block_count * sizeof(bool));
    int *worklist = (int *)arena_alloc(sf->arena,
                                       (size_t)sf->block_count * sizeof(int));

    for (int t = 0; t < n_temps; t++) {
        if (def_counts[t] < 2) continue; /* single-def → no phi needed */

        memset(has_phi, 0, (size_t)sf->block_count * sizeof(bool));
        memset(in_worklist, 0, (size_t)sf->block_count * sizeof(bool));
        int wl_count = 0;

        /* Initialize worklist with all definition blocks */
        for (int d = 0; d < def_counts[t]; d++) {
            int b = def_blocks[t][d];
            worklist[wl_count++] = b;
            in_worklist[b] = true;
        }

        /* Process worklist */
        int wl_idx = 0;
        while (wl_idx < wl_count) {
            int b = worklist[wl_idx++];
            in_worklist[b] = false;

            SSABlock *blk = &sf->blocks[b];
            for (int df = 0; df < blk->df_count; df++) {
                int y = blk->dom_frontier[df];
                if (has_phi[y]) continue;
                has_phi[y] = true;

                /* Insert phi for temp t at block y */
                SSAPhi phi;
                memset(&phi, 0, sizeof(phi));
                phi.original = t;
                phi.dest = -1; /* will be assigned during renaming */
                phi.arg_count = sf->blocks[y].pred_count;
                phi.arg_cap = phi.arg_count;
                phi.args = (int *)arena_alloc(sf->arena,
                               (size_t)phi.arg_count * sizeof(int));
                phi.arg_blocks = (int *)arena_alloc(sf->arena,
                               (size_t)phi.arg_count * sizeof(int));
                for (int p = 0; p < phi.arg_count; p++) {
                    phi.args[p] = -1; /* filled during renaming */
                    phi.arg_blocks[p] = sf->blocks[y].preds[p];
                }
                phi.eliminated = false;

                phi_array_push(&sf->blocks[y].phis, &sf->blocks[y].phi_count,
                               &sf->blocks[y].phi_cap, phi, sf->arena);

                /* If y was not initially a definition site, add to worklist */
                if (!in_worklist[y]) {
                    worklist[wl_count++] = y;
                    in_worklist[y] = true;
                }
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Step 6: SSA Renaming
 *
 * Walk the dominator tree and rename all temp uses/defs to
 * fresh SSA versions. Phi function operands are filled in
 * based on the predecessor block context.
 * ═════════════════════════════════════════════════════════════ */

/* Renaming stack per original temp (simple dynamic stack) */
typedef struct {
    int *data;
    int  count;
    int  cap;
} IntStack;

static void stack_push(IntStack *s, int val, Arena *arena)
{
    if (s->count >= s->cap) {
        int new_cap = s->cap == 0 ? 8 : s->cap * 2;
        int *new_data = (int *)arena_alloc(arena, (size_t)new_cap * sizeof(int));
        if (s->data && s->count > 0) memcpy(new_data, s->data,
                                             (size_t)s->count * sizeof(int));
        s->data = new_data;
        s->cap = new_cap;
    }
    s->data[s->count++] = val;
}

static int stack_top(const IntStack *s)
{
    if (s->count == 0) return -1;
    return s->data[s->count - 1];
}

static void rename_block(SSAFunc *sf, int block_id, IntStack *stacks,
                          int *next_ssa)
{
    IRFunc *fn = sf->ir_func;
    SSABlock *blk = &sf->blocks[block_id];

    /* Track how many pushes we make so we can pop them later */
    int *push_counts = (int *)arena_alloc(sf->arena,
                            (size_t)sf->orig_temp_count * sizeof(int));
    memset(push_counts, 0, (size_t)sf->orig_temp_count * sizeof(int));

    /* Rename phi destinations */
    for (int p = 0; p < blk->phi_count; p++) {
        SSAPhi *phi = &blk->phis[p];
        int orig = phi->original;
        int new_ver = (*next_ssa)++;
        phi->dest = new_ver;
        stack_push(&stacks[orig], new_ver, sf->arena);
        push_counts[orig]++;
    }

    /* Rename instructions */
    for (int i = blk->instr_start; i < blk->instr_end; i++) {
        IRInstr *ins = &fn->instrs[i];

        /* Rename uses (src1, src2) */
        if (ins->src1.kind == OPER_TEMP) {
            int orig = ins->src1.temp_id;
            if (orig >= 0 && orig < sf->orig_temp_count) {
                int ver = stack_top(&stacks[orig]);
                if (ver >= 0) ins->src1.temp_id = ver;
            }
        }
        if (ins->src2.kind == OPER_TEMP) {
            int orig = ins->src2.temp_id;
            if (orig >= 0 && orig < sf->orig_temp_count) {
                int ver = stack_top(&stacks[orig]);
                if (ver >= 0) ins->src2.temp_id = ver;
            }
        }

        /* Rename definition (dest) — or rename as USE for store-like ops */
        if (ins->dest.kind == OPER_TEMP) {
            int orig = ins->dest.temp_id;
            if (orig >= 0 && orig < sf->orig_temp_count) {
                if (instr_dest_is_use(ins->op)) {
                    /* dest is an address USE, not a definition —
                     * look up the current version like src1/src2 */
                    int ver = stack_top(&stacks[orig]);
                    if (ver >= 0) ins->dest.temp_id = ver;
                } else {
                    int new_ver = (*next_ssa)++;
                    ins->dest.temp_id = new_ver;
                    stack_push(&stacks[orig], new_ver, sf->arena);
                    push_counts[orig]++;
                }
            }
        }
    }

    /* Fill in phi arguments in successor blocks */
    for (int s = 0; s < blk->succ_count; s++) {
        int succ_id = blk->succs[s];
        SSABlock *succ = &sf->blocks[succ_id];

        /* Find which predecessor index `block_id` is for the successor */
        int pred_idx = -1;
        for (int p = 0; p < succ->pred_count; p++) {
            if (succ->preds[p] == block_id) {
                pred_idx = p;
                break;
            }
        }
        if (pred_idx < 0) continue;

        /* Fill in phi args */
        for (int p = 0; p < succ->phi_count; p++) {
            SSAPhi *phi = &succ->phis[p];
            int orig = phi->original;
            if (orig >= 0 && orig < sf->orig_temp_count) {
                int ver = stack_top(&stacks[orig]);
                if (pred_idx < phi->arg_count) {
                    phi->args[pred_idx] = ver >= 0 ? ver : -1;
                }
            }
        }
    }

    /* Recurse into dominator-tree children */
    for (int c = 0; c < blk->dom_child_count; c++) {
        rename_block(sf, blk->dom_children[c], stacks, next_ssa);
    }

    /* Pop the stacks to restore parent's context */
    for (int t = 0; t < sf->orig_temp_count; t++) {
        stacks[t].count -= push_counts[t];
    }
}

static void ssa_rename(SSAFunc *sf)
{
    int n_temps = sf->orig_temp_count;
    if (n_temps == 0) return;

    IntStack *stacks = (IntStack *)arena_alloc(sf->arena,
                            (size_t)n_temps * sizeof(IntStack));
    memset(stacks, 0, (size_t)n_temps * sizeof(IntStack));

    int next_ssa = n_temps; /* SSA versions start after original temps */

    /* Initialize stacks: original temps 0..n-1 map to themselves initially */
    for (int t = 0; t < n_temps; t++) {
        stack_push(&stacks[t], t, sf->arena);
    }

    rename_block(sf, 0, stacks, &next_ssa);

    sf->ssa_temp_count = next_ssa;

    /* Build def_block mapping */
    sf->def_block = (int *)arena_alloc(sf->arena,
                                        (size_t)next_ssa * sizeof(int));
    for (int i = 0; i < next_ssa; i++) sf->def_block[i] = -1;

    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        /* Phi defs */
        for (int p = 0; p < blk->phi_count; p++) {
            if (blk->phis[p].dest >= 0 && blk->phis[p].dest < next_ssa) {
                sf->def_block[blk->phis[p].dest] = b;
            }
        }
        /* Instruction defs */
        for (int i = blk->instr_start; i < blk->instr_end; i++) {
            IRInstr *ins = &sf->ir_func->instrs[i];
            if (ins->dest.kind == OPER_TEMP && ins->dest.temp_id >= 0
                && ins->dest.temp_id < next_ssa) {
                sf->def_block[ins->dest.temp_id] = b;
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Detect Natural Loops (SSA form)
 *
 * Find back-edges (succ dominates pred) and mark loop headers
 * and loop depths.
 * ═════════════════════════════════════════════════════════════ */

static bool dominates(const SSAFunc *sf, int a, int b)
{
    /* Does block a dominate block b? */
    if (a == b) return true;
    int cur = b;
    while (cur != -1) {
        if (cur == a) return true;
        cur = sf->blocks[cur].idom;
    }
    return false;
}

static void detect_loops(SSAFunc *sf)
{
    /* Find back-edges and mark loop headers */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int s = 0; s < blk->succ_count; s++) {
            int target = blk->succs[s];
            if (dominates(sf, target, b)) {
                /* Back-edge: b → target, so target is a loop header */
                sf->blocks[target].is_loop_header = true;

                /* Mark all blocks in the loop body */
                /* Walk backwards from b to target in the dominator tree */
                int *loop_body = (int *)arena_alloc(sf->arena,
                                     (size_t)sf->block_count * sizeof(int));
                bool *in_loop = (bool *)arena_alloc(sf->arena,
                                     (size_t)sf->block_count * sizeof(bool));
                memset(in_loop, 0, (size_t)sf->block_count * sizeof(bool));

                int body_count = 0;
                in_loop[target] = true;
                in_loop[b] = true;
                loop_body[body_count++] = b;

                /* BFS/DFS: add predecessors of loop body nodes
                 * until we reach the header */
                int wl = 0;
                while (wl < body_count) {
                    int node = loop_body[wl++];
                    SSABlock *nb = &sf->blocks[node];
                    for (int p = 0; p < nb->pred_count; p++) {
                        int pred = nb->preds[p];
                        if (!in_loop[pred]) {
                            in_loop[pred] = true;
                            loop_body[body_count++] = pred;
                        }
                    }
                }

                /* Increase loop depth for all body blocks */
                for (int i = 0; i < body_count; i++) {
                    sf->blocks[loop_body[i]].loop_depth++;
                    if (sf->blocks[loop_body[i]].loop_header < 0) {
                        sf->blocks[loop_body[i]].loop_header = target;
                    }
                }
                sf->blocks[target].loop_depth++;
                if (sf->blocks[target].loop_header < 0) {
                    sf->blocks[target].loop_header = target;
                }
            }
        }
    }
}

/* ═════════════════════════════════════════════════════════════
 * SSA Construction – Public API
 * ═════════════════════════════════════════════════════════════ */

void ssa_construct(SSAFunc *sf, IRFunc *fn, Arena *arena)
{
    memset(sf, 0, sizeof(*sf));
    sf->ir_func = fn;
    sf->arena = arena;
    sf->orig_temp_count = fn->temp_count;
    sf->ssa_temp_count = fn->temp_count;

    /* Step 1: Build basic blocks */
    build_basic_blocks(sf);

    /* Step 2: Build CFG */
    build_cfg(sf);

    /* Step 3: Compute dominator tree */
    compute_dominators(sf);

    /* Step 4: Compute dominance frontiers */
    compute_dom_frontiers(sf);

    /* Step 5: Insert phi functions */
    insert_phis(sf);

    /* Step 6: SSA renaming */
    ssa_rename(sf);

    /* Bonus: Detect natural loops */
    detect_loops(sf);
}

/* ═════════════════════════════════════════════════════════════
 * SSA Destruction
 *
 * 1. Deconstruct phi functions into copies at predecessor ends
 * 2. Flatten blocks back into linear instruction array
 * 3. Update the original IRFunc
 * ═════════════════════════════════════════════════════════════ */

void ssa_destruct(SSAFunc *sf)
{
    IRFunc *fn = sf->ir_func;

    /* ── Phase 1: collect phi copies per predecessor block ──────── */
    /* For each phi  dest = phi(a0[B_p0], a1[B_p1], ...),
     * we need  MOV dest, a_k  at the end of predecessor B_pk.      */

    /* Max phi-copies per block (conservative upper bound) */
    int *pcopy_count = (int *)arena_alloc(sf->arena,
                            (size_t)sf->block_count * sizeof(int));
    memset(pcopy_count, 0, (size_t)sf->block_count * sizeof(int));

    /* First pass: count copies per predecessor */
    int total_phi_copies = 0;
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated || phi->dest < 0) continue;
            for (int a = 0; a < phi->arg_count; a++) {
                if (phi->args[a] >= 0) {
                    int pred = phi->arg_blocks[a];
                    pcopy_count[pred]++;
                    total_phi_copies++;
                }
            }
        }
    }

    /* Allocate copy arrays per predecessor block */
    typedef struct { int dest, src; } PhiCopy;
    PhiCopy **pcopies = (PhiCopy **)arena_alloc(sf->arena,
                            (size_t)sf->block_count * sizeof(PhiCopy *));
    int *pcopy_idx = (int *)arena_alloc(sf->arena,
                            (size_t)sf->block_count * sizeof(int));
    for (int b = 0; b < sf->block_count; b++) {
        if (pcopy_count[b] > 0) {
            pcopies[b] = (PhiCopy *)arena_alloc(sf->arena,
                              (size_t)pcopy_count[b] * sizeof(PhiCopy));
        } else {
            pcopies[b] = NULL;
        }
        pcopy_idx[b] = 0;
    }

    /* Second pass: fill copy arrays */
    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];
        for (int p = 0; p < blk->phi_count; p++) {
            SSAPhi *phi = &blk->phis[p];
            if (phi->eliminated || phi->dest < 0) continue;
            for (int a = 0; a < phi->arg_count; a++) {
                if (phi->args[a] >= 0) {
                    int pred = phi->arg_blocks[a];
                    PhiCopy *pc = &pcopies[pred][pcopy_idx[pred]++];
                    pc->dest = phi->dest;
                    pc->src  = phi->args[a];
                }
            }
        }
    }

    /* ── Phase 2: flatten blocks, inserting copies before terminators ── */
    int total = fn->instr_count + total_phi_copies;
    IRInstr *new_instrs = (IRInstr *)arena_alloc(sf->arena,
                                                  (size_t)(total + 8) * sizeof(IRInstr));
    int w = 0;

    for (int b = 0; b < sf->block_count; b++) {
        SSABlock *blk = &sf->blocks[b];

        /* Find the terminator: last non-NOP instruction that is JMP/JZ/RET/RET_VOID */
        int term_pos = -1;
        for (int i = blk->instr_end - 1; i >= blk->instr_start; i--) {
            IROpcode op = fn->instrs[i].op;
            if (op == IR_NOP) continue;
            if (op == IR_JMP || op == IR_JZ || op == IR_RET
                || op == IR_RET_VOID) {
                term_pos = i;
            }
            break;
        }

        /* Emit all instructions except the terminator */
        int end_before_term = (term_pos >= 0) ? term_pos : blk->instr_end;
        for (int i = blk->instr_start; i < end_before_term; i++) {
            new_instrs[w++] = fn->instrs[i];
        }

        /* Emit phi-deconstruction MOVs for this block's successors' phis */
        for (int c = 0; c < pcopy_idx[b]; c++) {
            IRInstr mov;
            memset(&mov, 0, sizeof(mov));
            mov.op = IR_MOV;
            mov.dest.kind = OPER_TEMP;
            mov.dest.temp_id = pcopies[b][c].dest;
            mov.dest.size = 4;
            mov.src1.kind = OPER_TEMP;
            mov.src1.temp_id = pcopies[b][c].src;
            mov.src1.size = 4;
            new_instrs[w++] = mov;
        }

        /* Emit the terminator */
        if (term_pos >= 0) {
            new_instrs[w++] = fn->instrs[term_pos];
        }
    }

    /* Update the function */
    fn->instrs = new_instrs;
    fn->instr_count = w;
    fn->temp_count = sf->ssa_temp_count;
}

void ssa_func_free(SSAFunc *sf)
{
    /* Everything is arena-allocated, so nothing to free individually */
    memset(sf, 0, sizeof(*sf));
}
