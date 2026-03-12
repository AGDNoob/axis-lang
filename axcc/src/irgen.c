/*
 * irgen.c – AST → three-address-code IR lowering for AXIS.
 *
 * Each expression produces a virtual-register (temp) result.
 * Statements emit loads/stores and control-flow instructions.
 * Logical AND/OR are lowered to short-circuit jumps here.
 */

#include "axis_ir.h"
#include "axis_semantic.h"   /* for field/enum lookup helpers re-used */
#include <stdarg.h>
#include <inttypes.h>

/* ═════════════════════════════════════════════════════════════
 * Internal generator state
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    IRProgram    *prog;
    IRFunc       *cur;          /* function currently being lowered */
    Arena        *arena;
    const char   *filename;

    /* For resolving types → sizes we keep the semantic data around. */
    ASTFieldDef  *field_defs;
    int           field_count;
    ASTEnumDef   *enum_defs;
    int           enum_count;

    /* Break/continue targets (label ids) for innermost loop */
    int           break_label;
    int           continue_label;
} IRGen;

/* ── Forward declarations ─────────────────────────────────── */
static IROper gen_expr(IRGen *g, ASTExpr *e);
static void   gen_stmt(IRGen *g, ASTStmt *st);

/* ═════════════════════════════════════════════════════════════
 * Helpers
 * ═════════════════════════════════════════════════════════════ */

static _Noreturn void ir_error(IRGen *g, SrcLoc loc,
                               const char *fmt, ...)
{
    fprintf(stderr, "%s:%d:%d: IR error: ",
            g->filename ? g->filename : "<unknown>", loc.line, loc.col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
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

static int new_temp(IRGen *g, int size)
{
    AXIS_UNUSED(size);
    return g->cur->temp_count++;
}

static int new_label(IRGen *g)
{
    static int label_counter = 0;
    AXIS_UNUSED(g);
    return label_counter++;
}

/* ── Emit instruction ─────────────────────────────────────── */

static void emit(IRGen *g, IROpcode op, IROper dest, IROper src1,
                 IROper src2, int extra, SrcLoc loc)
{
    IRFunc *f = g->cur;
    if (f->instr_count >= f->instr_cap) {
        int newcap = f->instr_cap < 64 ? 64 : f->instr_cap * 2;
        IRInstr *ni = arena_alloc(g->arena, (size_t)newcap * sizeof(IRInstr));
        if (f->instr_count > 0)
            memcpy(ni, f->instrs, (size_t)f->instr_count * sizeof(IRInstr));
        f->instrs   = ni;
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
 * Name → stack-offset tracking (simple lexical scope for IR)
 * Defined here so expression generators can call irgen_name_lookup.
 * ═════════════════════════════════════════════════════════════ */

typedef struct IRScope IRScope;
typedef struct IRLocal IRLocal;

struct IRLocal {
    const char *name;
    int         stack_off;
    int         size;
    IRLocal    *next;
};

struct IRScope {
    IRScope *parent;
    IRLocal *locals;
};

static IRScope *s_scope = NULL;

static IRScope *irgen_scope_push(IRGen *g)
{
    IRScope *sc = ARENA_NEW(g->arena, IRScope);
    sc->parent = s_scope;
    sc->locals = NULL;
    s_scope = sc;
    return sc;
}

static void irgen_scope_pop(IRGen *g)
{
    AXIS_UNUSED(g);
    assert(s_scope);
    s_scope = s_scope->parent;
}

static void irgen_scope_add(IRGen *g, const char *name,
                            int stack_off, int size)
{
    IRLocal *l = ARENA_NEW(g->arena, IRLocal);
    l->name      = name;
    l->stack_off = stack_off;
    l->size      = size;
    l->next      = s_scope->locals;
    s_scope->locals = l;
}

static int irgen_name_lookup(IRGen *g, const char *name, SrcLoc loc)
{
    for (IRScope *sc = s_scope; sc; sc = sc->parent)
        for (IRLocal *l = sc->locals; l; l = l->next)
            if (strcmp(l->name, name) == 0)
                return l->stack_off;
    ir_error(g, loc, "IRGen: variable '%s' not found in scope", name);
}

/* Return the IRLocal entry for a variable (or NULL if not found). */
static IRLocal *irgen_scope_lookup(IRGen *g, const char *name)
{
    AXIS_UNUSED(g);
    for (IRScope *sc = s_scope; sc; sc = sc->parent)
        for (IRLocal *l = sc->locals; l; l = l->next)
            if (strcmp(l->name, name) == 0)
                return l;
    return NULL;
}

/* ── String table ─────────────────────────────────────────── */

static int intern_string(IRGen *g, const char *str)
{
    IRProgram *p = g->prog;
    /* Deduplicate */
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

/* Used later by x64 codegen via expr_type; keep available. */
__attribute__((unused))
static bool is_signed(const char *t)
{
    if (!t) return true;
    return t[0] == 'i';
}

static const char *expr_type(ASTExpr *e)
{
    return e->inferred_type ? e->inferred_type : "i32";
}

static ASTFieldDef *irgen_find_field(IRGen *g, const char *name)
{
    for (int i = 0; i < g->field_count; i++)
        if (strcmp(g->field_defs[i].name, name) == 0)
            return &g->field_defs[i];
    return NULL;
}

static ASTEnumDef *irgen_find_enum(IRGen *g, const char *name)
{
    for (int i = 0; i < g->enum_count; i++)
        if (strcmp(g->enum_defs[i].name, name) == 0)
            return &g->enum_defs[i];
    return NULL;
}

static int irgen_field_member_offset(IRGen *g, const char *field_type,
                                     const char *member)
{
    ASTFieldDef *fd = irgen_find_field(g, field_type);
    if (!fd) return 0;
    int off = 0;
    for (int i = 0; i < fd->member_count; i++) {
        if (strcmp(fd->members[i].name, member) == 0)
            return off;
        const char *mt = fd->members[i].type_node
                         ? (fd->members[i].type_node->kind == TYPE_NODE_SIMPLE
                            ? fd->members[i].type_node->simple.name : "i32")
                         : "i32";
        ASTFieldDef *mfd = irgen_find_field(g, mt);
        if (mfd) {
            /* nested field — sum of its members */
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
    return off; /* member not found → returns total size */
}

/* ═════════════════════════════════════════════════════════════
 * Expression IR generation  (returns IROper with the result temp)
 * ═════════════════════════════════════════════════════════════ */

static IROper gen_int_lit(IRGen *g, ASTExpr *e)
{
    int sz = type_size(expr_type(e));
    int t  = new_temp(g, sz);
    EMIT(IR_LOAD_IMM, oper_temp(t, sz), oper_imm(e->int_lit.value, sz),
         oper_none());
    return oper_temp(t, sz);
}

static IROper gen_string_lit(IRGen *g, ASTExpr *e)
{
    int idx = intern_string(g, e->string_lit.value);
    int t   = new_temp(g, 8);
    EMIT(IR_LOAD_STR, oper_temp(t, 8), oper_str(idx), oper_none());
    return oper_temp(t, 8);
}

static IROper gen_bool_lit(IRGen *g, ASTExpr *e)
{
    int t = new_temp(g, 1);
    EMIT(IR_LOAD_IMM, oper_temp(t, 1),
         oper_imm(e->bool_lit.value ? 1 : 0, 1), oper_none());
    return oper_temp(t, 1);
}

static IROper gen_ident(IRGen *g, ASTExpr *e)
{
    int sz = type_size(expr_type(e));
    int t  = new_temp(g, sz);
    int off = irgen_name_lookup(g, e->ident.name, e->loc);
    /* extra=1 flags unsigned type → x64 uses zero-extension */
    int unsig = !is_signed(expr_type(e)) ? 1 : 0;
    emit(g, IR_LOAD_VAR, oper_temp(t, sz), oper_stack(off, sz),
         oper_none(), unsig, e->loc);
    return oper_temp(t, sz);
}

static IROper gen_binop(IRGen *g, ASTExpr *e)
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
    int t     = new_temp(g, sz);

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
        ir_error(g, e->loc, "Unknown binary operator");
    }

    {
        int unsig = 0;
        if ((irop == IR_CMP_LT || irop == IR_CMP_LE ||
             irop == IR_CMP_GT || irop == IR_CMP_GE ||
             irop == IR_DIV || irop == IR_MOD || irop == IR_SHR)
            && !is_signed(expr_type(e->binary.left)))
            unsig = 1;
        EMIT_X(irop, oper_temp(t, sz), lv, rv, unsig, e->loc);
    }
    return oper_temp(t, sz);
}

static IROper gen_unary(IRGen *g, ASTExpr *e)
{
    IROper ov = gen_expr(g, e->unary.operand);
    int    sz = type_size(expr_type(e));
    int    t  = new_temp(g, sz);

    if (e->unary.op == TOK_MINUS) {
        EMIT(IR_NEG, oper_temp(t, sz), ov, oper_none());
    } else {    /* TOK_NOT / TOK_BANG */
        EMIT(IR_LOG_NOT, oper_temp(t, 1), ov, oper_none());
    }
    return oper_temp(t, sz);
}

static IROper gen_call(IRGen *g, ASTExpr *e)
{
    const char *name = e->call.name;
    int sz = type_size(expr_type(e));

    /* Built-in I/O */
    if (strcmp(name, "read") == 0 || strcmp(name, "readln") == 0 ||
        strcmp(name, "readchar") == 0)
    {
        int kind = strcmp(name, "readchar") == 0 ? 2
                 : strcmp(name, "readln") == 0   ? 1 : 0;
        int t = new_temp(g, sz);
        EMIT(IR_READ, oper_temp(t, sz), oper_imm(kind, 4), oper_none());
        return oper_temp(t, sz);
    }

    /* Evaluate all arguments into temps first, so that nested calls
     * don't clobber the outer call's argument registers. */
    int nargs = e->call.arg_count;

    /* Count hidden address args for 'update' parameters */
    int update_count = 0;
    if (e->call.update_flags) {
        for (int i = 0; i < nargs; i++)
            if (e->call.update_flags[i]) update_count++;
    }
    int total_args = nargs + update_count;

    IROper *arg_vals = (total_args > 0)
        ? arena_alloc(g->arena, (size_t)total_args * sizeof(IROper))
        : NULL;

    /* Evaluate visible args — field/array-type args are passed by pointer */
    for (int i = 0; i < nargs; i++) {
        const char *arg_type = expr_type(e->call.args[i]);
        ASTFieldDef *afd = irgen_find_field(g, arg_type);
        bool is_array_arg = (strcmp(arg_type, "array") == 0);
        if ((afd || is_array_arg) && e->call.args[i]->kind == EXPR_IDENT) {
            /* Pass address of the field/array variable */
            int off = irgen_name_lookup(g, e->call.args[i]->ident.name,
                                        e->call.args[i]->loc);
            int ta = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(ta, 8), oper_stack(off, 8),
                 oper_none(), 0, e->loc);
            arg_vals[i] = oper_temp(ta, 8);
        } else {
            arg_vals[i] = gen_expr(g, e->call.args[i]);
        }
    }

    /* Append hidden address args for each update parameter */
    int hi = nargs;
    if (e->call.update_flags) {
        for (int i = 0; i < nargs; i++) {
            if (!e->call.update_flags[i]) continue;
            /* The arg is guaranteed to be EXPR_IDENT by semantic pass */
            int off = irgen_name_lookup(g, e->call.args[i]->ident.name,
                                        e->call.args[i]->loc);
            int ta = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(ta, 8), oper_stack(off, 8),
                 oper_none(), 0, e->loc);
            arg_vals[hi++] = oper_temp(ta, 8);
        }
    }

    /* Now emit all IR_ARGs back-to-back, right before the CALL */
    for (int i = 0; i < total_args; i++)
        emit(g, IR_ARG, oper_imm(i, 4), arg_vals[i], oper_none(), 0, e->loc);

    int t = new_temp(g, sz);
    emit(g, IR_CALL, oper_temp(t, sz), oper_func(name),
         oper_imm(total_args, 4), 0, e->loc);
    return oper_temp(t, sz);
}

/* Resolve an array expression to an OPER_STACK base operand.
 * For a simple identifier, returns oper_stack directly (LEA in x64).
 * For anything else, falls back to gen_expr (OPER_TEMP). */
static IROper gen_array_base(IRGen *g, ASTExpr *arr, int esz)
{
    if (arr->kind == EXPR_IDENT) {
        int off = irgen_name_lookup(g, arr->ident.name, arr->loc);
        return oper_stack(off, esz);
    }
    return gen_expr(g, arr);
}

/* Return the base operand for a struct/field variable.
   Like gen_array_base, bypasses gen_ident→IR_LOAD_VAR so the x64
   backend receives an OPER_STACK and uses LEA (address) not MOV (value). */
static IROper gen_struct_base(IRGen *g, ASTExpr *obj, int sz)
{
    if (obj->kind == EXPR_IDENT) {
        int off = irgen_name_lookup(g, obj->ident.name, obj->loc);
        return oper_stack(off, sz);
    }
    /* Nested field access: compute ADDRESS of the sub-field, don't load VALUE.
     * e.g. for r.origin.x — gen_struct_base(r.origin) returns address of origin
     * so that the outer FIELD_LOAD can add the member offset and dereference. */
    if (obj->kind == EXPR_FIELD_ACCESS) {
        int parent_sz = type_size(expr_type(obj->field_access.object));
        IROper parent_base = gen_struct_base(g, obj->field_access.object, parent_sz);
        const char *parent_type = expr_type(obj->field_access.object);
        int mem_off = irgen_field_member_offset(g, parent_type,
                                                obj->field_access.member);
        if (parent_base.kind == OPER_STACK) {
            /* Fold offset directly: base_off + member_off */
            return oper_stack(parent_base.stack_off + mem_off, sz);
        }
        /* Parent is already an address in a temp — add offset */
        if (mem_off == 0)
            return parent_base;
        int ta = new_temp(g, 8);
        emit(g, IR_ADD, oper_temp(ta, 8), parent_base,
             oper_imm(mem_off, 4), 0, obj->loc);
        return oper_temp(ta, 8);
    }
    /* Index access on array of fields: compute element ADDRESS, not VALUE.
     * e.g. points[0].x — gen_struct_base(points[0]) returns address of element. */
    if (obj->kind == EXPR_INDEX) {
        int esz = type_size(expr_type(obj));
        IROper base = gen_array_base(g, obj->index.array, esz);
        IROper idx  = gen_expr(g, obj->index.index);
        /* addr = base + idx * esz */
        int tm = new_temp(g, 8);
        emit(g, IR_MUL, oper_temp(tm, 8), idx, oper_imm(esz, 4), 0, obj->loc);
        int ta = new_temp(g, 8);
        if (base.kind == OPER_STACK) {
            /* LEA base addr, then add */
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

static IROper gen_index_access(IRGen *g, ASTExpr *e)
{
    int    sz   = type_size(expr_type(e));
    IROper base = gen_array_base(g, e->index.array, sz);
    IROper idx  = gen_expr(g, e->index.index);
    int    t    = new_temp(g, sz);
    int    unsig = !is_signed(expr_type(e)) ? 0x100 : 0;

    emit(g, IR_INDEX_LOAD, oper_temp(t, sz), base, idx, sz | unsig, e->loc);
    return oper_temp(t, sz);
}

static IROper gen_field_access(IRGen *g, ASTExpr *e)
{
    int    sz   = type_size(expr_type(e));
    IROper base = gen_struct_base(g, e->field_access.object, sz);
    int    t    = new_temp(g, sz);

    /* Compute member offset within the field struct */
    const char *obj_type = expr_type(e->field_access.object);
    int mem_off = irgen_field_member_offset(g, obj_type,
                                            e->field_access.member);
    int unsig = !is_signed(expr_type(e)) ? 0x100 : 0;

    emit(g, IR_FIELD_LOAD, oper_temp(t, sz), base,
         oper_imm(mem_off, 4), sz | unsig, e->loc);
    return oper_temp(t, sz);
}

static IROper gen_enum_access(IRGen *g, ASTExpr *e)
{
    ASTEnumDef *ed = irgen_find_enum(g, e->enum_access.enum_name);
    if (!ed) ir_error(g, e->loc, "Unknown enum: %s", e->enum_access.enum_name);

    int val = 0;
    for (int i = 0; i < ed->variant_count; i++) {
        if (strcmp(ed->variants[i].name, e->enum_access.variant) == 0) {
            val = ed->variants[i].has_value ? ed->variants[i].value : i;
            break;
        }
    }

    int sz = type_size(ed->underlying_type);
    int t  = new_temp(g, sz);
    EMIT(IR_LOAD_IMM, oper_temp(t, sz), oper_imm(val, sz), oper_none());
    return oper_temp(t, sz);
}

static IROper gen_array_lit(IRGen *g, ASTExpr *e)
{
    /* Array literals should have been stored into a stack variable
       during STMT_VAR_DECL.  If encountered standalone, create a temp
       base address and store elements. */
    int elem_sz = 4;   /* fallback; real size from type if available */
    if (e->array_lit.count > 0) {
        const char *et = expr_type(e->array_lit.elements[0]);
        elem_sz = type_size(et);
    }
    AXIS_UNUSED(elem_sz);

    for (int i = 0; i < e->array_lit.count; i++) {
        IROper v = gen_expr(g, e->array_lit.elements[i]);
        /* Store element – address will be patched during var-decl gen */
        AXIS_UNUSED(v);
    }

    /* Return a placeholder – usually arrays flow through var_decl */
    int t = new_temp(g, 8);
    EMIT(IR_LOAD_IMM, oper_temp(t, 8), oper_imm(0, 8), oper_none());
    return oper_temp(t, 8);
}

static IROper gen_copy(IRGen *g, ASTExpr *e)
{
    return gen_expr(g, e->copy.expr);
}

static IROper gen_read_failed(IRGen *g, ASTExpr *e)
{
    (void)e;
    int t = new_temp(g, 1);
    EMIT(IR_READ, oper_temp(t, 1), oper_imm(3, 4), oper_none());
    return oper_temp(t, 1);
}

/* ── Expression dispatch ────────────────────────────────── */

static IROper gen_expr(IRGen *g, ASTExpr *e)
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
    case EXPR_RANGE:        ir_error(g, e->loc, "Range not in for-loop");
    case EXPR_READ_FAILED:  return gen_read_failed(g, e);
    }
    ir_error(g, e->loc, "Unknown expression kind in IR gen");
}

/* ═════════════════════════════════════════════════════════════
 * Statement IR generation
 * ═════════════════════════════════════════════════════════════ */

static void gen_vardecl(IRGen *g, ASTStmt *st)
{
    int off  = st->var_decl.stack_offset;
    int sz   = type_size(st->var_decl.type_node
                         ? (st->var_decl.type_node->kind == TYPE_NODE_SIMPLE
                            ? st->var_decl.type_node->simple.name : "i32")
                         : "i32");

    /* Array */
    if (st->var_decl.type_node &&
        st->var_decl.type_node->kind == TYPE_NODE_ARRAY)
    {
        int total = st->var_decl.total_size;
        irgen_scope_add(g, st->var_decl.name, off, total);

        if (st->var_decl.value && st->var_decl.value->kind == EXPR_ARRAY_LIT) {
            ASTExpr *alit = st->var_decl.value;
            const char *elem_tn = "i32";
            if (st->var_decl.type_node->array.elem &&
                st->var_decl.type_node->array.elem->kind == TYPE_NODE_SIMPLE)
                elem_tn = st->var_decl.type_node->array.elem->simple.name;
            int esz = type_size(elem_tn);

            for (int i = 0; i < alit->array_lit.count; i++) {
                IROper v = gen_expr(g, alit->array_lit.elements[i]);
                int elem_off = off + (i * esz);  /* array grows upward */
                emit(g, IR_STORE_VAR, oper_stack(elem_off, esz),
                     v, oper_none(), 0, st->loc);
            }
        } else if (st->var_decl.value &&
                   st->var_decl.value->kind == EXPR_COPY) {
            /* Copy from another array — need ADDRESS of source, not value */
            ASTExpr *csrc = st->var_decl.value->copy.expr;
            const char *elem_t = "i32";
            if (st->var_decl.type_node->array.elem &&
                st->var_decl.type_node->array.elem->kind == TYPE_NODE_SIMPLE)
                elem_t = st->var_decl.type_node->array.elem->simple.name;
            int cesz = type_size(elem_t);
            IROper src_base = gen_array_base(g, csrc, cesz);
            /* src_base is OPER_STACK for identifiers → x64 will LEA.
               If it's a temp (non-ident), it's already an address. */
            int src_addr = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(src_addr, 8), src_base,
                 oper_none(), 0, st->loc);
            int t_addr = new_temp(g, 8);
            emit(g, IR_LEA, oper_temp(t_addr, 8), oper_stack(off, 8),
                 oper_none(), 0, st->loc);
            emit(g, IR_MEMCPY, oper_temp(t_addr, 8), oper_temp(src_addr, 8),
                 oper_imm(total, 4), 0, st->loc);
        }
        return;
    }

    /* Field (struct) constructor: expand Point(10, 20) into per-member stores */
    const char *tn = st->var_decl.type_node
        ? (st->var_decl.type_node->kind == TYPE_NODE_SIMPLE
           ? st->var_decl.type_node->simple.name : NULL)
        : NULL;
    ASTFieldDef *fd = tn ? irgen_find_field(g, tn) : NULL;
    if (fd) {
        /* Compute total size from field def members */
        int total = 0;
        for (int i = 0; i < fd->member_count; i++) {
            const char *mt = fd->members[i].type_node
                ? (fd->members[i].type_node->kind == TYPE_NODE_SIMPLE
                   ? fd->members[i].type_node->simple.name : "i32")
                : "i32";
            total += type_size(mt);
        }
        irgen_scope_add(g, st->var_decl.name, off, total);

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

    irgen_scope_add(g, st->var_decl.name, off, sz);

    if (st->var_decl.value) {
        IROper v = gen_expr(g, st->var_decl.value);
        emit(g, IR_STORE_VAR, oper_stack(off, sz), v, oper_none(),
             0, st->loc);
    }
}

static void gen_assign(IRGen *g, ASTStmt *st)
{
    int off = irgen_name_lookup(g, st->assign.name, st->loc);
    IROper v = gen_expr(g, st->assign.value);
    int sz = v.size > 0 ? v.size : 4;
    emit(g, IR_STORE_VAR, oper_stack(off, sz), v, oper_none(), 0, st->loc);
}

static void gen_index_assign(IRGen *g, ASTStmt *st)
{
    IROper val  = gen_expr(g, st->index_assign.value);
    int    esz  = val.size > 0 ? val.size : 4;
    IROper base = gen_array_base(g, st->index_assign.array, esz);
    IROper idx  = gen_expr(g, st->index_assign.index);
    emit(g, IR_INDEX_STORE, base, idx, val, esz, st->loc);
}

static void gen_field_assign(IRGen *g, ASTStmt *st)
{
    IROper val  = gen_expr(g, st->field_assign.value);
    int    sz   = val.size > 0 ? val.size : 4;
    IROper base = gen_struct_base(g, st->field_assign.object, sz);

    const char *obj_type = expr_type(st->field_assign.object);
    int mem_off = irgen_field_member_offset(g, obj_type,
                                            st->field_assign.member);
    emit(g, IR_FIELD_STORE, base, oper_imm(mem_off, 4), val, sz, st->loc);
}

static void gen_compound_assign(IRGen *g, ASTStmt *st)
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
    default: ir_error(g, st->loc, "Unknown compound-assign op");
    }

    emit(g, irop, oper_temp(t, sz), tv, vv, 0, st->loc);

    /* Store back */
    ASTExpr *tgt = st->compound_assign.target;
    if (tgt->kind == EXPR_IDENT) {
        int off = irgen_name_lookup(g, tgt->ident.name, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(off, sz), oper_temp(t, sz),
             oper_none(), 0, st->loc);
    } else if (tgt->kind == EXPR_INDEX) {
        IROper base = gen_array_base(g, tgt->index.array, sz);
        IROper idx  = gen_expr(g, tgt->index.index);
        emit(g, IR_INDEX_STORE, base, idx, oper_temp(t, sz), sz, st->loc);
    } else if (tgt->kind == EXPR_FIELD_ACCESS) {
        IROper base = gen_struct_base(g, tgt->field_access.object, sz);
        const char *obj_type = expr_type(tgt->field_access.object);
        int mem_off = irgen_field_member_offset(g, obj_type,
                                                tgt->field_access.member);
        emit(g, IR_FIELD_STORE, base, oper_imm(mem_off, 4),
             oper_temp(t, sz), sz, st->loc);
    }
}

static int infer_write_type(ASTExpr *e)
{
    /* Determine write-type hint from the expression's inferred_type:
     * 0 = integer, 1 = string, 2 = bool, 3 = char */
    const char *ty = e->inferred_type;
    if (ty) {
        if (strcmp(ty, "str") == 0)  return 1;
        if (strcmp(ty, "bool") == 0) return 2;
        if (strcmp(ty, "char") == 0) return 3;
    }
    /* Also check for string literal expression directly */
    if (e->kind == EXPR_STRING_LIT) return 1;
    return 0;  /* default: integer */
}

static void gen_write(IRGen *g, ASTStmt *st)
{
    IROper v = gen_expr(g, st->write.value);
    int wtype = infer_write_type(st->write.value);
    emit(g, IR_WRITE, oper_imm(st->write.newline ? 1 : 0, 4),
         v, oper_none(), wtype, st->loc);
}

static void gen_read_stmt(IRGen *g, ASTStmt *st)
{
    int kind = st->read.read_kind == READ_READCHAR ? 2
             : st->read.read_kind == READ_READLN   ? 1 : 0;
    int off = irgen_name_lookup(g, st->read.target, st->loc);
    int sz  = 8;  /* str or i32 */
    int t   = new_temp(g, sz);
    emit(g, IR_READ, oper_temp(t, sz), oper_imm(kind, 4), oper_none(),
         0, st->loc);
    emit(g, IR_STORE_VAR, oper_stack(off, sz), oper_temp(t, sz),
         oper_none(), 0, st->loc);
}

/* Emit copy-back stores for every 'update' parameter before a return.
 * Loads the current value, loads the saved caller-address from the
 * hidden slot, then writes back through the pointer (IR_STORE_IND). */
static void emit_update_writeback(IRGen *g, SrcLoc loc)
{
    IRFunc *fn = g->cur;
    if (!fn->param_info) return;
    for (int i = 0; i < fn->visible_param_count; i++) {
        if (!fn->param_info[i].is_update) continue;
        int val_off = fn->param_info[i].offset;
        int val_sz  = fn->param_info[i].size;
        int addr_off = fn->param_info[i].wb_offset;

        /* Load current parameter value */
        int tv = new_temp(g, val_sz);
        emit(g, IR_LOAD_VAR, oper_temp(tv, val_sz),
             oper_stack(val_off, val_sz), oper_none(), 0, loc);
        /* Load caller address from hidden slot */
        int ta = new_temp(g, 8);
        emit(g, IR_LOAD_VAR, oper_temp(ta, 8),
             oper_stack(addr_off, 8), oper_none(), 0, loc);
        /* Store value through pointer */
        emit(g, IR_STORE_IND, oper_temp(ta, 8),
             oper_temp(tv, val_sz), oper_none(), val_sz, loc);
    }
}

static void gen_return(IRGen *g, ASTStmt *st)
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

static void gen_if(IRGen *g, ASTStmt *st)
{
    IROper cond = gen_expr(g, st->if_stmt.condition);
    int else_lbl = new_label(g);
    int end_lbl  = new_label(g);

    if (st->if_stmt.else_body) {
        emit(g, IR_JZ, oper_label(else_lbl), cond, oper_none(), 0, st->loc);
    } else {
        emit(g, IR_JZ, oper_label(end_lbl), cond, oper_none(), 0, st->loc);
    }

    irgen_scope_push(g);
    for (int i = 0; i < st->if_stmt.body_count; i++)
        gen_stmt(g, st->if_stmt.body[i]);
    irgen_scope_pop(g);

    if (st->if_stmt.else_body) {
        emit(g, IR_JMP, oper_label(end_lbl), oper_none(), oper_none(),
             0, st->loc);
        emit(g, IR_LABEL, oper_label(else_lbl), oper_none(), oper_none(),
             0, st->loc);
        irgen_scope_push(g);
        for (int i = 0; i < st->if_stmt.else_count; i++)
            gen_stmt(g, st->if_stmt.else_body[i]);
        irgen_scope_pop(g);
    }

    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);
}

static void gen_while(IRGen *g, ASTStmt *st)
{
    int top_lbl = new_label(g);
    int end_lbl = new_label(g);

    int prev_break    = g->break_label;
    int prev_continue = g->continue_label;
    g->break_label    = end_lbl;
    g->continue_label = top_lbl;

    emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);
    IROper cond = gen_expr(g, st->while_loop.condition);
    emit(g, IR_JZ, oper_label(end_lbl), cond, oper_none(), 0, st->loc);

    irgen_scope_push(g);
    for (int i = 0; i < st->while_loop.body_count; i++)
        gen_stmt(g, st->while_loop.body[i]);
    irgen_scope_pop(g);

    emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);
    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);

    g->break_label    = prev_break;
    g->continue_label = prev_continue;
}

static void gen_repeat(IRGen *g, ASTStmt *st)
{
    int top_lbl = new_label(g);
    int end_lbl = new_label(g);

    int prev_break    = g->break_label;
    int prev_continue = g->continue_label;
    g->break_label    = end_lbl;
    g->continue_label = top_lbl;

    emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);

    irgen_scope_push(g);
    for (int i = 0; i < st->repeat_loop.body_count; i++)
        gen_stmt(g, st->repeat_loop.body[i]);
    irgen_scope_pop(g);

    emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
         0, st->loc);
    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);

    g->break_label    = prev_break;
    g->continue_label = prev_continue;
}

static void gen_for(IRGen *g, ASTStmt *st)
{
    int top_lbl  = new_label(g);
    int step_lbl = new_label(g);
    int end_lbl  = new_label(g);

    int prev_break    = g->break_label;
    int prev_continue = g->continue_label;
    g->break_label    = end_lbl;
    g->continue_label = step_lbl;

    irgen_scope_push(g);

    if (st->for_loop.iterable && st->for_loop.iterable->kind == EXPR_RANGE) {
        ASTExpr *rng = st->for_loop.iterable;
        IROper start_v = gen_expr(g, rng->range.start);
        IROper end_v   = gen_expr(g, rng->range.end);
        int sz = start_v.size > 0 ? start_v.size : 4;
        IROper step_v;
        if (rng->range.step) {
            step_v = gen_expr(g, rng->range.step);
        } else {
            step_v = oper_imm(1, sz);
        }

        /* Allocate loop var on stack — we need a place to store it.
           The semantic pass defined the var in a scope,
           but for IR we need its offset. We'll use a synthetic offset. */
        int var_off = -(g->cur->stack_size + sz);
        g->cur->stack_size += sz + (sz < 8 ? (8 - sz) : 0); /* align */
        irgen_scope_add(g, st->for_loop.var_name, var_off, sz);

        /* init: var = start */
        emit(g, IR_STORE_VAR, oper_stack(var_off, sz), start_v,
             oper_none(), 0, st->loc);

        /* top: if var >= end goto end */
        emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
        int tv = new_temp(g, sz);
        emit(g, IR_LOAD_VAR, oper_temp(tv, sz), oper_stack(var_off, sz),
             oper_none(), 0, st->loc);
        int cmp = new_temp(g, 1);
        emit(g, IR_CMP_GE, oper_temp(cmp, 1), oper_temp(tv, sz), end_v,
             0, st->loc);
        emit(g, IR_JNZ, oper_label(end_lbl), oper_temp(cmp, 1), oper_none(),
             0, st->loc);

        /* body */
        for (int i = 0; i < st->for_loop.body_count; i++)
            gen_stmt(g, st->for_loop.body[i]);

        /* step: var += step */
        emit(g, IR_LABEL, oper_label(step_lbl), oper_none(), oper_none(),
             0, st->loc);
        int cur = new_temp(g, sz);
        emit(g, IR_LOAD_VAR, oper_temp(cur, sz), oper_stack(var_off, sz),
             oper_none(), 0, st->loc);
        int nxt = new_temp(g, sz);
        emit(g, IR_ADD, oper_temp(nxt, sz), oper_temp(cur, sz), step_v,
             0, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(var_off, sz), oper_temp(nxt, sz),
             oper_none(), 0, st->loc);
        emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
    } else {
        /* ── Array for-each ──────────────────────────────────
         * Desugars:   for x in arr:   =>   __idx = 0
         *                                  top: if __idx >= count goto end
         *                                       x = arr[__idx]
         *                                       <body>
         *                                  step: __idx += 1; goto top
         */
        int esz   = st->for_loop.array_elem_size;
        int count = st->for_loop.array_count;
        if (esz <= 0) esz = 4;
        if (count <= 0)
            ir_error(g, st->loc, "for-each: unknown array size");

        /* Look up array base offset */
        IRLocal *arr_loc = irgen_scope_lookup(g,
                              st->for_loop.iterable->ident.name);
        if (!arr_loc)
            ir_error(g, st->loc, "for-each: array '%s' not found",
                     st->for_loop.iterable->ident.name);

        int arr_off = arr_loc->stack_off;

        /* Hidden index counter (i32, 4 bytes, aligned to 8) */
        int idx_off = -(g->cur->stack_size + 4);
        g->cur->stack_size += 8;

        /* Loop variable for the element */
        int var_off = -(g->cur->stack_size + esz);
        g->cur->stack_size += esz + (esz < 8 ? (8 - esz) : 0);
        irgen_scope_add(g, st->for_loop.var_name, var_off, esz);

        /* __idx = 0 */
        emit(g, IR_STORE_VAR, oper_stack(idx_off, 4), oper_imm(0, 4),
             oper_none(), 0, st->loc);

        /* top: if __idx >= count goto end */
        emit(g, IR_LABEL, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
        int t_idx = new_temp(g, 4);
        emit(g, IR_LOAD_VAR, oper_temp(t_idx, 4), oper_stack(idx_off, 4),
             oper_none(), 0, st->loc);
        int cmp = new_temp(g, 1);
        emit(g, IR_CMP_GE, oper_temp(cmp, 1), oper_temp(t_idx, 4),
             oper_imm(count, 4), 0, st->loc);
        emit(g, IR_JNZ, oper_label(end_lbl), oper_temp(cmp, 1),
             oper_none(), 0, st->loc);

        /* x = arr[__idx] — use IR_INDEX_LOAD */
        int t_idx2 = new_temp(g, 4);
        emit(g, IR_LOAD_VAR, oper_temp(t_idx2, 4), oper_stack(idx_off, 4),
             oper_none(), 0, st->loc);
        int t_elem = new_temp(g, esz);
        emit(g, IR_INDEX_LOAD, oper_temp(t_elem, esz),
             oper_stack(arr_off, esz), oper_temp(t_idx2, 4), esz, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(var_off, esz),
             oper_temp(t_elem, esz), oper_none(), 0, st->loc);

        /* body */
        for (int i = 0; i < st->for_loop.body_count; i++)
            gen_stmt(g, st->for_loop.body[i]);

        /* step: __idx += 1 */
        emit(g, IR_LABEL, oper_label(step_lbl), oper_none(), oper_none(),
             0, st->loc);
        int t_cur = new_temp(g, 4);
        emit(g, IR_LOAD_VAR, oper_temp(t_cur, 4), oper_stack(idx_off, 4),
             oper_none(), 0, st->loc);
        int t_nxt = new_temp(g, 4);
        emit(g, IR_ADD, oper_temp(t_nxt, 4), oper_temp(t_cur, 4),
             oper_imm(1, 4), 0, st->loc);
        emit(g, IR_STORE_VAR, oper_stack(idx_off, 4), oper_temp(t_nxt, 4),
             oper_none(), 0, st->loc);
        emit(g, IR_JMP, oper_label(top_lbl), oper_none(), oper_none(),
             0, st->loc);
    }

    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);
    irgen_scope_pop(g);

    g->break_label    = prev_break;
    g->continue_label = prev_continue;
}

static void gen_match(IRGen *g, ASTStmt *st)
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

        irgen_scope_push(g);
        for (int i = 0; i < arm->body_count; i++)
            gen_stmt(g, arm->body[i]);
        irgen_scope_pop(g);

        emit(g, IR_JMP, oper_label(end_lbl), oper_none(), oper_none(),
             0, st->loc);
        emit(g, IR_LABEL, oper_label(next_lbl), oper_none(), oper_none(),
             0, st->loc);
    }

    emit(g, IR_LABEL, oper_label(end_lbl), oper_none(), oper_none(),
         0, st->loc);
}

static void gen_syscall(IRGen *g, ASTStmt *st)
{
    for (int i = 0; i < st->syscall.arg_count; i++) {
        IROper v = gen_expr(g, st->syscall.args[i]);
        emit(g, IR_ARG, oper_imm(i, 4), v, oper_none(), 0, st->loc);
    }
    emit(g, IR_SYSCALL, oper_none(), oper_imm(st->syscall.arg_count, 4),
         oper_none(), 0, st->loc);
}

/* ── Statement dispatch ─────────────────────────────────── */

static void gen_stmt(IRGen *g, ASTStmt *st)
{
    switch (st->kind) {
    case STMT_VAR_DECL:        gen_vardecl(g, st);           break;
    case STMT_ASSIGN:          gen_assign(g, st);            break;
    case STMT_INDEX_ASSIGN:    gen_index_assign(g, st);      break;
    case STMT_FIELD_ASSIGN:    gen_field_assign(g, st);      break;
    case STMT_COMPOUND_ASSIGN: gen_compound_assign(g, st);   break;
    case STMT_WRITE:           gen_write(g, st);             break;
    case STMT_READ:            gen_read_stmt(g, st);         break;
    case STMT_IF:              gen_if(g, st);                break;
    case STMT_WHILE:           gen_while(g, st);             break;
    case STMT_REPEAT:          gen_repeat(g, st);            break;
    case STMT_FOR:             gen_for(g, st);               break;
    case STMT_MATCH:           gen_match(g, st);             break;
    case STMT_RETURN:          gen_return(g, st);            break;
    case STMT_SYSCALL:         gen_syscall(g, st);           break;
    case STMT_EXPR:
        gen_expr(g, st->expr_stmt.expr);
        break;
    case STMT_BREAK:
        emit(g, IR_JMP, oper_label(g->break_label), oper_none(),
             oper_none(), 0, st->loc);
        break;
    case STMT_CONTINUE:
        emit(g, IR_JMP, oper_label(g->continue_label), oper_none(),
             oper_none(), 0, st->loc);
        break;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Function IR generation
 * ═════════════════════════════════════════════════════════════ */

static void gen_function(IRGen *g, ASTFunction *fn, IRFunc *out)
{
    memset(out, 0, sizeof(*out));
    out->name        = fn->name;
    out->stack_size  = fn->stack_size;
    out->param_count = fn->param_count;

    /* Count hidden address params for update modifier */
    int update_count = 0;
    for (int i = 0; i < fn->param_count; i++)
        if (fn->params[i].is_update) update_count++;
    int total_params = fn->param_count + update_count;
    out->visible_param_count = fn->param_count;

    /* Allocate and fill param_info so x64 can spill registers */
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

            /* Detect array parameters — pass by pointer + memcpy */
            if (p->type_node && p->type_node->kind == TYPE_NODE_ARRAY) {
                const char *et = p->type_node->array.elem
                    ? (p->type_node->array.elem->kind == TYPE_NODE_SIMPLE
                       ? p->type_node->array.elem->simple.name : "i32")
                    : "i32";
                int elem_sz = type_size(et);
                int cnt = p->type_node->array.size;
                out->param_info[i].is_field   = true;
                out->param_info[i].size       = 8; /* passed as pointer */
                out->param_info[i].field_size = elem_sz * (cnt > 0 ? cnt : 1);
            }

            /* Detect aggregate (field/struct) parameters */
            ASTFieldDef *pfd = irgen_find_field(g, pt);
            if (pfd) {
                out->param_info[i].is_field   = true;
                out->param_info[i].size       = 8; /* passed as pointer */
                /* Compute total field size for memcpy in prologue */
                int fsz = 0;
                for (int j = 0; j < pfd->member_count; j++) {
                    const char *mt = pfd->members[j].type_node
                        ? (pfd->members[j].type_node->kind == TYPE_NODE_SIMPLE
                           ? pfd->members[j].type_node->simple.name : "i32")
                        : "i32";
                    ASTFieldDef *mfd = irgen_find_field(g, mt);
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
                /* Compute writeback-address slot: semantic allocates it
                   right after the value slot (align to 8, +8 bytes). */
                int cur = -(p->stack_offset);   /* positive running pos */
                cur = (cur + 7) & ~7;           /* align_up(cur, 8)    */
                cur += 8;
                out->param_info[i].wb_offset = -cur;
            }
        }

        /* Fill hidden address param entries (for prologue spilling) */
        int hi = fn->param_count;
        for (int i = 0; i < fn->param_count; i++) {
            if (!fn->params[i].is_update) continue;
            out->param_info[hi].offset    = out->param_info[i].wb_offset;
            out->param_info[hi].size      = 8;  /* pointer */
            out->param_info[hi].is_update = false;
            out->param_info[hi].wb_offset = 0;
            hi++;
        }
    }

    IRFunc *prev = g->cur;
    g->cur = out;

    irgen_scope_push(g);

    /* Register parameters */
    for (int i = 0; i < fn->param_count; i++) {
        ASTParam *p = &fn->params[i];
        const char *pt = p->type_node
            ? (p->type_node->kind == TYPE_NODE_SIMPLE
               ? p->type_node->simple.name : "i32")
            : "i32";
        int sz = type_size(pt);
        irgen_scope_add(g, p->name, p->stack_offset, sz);
    }

    /* Body */
    for (int i = 0; i < fn->body_count; i++)
        gen_stmt(g, fn->body[i]);

    /* Implicit void return at end */
    if (out->instr_count == 0 ||
        (out->instrs[out->instr_count - 1].op != IR_RET &&
         out->instrs[out->instr_count - 1].op != IR_RET_VOID))
    {
        SrcLoc end_loc = fn->loc;
        emit_update_writeback(g, end_loc);
        emit(g, IR_RET_VOID, oper_none(), oper_none(), oper_none(),
             0, end_loc);
    }

    irgen_scope_pop(g);
    g->cur = prev;
}

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

void ir_program_init(IRProgram *p, Arena *arena)
{
    memset(p, 0, sizeof(*p));
    p->arena = arena;
}

void ir_generate(IRProgram *p, ASTProgram *ast, const char *filename)
{
    IRGen g;
    memset(&g, 0, sizeof(g));
    g.prog       = p;
    g.arena      = p->arena;
    g.filename   = filename;
    g.field_defs = ast->field_defs;
    g.field_count = ast->field_count;
    g.enum_defs  = ast->enum_defs;
    g.enum_count = ast->enum_count;
    g.break_label    = -1;
    g.continue_label = -1;

    s_scope = NULL;   /* reset */

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
        p->top_level.stack_size = 256;  /* conservative default */
        g.cur = &p->top_level;

        irgen_scope_push(&g);
        for (int i = 0; i < ast->stmt_count; i++)
            gen_stmt(&g, ast->statements[i]);
        irgen_scope_pop(&g);

        /* End with exit */
        emit(&g, IR_RET_VOID, oper_none(), oper_none(), oper_none(),
             0, ast->loc);
    }
}

/* ═════════════════════════════════════════════════════════════
 * IR text dump  (for debugging: axisc --dump-ir)
 * ═════════════════════════════════════════════════════════════ */

static const char *opcode_names[] = {
    [IR_NOP]         = "nop",
    [IR_MOV]         = "mov",
    [IR_LOAD_IMM]    = "load_imm",
    [IR_LOAD_STR]    = "load_str",
    [IR_LOAD_VAR]    = "load_var",
    [IR_STORE_VAR]   = "store_var",
    [IR_ADD]         = "add",
    [IR_SUB]         = "sub",
    [IR_MUL]         = "mul",
    [IR_DIV]         = "div",
    [IR_MOD]         = "mod",
    [IR_NEG]         = "neg",
    [IR_BIT_AND]     = "bit_and",
    [IR_BIT_OR]      = "bit_or",
    [IR_BIT_XOR]     = "bit_xor",
    [IR_SHL]         = "shl",
    [IR_SHR]         = "shr",
    [IR_CMP_EQ]      = "cmp_eq",
    [IR_CMP_NE]      = "cmp_ne",
    [IR_CMP_LT]      = "cmp_lt",
    [IR_CMP_LE]      = "cmp_le",
    [IR_CMP_GT]      = "cmp_gt",
    [IR_CMP_GE]      = "cmp_ge",
    [IR_LOG_NOT]     = "log_not",
    [IR_LABEL]       = "label",
    [IR_JMP]         = "jmp",
    [IR_JZ]          = "jz",
    [IR_JNZ]         = "jnz",
    [IR_ARG]         = "arg",
    [IR_CALL]        = "call",
    [IR_RET]         = "ret",
    [IR_RET_VOID]    = "ret_void",
    [IR_WRITE]       = "write",
    [IR_READ]        = "read",
    [IR_INDEX_LOAD]  = "idx_load",
    [IR_INDEX_STORE] = "idx_store",
    [IR_FIELD_LOAD]  = "fld_load",
    [IR_FIELD_STORE] = "fld_store",
    [IR_LEA]         = "lea",
    [IR_MEMCPY]      = "memcpy",
    [IR_STORE_IND]   = "store_ind",
    [IR_SEXT]        = "sext",
    [IR_ZEXT]        = "zext",
    [IR_TRUNC]       = "trunc",
    [IR_SYSCALL]     = "syscall",
};

static void dump_oper(FILE *f, IROper o)
{
    switch (o.kind) {
    case OPER_NONE:  fprintf(f, "_");    break;
    case OPER_TEMP:  fprintf(f, "t%d:%d", o.temp_id, o.size); break;
    case OPER_IMM:   fprintf(f, "#%" PRId64, o.imm); break;
    case OPER_STACK: fprintf(f, "[rbp%+d]:%d", o.stack_off, o.size); break;
    case OPER_LABEL: fprintf(f, "L%d", o.label_id); break;
    case OPER_FUNC:  fprintf(f, "@%s", o.func_name); break;
    case OPER_STR:   fprintf(f, "str[%d]", o.str_idx); break;
    }
}

static void dump_func(FILE *f, const IRFunc *fn)
{
    fprintf(f, "\nfunction %s (params=%d, stack=%d, temps=%d):\n",
            fn->name, fn->param_count, fn->stack_size, fn->temp_count);
    for (int i = 0; i < fn->instr_count; i++) {
        const IRInstr *ins = &fn->instrs[i];
        if (ins->op == IR_LABEL)
            fprintf(f, "  L%d:\n", ins->dest.label_id);
        else {
            const char *name = (ins->op < (int)AXIS_ARRAY_LEN(opcode_names)
                                && opcode_names[ins->op])
                               ? opcode_names[ins->op] : "???";
            fprintf(f, "    %-12s ", name);
            dump_oper(f, ins->dest);
            if (ins->src1.kind != OPER_NONE) {
                fprintf(f, ", ");
                dump_oper(f, ins->src1);
            }
            if (ins->src2.kind != OPER_NONE) {
                fprintf(f, ", ");
                dump_oper(f, ins->src2);
            }
            if (ins->extra)
                fprintf(f, "  ; extra=%d", ins->extra);
            fputc('\n', f);
        }
    }
}

void ir_dump(const IRProgram *p, FILE *out)
{
    fprintf(out, "=== AXIS IR Dump ===\n");
    fprintf(out, "String table (%d entries):\n", p->str_count);
    for (int i = 0; i < p->str_count; i++)
        fprintf(out, "  [%d] \"%s\"\n", i, p->strings[i]);

    for (int i = 0; i < p->func_count; i++)
        dump_func(out, &p->funcs[i]);

    if (p->top_level.instr_count > 0)
        dump_func(out, &p->top_level);
}
