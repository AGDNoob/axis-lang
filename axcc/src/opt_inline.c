#include "axis_opt.h"
#include <string.h>
#include <stdlib.h>

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

#define INLINE_THRESHOLD 80
#define INLINE_ONCE_THRESHOLD 512

/* ── Simple hash map: function name → IRFunc* + call count ── */

typedef struct {
    const char *name;   /* NULL = empty slot */
    IRFunc     *func;
    int         calls;  /* total call count across program */
} FuncMapSlot;

typedef struct {
    FuncMapSlot *slots;
    int          cap;   /* always power of 2 */
} FuncMap;

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static void funcmap_init(FuncMap *m, int n)
{
    /* Round up to next power of 2, at least 16 */
    int cap = 16;
    while (cap < n * 2) cap *= 2;
    m->cap   = cap;
    m->slots = (FuncMapSlot *)xcalloc((size_t)cap, sizeof(FuncMapSlot));
}

static void funcmap_free(FuncMap *m)
{
    free(m->slots);
    m->slots = NULL;
    m->cap   = 0;
}

static void funcmap_insert(FuncMap *m, const char *name, IRFunc *fn)
{
    uint32_t idx = fnv1a(name) & (uint32_t)(m->cap - 1);
    while (m->slots[idx].name) {
        if (strcmp(m->slots[idx].name, name) == 0) return; /* dup */
        idx = (idx + 1) & (uint32_t)(m->cap - 1);
    }
    m->slots[idx].name  = name;
    m->slots[idx].func  = fn;
    m->slots[idx].calls = 0;
}

static FuncMapSlot *funcmap_get(const FuncMap *m, const char *name)
{
    uint32_t idx = fnv1a(name) & (uint32_t)(m->cap - 1);
    while (m->slots[idx].name) {
        if (strcmp(m->slots[idx].name, name) == 0)
            return &m->slots[idx];
        idx = (idx + 1) & (uint32_t)(m->cap - 1);
    }
    return NULL;
}

/* Count calls across all functions + top-level and store in map */
static void funcmap_count_calls(FuncMap *m, const IRProgram *ir)
{
    /* Reset counts */
    for (int i = 0; i < m->cap; i++)
        m->slots[i].calls = 0;

    for (int fi = 0; fi < ir->func_count; fi++) {
        const IRFunc *fn = &ir->funcs[fi];
        for (int j = 0; j < fn->instr_count; j++) {
            if (fn->instrs[j].op == IR_CALL &&
                fn->instrs[j].src1.kind == OPER_FUNC) {
                FuncMapSlot *s = funcmap_get(m, fn->instrs[j].src1.func_name);
                if (s) s->calls++;
            }
        }
    }
    for (int j = 0; j < ir->top_level.instr_count; j++) {
        if (ir->top_level.instrs[j].op == IR_CALL &&
            ir->top_level.instrs[j].src1.kind == OPER_FUNC) {
            FuncMapSlot *s = funcmap_get(m, ir->top_level.instrs[j].src1.func_name);
            if (s) s->calls++;
        }
    }
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

bool has_complex_params(const IRFunc *fn)
{
    for (int i = 0; i < fn->param_count; i++) {
        if (fn->param_info[i].is_update || fn->param_info[i].is_field)
            return true;
    }
    return false;
}

int func_max_label(const IRFunc *fn)
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
        *buf = (IRInstr *)xrealloc(*buf, (size_t)(*cap) * sizeof(IRInstr));
    }
}

/* Try to inline calls within a single function.  Returns true if any
 * inlining occurred. */
static bool inline_in_func(IRFunc *fn, const FuncMap *fmap)
{
    bool any_inlined = false;

    for (int i = 0; i < fn->instr_count; i++) {
        IRInstr *call_ins = &fn->instrs[i];
        if (call_ins->op != IR_CALL) continue;
        if (call_ins->src1.kind != OPER_FUNC) continue;

        const char *callee_name = call_ins->src1.func_name;

        /* Don't inline self-recursive calls */
        if (fn->name && strcmp(callee_name, fn->name) == 0) continue;

        IRFunc *callee = NULL;
        {
            FuncMapSlot *slot = funcmap_get(fmap, callee_name);
            if (slot) callee = slot->func;
        }
        if (!callee) continue;
        if (callee->instr_count == 0) continue;
        if (has_complex_params(callee)) continue;

        /* Inline-once: single-call functions get a much larger threshold
         * and may contain side-effects (CALL, WRITE, etc.) */
        bool single_call = false;
        {
            FuncMapSlot *slot = funcmap_get(fmap, callee_name);
            single_call = slot && slot->calls == 1;
        }
        if (single_call) {
            if (callee->instr_count >= INLINE_ONCE_THRESHOLD) continue;
        } else {
            if (callee->instr_count >= INLINE_THRESHOLD) continue;
            if (!is_leaf_func(callee)) continue;
        }

        /* Collect IR_ARG instructions preceding this CALL */
        int nargs = (call_ins->src2.kind == OPER_IMM)
                    ? (int)call_ins->src2.imm : 0;
        IROper *arg_vals = NULL;
        int    *arg_positions = NULL;
        if (nargs > 0) {
            arg_vals     = (IROper *)xcalloc((size_t)nargs, sizeof(IROper));
            arg_positions = (int *)xmalloc((size_t)nargs * sizeof(int));
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
        IRInstr *inline_buf = (IRInstr *)xmalloc(
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
        IRInstr *new_instrs = (IRInstr *)xmalloc(
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
    /* Build function name → IRFunc* hash map */
    FuncMap fmap;
    funcmap_init(&fmap, ir->func_count);
    for (int i = 0; i < ir->func_count; i++) {
        if (ir->funcs[i].name)
            funcmap_insert(&fmap, ir->funcs[i].name, &ir->funcs[i]);
    }

    /* Run up to 3 inlining passes to allow cascading */
    for (int pass = 0; pass < 3; pass++) {
        /* Recount calls each pass (inlining changes the call graph) */
        funcmap_count_calls(&fmap, ir);

        bool any = false;
        for (int i = 0; i < ir->func_count; i++) {
            if (inline_in_func(&ir->funcs[i], &fmap))
                any = true;
        }
        if (ir->top_level.instr_count > 0) {
            if (inline_in_func(&ir->top_level, &fmap))
                any = true;
        }
        if (!any) break;
    }

    funcmap_free(&fmap);
}

