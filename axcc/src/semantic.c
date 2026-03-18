/*
 * semantic.c – AXIS multi-pass semantic analyser.
 *
 * Pass 0a: Collect field definitions
 * Pass 0b: Collect enum definitions
 * Pass 1 : Collect function signatures
 * Pass 2 : Analyse function bodies
 * Pass 3 : Analyse top-level statements
 *
 * Annotates AST nodes with inferred types and stack layout information.
 */

#include "axis_semantic.h"
#include <stdarg.h>
#include <setjmp.h>

/* ═════════════════════════════════════════════════════════════
 * Forward declarations
 * ═════════════════════════════════════════════════════════════ */

static const char *analyze_expr(Semantic *s, ASTExpr *e);
static void        analyze_stmt(Semantic *s, ASTStmt *st);
static void        analyze_function(Semantic *s, ASTFunction *func);
static void        check_block_dead_code(Semantic *s, ASTStmt **stmts, int count);
static int         calc_field_size(Semantic *s, ASTFieldDef *fd);

/* ═════════════════════════════════════════════════════════════
 * Error reporting
 * ═════════════════════════════════════════════════════════════ */

static void sem_error(Semantic *s, SrcLoc loc,
                      const char *fmt, ...)
{
    fprintf(stderr, "%s:%d:%d: semantic error: ",
            s->filename ? s->filename : "<unknown>", loc.line, loc.col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    s->error_count++;
    if (s->check_mode) {
        longjmp(s->err_jmp, 1);
    }
    exit(1);
}

/* ═════════════════════════════════════════════════════════════
 * Type helpers
 * ═════════════════════════════════════════════════════════════ */

static const struct { const char *name; int size; } type_size_tab[] = {
    {"i8",  1}, {"i16", 2}, {"i32", 4}, {"i64",  8},
    {"u8",  1}, {"u16", 2}, {"u32", 4}, {"u64",  8},
    {"bool", 1},
    {"str",  8},
};

static bool is_integer_type(const char *t)
{
    if (!t) return false;
    return strcmp(t,"i8")==0  || strcmp(t,"i16")==0 ||
           strcmp(t,"i32")==0 || strcmp(t,"i64")==0 ||
           strcmp(t,"u8")==0  || strcmp(t,"u16")==0 ||
           strcmp(t,"u32")==0 || strcmp(t,"u64")==0;
}

static bool is_signed_type(const char *t)
{
    if (!t) return false;
    return strcmp(t,"i8")==0 || strcmp(t,"i16")==0 ||
           strcmp(t,"i32")==0 || strcmp(t,"i64")==0;
}

static bool is_scalar_type(const char *t)
{
    return is_integer_type(t) ||
           (t && (strcmp(t,"bool")==0 || strcmp(t,"str")==0));
}

static int get_type_size(const char *t)
{
    if (!t) return 8;
    for (size_t i = 0; i < AXIS_ARRAY_LEN(type_size_tab); i++)
        if (strcmp(type_size_tab[i].name, t) == 0)
            return type_size_tab[i].size;
    return 8;
}

static int align_up(int offset, int alignment)
{
    if (alignment <= 0) alignment = 1;
    return ((offset + alignment - 1) / alignment) * alignment;
}

static const char *type_name_of_node(ASTTypeNode *tn)
{
    if (!tn) return "void";
    if (tn->kind == TYPE_NODE_SIMPLE) return tn->simple.name;
    if (tn->kind == TYPE_NODE_ARRAY)  return "array";
    return "unknown";
}

/* ═════════════════════════════════════════════════════════════
 * Scope management
 * ═════════════════════════════════════════════════════════════ */

static Scope *new_scope(Semantic *s, Scope *parent)
{
    Scope *sc = ARENA_NEW(s->arena, Scope);
    sc->parent  = parent;
    sc->symbols = NULL;
    return sc;
}

static void enter_scope(Semantic *s)
{
    s->current_scope = new_scope(s, s->current_scope);
}

static void exit_scope(Semantic *s)
{
    assert(s->current_scope);
    /* Check for unused variables when in check mode with --unused */
    if (s->check_unused) {
        for (Symbol *sym = s->current_scope->symbols; sym; sym = sym->next) {
            if (!sym->used && !sym->is_param) {
                fprintf(stderr, "%s:%d:%d: warning: unused variable '%s'\n",
                        s->filename ? s->filename : "<unknown>",
                        sym->def_loc.line, sym->def_loc.col, sym->name);
            }
        }
    }
    s->current_scope = s->current_scope->parent;
}

static void scope_define(Semantic *s, Symbol *sym, SrcLoc loc)
{
    for (Symbol *p = s->current_scope->symbols; p; p = p->next) {
        if (strcmp(p->name, sym->name) == 0)
            sem_error(s, loc, "Symbol '%s' already defined in this scope",
                      sym->name);
    }
    sym->next = s->current_scope->symbols;
    s->current_scope->symbols = sym;
}

static Symbol *scope_lookup(Scope *sc, const char *name)
{
    for (; sc; sc = sc->parent)
        for (Symbol *sym = sc->symbols; sym; sym = sym->next)
            if (strcmp(sym->name, name) == 0) {
                sym->used = true;
                return sym;
            }
    return NULL;
}

/* ═════════════════════════════════════════════════════════════
 * Lookup helpers
 * ═════════════════════════════════════════════════════════════ */

static ASTFieldDef *find_field_def(Semantic *s, const char *name)
{
    for (int i = 0; i < s->field_count; i++)
        if (strcmp(s->field_defs[i].name, name) == 0)
            return &s->field_defs[i];
    return NULL;
}

static ASTEnumDef *find_enum_def(Semantic *s, const char *name)
{
    for (int i = 0; i < s->enum_count; i++)
        if (strcmp(s->enum_defs[i].name, name) == 0)
            return &s->enum_defs[i];
    return NULL;
}

static FuncSig *find_func_sig(Semantic *s, const char *name)
{
    for (FuncSig *f = s->func_sigs; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

static Symbol *lookup_var(Semantic *s, const char *name, SrcLoc loc)
{
    Symbol *sym = scope_lookup(s->current_scope, name);
    if (!sym) sem_error(s, loc, "Undefined variable: %s", name);
    return sym;
}

static FuncSig *lookup_func(Semantic *s, const char *name, SrcLoc loc)
{
    FuncSig *f = find_func_sig(s, name);
    if (!f) sem_error(s, loc, "Undefined function: %s", name);
    return f;
}

/* ═════════════════════════════════════════════════════════════
 * Field size calculation
 * ═════════════════════════════════════════════════════════════ */

static int field_size(Semantic *s, const char *name)
{
    ASTFieldDef *fd = find_field_def(s, name);
    if (!fd) return 8;
    return calc_field_size(s, fd);
}

static int calc_field_size(Semantic *s, ASTFieldDef *fd)
{
    int total = 0;
    for (int i = 0; i < fd->member_count; i++) {
        ASTFieldMember *m = &fd->members[i];

        if (m->inline_members && m->inline_count > 0) {
            /* Inline nested field */
            ASTFieldDef tmp;
            tmp.name = "";
            tmp.members = m->inline_members;
            tmp.member_count = m->inline_count;
            int sub = calc_field_size(s, &tmp);
            if (m->type_node && m->type_node->kind == TYPE_NODE_ARRAY) {
                /* Array of inline fields */
                int cnt = m->type_node->array.size;
                total += sub * (cnt > 0 ? cnt : 1);
            } else {
                total += sub;
            }
        } else if (m->type_node && m->type_node->kind == TYPE_NODE_ARRAY) {
            /* Array member */
            const char *et = type_name_of_node(m->type_node->array.elem);
            ASTFieldDef *efd = find_field_def(s, et);
            int elem_sz = efd ? calc_field_size(s, efd) : get_type_size(et);
            int cnt = m->type_node->array.size;
            total += elem_sz * (cnt > 0 ? cnt : 1);
        } else {
            /* Scalar / named type member */
            const char *tn = type_name_of_node(m->type_node);
            ASTFieldDef *fdf = find_field_def(s, tn);
            total += fdf ? calc_field_size(s, fdf) : get_type_size(tn);
        }
    }
    return total;
}

/* ═════════════════════════════════════════════════════════════
 * Symbol definition
 * ═════════════════════════════════════════════════════════════ */

static Symbol *define_symbol(Semantic *s, const char *name,
                             const char *type_name, bool mutable,
                             bool is_param, bool is_update,
                             ASTTypeNode *array_type, SrcLoc loc)
{
    /* Validate type */
    if (!is_scalar_type(type_name) &&
        strcmp(type_name, "array") != 0 &&
        !find_field_def(s, type_name) &&
        !find_enum_def(s, type_name))
    {
        sem_error(s, loc, "Unknown type: %s", type_name);
    }

    /* Determine size */
    int type_size;
    ASTFieldDef *fd = find_field_def(s, type_name);
    ASTEnumDef  *ed = find_enum_def(s, type_name);
    if (fd) {
        type_size = calc_field_size(s, fd);
    } else if (ed) {
        type_size = get_type_size(ed->underlying_type);
    } else {
        type_size = get_type_size(type_name);
    }

    int offset = 0;
    if (!is_param) {
        int alignment = type_size < 8 ? type_size : 8;
        if (alignment < 1) alignment = 1;
        s->stack_offset = align_up(s->stack_offset, alignment);
        s->stack_offset += type_size;
        offset = -s->stack_offset;
    }

    Symbol *sym = ARENA_NEW(s->arena, Symbol);
    sym->name        = name;
    sym->type_name   = type_name;
    sym->mutable     = mutable;
    sym->stack_offset = offset;
    sym->is_param    = is_param;
    sym->is_update   = is_update;
    sym->array_type  = array_type;
    sym->next        = NULL;
    sym->used        = false;
    sym->def_loc     = loc;

    scope_define(s, sym, loc);
    return sym;
}

static Symbol *define_array_symbol(Semantic *s, const char *name,
                                   ASTTypeNode *arr_type, bool mutable,
                                   int total_size, SrcLoc loc)
{
    s->stack_offset = align_up(s->stack_offset, 8);
    s->stack_offset += total_size;

    Symbol *sym = ARENA_NEW(s->arena, Symbol);
    sym->name        = name;
    sym->type_name   = "array";
    sym->mutable     = mutable;
    sym->stack_offset = -s->stack_offset;
    sym->is_param    = false;
    sym->is_update   = false;
    sym->array_type  = arr_type;
    sym->next        = NULL;
    sym->used        = false;
    sym->def_loc     = loc;

    scope_define(s, sym, loc);
    return sym;
}

/* ═════════════════════════════════════════════════════════════
 * Literal coercion
 * ═════════════════════════════════════════════════════════════ */

static const char *coerce_literal(ASTExpr *expr, const char *from,
                                  const char *to)
{
    if (strcmp(from, to) == 0) return from;

    /* i32 integer literal → target integer type */
    if (strcmp(from, "i32") == 0 && expr->kind == EXPR_INT_LIT) {
        if (is_integer_type(to)) {
            expr->inferred_type = to;
            return to;
        }
        /* i32 literal 0/1 → bool */
        if (strcmp(to, "bool") == 0 &&
            (expr->int_lit.value == 0 || expr->int_lit.value == 1))
        {
            expr->inferred_type = "bool";
            return "bool";
        }
    }
    return from;
}

/* ═════════════════════════════════════════════════════════════
 * Token helpers for operator classification
 * ═════════════════════════════════════════════════════════════ */

static bool is_comparison_op(TokenType t)
{
    return t == TOK_EQ || t == TOK_NE ||
           t == TOK_LT || t == TOK_LE ||
           t == TOK_GT || t == TOK_GE;
}

static bool is_arithmetic_op(TokenType t)
{
    return t == TOK_PLUS || t == TOK_MINUS ||
           t == TOK_STAR || t == TOK_SLASH || t == TOK_PERCENT;
}

static bool is_bitwise_op(TokenType t)
{
    return t == TOK_AMP || t == TOK_PIPE || t == TOK_CARET;
}

static bool is_shift_op(TokenType t)
{
    return t == TOK_LSHIFT || t == TOK_RSHIFT;
}

static bool is_logical_op(TokenType t)
{
    return t == TOK_AND || t == TOK_OR;
}

/* ═════════════════════════════════════════════════════════════
 * Expression analysis  (each returns the inferred type string)
 * ═════════════════════════════════════════════════════════════ */

/* ── Identifier ─────────────────────────────────────────── */

static const char *analyze_identifier(Semantic *s, ASTExpr *e)
{
    Symbol *sym = lookup_var(s, e->ident.name, e->loc);
    e->inferred_type = sym->type_name;
    return sym->type_name;
}

/* ── Binary op ──────────────────────────────────────────── */

static const char *analyze_binop(Semantic *s, ASTExpr *e)
{
    const char *lt = analyze_expr(s, e->binary.left);
    const char *rt = analyze_expr(s, e->binary.right);
    TokenType   op = e->binary.op;

    /* Literal coercion when types differ */
    if (strcmp(lt, rt) != 0) {
        if (strcmp(lt, "i32") == 0 &&
            e->binary.left->kind == EXPR_INT_LIT && is_integer_type(rt))
        {
            e->binary.left->inferred_type = rt;
            lt = rt;
        } else if (strcmp(rt, "i32") == 0 &&
                   e->binary.right->kind == EXPR_INT_LIT && is_integer_type(lt))
        {
            e->binary.right->inferred_type = lt;
            rt = lt;
        } else {
            sem_error(s, e->loc,
                      "Type mismatch in binary op: %s vs %s", lt, rt);
        }
    }

    if (is_comparison_op(op)) {
        e->inferred_type = "bool";
        return "bool";
    }
    if (is_arithmetic_op(op)) {
        if (!is_integer_type(lt))
            sem_error(s, e->loc, "Arithmetic requires integer, got %s", lt);
        e->inferred_type = lt;
        return lt;
    }
    if (is_bitwise_op(op)) {
        if (!is_integer_type(lt))
            sem_error(s, e->loc, "Bitwise op requires integer, got %s", lt);
        e->inferred_type = lt;
        return lt;
    }
    if (is_shift_op(op)) {
        if (!is_integer_type(lt))
            sem_error(s, e->loc, "Shift requires integer, got %s", lt);
        e->inferred_type = lt;
        return lt;
    }
    if (is_logical_op(op)) {
        if (strcmp(lt, "bool") != 0)
            sem_error(s, e->loc, "Logical op requires bool, got %s", lt);
        e->inferred_type = "bool";
        return "bool";
    }

    sem_error(s, e->loc, "Unknown binary operator");
    return NULL; /* unreachable */
}

/* ── Unary op ───────────────────────────────────────────── */

static const char *analyze_unary(Semantic *s, ASTExpr *e)
{
    const char *ot = analyze_expr(s, e->unary.operand);

    if (e->unary.op == TOK_MINUS) {
        if (!is_signed_type(ot))
            sem_error(s, e->loc, "Unary minus requires signed int, got %s", ot);
        e->inferred_type = ot;
        return ot;
    }
    if (e->unary.op == TOK_BANG || e->unary.op == TOK_NOT) {
        if (strcmp(ot, "bool") != 0)
            sem_error(s, e->loc, "Logical NOT requires bool, got %s", ot);
        e->inferred_type = "bool";
        return "bool";
    }

    sem_error(s, e->loc, "Unknown unary operator");
    return NULL; /* unreachable */
}

/* ── Call ────────────────────────────────────────────────── */

static const char *analyze_call(Semantic *s, ASTExpr *e)
{
    const char *name = e->call.name;

    /* Built-in read functions */
    if (strcmp(name, "read") == 0) {
        if (e->call.arg_count != 0)
            sem_error(s, e->loc, "'read' takes no arguments, got %d",
                      e->call.arg_count);
        e->inferred_type = "i32";
        return "i32";
    }
    if (strcmp(name, "readln") == 0) {
        if (e->call.arg_count != 0)
            sem_error(s, e->loc, "'readln' takes no arguments, got %d",
                      e->call.arg_count);
        e->inferred_type = "str";
        return "str";
    }
    if (strcmp(name, "readchar") == 0) {
        if (e->call.arg_count != 0)
            sem_error(s, e->loc, "'readchar' takes no arguments, got %d",
                      e->call.arg_count);
        e->inferred_type = "i32";
        return "i32";
    }

    FuncSig *fs = lookup_func(s, name, e->loc);

    if (e->call.arg_count != fs->param_count)
        sem_error(s, e->loc, "'%s' expects %d args, got %d",
                  name, fs->param_count, e->call.arg_count);

    for (int i = 0; i < e->call.arg_count; i++) {
        const char *at = analyze_expr(s, e->call.args[i]);
        const char *pt = type_name_of_node(fs->params[i].type_node);
        at = coerce_literal(e->call.args[i], at, pt);
        if (strcmp(at, pt) != 0)
            sem_error(s, e->loc,
                      "Arg %d to '%s': expected %s, got %s",
                      i + 1, name, pt, at);

        /* Track update args (flag already in AST from parser) */
        if (e->call.update_flags && fs->params[i].is_update) {
            e->call.update_flags[i] = true;
            /* update arg must be a plain variable */
            if (e->call.args[i]->kind != EXPR_IDENT)
                sem_error(s, e->call.args[i]->loc,
                          "Argument %d to '%s' requires a variable "
                          "(parameter is 'update')", i + 1, name);
            else {
                Symbol *sym = lookup_var(s, e->call.args[i]->ident.name,
                                         e->call.args[i]->loc);
                if (!sym->mutable)
                    sem_error(s, e->call.args[i]->loc,
                              "Argument %d to '%s': variable '%s' must be "
                              "mutable for 'update' parameter",
                              i + 1, name, sym->name);
            }
        }
    }

    const char *rt = fs->return_type ? fs->return_type : "void";
    e->inferred_type = rt;
    return rt;
}

/* ── Index access  (shared helper) ──────────────────────── */

static const char *analyze_index_access_node(Semantic *s,
                                             ASTExpr *arr_expr,
                                             ASTExpr *idx_expr,
                                             SrcLoc loc)
{
    const char *at = analyze_expr(s, arr_expr);

    /* Resolve identifier that should be array */
    if (strcmp(at, "array") != 0 && arr_expr->kind == EXPR_IDENT) {
        Symbol *sym = lookup_var(s, arr_expr->ident.name, loc);
        if (strcmp(sym->type_name, "array") != 0)
            sem_error(s, loc, "Cannot index non-array: %s", sym->type_name);
        at = "array";
    } else if (strcmp(at, "array") != 0) {
        sem_error(s, loc, "Cannot index non-array: %s", at);
    }

    const char *it = analyze_expr(s, idx_expr);
    if (!is_integer_type(it))
        sem_error(s, loc, "Array index must be integer, got %s", it);

    /* Determine element type */
    if (arr_expr->kind == EXPR_IDENT) {
        Symbol *sym = scope_lookup(s->current_scope, arr_expr->ident.name);
        if (sym && sym->array_type &&
            sym->array_type->kind == TYPE_NODE_ARRAY &&
            sym->array_type->array.elem &&
            sym->array_type->array.elem->kind == TYPE_NODE_SIMPLE)
        {
            return sym->array_type->array.elem->simple.name;
        }
    }
    return "i32";  /* fallback */
}

static const char *analyze_index_access(Semantic *s, ASTExpr *e)
{
    const char *et = analyze_index_access_node(s, e->index.array,
                                               e->index.index, e->loc);
    e->inferred_type = et;
    return et;
}

/* ── Field access (shared helper) ───────────────────────── */

static const char *analyze_enum_access(Semantic *s, ASTExpr *e);

static const char *analyze_field_access_on(Semantic *s, ASTExpr *obj,
                                           const char *member, SrcLoc loc)
{
    /* Check enum-style dot access first */
    if (obj->kind == EXPR_IDENT) {
        ASTEnumDef *ed = find_enum_def(s, obj->ident.name);
        if (ed) {
            for (int v = 0; v < ed->variant_count; v++)
                if (strcmp(ed->variants[v].name, member) == 0)
                    return ed->name;
            sem_error(s, loc, "Enum '%s' has no variant '%s'",
                      ed->name, member);
        }
    }

    const char *ot = analyze_expr(s, obj);

    /* Look up field definition */
    ASTFieldDef *fd = find_field_def(s, ot);
    if (!fd) {
        if (strcmp(ot, "field") == 0) return "i32";  /* inline fallback */
        sem_error(s, loc, "Cannot access member of non-field type '%s'", ot);
    }

    for (int i = 0; i < fd->member_count; i++)
        if (strcmp(fd->members[i].name, member) == 0)
            return type_name_of_node(fd->members[i].type_node);

    sem_error(s, loc, "Field '%s' has no member '%s'", ot, member);
    return NULL; /* unreachable */
}

static const char *analyze_field_access(Semantic *s, ASTExpr *e)
{
    /* Check if this is enum-style dot access (EnumName.Variant).
       If so, convert the AST node from FIELD_ACCESS to ENUM_ACCESS
       so that the IR generator handles it correctly. */
    if (e->field_access.object->kind == EXPR_IDENT) {
        ASTEnumDef *ed = find_enum_def(s, e->field_access.object->ident.name);
        if (ed) {
            const char *ename  = e->field_access.object->ident.name;
            const char *vname  = e->field_access.member;
            e->kind = EXPR_ENUM_ACCESS;
            e->enum_access.enum_name = ename;
            e->enum_access.variant   = vname;
            return analyze_enum_access(s, e);
        }
    }
    const char *mt = analyze_field_access_on(s, e->field_access.object,
                                             e->field_access.member, e->loc);
    e->inferred_type = mt;
    return mt;
}

/* ── Enum access  (EnumName::Variant) ───────────────────── */

static const char *analyze_enum_access(Semantic *s, ASTExpr *e)
{
    const char *en = e->enum_access.enum_name;
    ASTEnumDef *ed = find_enum_def(s, en);
    if (!ed) sem_error(s, e->loc, "Unknown enum: %s", en);

    for (int v = 0; v < ed->variant_count; v++)
        if (strcmp(ed->variants[v].name, e->enum_access.variant) == 0) {
            e->inferred_type = en;
            return en;
        }

    sem_error(s, e->loc, "Enum '%s' has no variant '%s'",
              en, e->enum_access.variant);
    return NULL; /* unreachable */
}

/* ── Array literal ──────────────────────────────────────── */

static const char *analyze_array_literal(Semantic *s, ASTExpr *e,
                                         const char *expected)
{
    if (e->array_lit.count == 0)
        sem_error(s, e->loc, "Empty array literal");

    const char *first = analyze_expr(s, e->array_lit.elements[0]);

    /* Coerce first element if expected type known */
    if (expected && is_integer_type(first) && is_integer_type(expected)) {
        if (e->array_lit.elements[0]->kind == EXPR_INT_LIT) {
            e->array_lit.elements[0]->inferred_type = expected;
            first = expected;
        }
    }

    for (int i = 1; i < e->array_lit.count; i++) {
        const char *et = analyze_expr(s, e->array_lit.elements[i]);
        if (strcmp(et, first) != 0) {
            if (strcmp(et, "i32") == 0 &&
                e->array_lit.elements[i]->kind == EXPR_INT_LIT &&
                is_integer_type(first))
            {
                e->array_lit.elements[i]->inferred_type = first;
            } else {
                sem_error(s, e->loc,
                          "Array element %d type %s != %s", i, et, first);
            }
        }
    }

    e->inferred_type = "array";
    return "array";
}

/* ── Copy ───────────────────────────────────────────────── */

static const char *analyze_copy(Semantic *s, ASTExpr *e)
{
    const char *t = analyze_expr(s, e->copy.expr);
    e->inferred_type = t;
    return t;
}

/* ── Expression dispatch ────────────────────────────────── */

static const char *analyze_expr(Semantic *s, ASTExpr *e)
{
    switch (e->kind) {
    case EXPR_INT_LIT:
        e->inferred_type = "i32";
        return "i32";

    case EXPR_STRING_LIT:
        e->inferred_type = "str";
        return "str";

    case EXPR_BOOL_LIT:
        e->inferred_type = "bool";
        return "bool";

    case EXPR_IDENT:
        return analyze_identifier(s, e);

    case EXPR_BINARY:
        return analyze_binop(s, e);

    case EXPR_UNARY:
        return analyze_unary(s, e);

    case EXPR_CALL:
        return analyze_call(s, e);

    case EXPR_INDEX:
        return analyze_index_access(s, e);

    case EXPR_FIELD_ACCESS:
        return analyze_field_access(s, e);

    case EXPR_ENUM_ACCESS:
        return analyze_enum_access(s, e);

    case EXPR_ARRAY_LIT:
        return analyze_array_literal(s, e, NULL);

    case EXPR_COPY:
        return analyze_copy(s, e);

    case EXPR_RANGE:
        /* Range doesn't resolve to a single type */
        return "range";

    case EXPR_READ_FAILED:
        e->inferred_type = "bool";
        return "bool";
    }

    sem_error(s, e->loc, "Unknown expression kind");
    return NULL; /* unreachable */
}

/* ═════════════════════════════════════════════════════════════
 * Statement analysis
 * ═════════════════════════════════════════════════════════════ */

/* ── VarDecl ────────────────────────────────────────────── */

static void analyze_array_vardecl(Semantic *s, ASTStmt *vd)
{
    ASTTypeNode *arr = vd->var_decl.type_node;
    assert(arr && arr->kind == TYPE_NODE_ARRAY);

    const char *elem_tn = (arr->array.elem &&
                           arr->array.elem->kind == TYPE_NODE_SIMPLE)
                          ? arr->array.elem->simple.name
                          : "unknown";

    if (vd->var_decl.value) {
        ASTExpr *val = vd->var_decl.value;
        if (val->kind == EXPR_COPY) {
            analyze_expr(s, val);
        } else if (val->kind == EXPR_ARRAY_LIT) {
            analyze_array_literal(s, val, elem_tn);
            int actual = val->array_lit.count;
            if (arr->array.size > 0 && actual != arr->array.size)
                sem_error(s, vd->loc,
                          "Array '%s' size %d but %d elements",
                          vd->var_decl.name, arr->array.size, actual);
            if (arr->array.size == 0)
                arr->array.size = actual;
        } else {
            sem_error(s, vd->loc,
                      "Array '%s' must be initialised with array literal or copy",
                      vd->var_decl.name);
        }
    } else {
        if (arr->array.size <= 0)
            sem_error(s, vd->loc,
                      "Array '%s' must have size or initialiser",
                      vd->var_decl.name);
    }

    /* Compute total size */
    bool is_field_elem = find_field_def(s, elem_tn) != NULL;
    int elem_sz;
    if (is_field_elem) {
        elem_sz = field_size(s, elem_tn);
    } else {
        elem_sz = get_type_size(elem_tn);
    }
    int total = elem_sz * (arr->array.size > 0 ? arr->array.size : 1);

    Symbol *sym = define_array_symbol(s, vd->var_decl.name, arr, true,
                                      total, vd->loc);
    vd->var_decl.stack_offset = sym->stack_offset;
    vd->var_decl.total_size   = total;
}

static void analyze_vardecl(Semantic *s, ASTStmt *vd)
{
    const char *decl_type = type_name_of_node(vd->var_decl.type_node);

    /* Array variable */
    if (vd->var_decl.type_node &&
        vd->var_decl.type_node->kind == TYPE_NODE_ARRAY)
    {
        analyze_array_vardecl(s, vd);
        return;
    }

    /* Field type */
    if (find_field_def(s, decl_type)) {
        Symbol *sym = define_symbol(s, vd->var_decl.name, decl_type,
                                    true, false, false, NULL, vd->loc);
        vd->var_decl.stack_offset = sym->stack_offset;
        return;
    }

    /* Enum type */
    if (find_enum_def(s, decl_type)) {
        if (vd->var_decl.value) {
            const char *vt = analyze_expr(s, vd->var_decl.value);
            if (strcmp(vt, decl_type) != 0)
                sem_error(s, vd->loc,
                          "Type mismatch in '%s': expected %s, got %s",
                          vd->var_decl.name, decl_type, vt);
        }
        Symbol *sym = define_symbol(s, vd->var_decl.name, decl_type,
                                    true, false, false, NULL, vd->loc);
        vd->var_decl.stack_offset = sym->stack_offset;
        return;
    }

    /* Scalar */
    if (vd->var_decl.value) {
        const char *vt = analyze_expr(s, vd->var_decl.value);
        vt = coerce_literal(vd->var_decl.value, vt, decl_type);
        if (strcmp(vt, decl_type) != 0)
            sem_error(s, vd->loc,
                      "Type mismatch in '%s': expected %s, got %s",
                      vd->var_decl.name, decl_type, vt);
    }

    Symbol *sym = define_symbol(s, vd->var_decl.name, decl_type,
                                true, false, false, NULL, vd->loc);
    vd->var_decl.stack_offset = sym->stack_offset;
}

/* ── Assignment ─────────────────────────────────────────── */

static void analyze_assignment(Semantic *s, ASTStmt *a)
{
    Symbol *sym = lookup_var(s, a->assign.name, a->loc);
    if (!sym->mutable)
        sem_error(s, a->loc,
                  "Cannot assign to immutable variable: %s", a->assign.name);
    const char *target = sym->type_name;

    if (strcmp(sym->type_name, "array") == 0 &&
        a->assign.value->kind != EXPR_COPY)
    {
        sem_error(s, a->loc,
                  "Array assignment requires 'copy': %s = copy ...",
                  a->assign.name);
    }

    const char *vt = analyze_expr(s, a->assign.value);
    vt = coerce_literal(a->assign.value, vt, target);
    if (strcmp(vt, target) != 0)
        sem_error(s, a->loc,
                  "Type mismatch assigning to '%s': expected %s, got %s",
                  a->assign.name, target, vt);
}

static void analyze_index_assignment(Semantic *s, ASTStmt *a)
{
    const char *elem = analyze_index_access_node(
        s, a->index_assign.array, a->index_assign.index, a->loc);
    const char *vt = analyze_expr(s, a->index_assign.value);
    vt = coerce_literal(a->index_assign.value, vt, elem);
    if (strcmp(vt, elem) != 0)
        sem_error(s, a->loc,
                  "Type mismatch in array element assignment: expected %s, got %s",
                  elem, vt);
}

static void analyze_field_assignment(Semantic *s, ASTStmt *a)
{
    const char *mt = analyze_field_access_on(
        s, a->field_assign.object, a->field_assign.member, a->loc);
    const char *vt = analyze_expr(s, a->field_assign.value);
    vt = coerce_literal(a->field_assign.value, vt, mt);
    if (strcmp(vt, mt) != 0)
        sem_error(s, a->loc,
                  "Type mismatch in field assignment: expected %s, got %s",
                  mt, vt);
}

static void analyze_compound_assignment(Semantic *s, ASTStmt *ca)
{
    const char *tt = analyze_expr(s, ca->compound_assign.target);

    if (ca->compound_assign.target->kind == EXPR_IDENT) {
        Symbol *sym = lookup_var(s, ca->compound_assign.target->ident.name,
                                 ca->loc);
        if (!sym->mutable)
            sem_error(s, ca->loc,
                      "Cannot assign to immutable variable: %s", sym->name);
    }

    if (!is_integer_type(tt))
        sem_error(s, ca->loc,
                  "Compound assignment requires numeric type, got %s", tt);

    const char *vt = analyze_expr(s, ca->compound_assign.value);
    coerce_literal(ca->compound_assign.value, vt, tt);
}

/* ── Control flow ───────────────────────────────────────── */

static void analyze_return(Semantic *s, ASTStmt *r)
{
    if (!s->current_func)
        sem_error(s, r->loc, "return outside of function");

    FuncSig *fs = find_func_sig(s, s->current_func->name);
    assert(fs);

    if (r->return_stmt.value) {
        const char *vt = analyze_expr(s, r->return_stmt.value);
        if (!fs->return_type)
            sem_error(s, r->loc,
                      "Function has no return type but returns a value");
        vt = coerce_literal(r->return_stmt.value, vt, fs->return_type);
        if (strcmp(vt, fs->return_type) != 0)
            sem_error(s, r->loc,
                      "Return type mismatch: expected %s, got %s",
                      fs->return_type, vt);
    } else if (fs->return_type) {
        sem_error(s, r->loc,
                  "Function must return a value of type %s",
                  fs->return_type);
    }
}

static void analyze_if(Semantic *s, ASTStmt *st)
{
    const char *ct = analyze_expr(s, st->if_stmt.condition);
    if (strcmp(ct, "bool") != 0 && !is_integer_type(ct))
        sem_error(s, st->loc,
                  "'when' condition must be bool or integer, got %s", ct);

    enter_scope(s);
    check_block_dead_code(s, st->if_stmt.body, st->if_stmt.body_count);
    for (int i = 0; i < st->if_stmt.body_count; i++)
        analyze_stmt(s, st->if_stmt.body[i]);
    exit_scope(s);

    if (st->if_stmt.else_body) {
        enter_scope(s);
        check_block_dead_code(s, st->if_stmt.else_body, st->if_stmt.else_count);
        for (int i = 0; i < st->if_stmt.else_count; i++)
            analyze_stmt(s, st->if_stmt.else_body[i]);
        exit_scope(s);
    }
}

static void analyze_while(Semantic *s, ASTStmt *w)
{
    const char *ct = analyze_expr(s, w->while_loop.condition);
    if (strcmp(ct, "bool") != 0 && !is_integer_type(ct))
        sem_error(s, w->loc,
                  "'while' condition must be bool or integer, got %s", ct);

    s->loop_depth++;
    enter_scope(s);
    check_block_dead_code(s, w->while_loop.body, w->while_loop.body_count);
    for (int i = 0; i < w->while_loop.body_count; i++)
        analyze_stmt(s, w->while_loop.body[i]);
    exit_scope(s);
    s->loop_depth--;
}

static void analyze_repeat(Semantic *s, ASTStmt *r)
{
    s->loop_depth++;
    enter_scope(s);
    check_block_dead_code(s, r->repeat_loop.body, r->repeat_loop.body_count);
    for (int i = 0; i < r->repeat_loop.body_count; i++)
        analyze_stmt(s, r->repeat_loop.body[i]);
    exit_scope(s);
    s->loop_depth--;
}

static void analyze_for(Semantic *s, ASTStmt *f)
{
    enter_scope(s);

    const char *var_type = "i32";

    if (f->for_loop.iterable && f->for_loop.iterable->kind == EXPR_RANGE) {
        ASTExpr *rng = f->for_loop.iterable;
        const char *st = analyze_expr(s, rng->range.start);
        const char *et = analyze_expr(s, rng->range.end);
        if (!is_integer_type(st))
            sem_error(s, f->loc, "Range start must be integer, got %s", st);
        if (!is_integer_type(et))
            sem_error(s, f->loc, "Range end must be integer, got %s", et);
        var_type = is_integer_type(st) ? st : "i32";
        if (rng->range.step)
            analyze_expr(s, rng->range.step);
    } else if (f->for_loop.iterable &&
               f->for_loop.iterable->kind == EXPR_IDENT)
    {
        Symbol *sym = lookup_var(s, f->for_loop.iterable->ident.name, f->loc);
        if (strcmp(sym->type_name, "array") != 0 || !sym->array_type)
            sem_error(s, f->loc,
                      "Cannot iterate over non-array: %s", sym->type_name);
        if (sym->array_type->kind == TYPE_NODE_ARRAY &&
            sym->array_type->array.elem &&
            sym->array_type->array.elem->kind == TYPE_NODE_SIMPLE)
        {
            var_type = sym->array_type->array.elem->simple.name;
            f->for_loop.array_elem_size = get_type_size(var_type);
            f->for_loop.array_count     = sym->array_type->array.size;
        }
    } else if (f->for_loop.iterable) {
        analyze_expr(s, f->for_loop.iterable);
        var_type = "i32";
    }

    define_symbol(s, f->for_loop.var_name, var_type,
                  false, false, false, NULL, f->loc);

    s->loop_depth++;
    check_block_dead_code(s, f->for_loop.body, f->for_loop.body_count);
    for (int i = 0; i < f->for_loop.body_count; i++)
        analyze_stmt(s, f->for_loop.body[i]);
    s->loop_depth--;

    exit_scope(s);
}

static void analyze_match(Semantic *s, ASTStmt *m)
{
    const char *vt = analyze_expr(s, m->match.expr);

    bool has_wildcard = false;
    for (int a = 0; a < m->match.arm_count; a++) {
        ASTMatchArm *arm = &m->match.arms[a];
        if (arm->is_wildcard) { has_wildcard = true; }
        if (!arm->is_wildcard && arm->pattern) {
            const char *pt = analyze_expr(s, arm->pattern);
            if (strcmp(pt, vt) != 0 &&
                !(strcmp(pt, "i32") == 0 && is_integer_type(vt)))
            {
                sem_error(s, arm->loc,
                          "Match pattern type '%s' != value type '%s'", pt, vt);
            }
        }
        enter_scope(s);
        check_block_dead_code(s, arm->body, arm->body_count);
        for (int i = 0; i < arm->body_count; i++)
            analyze_stmt(s, arm->body[i]);
        exit_scope(s);
    }

    /* Warn if matching on an enum without exhaustive coverage */
    if (!has_wildcard) {
        ASTEnumDef *ed = find_enum_def(s, vt);
        if (ed) {
            for (int v = 0; v < ed->variant_count; v++) {
                bool covered = false;
                for (int a = 0; a < m->match.arm_count; a++) {
                    ASTMatchArm *arm = &m->match.arms[a];
                    if (arm->pattern && arm->pattern->kind == EXPR_FIELD_ACCESS) {
                        if (strcmp(arm->pattern->field_access.member,
                                   ed->variants[v].name) == 0) {
                            covered = true;
                            break;
                        }
                    }
                }
                if (!covered) {
                    fprintf(stderr, "%s:%d:%d: warning: match on enum '%s' "
                            "missing variant '%s'\n",
                            s->filename, m->loc.line, m->loc.col,
                            vt, ed->variants[v].name);
                    break;  /* one warning is enough */
                }
            }
        }
    }
}

static void analyze_write(Semantic *s, ASTStmt *w)
{
    analyze_expr(s, w->write.value);
    /* any printable type is fine */
}

static void analyze_read_stmt(Semantic *s, ASTStmt *r)
{
    /* Target type checked at codegen */
    AXIS_UNUSED(s);
    AXIS_UNUSED(r);
}

/* ── Dead-code check for a statement block ──────────────── */

static void check_block_dead_code(Semantic *s, ASTStmt **stmts, int count)
{
    if (!s->check_dead) return;
    for (int i = 0; i < count; i++) {
        StmtKind k = stmts[i]->kind;
        if ((k == STMT_RETURN || k == STMT_BREAK || k == STMT_CONTINUE)
            && i + 1 < count)
        {
            fprintf(stderr, "%s:%d:%d: warning: unreachable code after %s\n",
                    s->filename,
                    stmts[i + 1]->loc.line,
                    stmts[i + 1]->loc.col,
                    k == STMT_RETURN ? "return" :
                    k == STMT_BREAK  ? "break"  : "continue");
            break;  /* one warning per block is enough */
        }
    }
}

/* ── Statement dispatch ─────────────────────────────────── */

static void analyze_stmt(Semantic *s, ASTStmt *st)
{
    switch (st->kind) {
    case STMT_VAR_DECL:         analyze_vardecl(s, st);              break;
    case STMT_ASSIGN:           analyze_assignment(s, st);           break;
    case STMT_INDEX_ASSIGN:     analyze_index_assignment(s, st);     break;
    case STMT_FIELD_ASSIGN:     analyze_field_assignment(s, st);     break;
    case STMT_COMPOUND_ASSIGN:  analyze_compound_assignment(s, st);  break;
    case STMT_RETURN:           analyze_return(s, st);               break;
    case STMT_IF:               analyze_if(s, st);                   break;
    case STMT_WHILE:            analyze_while(s, st);                break;
    case STMT_REPEAT:           analyze_repeat(s, st);               break;
    case STMT_FOR:              analyze_for(s, st);                  break;
    case STMT_MATCH:            analyze_match(s, st);                break;
    case STMT_WRITE:            analyze_write(s, st);                break;
    case STMT_READ:             analyze_read_stmt(s, st);            break;
    case STMT_EXPR:             analyze_expr(s, st->expr_stmt.expr); break;
    case STMT_SYSCALL:
        for (int i = 0; i < st->syscall.arg_count; i++)
            analyze_expr(s, st->syscall.args[i]);
        break;

    case STMT_BREAK:
        if (s->loop_depth == 0)
            sem_error(s, st->loc, "break outside of loop");
        break;
    case STMT_CONTINUE:
        if (s->loop_depth == 0)
            sem_error(s, st->loc, "continue outside of loop");
        break;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Function analysis
 * ═════════════════════════════════════════════════════════════ */

static void analyze_function(Semantic *s, ASTFunction *func)
{
    s->current_func  = func;
    s->stack_offset  = 0;
    enter_scope(s);

    for (int i = 0; i < func->param_count; i++) {
        ASTParam *p = &func->params[i];
        bool is_mut = p->is_update || p->is_copy;
        const char *ptype = type_name_of_node(p->type_node);

        /* Determine size */
        int ts;
        ASTFieldDef *fd = find_field_def(s, ptype);
        if (fd) {
            ts = calc_field_size(s, fd);
        } else if (p->type_node && p->type_node->kind == TYPE_NODE_ARRAY) {
            /* Array parameter: elem_size * count */
            const char *et = type_name_of_node(p->type_node->array.elem);
            ASTFieldDef *efd = find_field_def(s, et);
            int elem_sz = efd ? calc_field_size(s, efd) : get_type_size(et);
            int cnt = p->type_node->array.size;
            ts = elem_sz * (cnt > 0 ? cnt : 1);
        } else {
            ts = get_type_size(ptype);
        }

        int alignment = ts < 8 ? ts : 8;
        if (alignment < 1) alignment = 1;
        s->stack_offset = align_up(s->stack_offset, alignment);
        s->stack_offset += ts;
        int offset = -s->stack_offset;

        Symbol *sym = ARENA_NEW(s->arena, Symbol);
        sym->name        = p->name;
        sym->type_name   = ptype;
        sym->mutable     = is_mut;
        sym->stack_offset = offset;
        sym->is_param    = true;
        sym->is_update   = p->is_update;
        sym->array_type  = NULL;
        sym->next        = NULL;
        scope_define(s, sym, p->loc);
        p->stack_offset = offset;

        /* update params need extra slot for caller address */
        if (p->is_update) {
            s->stack_offset = align_up(s->stack_offset, 8);
            s->stack_offset += 8;
        }
    }

    /* Body */
    check_block_dead_code(s, func->body, func->body_count);
    for (int i = 0; i < func->body_count; i++)
        analyze_stmt(s, func->body[i]);

    func->stack_size = align_up(s->stack_offset, 16);
    exit_scope(s);
    s->current_func = NULL;
}

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

void semantic_init(Semantic *s, Arena *arena, const char *filename)
{
    memset(s, 0, sizeof(*s));
    s->arena    = arena;
    s->filename = filename;
}

int semantic_analyze(Semantic *s, ASTProgram *prog)
{
    s->global_scope  = new_scope(s, NULL);
    s->current_scope = s->global_scope;

    /* Pass 0a – collect field definitions */
    s->field_defs  = prog->field_defs;
    s->field_count = prog->field_count;
    for (int i = 0; i < prog->field_count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(prog->field_defs[i].name,
                       prog->field_defs[j].name) == 0)
                sem_error(s, prog->field_defs[i].loc,
                          "Duplicate field type: %s", prog->field_defs[i].name);
        }
    }

    /* Pass 0b – collect enum definitions */
    s->enum_defs  = prog->enum_defs;
    s->enum_count = prog->enum_count;
    for (int i = 0; i < prog->enum_count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(prog->enum_defs[i].name,
                       prog->enum_defs[j].name) == 0)
                sem_error(s, prog->enum_defs[i].loc,
                          "Duplicate enum type: %s", prog->enum_defs[i].name);
        }
        if (find_field_def(s, prog->enum_defs[i].name))
            sem_error(s, prog->enum_defs[i].loc,
                      "Enum name conflicts with field type: %s",
                      prog->enum_defs[i].name);
    }

    /* Pass 1 – collect function signatures */
    for (int i = 0; i < prog->func_count; i++) {
        ASTFunction *fn = &prog->functions[i];
        if (find_func_sig(s, fn->name))
            sem_error(s, fn->loc, "Duplicate function: %s", fn->name);

        FuncSig *fs = ARENA_NEW(s->arena, FuncSig);
        fs->name        = fn->name;
        fs->params      = fn->params;
        fs->param_count = fn->param_count;
        fs->return_type = fn->return_type
                          ? type_name_of_node(fn->return_type) : NULL;
        /* Treat explicit "void" the same as omitted return type */
        if (fs->return_type && strcmp(fs->return_type, "void") == 0)
            fs->return_type = NULL;
        fs->next       = s->func_sigs;
        s->func_sigs   = fs;
    }

    /* Pass 2 – analyse function bodies */
    for (int i = 0; i < prog->func_count; i++) {
        if (s->check_mode && setjmp(s->err_jmp)) {
            /* Recovery: skip this function, continue with next */
            continue;
        }
        analyze_function(s, &prog->functions[i]);
    }

    /* Pass 3 – top-level statements */
    if (prog->stmt_count > 0) {
        s->current_func = NULL;
        s->stack_offset = 0;
        for (volatile int i = 0; i < prog->stmt_count; i++) {
            if (s->check_mode && setjmp(s->err_jmp)) {
                continue;
            }
            analyze_stmt(s, prog->statements[i]);
        }
    }

    return s->error_count;
}
