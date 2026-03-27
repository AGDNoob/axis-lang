/*
 * axis_ssa.h – SSA-based Intermediate Representation for the AXIS compiler.
 *
 * Transforms flat three-address-code IR into SSA form with basic blocks,
 * a control-flow graph, dominator tree, phi functions, and use-def chains.
 *
 * Architecture:
 *   Tier 1 – SSA Construction  (CFG, Dominators, Phi Insertion, Renaming)
 *   Tier 2 – Scalar Opts       (SCCP, ADCE, GVN, Copy Prop, Phi Elim)
 *   Tier 3 – Loop Opts         (LICM, Unrolling, IV Simp, Strength Red, Rotation)
 *   Tier 4 – Lowering / ISel   (Addr Mode Selection, Instruction Scheduling)
 *   Tier 5 – Register Alloc    (SSA Linear Scan, Live Range Split, Phi Decon)
 *   Tier 6 – Post-RA           (Peephole, RIE, Branch Relaxation, NOP Align)
 *
 * The SSA pass operates between IR generation and x64 code generation:
 *   AST → IRGen → [SSA Construction → SSA Opts → SSA Destruction] → x64
 */
#ifndef AXIS_SSA_H
#define AXIS_SSA_H

#include "axis_ir.h"
#include "axis_arena.h"

/* ═════════════════════════════════════════════════════════════
 * Optimization Level
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    OPT_O0,    /* No SSA, opt_constfold + opt_dce                  */
    OPT_O1,    /* SSA + Scalar + Inlining + flat-IR + Post-RA     */
    OPT_O2,    /* All 32 passes: GVN, Loop Opts, Unroll, Sched    */
    OPT_O3,    /* Same as O2 with more aggressive thresholds      */
    OPT_Os,    /* Like O2 but no unroll – smallest code           */
} OptLevel;

/* ═════════════════════════════════════════════════════════════
 * Phi Function
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    int   dest;              /* SSA destination temp                 */
    int   original;          /* original (pre-SSA) temp id           */
    int  *args;              /* one SSA temp per predecessor         */
    int  *arg_blocks;        /* predecessor block id for each arg    */
    int   arg_count;
    int   arg_cap;
    bool  eliminated;        /* marked dead by phi-elim              */
} SSAPhi;

/* ═════════════════════════════════════════════════════════════
 * Basic Block
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    int        id;

    /* Instructions: indices into the parent function's instrs array */
    int        instr_start;      /* first instruction index          */
    int        instr_end;        /* one past last instruction index  */

    /* CFG edges */
    int       *preds;
    int        pred_count;
    int        pred_cap;
    int       *succs;
    int        succ_count;
    int        succ_cap;

    /* Dominator tree */
    int        idom;             /* immediate dominator (-1 = entry) */
    int       *dom_frontier;     /* dominance frontier set           */
    int        df_count;
    int        df_cap;
    int        dom_depth;        /* depth in dominator tree          */
    int       *dom_children;     /* children in the dominator tree   */
    int        dom_child_count;
    int        dom_child_cap;

    /* Phi functions */
    SSAPhi    *phis;
    int        phi_count;
    int        phi_cap;

    /* Loop info */
    int        loop_depth;       /* 0 = not in loop                  */
    int        loop_header;      /* block id of header (-1 if none)  */
    bool       is_loop_header;
} SSABlock;

/* ═════════════════════════════════════════════════════════════
 * SSA Function
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    IRFunc     *ir_func;         /* original IR function             */

    /* Basic blocks */
    SSABlock   *blocks;
    int         block_count;
    int         block_cap;

    /* SSA versioning */
    int         ssa_temp_count;  /* total SSA temps allocated        */
    int        *version_stacks;  /* renaming stacks (internal use)   */
    int        *version_stack_tops;
    int         orig_temp_count; /* original temp count before SSA   */

    /* Use-def chains */
    int        *def_block;       /* SSA temp → defining block id     */

    Arena      *arena;
} SSAFunc;

/* ═════════════════════════════════════════════════════════════
 * SCCP Lattice value (for Sparse Conditional Constant Propagation)
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    LATTICE_TOP,         /* unknown / unreached                    */
    LATTICE_CONST,       /* known constant                         */
    LATTICE_BOTTOM,      /* varying / overdefined                  */
} LatticeKind;

typedef struct {
    LatticeKind kind;
    int64_t     value;   /* valid when kind == LATTICE_CONST       */
} LatticeVal;

/* ═════════════════════════════════════════════════════════════
 * GVN Value table entry
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    IROpcode  op;
    int       src1_vn;       /* value number of src1               */
    int       src2_vn;       /* value number of src2               */
    int       result_temp;   /* temp that holds this value         */
    int       extra;         /* extra field from instruction       */
} GVNEntry;

/* ═════════════════════════════════════════════════════════════
 * SSA Construction API
 * ═════════════════════════════════════════════════════════════ */

/* Build SSA form for a single IR function.
 * Returns a new SSAFunc with basic blocks, CFG, dominator tree,
 * phi functions, and all temps renamed to SSA versions. */
void ssa_construct(SSAFunc *sf, IRFunc *fn, Arena *arena);

/* Destroy SSA form: deconstruct phis into copies, flatten back
 * to linear IR.  After this call, fn->instrs is updated. */
void ssa_destruct(SSAFunc *sf);

/* Free SSAFunc internal arrays (not the arena-allocated parts). */
void ssa_func_free(SSAFunc *sf);

/* ═════════════════════════════════════════════════════════════
 * Tier 2 – Scalar Optimizations (SSA form)
 * ═════════════════════════════════════════════════════════════ */

/* Sparse Conditional Constant Propagation */
void ssa_sccp(SSAFunc *sf);

/* SSA Copy Propagation – eliminate redundant temp-to-temp copies */
void ssa_copyprop(SSAFunc *sf);

/* Aggressive Dead Code Elimination – removes all non-essential instrs */
void ssa_adce(SSAFunc *sf);

/* SSA-Aware Function Inlining (operates on IRProgram level).
 * aggressive=true: higher threshold, non-leaf allowed */
void ssa_inline(IRProgram *ir, int passes);

/* Global Value Numbering – eliminate redundant computations */
void ssa_gvn(SSAFunc *sf);

/* Phi Elimination – remove trivial and unnecessary phi functions */
void ssa_phi_elim(SSAFunc *sf);

/* ═════════════════════════════════════════════════════════════
 * Tier 3 – Loop Optimizations (SSA form)
 * ═════════════════════════════════════════════════════════════ */

/* SSA Loop-Invariant Code Motion */
void ssa_licm(SSAFunc *sf);

/* SSA Loop Unrolling (2× unroll) */
void ssa_unroll(SSAFunc *sf);

/* Induction Variable Simplification */
void ssa_iv_simplify(SSAFunc *sf);

/* Loop Strength Reduction – MUL with IV → ADD chain */
void ssa_loop_strength_reduce(SSAFunc *sf);

/* Loop Rotation – while → do-while transformation */
void ssa_loop_rotate(SSAFunc *sf);

/* ═════════════════════════════════════════════════════════════
 * Tier 4 – Lowering & Instruction Selection
 * ═════════════════════════════════════════════════════════════ */

/* Address Mode Selection: fold base+index*scale patterns */
void ssa_addr_mode_select(SSAFunc *sf);

/* Instruction Scheduling: reduce pipeline stalls */
void ssa_insn_schedule(SSAFunc *sf);

/* ═════════════════════════════════════════════════════════════
 * Tier 5 – Register Allocation (SSA-aware)
 * ═════════════════════════════════════════════════════════════ */

/* SSA Live Range Splitting: split long-lived temps at loop boundaries */
void ssa_live_range_split(SSAFunc *sf);

/* ═════════════════════════════════════════════════════════════
 * Tier 6 – Post-RA Optimizations
 * ═════════════════════════════════════════════════════════════ */

/* Post-RA Peephole: x64 instruction-level patterns */
void ssa_post_peephole(IRFunc *fn);

/* Branch Relaxation: shorten branches where possible */
void ssa_branch_relax(IRFunc *fn);

/* NOP Alignment: align hot loop headers to 16- or 32-byte boundaries */
void ssa_nop_align(IRFunc *fn);

/* Post-RA Scheduling: reduce stalls after register assignment */
void ssa_post_schedule(IRFunc *fn);

/* ═════════════════════════════════════════════════════════════
 * Top-level Pipeline
 * ═════════════════════════════════════════════════════════════ */

/* Run the complete SSA optimization pipeline on an IRProgram.
 * Uses the given OptLevel to decide which passes to enable. */
void ssa_optimize(IRProgram *ir, OptLevel level, Arena *arena, bool verbose);

#endif /* AXIS_SSA_H */
