/*
 * script_irgen.c – Lightweight AST → IR lowering for AXIS script mode.
 *
 * Key difference from irgen.c:
 *   gen_expr() returns IROper directly — IMM for literals, STACK for
 *   signed variables, TEMP only when a computation actually produces a
 *   new value.  This eliminates the bulk of LOAD_IMM / LOAD_VAR
 *   instructions and drastically reduces temp count, cutting both
 *   IR-generation time and downstream code-gen work.
 *
 *   Additionally, constant folding is performed inline: if both
 *   operands of a binary/unary op are IMM, the result is folded at
 *   IR-generation time without emitting any instruction.
 */

#include "axis_ir.h"
#include "axis_semantic.h"
#include "axis_error.h"
#include <stdarg.h>
#include <inttypes.h>

/* ═════════════════════════════════════════════════════════════
 * Internal generator state
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    IRProgram    *prog;
    IRFunc       *cur;
    Arena        *arena;
    const char   *filename;
    const char   *source;

    ASTFieldDef  *field_defs;
    int           field_count;
    ASTEnumDef   *enum_defs;
    int           enum_count;

    int           break_label;
    int           continue_label;

    /* Flagged loop label stack */
    #define MAX_SIR_FLAGS 16
    struct { const char *name; int brk; int cont; } flag_labels[MAX_SIR_FLAGS];
    int           flag_label_count;
} SIRGen;

/* ── Forward declarations ─────────────────────────────────── */
static IROper gen_expr(SIRGen *g, ASTExpr *e);
static void   gen_stmt(SIRGen *g, ASTStmt *st);

/* ═════════════════════════════════════════════════════════════
 * Helpers  (file-local copies – identical to irgen.c)
 * ═════════════════════════════════════════════════════════════ */

static _Noreturn void ir_error(SIRGen *g, SrcLoc loc,
                               const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag_reportv(DIAG_ERROR, g->filename, g->source, loc.line, loc.col,
                 fmt, ap);
    va_end(ap);
    exit(1);
}

/* ── Operand constructors ─────────────────────────────────── */

static IROper oper_none(void)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind = OPER_NONE;
    return o;
}

static IROper oper_temp(int id, int size)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind    = OPER_TEMP;
    o.temp_id = id;
    o.size    = size;
    return o;
}

static IROper oper_imm(int64_t val, int size)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind = OPER_IMM;
    o.imm  = val;
    o.size = size;
    return o;
}

static IROper oper_stack(int off, int size)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind      = OPER_STACK;
    o.stack_off = off;
    o.size      = size;
    return o;
}

static IROper oper_label(int id)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind     = OPER_LABEL;
    o.label_id = id;
    return o;
}

static IROper oper_func(const char *name)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind      = OPER_FUNC;
    o.func_name = name;
    return o;
}

static IROper oper_str(int idx)
{
    IROper o;
    memset(&o, 0, sizeof(o));
    o.kind    = OPER_STR;
    o.str_idx = idx;
    o.size    = 8;
    return o;
}

/* ── New temporaries / labels ─────────────────────────────── */

static int new_temp(SIRGen *g, int size)
{
    AXIS_UNUSED(size);
    return g->cur->temp_count++;
}

static int new_label(SIRGen *g)
{
    static int label_counter = 0;
    AXIS_UNUSED(g);
    return label_counter++;
}

/* ── Emit instruction ─────────────────────────────────────── */

static void emit(SIRGen *g, IROpcode op, IROper dest, IROper src1,
                 IROper src2, int extra, SrcLoc loc)
{
    IRFunc *f = g->cur;
    if (f->instr_count >= f->instr_cap) {
        int newcap = f->instr_cap < 64 ? 64 : f->instr_cap * 2;
        IRInstr *ni = arena_alloc(g->arena, (size_t)newcap * sizeof(IRInstr));
        if (f->instr_count > 0)
            memcpy(ni, f->instrs, (size_t)f->instr_count * sizeof(IRInstr));
        f->instrs    = ni;
        f->instr_cap = newcap;
    }
    IRInstr *ins = &f->instrs[f->instr_count++];
    ins->op    = op;
    ins->dest  = dest;
    ins->src1  = src1;
    ins->src2  = src2;
    ins->extra = extra;
    ins->loc   = loc;
}

#define EMIT(op, d, s1, s2)  emit(g, (op), (d), (s1), (s2), 0, e->loc)
#define EMIT_LOC(op, d, s1, s2, loc)  emit(g, (op), (d), (s1), (s2), 0, (loc))
#define EMIT_X(op, d, s1, s2, x, loc) emit(g, (op), (d), (s1), (s2), (x), (loc))

/* ═════════════════════════════════════════════════════════════
 * Scope tracking  (file-local copy)
 * ═════════════════════════════════════════════════════════════ */

typedef struct SIRScope SIRScope;
typedef struct SIRLocal SIRLocal;

struct SIRLocal {
    const char *name;
    int         stack_off;
    int         size;
    SIRLocal   *next;
};

struct SIRScope {
    SIRScope *parent;
    SIRLocal *locals;
};

static SIRScope *s_scope = NULL;

static SIRScope *scope_push(SIRGen *g)
{
    SIRScope *sc = ARENA_NEW(g->arena, SIRScope);
    sc->parent = s_scope;
    sc->locals = NULL;
    s_scope = sc;
    return sc;
}

static void scope_pop(SIRGen *g)
{
    AXIS_UNUSED(g);
    assert(s_scope);
    s_scope = s_scope->parent;
}

static void scope_add(SIRGen *g, const char *name, int stack_off, int size)
{
    SIRLocal *l = ARENA_NEW(g->arena, SIRLocal);
    l->name      = name;
    l->stack_off = stack_off;
    l->size      = size;
    l->next      = s_scope->locals;
    s_scope->locals = l;
}

static int scope_lookup(SIRGen *g, const char *name, SrcLoc loc)
{
    for (SIRScope *sc = s_scope; sc; sc = sc->parent)
        for (SIRLocal *l = sc->locals; l; l = l->next)
            if (strcmp(l->name, name) == 0)
                return l->stack_off;
    ir_error(g, loc, "Undefined variable '%s'", name);
}

static SIRLocal *scope_find(SIRGen *g, const char *name)
{
    AXIS_UNUSED(g);
    for (SIRScope *sc = s_scope; sc; sc = sc->parent)
        for (SIRLocal *l = sc->locals; l; l = l->next)
            if (strcmp(l->name, name) == 0)
                return l;
    return NULL;
}

/* ── String table ─────────────────────────────────────────── */

static int intern_string(SIRGen *g, const char *str)
{
    IRProgram *p = g->prog;
    for (int i = 0; i < p->str_count; i++)
        if (strcmp(p->strings[i], str) == 0) return i;

    if (p->str_count >= p->str_cap) {
        int nc = p->str_cap < 16 ? 16 : p->str_cap * 2;
        const char **ns = arena_alloc(g->arena, (size_t)nc * sizeof(char*));
        if (p->str_count > 0)
            memcpy(ns, p->strings, (size_t)p->str_count * sizeof(char*));
        p->strings = ns;
        p->str_cap = nc;
    }
    p->strings[p->str_count] = str;
    return p->str_count++;
}

/* ── Type helpers ─────────────────────────────────────────── */

static int type_size(const char *t)
{
    if (!t) return 8;
    if (strcmp(t,"i8")==0 || strcmp(t,"u8")==0 || strcmp(t,"bool")==0) return 1;
    if (strcmp(t,"i16")==0 || strcmp(t,"u16")==0) return 2;
    if (strcmp(t,"i32")==0 || strcmp(t,"u32")==0) return 4;
    return 8;
}

static bool is_signed(const char *t)
{
    if (!t) return true;
    return t[0] == 'i';
}

static const char *expr_type(ASTExpr *e)
{
    return e->inferred_type ? e->inferred_type : "i32";
}

static ASTFieldDef *find_field(SIRGen *g, const char *name)
{
    for (int i = 0; i < g->field_count; i++)
        if (strcmp(g->field_defs[i].name, name) == 0)
            return &g->field_defs[i];
    return NULL;
}

static ASTEnumDef *find_enum(SIRGen *g, const char *name)
{
    for (int i = 0; i < g->enum_count; i++)
        if (strcmp(g->enum_defs[i].name, name) == 0)
            return &g->enum_defs[i];
    return NULL;
}

static int field_member_offset(SIRGen *g, const char *field_type,
                               const char *member)
{
    ASTFieldDef *fd = find_field(g, field_type);
    if (!fd) return 0;
    int off = 0;
    for (int i = 0; i < fd->member_count; i++) {
        if (strcmp(fd->members[i].name, member) == 0)
            return off;
        const char *mt = fd->members[i].type_node
                         ? (fd->members[i].type_node->kind == TYPE_NODE_SIMPLE
                            ? fd->members[i].type_node->simple.name : "i32")
                         : "i32";
        ASTFieldDef *mfd = find_field(g, mt);
        if (mfd) {
            for (int j = 0; j < mfd->member_count; j++) {
                const char *mmt = mfd->members[j].type_node
                    ? (mfd->members[j].type_node->kind == TYPE_NODE_SIMPLE
                       ? mfd->members[j].type_node->simple.name : "i32")
                    : "i32";
                off += type_size(mmt);
            }
        } else {
            off += type_size(mt);
        }
    }
    return off;
}

/* ═════════════════════════════════════════════════════════════
 * Expression IR generation
 *
 * SCRIPT-IR KEY CHANGE:  gen_expr returns the result as a *direct*
 * IROper — an OPER_IMM for compile-time constants, an OPER_STACK
 * for signed variables, or an OPER_TEMP only when a real computation
 * must be stored somewhere.  This avoids emitting LOAD_IMM / LOAD_VAR
 * for every leaf expression, cutting instruction count dramatically.
 * ═════════════════════════════════════════════════════════════ */

/* ── Integer literal → direct IMM, no instruction emitted ── */
static IROper gen_int_lit(SIRGen *g, ASTExpr *e)
{
    AXIS_UNUSED(g);
    return oper_imm(e->int_lit.value, type_size(expr_type(e)));
}

/* ── String literal → still needs IR_LOAD_STR for address ── */
static IROper gen_string_lit(SIRGen *g, ASTExpr *e)
{
    int idx = intern_string(g, e->string_lit.value);
    int t   = new_temp(g, 8);
    EMIT(IR_LOAD_STR, oper_temp(t, 8), oper_str(idx), oper_none());
    return oper_temp(t, 8);
}

/* ── Boolean literal → direct IMM ──────────────────────── */
static IROper gen_bool_lit(SIRGen *g, ASTExpr *e)
{
    AXIS_UNUSED(g);
    return oper_imm(e->bool_lit.value ? 1 : 0, 1);
}

/* ── Identifier → OPER_STACK for signed types (no instruction),
 *                 LOAD_VAR+TEMP for unsigned < 8 bytes (safe zext) ── */
static IROper gen_ident(SIRGen *g, ASTExpr *e)
{
    int sz  = type_size(expr_type(e));
    int off = scope_lookup(g, e->ident.name, e->loc);

    if (is_signed(expr_type(e)) || sz == 8) {
        /* Signed or 8-byte: OPER_STACK directly, x64 load_oper handles it */
        return oper_stack(off, sz);
    }
    /* Unsigned < 8 bytes: need explicit load with zero-extension flag */
    int t = new_temp(g, sz);
    emit(g, IR_LOAD_VAR, oper_temp(t, sz), oper_stack(off, sz),
         oper_none(), /*unsig=*/1, e->loc);
    return oper_temp(t, sz);
}

/* ── Inline constant folding for binary operations ──────── */
static int64_t fold_binop(int64_t a, int64_t b, IROpcode op)
{
    switch (op) {
    case IR_ADD:     return a + b;
    case IR_SUB:     return a - b;
    case IR_MUL:     return a * b;
    case IR_DIV:     return b != 0 ? a / b : 0;
    case IR_MOD:     return b != 0 ? a % b : 0;
    case IR_BIT_AND: return a & b;
    case IR_BIT_OR:  return a | b;
    case IR_BIT_XOR: return a ^ b;
    case IR_SHL:     return a << (b & 63);
    case IR_SHR:     return a >> (b & 63);
    case IR_CMP_EQ:  return a == b ? 1 : 0;
    case IR_CMP_NE:  return a != b ? 1 : 0;
    case IR_CMP_LT:  return a <  b ? 1 : 0;
    case IR_CMP_LE:  return a <= b ? 1 : 0;
    case IR_CMP_GT:  return a >  b ? 1 : 0;
    case IR_CMP_GE:  return a >= b ? 1 : 0;
    default:         return 0;
    }
}

static IROper gen_binop(SIRGen *g, ASTExpr *e)
{
    TokenType op = e->binary.op;

    /* ── Logical short-circuit ────────────────────────── */
    if (op == TOK_AND) {
        int end_lbl   = new_label(g);
        int false_lbl = new_label(g);
        int t = new_temp(g, 1);

        IROper lv = gen_expr(g, e->binary.left);
        EMIT(IR_JZ, oper_label(false_lbl), lv, oper_none());

        IROper rv = gen_expr(g, e->binary.right);
        EMIT(IR_MOV, oper_temp(t, 1), rv, oper_none());
        EMIT(IR_JMP, oper_label(end_lbl), oper_none(), oper_none());

        EMIT(IR_LABEL, oper_label(false_lbl), oper_none(), oper_none());
        EMIT(IR_LOAD_IMM, oper_temp(t, 1), oper_imm(0, 1), oper_none());
        EMIT(IR_LABEL, oper_label(end_lbl), oper_none(), oper_none());
        return oper_temp(t, 1);
    }
    if (op == TOK_OR) {
        int end_lbl  = new_label(g);
        int true_lbl = new_label(g);
        int t = new_temp(g, 1);

        IROper lv = gen_expr(g, e->binary.left);
        EMIT(IR_JNZ, oper_label(true_lbl), lv, oper_none());

        IROper rv = gen_expr(g, e->binary.right);
        EMIT(IR_MOV, oper_temp(t, 1), rv, oper_none());
        EMIT(IR_JMP, oper_label(end_lbl), oper_none(), oper_none());

        EMIT(IR_LABEL, oper_label(true_lbl), oper_none(), oper_none());
        EMIT(IR_LOAD_IMM, oper_temp(t, 1), oper_imm(1, 1), oper_none());
        EMIT(IR_LABEL, oper_label(end_lbl), oper_none(), oper_none());
        return oper_temp(t, 1);
    }

    /* ── Standard binary ops ──────────────────────────── */
    IROper lv = gen_expr(g, e->binary.left);
    IROper rv = gen_expr(g, e->binary.right);
    int sz    = type_size(expr_type(e));

    /* ── String binary ops ────────────────────────────── */
    if (strcmp(expr_type(e->binary.left), "str") == 0) {
        if (op == TOK_PLUS) {
            int t = new_temp(g, 8);
            EMIT(IR_STR_CONCAT, oper_temp(t, 8), lv, rv);
            return oper_temp(t, 8);
        }
        if (op == TOK_EQ || op == TOK_NE) {
            int t = new_temp(g, 1);
            EMIT_X(IR_STR_EQ, oper_temp(t, 1), lv, rv,
                   op == TOK_NE ? 1 : 0, e->loc);
            return oper_temp(t, 1);
        }
    }

    IROpcode irop;
    switch (op) {
    case TOK_PLUS:    irop = IR_ADD;     break;
    case TOK_MINUS:   irop = IR_SUB;     break;
    case TOK_STAR:    irop = IR_MUL;     break;
    case TOK_SLASH:   irop = IR_DIV;     break;
    case TOK_PERCENT: irop = IR_MOD;     break;
    case TOK_AMP:     irop = IR_BIT_AND; break;
    case TOK_PIPE:    irop = IR_BIT_OR;  break;
    case TOK_CARET:   irop = IR_BIT_XOR; break;
    case TOK_LSHIFT:  irop = IR_SHL;     break;
    case TOK_RSHIFT:  irop = IR_SHR;     break;
    case TOK_EQ:      irop = IR_CMP_EQ;  break;
    case TOK_NE:      irop = IR_CMP_NE;  break;
    case TOK_LT:      irop = IR_CMP_LT;  break;
    case TOK_LE:      irop = IR_CMP_LE;  break;
    case TOK_GT:      irop = IR_CMP_GT;  break;
    case TOK_GE:      irop = IR_CMP_GE;  break;
    default:
        ir_error(g, e->loc, "Unsupported binary operator");
    }

    int is_cmp = (irop == IR_CMP_EQ || irop == IR_CMP_NE ||
                  irop == IR_CMP_LT || irop == IR_CMP_LE ||
                  irop == IR_CMP_GT || irop == IR_CMP_GE);
    int res_sz = is_cmp ? 1 : sz;

    /* ── INLINE CONSTANT FOLD ─────────────────────────── */
    if (lv.kind == OPER_IMM && rv.kind == OPER_IMM) {
        int64_t r = fold_binop(lv.imm, rv.imm, irop);
        return oper_imm(r, res_sz);
    }

    int t = new_temp(g, res_sz);
    {
        int unsig = 0;
        if ((irop == IR_CMP_LT || irop == IR_CMP_LE ||
             irop == IR_CMP_GT || irop == IR_CMP_GE ||
             irop == IR_DIV || irop == IR_MOD || irop == IR_SHR)
            && !is_signed(expr_type(e->binary.left)))
            unsig = 1;
        EMIT_X(irop, oper_temp(t, res_sz), lv, rv, unsig, e->loc);
    }
    return oper_temp(t, res_sz);
}

static IROper gen_unary(SIRGen *g, ASTExpr *e)
{
    IROper ov = gen_expr(g, e->unary.operand);
    int    sz = type_size(expr_type(e));

    /* ── INLINE FOLD ──────────────────────────────────── */
    if (ov.kind == OPER_IMM) {
        if (e->unary.op == TOK_MINUS)
            return oper_imm(-ov.imm, sz);
        else
            return oper_imm(ov.imm ? 0 : 1, 1);
    }

    if (e->unary.op == TOK_MINUS) {
        int t = new_temp(g, sz);
        EMIT(IR_NEG, oper_temp(t, sz), ov, oper_none());
        return oper_temp(t, sz);
    } else {
        int t = new_temp(g, 1);
        EMIT(IR_LOG_NOT, oper_temp(t, 1), ov, oper_none());
        return oper_temp(t, 1);
    }
}

static IROper gen_call(SIRGen *g, ASTExpr *e)
{
    const char *name = e->call.name;
    int sz = type_size(expr_type(e));

    int nargs = e->call.arg_count;

    int update_count = 0;
    if (e->call.update_flags) {
        for (int i = 0; i < nargs; i++)
            if (e->call.update_flags[i]) update_count++;
    }
    int total_args = nargs + update_count;

    IROper *arg_vals = (total_args > 0)
        ? arena_alloc(g->arena, (size_t)total_args * sizeof(IROper))
        : NULL;

    for (int i = 0; i < nargs; i++) {
        const char *arg_type = expr_type(e->call.args[i]);
        ASTFieldDef *afd = find_field(g, arg_type);
        bool is_array_arg = (strcmp(arg_type, "array") == 0);
        if ((afd || is_array_arg) && e->call.args[i]->kind == EXPR_IDENT) {
            int off = scope_lookup(g, e->call.args[i]->ident.name,
                                   e->call.args[i]->loc);
            int ta = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(ta, 8), oper_stack(off, 8),
                 oper_none(), 0, e->loc);
            arg_vals[i] = oper_temp(ta, 8);
        } else {
            arg_vals[i] = gen_expr(g, e->call.args[i]);
        }
    }

    int hi = nargs;
    if (e->call.update_flags) {
        for (int i = 0; i < nargs; i++) {
            if (!e->call.update_flags[i]) continue;
            int off = scope_lookup(g, e->call.args[i]->ident.name,
                                   e->call.args[i]->loc);
            int ta = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(ta, 8), oper_stack(off, 8),
                 oper_none(), 0, e->loc);
            arg_vals[hi++] = oper_temp(ta, 8);
        }
    }

    for (int i = 0; i < total_args; i++)
        emit(g, IR_ARG, oper_imm(i, 4), arg_vals[i], oper_none(), 0, e->loc);

    int t = new_temp(g, sz);
    emit(g, IR_CALL, oper_temp(t, sz), oper_func(name),
         oper_imm(total_args, 4), 0, e->loc);
    return oper_temp(t, sz);
}

/* ── Array / struct base helpers ────────────────────────── */

static IROper gen_array_base(SIRGen *g, ASTExpr *arr, int esz)
{
    if (arr->kind == EXPR_IDENT) {
        int off = scope_lookup(g, arr->ident.name, arr->loc);
        return oper_stack(off, esz);
    }
    return gen_expr(g, arr);
}

static IROper gen_struct_base(SIRGen *g, ASTExpr *obj, int sz)
{
    if (obj->kind == EXPR_IDENT) {
        int off = scope_lookup(g, obj->ident.name, obj->loc);
        return oper_stack(off, sz);
    }
    if (obj->kind == EXPR_FIELD_ACCESS) {
        int parent_sz = type_size(expr_type(obj->field_access.object));
        IROper parent_base = gen_struct_base(g, obj->field_access.object,
                                             parent_sz);
        const char *parent_type = expr_type(obj->field_access.object);
        int mem_off = field_member_offset(g, parent_type,
                                          obj->field_access.member);
        if (parent_base.kind == OPER_STACK)
            return oper_stack(parent_base.stack_off + mem_off, sz);
        if (mem_off == 0)
            return parent_base;
        int ta = new_temp(g, 8);
        emit(g, IR_ADD, oper_temp(ta, 8), parent_base,
             oper_imm(mem_off, 4), 0, obj->loc);
        return oper_temp(ta, 8);
    }
    if (obj->kind == EXPR_INDEX) {
        int esz = type_size(expr_type(obj));
        IROper base = gen_array_base(g, obj->index.array, esz);
        IROper idx  = gen_expr(g, obj->index.index);
        int tm = new_temp(g, 8);
        emit(g, IR_MUL, oper_temp(tm, 8), idx, oper_imm(esz, 4), 0, obj->loc);
        int ta = new_temp(g, 8);
        if (base.kind == OPER_STACK) {
            int tb = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(tb, 8), base, oper_none(), 0, obj->loc);
            emit(g, IR_ADD, oper_temp(ta, 8), oper_temp(tb, 8),
                 oper_temp(tm, 8), 0, obj->loc);
        } else {
            emit(g, IR_ADD, oper_temp(ta, 8), base,
                 oper_temp(tm, 8), 0, obj->loc);
        }
        return oper_temp(ta, 8);
    }
    return gen_expr(g, obj);
}

static IROper gen_index_access(SIRGen *g, ASTExpr *e)
{
    int    sz   = type_size(expr_type(e));
    IROper base = gen_array_base(g, e->index.array, sz);
    IROper idx  = gen_expr(g, e->index.index);
    int    t    = new_temp(g, sz);
    int    unsig = !is_signed(expr_type(e)) ? 0x100 : 0;

    emit(g, IR_INDEX_LOAD, oper_temp(t, sz), base, idx, sz | unsig, e->loc);
    return oper_temp(t, sz);
}

static IROper gen_field_access(SIRGen *g, ASTExpr *e)
{
    int    sz   = type_size(expr_type(e));
    IROper base = gen_struct_base(g, e->field_access.object, sz);
    int    t    = new_temp(g, sz);

    const char *obj_type = expr_type(e->field_access.object);
    int mem_off = field_member_offset(g, obj_type, e->field_access.member);
    int unsig   = !is_signed(expr_type(e)) ? 0x100 : 0;

    emit(g, IR_FIELD_LOAD, oper_temp(t, sz), base,
         oper_imm(mem_off, 4), sz | unsig, e->loc);
    return oper_temp(t, sz);
}

/* ── Enum access → direct IMM (no instruction emitted) ──── */
static IROper gen_enum_access(SIRGen *g, ASTExpr *e)
{
    ASTEnumDef *ed = find_enum(g, e->enum_access.enum_name);
    if (!ed) ir_error(g, e->loc, "Unknown enum: %s", e->enum_access.enum_name);

    int val = 0;
    for (int i = 0; i < ed->variant_count; i++) {
        if (strcmp(ed->variants[i].name, e->enum_access.variant) == 0) {
            val = ed->variants[i].has_value ? ed->variants[i].value : i;
            break;
        }
    }

    int sz = type_size(ed->underlying_type);
    return oper_imm(val, sz);
}

static IROper gen_array_lit(SIRGen *g, ASTExpr *e)
{
    int elem_sz = 4;
    if (e->array_lit.count > 0) {
        const char *et = expr_type(e->array_lit.elements[0]);
        elem_sz = type_size(et);
    }
    AXIS_UNUSED(elem_sz);

    for (int i = 0; i < e->array_lit.count; i++) {
        IROper v = gen_expr(g, e->array_lit.elements[i]);
        AXIS_UNUSED(v);
    }

    int t = new_temp(g, 8);
    EMIT(IR_LOAD_IMM, oper_temp(t, 8), oper_imm(0, 8), oper_none());
    return oper_temp(t, 8);
}

static IROper gen_copy(SIRGen *g, ASTExpr *e)
{
    return gen_expr(g, e->copy.expr);
}

static IROper gen_copy_cast(SIRGen *g, ASTExpr *e)
{
    IROper src = gen_expr(g, e->copy_cast.expr);
    int old_sz = e->copy_cast.old_size;
    int new_sz = e->copy_cast.new_size;

    if (old_sz == new_sz) return src;

    int t = new_temp(g, new_sz);
    if (new_sz > old_sz) {
        IROpcode op = e->copy_cast.is_signed_src ? IR_SEXT : IR_ZEXT;
        EMIT(op, oper_temp(t, new_sz), src, oper_none());
    } else {
        EMIT(IR_TRUNC, oper_temp(t, new_sz), src, oper_none());
    }
    return oper_temp(t, new_sz);
}

static IROper gen_input(SIRGen *g, ASTExpr *e)
{
    /* Optional prompt */
    if (e->input.prompt) {
        IROper pv = gen_expr(g, e->input.prompt);
        emit(g, IR_WRITE, oper_imm(0, 4), pv, oper_none(), 1, e->loc);
    }
    const char *ty = e->inferred_type ? e->inferred_type : "i32";
    int kind = (strcmp(ty, "str") == 0) ? 1 : 0;
    int sz   = type_size(ty);
    int t    = new_temp(g, sz);
    emit(g, IR_READ, oper_temp(t, sz), oper_imm(kind, 4), oper_none(),
         0, e->loc);
    return oper_temp(t, sz);
}

static IROper gen_input_failed(SIRGen *g, ASTExpr *e)
{
    int flag_off = e->input_failed.input_flag_offset;
    int t = new_temp(g, 1);
    emit(g, IR_LOAD_VAR, oper_temp(t, 1), oper_stack(flag_off, 1),
         oper_none(), 0, e->loc);
    return oper_temp(t, 1);
}

/* ── Expression dispatch ────────────────────────────────── */

static IROper gen_expr(SIRGen *g, ASTExpr *e)
{
    switch (e->kind) {
    case EXPR_INT_LIT:      return gen_int_lit(g, e);
    case EXPR_STRING_LIT:   return gen_string_lit(g, e);
    case EXPR_BOOL_LIT:     return gen_bool_lit(g, e);
    case EXPR_IDENT:        return gen_ident(g, e);
    case EXPR_BINARY:       return gen_binop(g, e);
    case EXPR_UNARY:        return gen_unary(g, e);
    case EXPR_CALL:         return gen_call(g, e);
    case EXPR_INDEX:        return gen_index_access(g, e);
    case EXPR_FIELD_ACCESS: return gen_field_access(g, e);
    case EXPR_ENUM_ACCESS:  return gen_enum_access(g, e);
    case EXPR_ARRAY_LIT:    return gen_array_lit(g, e);
    case EXPR_COPY:         return gen_copy(g, e);
    case EXPR_COPY_CAST:    return gen_copy_cast(g, e);
    case EXPR_RANGE:        ir_error(g, e->loc, "Range expression is only valid in a for-loop");
    case EXPR_INPUT:        return gen_input(g, e);
    case EXPR_INPUT_FAILED: return gen_input_failed(g, e);
    }
    ir_error(g, e->loc, "Unsupported expression kind");
}

/* ═════════════════════════════════════════════════════════════
 * Statement IR generation
 * ═════════════════════════════════════════════════════════════ */

static void gen_vardecl(SIRGen *g, ASTStmt *st)
{
    int off = st->var_decl.stack_offset;
    int sz  = type_size(st->var_decl.type_node
                        ? (st->var_decl.type_node->kind == TYPE_NODE_SIMPLE
                           ? st->var_decl.type_node->simple.name : "i32")
                        : "i32");

    /* Array */
    if (st->var_decl.type_node &&
        st->var_decl.type_node->kind == TYPE_NODE_ARRAY)
    {
        int total = st->var_decl.total_size;
        scope_add(g, st->var_decl.name, off, total);

        if (st->var_decl.value && st->var_decl.value->kind == EXPR_ARRAY_LIT) {
            ASTExpr *alit = st->var_decl.value;
            const char *elem_tn = "i32";
            if (st->var_decl.type_node->array.elem &&
                st->var_decl.type_node->array.elem->kind == TYPE_NODE_SIMPLE)
                elem_tn = st->var_decl.type_node->array.elem->simple.name;
            int esz = type_size(elem_tn);

            for (int i = 0; i < alit->array_lit.count; i++) {
                IROper v = gen_expr(g, alit->array_lit.elements[i]);
                int elem_off = off + (i * esz);
                emit(g, IR_STORE_VAR, oper_stack(elem_off, esz),
                     v, oper_none(), 0, st->loc);
            }
        } else if (st->var_decl.value &&
                   st->var_decl.value->kind == EXPR_COPY) {
            ASTExpr *csrc = st->var_decl.value->copy.expr;
            const char *elem_t = "i32";
            if (st->var_decl.type_node->array.elem &&
                st->var_decl.type_node->array.elem->kind == TYPE_NODE_SIMPLE)
                elem_t = st->var_decl.type_node->array.elem->simple.name;
            int cesz = type_size(elem_t);
            IROper src_base = gen_array_base(g, csrc, cesz);
            int src_addr = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(src_addr, 8), src_base,
                 oper_none(), 0, st->loc);
            int t_addr = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(t_addr, 8), oper_stack(off, 8),
                 oper_none(), 0, st->loc);
            emit(g, IR_MEMCPY, oper_temp(t_addr, 8), oper_temp(src_addr, 8),
                 oper_imm(total, 4),
                 st->var_decl.value->copy.compile_time ? 1 : 0,
                 st->loc);
        }
        return;
    }

    /* Field (struct) constructor */
    const char *tn = st->var_decl.type_node
        ? (st->var_decl.type_node->kind == TYPE_NODE_SIMPLE
           ? st->var_decl.type_node->simple.name : NULL)
        : NULL;
    ASTFieldDef *fd = tn ? find_field(g, tn) : NULL;
    if (fd) {
        int total = 0;
        for (int i = 0; i < fd->member_count; i++) {
            const char *mt = fd->members[i].type_node
                ? (fd->members[i].type_node->kind == TYPE_NODE_SIMPLE
                   ? fd->members[i].type_node->simple.name : "i32")
                : "i32";
            total += type_size(mt);
        }
        scope_add(g, st->var_decl.name, off, total);

        if (st->var_decl.value && st->var_decl.value->kind == EXPR_CALL) {
            ASTExpr *ctor = st->var_decl.value;
            int mem_off = 0;
            for (int i = 0; i < fd->member_count && i < ctor->call.arg_count; i++) {
                const char *mt = fd->members[i].type_node
                    ? (fd->members[i].type_node->kind == TYPE_NODE_SIMPLE
                       ? fd->members[i].type_node->simple.name : "i32")
                    : "i32";
                int msz = type_size(mt);
                IROper v = gen_expr(g, ctor->call.args[i]);
                emit(g, IR_STORE_VAR, oper_stack(off + mem_off, msz),
                     v, oper_none(), 0, st->loc);
                mem_off += msz;
            }
        }
        return;
    }

    scope_add(g, st->var_decl.name, off, sz);

    if (st->var_decl.value) {
        /* Automatic alias: already shares stack slot, no store needed */
        if (st->var_decl.value->kind == EXPR_IDENT)
            return;

        /* input(): read value, then copy global flag to per-var slot */
        if (st->var_decl.value->kind == EXPR_INPUT) {
            IROper v = gen_input(g, st->var_decl.value);
            emit(g, IR_STORE_VAR, oper_stack(off, sz), v, oper_none(),
                 0, st->loc);
            /* Copy global read_failed flag to per-variable slot */
            int flag_off = st->var_decl.input_flag_offset;
            int tf = new_temp(g, 1);
            emit(g, IR_READ, oper_temp(tf, 1), oper_imm(3, 4),
                 oper_none(), 0, st->loc);
            emit(g, IR_STORE_VAR, oper_stack(flag_off, 1),
                 oper_temp(tf, 1), oper_none(), 0, st->loc);
            return;
        }

        IROper v = gen_expr(g, st->var_decl.value);
        emit(g, IR_STORE_VAR, oper_stack(off, sz), v, oper_none(),
             0, st->loc);
    }
}

static void gen_assign(SIRGen *g, ASTStmt *st)
{
    int off = scope_lookup(g, st->assign.name, st->loc);
    IROper v = gen_expr(g, st->assign.value);
    SIRLocal *local = scope_find(g, st->assign.name);
    int sz = local ? local->size : (v.size > 0 ? v.size : 4);
    emit(g, IR_STORE_VAR, oper_stack(off, sz), v, oper_none(), 0, st->loc);
}

static void gen_index_assign(SIRGen *g, ASTStmt *st)
{
    IROper val  = gen_expr(g, st->index_assign.value);
    int    esz  = val.size > 0 ? val.size : 4;
    IROper base = gen_array_base(g, st->index_assign.array, esz);
    IROper idx  = gen_expr(g, st->index_assign.index);
    emit(g, IR_INDEX_STORE, base, idx, val, esz, st->loc);
}

static void gen_field_assign(SIRGen *g, ASTStmt *st)
{
    IROper val  = gen_expr(g, st->field_assign.value);
    int    sz   = val.size > 0 ? val.size : 4;
    IROper base = gen_struct_base(g, st->field_assign.object, sz);

    const char *obj_type = expr_type(st->field_assign.object);
    int mem_off = field_member_offset(g, obj_type, st->field_assign.member);
    emit(g, IR_FIELD_STORE, base, oper_imm(mem_off, 4), val, sz, st->loc);
}

static void gen_compound_assign(SIRGen *g, ASTStmt *st)
{
    IROper tv = gen_expr(g, st->compound_assign.target);
    IROper vv = gen_expr(g, st->compound_assign.value);
    int sz = tv.size > 0 ? tv.size : 4;
    int t  = new_temp(g, sz);

    IROpcode irop;
    switch (st->compound_assign.op) {
    case TOK_PLUS_ASSIGN:    irop = IR_ADD;     break;
    case TOK_MINUS_ASSIGN:   irop = IR_SUB;     break;
    case TOK_STAR_ASSIGN:    irop = IR_MUL;     break;
    case TOK_SLASH_ASSIGN:   irop = IR_DIV;     break;
    case TOK_PERCENT_ASSIGN: irop = IR_MOD;     break;
    case TOK_AMP_ASSIGN:     irop = IR_BIT_AND; break;
    case TOK_PIPE_ASSIGN:    irop = IR_BIT_OR;  break;
    case TOK_CARET_ASSIGN:   irop = IR_BIT_XOR; break;
    case TOK_LSHIFT_ASSIGN:  irop = IR_SHL;     break;
    case TOK_RSHIFT_ASSIGN:  irop = IR_SHR;     break;
    default: ir_error(g, st->loc, "Unsupported compound assignment operator");
    }

    emit(g, irop, oper_temp(t, sz), tv, vv, 0, st->loc);

    ASTExpr *tgt = st->compound_assign.target;
    if (tgt->kind == EXPR_IDENT) {
        int off = scope_lookup(g, tgt->ident.name, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(off, sz), oper_temp(t, sz),
             oper_none(), 0, st->loc);
    } else if (tgt->kind == EXPR_INDEX) {
        IROper base = gen_array_base(g, tgt->index.array, sz);
        IROper idx  = gen_expr(g, tgt->index.index);
        emit(g, IR_INDEX_STORE, base, idx, oper_temp(t, sz), sz, st->loc);
    } else if (tgt->kind == EXPR_FIELD_ACCESS) {
        IROper base = gen_struct_base(g, tgt->field_access.object, sz);
        const char *obj_type = expr_type(tgt->field_access.object);
        int mem_off = field_member_offset(g, obj_type,
                                          tgt->field_access.member);
        emit(g, IR_FIELD_STORE, base, oper_imm(mem_off, 4),
             oper_temp(t, sz), sz, st->loc);
    }
}

static int infer_write_type(ASTExpr *e)
{
    const char *ty = e->inferred_type;
    if (ty) {
        if (strcmp(ty, "str") == 0)  return 1;
        if (strcmp(ty, "bool") == 0) return 2;
        if (strcmp(ty, "char") == 0) return 3;
    }
    if (e->kind == EXPR_STRING_LIT) return 1;
    return 0;
}

static void gen_write(SIRGen *g, ASTStmt *st)
{
    IROper v = gen_expr(g, st->write.value);
    int wtype = infer_write_type(st->write.value);
    emit(g, IR_WRITE, oper_imm(st->write.newline ? 1 : 0, 4),
         v, oper_none(), wtype, st->loc);
}



static void emit_update_writeback(SIRGen *g, SrcLoc loc)
{
    IRFunc *fn = g->cur;
    if (!fn->param_info) return;
    for (int i = 0; i < fn->visible_param_count; i++) {
        if (!fn->param_info[i].is_update) continue;
        int val_off  = fn->param_info[i].offset;
        int val_sz   = fn->param_info[i].size;
        int addr_off = fn->param_info[i].wb_offset;

        int ta = new_temp(g, 8);
        emit(g, IR_LOAD_VAR, oper_temp(ta, 8),
             oper_stack(addr_off, 8), oper_none(), 0, loc);

        if (fn->param_info[i].is_field && fn->param_info[i].field_size > 0) {
            int src = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(src, 8), oper_stack(val_off, 8),
                 oper_none(), 0, loc);
            emit(g, IR_MEMCPY, oper_temp(ta, 8), oper_temp(src, 8),
                 oper_imm(fn->param_info[i].field_size, 4), 1, loc);
        } else {
            int tv = new_temp(g, val_sz);
            emit(g, IR_LOAD_VAR, oper_temp(tv, val_sz),
                 oper_stack(val_off, val_sz), oper_none(), 0, loc);
            emit(g, IR_STORE_IND, oper_temp(ta, 8),
                 oper_temp(tv, val_sz), oper_none(), val_sz, loc);
        }
    }
}

static void gen_return(SIRGen *g, ASTStmt *st)
{
    emit_update_writeback(g, st->loc);
    if (st->return_stmt.value) {
        IROper v = gen_expr(g, st->return_stmt.value);
        emit(g, IR_RET, oper_none(), v, oper_none(), 0, st->loc);
    } else {
        emit(g, IR_RET_VOID, oper_none(), oper_none(), oper_none(),
             0, st->loc);
    }
}

static void gen_if(SIRGen *g, ASTStmt *st)
{
    IROper cond = gen_expr(g, st->if_stmt.condition);
    int else_lbl = new_label(g);
    int end_lbl  = new_label(g);

    if (st->if_stmt.else_body) {
        emit(g, IR_JZ, oper_label(else_lbl), cond, oper_none(), 0, st->loc);
    } else {
        emit(g, IR_JZ, oper_label(end_lbl), cond, oper_none(), 0, st->loc);
    }

    scope_push(g);
    for (int i = 0; i < st->if_stmt.body_count; i++)
        gen_stmt(g, st->if_stmt.body[i]);
    scope_pop(g);

    if (st->if_stmt.else_body) {
        emit(g, IR_JMP, oper_label(end_lbl), oper_none(), oper_none(),
             0, st->loc);
        emit(g, IR_LABEL, oper_label(else_lbl), oper_none(), oper_none(),
             0, st->loc);
        scope_push(g);
        for (int i = 0; i < st->if_stmt.else_count; i++)
            gen_stmt(g, st->if_stmt.else_body[i]);
        scope_pop(g);
    }

    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);
}

static void gen_while(SIRGen *g, ASTStmt *st)
{
    int top_lbl = new_label(g);
    int end_lbl = new_label(g);

    int prev_break    = g->break_label;
    int prev_continue = g->continue_label;
    g->break_label    = end_lbl;
    g->continue_label = top_lbl;

    int saved_fc = g->flag_label_count;
    if (st->while_loop.flag) {
        g->flag_labels[g->flag_label_count].name = st->while_loop.flag;
        g->flag_labels[g->flag_label_count].brk  = end_lbl;
        g->flag_labels[g->flag_label_count].cont = top_lbl;
        g->flag_label_count++;
    }

    emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);
    IROper cond = gen_expr(g, st->while_loop.condition);
    emit(g, IR_JZ, oper_label(end_lbl), cond, oper_none(), 0, st->loc);

    scope_push(g);
    for (int i = 0; i < st->while_loop.body_count; i++)
        gen_stmt(g, st->while_loop.body[i]);
    scope_pop(g);

    emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);
    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);

    g->break_label    = prev_break;
    g->continue_label = prev_continue;
    g->flag_label_count = saved_fc;
}

static void gen_repeat(SIRGen *g, ASTStmt *st)
{
    int top_lbl = new_label(g);
    int end_lbl = new_label(g);

    int prev_break    = g->break_label;
    int prev_continue = g->continue_label;
    g->break_label    = end_lbl;
    g->continue_label = top_lbl;

    int saved_fc = g->flag_label_count;
    if (st->repeat_loop.flag) {
        g->flag_labels[g->flag_label_count].name = st->repeat_loop.flag;
        g->flag_labels[g->flag_label_count].brk  = end_lbl;
        g->flag_labels[g->flag_label_count].cont = top_lbl;
        g->flag_label_count++;
    }

    emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);

    scope_push(g);
    for (int i = 0; i < st->repeat_loop.body_count; i++)
        gen_stmt(g, st->repeat_loop.body[i]);
    scope_pop(g);

    emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);
    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);

    g->break_label    = prev_break;
    g->continue_label = prev_continue;
    g->flag_label_count = saved_fc;
}

static void gen_for(SIRGen *g, ASTStmt *st)
{
    int top_lbl  = new_label(g);
    int step_lbl = new_label(g);
    int end_lbl  = new_label(g);

    int prev_break    = g->break_label;
    int prev_continue = g->continue_label;
    g->break_label    = end_lbl;
    g->continue_label = step_lbl;

    int saved_fc = g->flag_label_count;
    if (st->for_loop.flag) {
        g->flag_labels[g->flag_label_count].name = st->for_loop.flag;
        g->flag_labels[g->flag_label_count].brk  = end_lbl;
        g->flag_labels[g->flag_label_count].cont = step_lbl;
        g->flag_label_count++;
    }

    scope_push(g);

    if (st->for_loop.iterable && st->for_loop.iterable->kind == EXPR_RANGE) {
        ASTExpr *rng = st->for_loop.iterable;
        IROper start_v = gen_expr(g, rng->range.start);
        IROper end_v   = gen_expr(g, rng->range.end);
        int sz = start_v.size > 0 ? start_v.size : 4;
        IROper step_v;
        if (rng->range.step)
            step_v = gen_expr(g, rng->range.step);
        else
            step_v = oper_imm(1, sz);

        int var_off = -(g->cur->stack_size + sz);
        g->cur->stack_size += sz + (sz < 8 ? (8 - sz) : 0);
        scope_add(g, st->for_loop.var_name, var_off, sz);

        /* init: var = start */
        emit(g, IR_STORE_VAR, oper_stack(var_off, sz), start_v,
             oper_none(), 0, st->loc);

        /* top: if var >= end goto end_lbl */
        emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
        /* Script-IR: use oper_stack directly for loop var (signed i32) */
        int cmp = new_temp(g, 1);
        emit(g, IR_CMP_GE, oper_temp(cmp, 1), oper_stack(var_off, sz),
             end_v, 0, st->loc);
        emit(g, IR_JNZ, oper_label(end_lbl), oper_temp(cmp, 1), oper_none(),
             0, st->loc);

        /* body */
        for (int i = 0; i < st->for_loop.body_count; i++)
            gen_stmt(g, st->for_loop.body[i]);

        /* step: var += step */
        emit(g, IR_LABEL, oper_label(step_lbl), oper_none(), oper_none(),
             0, st->loc);
        int nxt = new_temp(g, sz);
        emit(g, IR_ADD, oper_temp(nxt, sz), oper_stack(var_off, sz),
             step_v, 0, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(var_off, sz), oper_temp(nxt, sz),
             oper_none(), 0, st->loc);
        emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
    } else {
        /* ── Array for-each ─────────────────────────────── */
        int esz   = st->for_loop.array_elem_size;
        int count = st->for_loop.array_count;
        if (esz <= 0) esz = 4;
        if (count <= 0)
            ir_error(g, st->loc, "Cannot determine array size in for-each loop");

        SIRLocal *arr_loc = scope_find(g, st->for_loop.iterable->ident.name);
        if (!arr_loc)
            ir_error(g, st->loc, "Undefined array '%s' in for-each loop",
                     st->for_loop.iterable->ident.name);
        int arr_off = arr_loc->stack_off;

        int idx_off = -(g->cur->stack_size + 4);
        g->cur->stack_size += 8;

        int var_off = -(g->cur->stack_size + esz);
        g->cur->stack_size += esz + (esz < 8 ? (8 - esz) : 0);
        scope_add(g, st->for_loop.var_name, var_off, esz);

        /* __idx = 0 */
        emit(g, IR_STORE_VAR, oper_stack(idx_off, 4), oper_imm(0, 4),
             oper_none(), 0, st->loc);

        /* top: if __idx >= count goto end */
        emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
        /* Script-IR: use oper_stack directly for index (signed i32) */
        int cmp = new_temp(g, 1);
        emit(g, IR_CMP_GE, oper_temp(cmp, 1), oper_stack(idx_off, 4),
             oper_imm(count, 4), 0, st->loc);
        emit(g, IR_JNZ, oper_label(end_lbl), oper_temp(cmp, 1),
             oper_none(), 0, st->loc);

        /* x = arr[__idx] */
        int t_elem = new_temp(g, esz);
        emit(g, IR_INDEX_LOAD, oper_temp(t_elem, esz),
             oper_stack(arr_off, esz), oper_stack(idx_off, 4), esz, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(var_off, esz),
             oper_temp(t_elem, esz), oper_none(), 0, st->loc);

        /* body */
        for (int i = 0; i < st->for_loop.body_count; i++)
            gen_stmt(g, st->for_loop.body[i]);

        /* step: __idx += 1 */
        emit(g, IR_LABEL, oper_label(step_lbl), oper_none(), oper_none(),
             0, st->loc);
        int t_nxt = new_temp(g, 4);
        emit(g, IR_ADD, oper_temp(t_nxt, 4), oper_stack(idx_off, 4),
             oper_imm(1, 4), 0, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(idx_off, 4), oper_temp(t_nxt, 4),
             oper_none(), 0, st->loc);
        emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
    }

    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);
    scope_pop(g);

    g->break_label    = prev_break;
    g->continue_label = prev_continue;
    g->flag_label_count = saved_fc;
}

static void gen_match(SIRGen *g, ASTStmt *st)
{
    IROper val = gen_expr(g, st->match.expr);
    int end_lbl = new_label(g);

    for (int a = 0; a < st->match.arm_count; a++) {
        ASTMatchArm *arm = &st->match.arms[a];
        int next_lbl = new_label(g);

        if (!arm->is_wildcard && arm->pattern) {
            IROper pv = gen_expr(g, arm->pattern);
            int cmp = new_temp(g, 1);
            emit(g, IR_CMP_NE, oper_temp(cmp, 1), val, pv, 0, st->loc);
            emit(g, IR_JNZ, oper_label(next_lbl), oper_temp(cmp, 1),
                 oper_none(), 0, st->loc);
        }

        scope_push(g);
        for (int i = 0; i < arm->body_count; i++)
            gen_stmt(g, arm->body[i]);
        scope_pop(g);

        emit(g, IR_JMP, oper_label(end_lbl), oper_none(), oper_none(),
             0, st->loc);
        emit(g, IR_LABEL, oper_label(next_lbl), oper_none(), oper_none(),
             0, st->loc);
    }

    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);
}

static void gen_syscall(SIRGen *g, ASTStmt *st)
{
    for (int i = 0; i < st->syscall.arg_count; i++) {
        IROper v = gen_expr(g, st->syscall.args[i]);
        emit(g, IR_ARG, oper_imm(i, 4), v, oper_none(), 0, st->loc);
    }
    emit(g, IR_SYSCALL, oper_none(), oper_imm(st->syscall.arg_count, 4),
         oper_none(), 0, st->loc);
}

/* ── Update cast: update X as TYPE ───────────────────────── */

static void gen_update_cast(SIRGen *g, ASTStmt *st)
{
    int old_off = st->update_cast.old_offset;
    int new_off = st->update_cast.new_offset;
    int old_sz  = st->update_cast.old_size;
    int new_sz  = st->update_cast.new_size;

    if (old_sz == new_sz) return;               /* no-op cast */

    /* Load current value from old stack slot */
    int t1 = new_temp(g, old_sz);
    emit(g, IR_LOAD_VAR, oper_temp(t1, old_sz),
         oper_stack(old_off, old_sz), oper_none(), 0, st->loc);

    /* Convert */
    int t2 = new_temp(g, new_sz);
    if (new_sz > old_sz) {
        IROpcode op = st->update_cast.is_signed_src ? IR_SEXT : IR_ZEXT;
        emit(g, op, oper_temp(t2, new_sz),
             oper_temp(t1, old_sz), oper_none(), 0, st->loc);
    } else {
        emit(g, IR_TRUNC, oper_temp(t2, new_sz),
             oper_temp(t1, old_sz), oper_none(), 0, st->loc);
    }

    /* Store to new stack slot */
    emit(g, IR_STORE_VAR, oper_stack(new_off, new_sz),
         oper_temp(t2, new_sz), oper_none(), 0, st->loc);

    /* Update scope entry */
    SIRLocal *local = scope_find(g, st->update_cast.var_name);
    if (local) {
        local->size      = new_sz;
        local->stack_off = new_off;
    }
}

/* ── Statement dispatch ─────────────────────────────────── */

static void gen_stmt(SIRGen *g, ASTStmt *st)
{
    switch (st->kind) {
    case STMT_VAR_DECL:        gen_vardecl(g, st);           break;
    case STMT_ASSIGN:          gen_assign(g, st);            break;
    case STMT_INDEX_ASSIGN:    gen_index_assign(g, st);      break;
    case STMT_FIELD_ASSIGN:    gen_field_assign(g, st);      break;
    case STMT_COMPOUND_ASSIGN: gen_compound_assign(g, st);   break;
    case STMT_WRITE:           gen_write(g, st);             break;

    case STMT_IF:              gen_if(g, st);                break;
    case STMT_WHILE:           gen_while(g, st);             break;
    case STMT_REPEAT:          gen_repeat(g, st);            break;
    case STMT_FOR:             gen_for(g, st);               break;
    case STMT_MATCH:           gen_match(g, st);             break;
    case STMT_RETURN:          gen_return(g, st);            break;
    case STMT_SYSCALL:         gen_syscall(g, st);           break;
    case STMT_UPDATE_CAST:     gen_update_cast(g, st);       break;
    case STMT_EXPR:
        gen_expr(g, st->expr_stmt.expr);
        break;
    case STMT_BREAK: {
        int lbl = g->break_label;
        if (st->break_stmt.flag) {
            for (int i = g->flag_label_count - 1; i >= 0; i--)
                if (strcmp(g->flag_labels[i].name, st->break_stmt.flag) == 0)
                    { lbl = g->flag_labels[i].brk; break; }
        }
        emit(g, IR_JMP, oper_label(lbl), oper_none(),
             oper_none(), 0, st->loc);
        break;
    }
    case STMT_CONTINUE: {
        int lbl = g->continue_label;
        if (st->continue_stmt.flag) {
            for (int i = g->flag_label_count - 1; i >= 0; i--)
                if (strcmp(g->flag_labels[i].name, st->continue_stmt.flag) == 0)
                    { lbl = g->flag_labels[i].cont; break; }
        }
        emit(g, IR_JMP, oper_label(lbl), oper_none(),
             oper_none(), 0, st->loc);
        break;
    }
    }
}

/* ═════════════════════════════════════════════════════════════
 * Function IR generation
 * ═════════════════════════════════════════════════════════════ */

static void gen_function(SIRGen *g, ASTFunction *fn, IRFunc *out)
{
    memset(out, 0, sizeof(*out));
    out->name        = fn->name;
    out->stack_size  = fn->stack_size;
    out->param_count = fn->param_count;

    int update_count = 0;
    for (int i = 0; i < fn->param_count; i++)
        if (fn->params[i].is_update) update_count++;
    int total_params = fn->param_count + update_count;
    out->visible_param_count = fn->param_count;

    if (total_params > 0) {
        out->param_count = total_params;
        out->param_info = arena_alloc(g->arena,
            (size_t)total_params * sizeof(IRParamInfo));
        memset(out->param_info, 0, (size_t)total_params * sizeof(IRParamInfo));

        for (int i = 0; i < fn->param_count; i++) {
            ASTParam *p = &fn->params[i];
            const char *pt = p->type_node
                ? (p->type_node->kind == TYPE_NODE_SIMPLE
                   ? p->type_node->simple.name : "i32")
                : "i32";
            out->param_info[i].offset    = p->stack_offset;
            out->param_info[i].size      = type_size(pt);
            out->param_info[i].is_update = p->is_update;

            if (p->type_node && p->type_node->kind == TYPE_NODE_ARRAY) {
                const char *et = p->type_node->array.elem
                    ? (p->type_node->array.elem->kind == TYPE_NODE_SIMPLE
                       ? p->type_node->array.elem->simple.name : "i32")
                    : "i32";
                int elem_sz = type_size(et);
                int cnt = p->type_node->array.size;
                out->param_info[i].is_field   = true;
                out->param_info[i].size       = 8;
                out->param_info[i].field_size = elem_sz * (cnt > 0 ? cnt : 1);
            }

            ASTFieldDef *pfd = find_field(g, pt);
            if (pfd) {
                out->param_info[i].is_field   = true;
                out->param_info[i].size       = 8;
                int fsz = 0;
                for (int j = 0; j < pfd->member_count; j++) {
                    const char *mt = pfd->members[j].type_node
                        ? (pfd->members[j].type_node->kind == TYPE_NODE_SIMPLE
                           ? pfd->members[j].type_node->simple.name : "i32")
                        : "i32";
                    ASTFieldDef *mfd = find_field(g, mt);
                    if (mfd) {
                        for (int k = 0; k < mfd->member_count; k++) {
                            const char *mmt = mfd->members[k].type_node
                                ? (mfd->members[k].type_node->kind == TYPE_NODE_SIMPLE
                                   ? mfd->members[k].type_node->simple.name : "i32")
                                : "i32";
                            fsz += type_size(mmt);
                        }
                    } else {
                        fsz += type_size(mt);
                    }
                }
                out->param_info[i].field_size = fsz;
            }
            if (p->is_update) {
                int cur = -(p->stack_offset);
                cur = (cur + 7) & ~7;
                cur += 8;
                out->param_info[i].wb_offset = -cur;
            }
        }

        int hi = fn->param_count;
        for (int i = 0; i < fn->param_count; i++) {
            if (!fn->params[i].is_update) continue;
            out->param_info[hi].offset    = out->param_info[i].wb_offset;
            out->param_info[hi].size      = 8;
            out->param_info[hi].is_update = false;
            out->param_info[hi].wb_offset = 0;
            hi++;
        }
    }

    IRFunc *prev = g->cur;
    g->cur = out;

    scope_push(g);

    for (int i = 0; i < fn->param_count; i++) {
        ASTParam *p = &fn->params[i];
        const char *pt = p->type_node
            ? (p->type_node->kind == TYPE_NODE_SIMPLE
               ? p->type_node->simple.name : "i32")
            : "i32";
        int sz = type_size(pt);
        scope_add(g, p->name, p->stack_offset, sz);
    }

    for (int i = 0; i < fn->body_count; i++)
        gen_stmt(g, fn->body[i]);

    if (out->instr_count == 0 ||
        (out->instrs[out->instr_count - 1].op != IR_RET &&
         out->instrs[out->instr_count - 1].op != IR_RET_VOID))
    {
        SrcLoc end_loc = fn->loc;
        emit_update_writeback(g, end_loc);
        emit(g, IR_RET_VOID, oper_none(), oper_none(), oper_none(),
             0, end_loc);
    }

    scope_pop(g);
    g->cur = prev;
}

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

void script_ir_generate(IRProgram *p, ASTProgram *ast, const char *filename,
                        const char *source)
{
    SIRGen g;
    memset(&g, 0, sizeof(g));
    g.prog           = p;
    g.arena          = p->arena;
    g.filename       = filename;
    g.source         = source;
    g.field_defs     = ast->field_defs;
    g.field_count    = ast->field_count;
    g.enum_defs      = ast->enum_defs;
    g.enum_count     = ast->enum_count;
    g.break_label    = -1;
    g.continue_label = -1;

    s_scope = NULL;

    /* ── Functions ────────────────────────────────────── */
    if (ast->func_count > 0) {
        int cap = ast->func_count;
        p->funcs = arena_alloc(g.arena, (size_t)cap * sizeof(IRFunc));
        p->func_cap = cap;
        for (int i = 0; i < ast->func_count; i++) {
            gen_function(&g, &ast->functions[i], &p->funcs[p->func_count]);
            p->func_count++;
        }
    }

    /* ── Top-level statements ─────────────────────────── */
    if (ast->stmt_count > 0) {
        memset(&p->top_level, 0, sizeof(IRFunc));
        p->top_level.name       = "__top_level__";
        p->top_level.stack_size = 256;
        g.cur = &p->top_level;

        scope_push(&g);
        for (int i = 0; i < ast->stmt_count; i++)
            gen_stmt(&g, ast->statements[i]);
        scope_pop(&g);

        emit(&g, IR_RET_VOID, oper_none(), oper_none(), oper_none(),
             0, ast->loc);
    }
}
