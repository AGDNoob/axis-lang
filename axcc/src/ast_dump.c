/*
 * ast_dump.c – AST pretty-printer for --dump-ast
 */

#include "axis_ast.h"

/* ── Helpers ──────────────────────────────────────────────── */

static void indent(FILE *out, int depth)
{
    for (int i = 0; i < depth; i++)
        fprintf(out, "  ");
}

static const char *op_str(TokenType t)
{
    switch (t) {
    case TOK_PLUS:      return "+";
    case TOK_MINUS:     return "-";
    case TOK_STAR:      return "*";
    case TOK_SLASH:     return "/";
    case TOK_PERCENT:   return "%%";
    case TOK_AMP:       return "&";
    case TOK_PIPE:      return "|";
    case TOK_CARET:     return "^";
    case TOK_LSHIFT:    return "<<";
    case TOK_RSHIFT:    return ">>";
    case TOK_AND:       return "and";
    case TOK_OR:        return "or";
    case TOK_NOT:       return "not";
    case TOK_BANG:      return "!";
    case TOK_EQ:        return "==";
    case TOK_NE:        return "!=";
    case TOK_LT:        return "<";
    case TOK_LE:        return "<=";
    case TOK_GT:        return ">";
    case TOK_GE:        return ">=";
    case TOK_PLUS_ASSIGN:    return "+=";
    case TOK_MINUS_ASSIGN:   return "-=";
    case TOK_STAR_ASSIGN:    return "*=";
    case TOK_SLASH_ASSIGN:   return "/=";
    case TOK_PERCENT_ASSIGN: return "%%=";
    case TOK_AMP_ASSIGN:     return "&=";
    case TOK_PIPE_ASSIGN:    return "|=";
    case TOK_CARET_ASSIGN:   return "^=";
    case TOK_LSHIFT_ASSIGN:  return "<<=";
    case TOK_RSHIFT_ASSIGN:  return ">>=";
    default:                 return "?";
    }
}

/* ── Forward declarations ─────────────────────────────────── */

static void dump_expr(FILE *out, const ASTExpr *e, int depth);
static void dump_stmt(FILE *out, const ASTStmt *s, int depth);
static void dump_block(FILE *out, ASTStmt **stmts, int count, int depth);

/* ── Type node ────────────────────────────────────────────── */

static void dump_type(FILE *out, const ASTTypeNode *t)
{
    if (!t) { fprintf(out, "void"); return; }
    switch (t->kind) {
    case TYPE_NODE_SIMPLE:
        fprintf(out, "%s", t->simple.name);
        break;
    case TYPE_NODE_ARRAY:
        fprintf(out, "(");
        dump_type(out, t->array.elem);
        fprintf(out, "; %d)", t->array.size);
        break;
    }
}

/* ── Expressions ──────────────────────────────────────────── */

static void dump_expr(FILE *out, const ASTExpr *e, int depth)
{
    if (!e) { fprintf(out, "<null>"); return; }

    switch (e->kind) {
    case EXPR_INT_LIT:
        fprintf(out, "%lld", (long long)e->int_lit.value);
        break;

    case EXPR_STRING_LIT:
        fprintf(out, "\"%s\"", e->string_lit.value ? e->string_lit.value : "");
        break;

    case EXPR_BOOL_LIT:
        fprintf(out, "%s", e->bool_lit.value ? "true" : "false");
        break;

    case EXPR_IDENT:
        fprintf(out, "%s", e->ident.name);
        break;

    case EXPR_BINARY:
        fprintf(out, "(");
        dump_expr(out, e->binary.left, depth);
        fprintf(out, " %s ", op_str(e->binary.op));
        dump_expr(out, e->binary.right, depth);
        fprintf(out, ")");
        break;

    case EXPR_UNARY:
        fprintf(out, "(%s ", op_str(e->unary.op));
        dump_expr(out, e->unary.operand, depth);
        fprintf(out, ")");
        break;

    case EXPR_CALL:
        fprintf(out, "%s(", e->call.name);
        for (int i = 0; i < e->call.arg_count; i++) {
            if (i > 0) fprintf(out, ", ");
            if (e->call.update_flags && e->call.update_flags[i])
                fprintf(out, "update ");
            dump_expr(out, e->call.args[i], depth);
        }
        fprintf(out, ")");
        break;

    case EXPR_INDEX:
        dump_expr(out, e->index.array, depth);
        fprintf(out, "[");
        dump_expr(out, e->index.index, depth);
        fprintf(out, "]");
        break;

    case EXPR_FIELD_ACCESS:
        dump_expr(out, e->field_access.object, depth);
        fprintf(out, ".%s", e->field_access.member);
        break;

    case EXPR_ENUM_ACCESS:
        fprintf(out, "%s::%s", e->enum_access.enum_name, e->enum_access.variant);
        break;

    case EXPR_ARRAY_LIT:
        fprintf(out, "[");
        for (int i = 0; i < e->array_lit.count; i++) {
            if (i > 0) fprintf(out, ", ");
            dump_expr(out, e->array_lit.elements[i], depth);
        }
        fprintf(out, "]");
        break;

    case EXPR_COPY:
        fprintf(out, "copy ");
        dump_expr(out, e->copy.expr, depth);
        break;

    case EXPR_COPY_CAST:
        fprintf(out, "copy ");
        dump_expr(out, e->copy_cast.expr, depth);
        fprintf(out, " as ");
        dump_type(out, e->copy_cast.target_type);
        break;

    case EXPR_RANGE:
        dump_expr(out, e->range.start, depth);
        fprintf(out, "..");
        dump_expr(out, e->range.end, depth);
        if (e->range.step) {
            fprintf(out, " step ");
            dump_expr(out, e->range.step, depth);
        }
        break;

    case EXPR_INPUT:
        fprintf(out, "input(");
        if (e->input.prompt) dump_expr(out, e->input.prompt, depth);
        fprintf(out, ")");
        break;

    case EXPR_INPUT_FAILED:
        fprintf(out, "%s_input_failed()", e->input_failed.var_name);
        break;
    }
}

/* ── Statements ───────────────────────────────────────────── */

static void dump_block(FILE *out, ASTStmt **stmts, int count, int depth)
{
    for (int i = 0; i < count; i++)
        dump_stmt(out, stmts[i], depth);
}

static void dump_stmt(FILE *out, const ASTStmt *s, int depth)
{
    if (!s) return;
    indent(out, depth);

    switch (s->kind) {
    case STMT_VAR_DECL:
        fprintf(out, "%s %s",
                s->var_decl.is_const ? "const" : "let",
                s->var_decl.name);
        if (s->var_decl.type_node) {
            fprintf(out, ": ");
            dump_type(out, s->var_decl.type_node);
        }
        if (s->var_decl.value) {
            fprintf(out, " = ");
            dump_expr(out, s->var_decl.value, depth);
        }
        fprintf(out, "\n");
        break;

    case STMT_ASSIGN:
        fprintf(out, "%s = ", s->assign.name);
        dump_expr(out, s->assign.value, depth);
        fprintf(out, "\n");
        break;

    case STMT_INDEX_ASSIGN:
        dump_expr(out, s->index_assign.array, depth);
        fprintf(out, "[");
        dump_expr(out, s->index_assign.index, depth);
        fprintf(out, "] = ");
        dump_expr(out, s->index_assign.value, depth);
        fprintf(out, "\n");
        break;

    case STMT_FIELD_ASSIGN:
        dump_expr(out, s->field_assign.object, depth);
        fprintf(out, ".%s = ", s->field_assign.member);
        dump_expr(out, s->field_assign.value, depth);
        fprintf(out, "\n");
        break;

    case STMT_COMPOUND_ASSIGN:
        dump_expr(out, s->compound_assign.target, depth);
        fprintf(out, " %s ", op_str(s->compound_assign.op));
        dump_expr(out, s->compound_assign.value, depth);
        fprintf(out, "\n");
        break;

    case STMT_EXPR:
        dump_expr(out, s->expr_stmt.expr, depth);
        fprintf(out, "\n");
        break;

    case STMT_WRITE:
        fprintf(out, "%s ", s->write.newline ? "writeln" : "write");
        dump_expr(out, s->write.value, depth);
        fprintf(out, "\n");
        break;

    case STMT_IF:
        fprintf(out, "when ");
        dump_expr(out, s->if_stmt.condition, depth);
        fprintf(out, ":\n");
        dump_block(out, s->if_stmt.body, s->if_stmt.body_count, depth + 1);
        if (s->if_stmt.else_body) {
            indent(out, depth);
            fprintf(out, "else:\n");
            dump_block(out, s->if_stmt.else_body, s->if_stmt.else_count, depth + 1);
        }
        break;

    case STMT_WHILE:
        fprintf(out, "while ");
        dump_expr(out, s->while_loop.condition, depth);
        if (s->while_loop.flag)
            fprintf(out, " @%s", s->while_loop.flag);
        fprintf(out, ":\n");
        dump_block(out, s->while_loop.body, s->while_loop.body_count, depth + 1);
        break;

    case STMT_REPEAT:
        fprintf(out, "repeat");
        if (s->repeat_loop.flag)
            fprintf(out, " @%s", s->repeat_loop.flag);
        fprintf(out, ":\n");
        dump_block(out, s->repeat_loop.body, s->repeat_loop.body_count, depth + 1);
        break;

    case STMT_FOR:
        fprintf(out, "for %s in ", s->for_loop.var_name);
        dump_expr(out, s->for_loop.iterable, depth);
        if (s->for_loop.flag)
            fprintf(out, " @%s", s->for_loop.flag);
        fprintf(out, ":\n");
        dump_block(out, s->for_loop.body, s->for_loop.body_count, depth + 1);
        break;

    case STMT_BREAK:
        fprintf(out, "break");
        if (s->break_stmt.flag)
            fprintf(out, " @%s", s->break_stmt.flag);
        fprintf(out, "\n");
        break;

    case STMT_CONTINUE:
        fprintf(out, "continue");
        if (s->continue_stmt.flag)
            fprintf(out, " @%s", s->continue_stmt.flag);
        fprintf(out, "\n");
        break;

    case STMT_RETURN:
        fprintf(out, "return");
        if (s->return_stmt.value) {
            fprintf(out, " ");
            dump_expr(out, s->return_stmt.value, depth);
        }
        fprintf(out, "\n");
        break;

    case STMT_MATCH:
        fprintf(out, "match ");
        dump_expr(out, s->match.expr, depth);
        fprintf(out, ":\n");
        for (int i = 0; i < s->match.arm_count; i++) {
            const ASTMatchArm *arm = &s->match.arms[i];
            indent(out, depth + 1);
            if (arm->is_wildcard)
                fprintf(out, "_");
            else
                dump_expr(out, arm->pattern, depth + 1);
            fprintf(out, " =>\n");
            dump_block(out, arm->body, arm->body_count, depth + 2);
        }
        break;

    case STMT_SYSCALL:
        fprintf(out, "syscall(");
        for (int i = 0; i < s->syscall.arg_count; i++) {
            if (i > 0) fprintf(out, ", ");
            dump_expr(out, s->syscall.args[i], depth);
        }
        fprintf(out, ")\n");
        break;

    case STMT_UPDATE_CAST:
        fprintf(out, "update %s as ", s->update_cast.var_name);
        dump_type(out, s->update_cast.target_type);
        fprintf(out, "\n");
        break;
    }
}

/* ── Functions ────────────────────────────────────────────── */

static void dump_function(FILE *out, const ASTFunction *fn)
{
    fprintf(out, "  fn %s(", fn->name);
    for (int i = 0; i < fn->param_count; i++) {
        if (i > 0) fprintf(out, ", ");
        const ASTParam *p = &fn->params[i];
        if (p->is_update) fprintf(out, "update ");
        if (p->is_copy)   fprintf(out, "copy ");
        fprintf(out, "%s: ", p->name);
        dump_type(out, p->type_node);
    }
    fprintf(out, ")");
    if (fn->return_type) {
        fprintf(out, " -> ");
        dump_type(out, fn->return_type);
    }
    fprintf(out, ":\n");
    dump_block(out, fn->body, fn->body_count, 2);
}

/* ── Field defs ───────────────────────────────────────────── */

static void dump_field_member(FILE *out, const ASTFieldMember *m, int depth)
{
    indent(out, depth);
    fprintf(out, "%s: ", m->name);
    dump_type(out, m->type_node);
    if (m->default_value) {
        fprintf(out, " = ");
        dump_expr(out, m->default_value, depth);
    }
    fprintf(out, "\n");
    for (int i = 0; i < m->inline_count; i++)
        dump_field_member(out, &m->inline_members[i], depth + 1);
}

static void dump_field_def(FILE *out, const ASTFieldDef *fd)
{
    fprintf(out, "  field %s:\n", fd->name);
    for (int i = 0; i < fd->member_count; i++)
        dump_field_member(out, &fd->members[i], 2);
}

/* ── Enum defs ────────────────────────────────────────────── */

static void dump_enum_def(FILE *out, const ASTEnumDef *ed)
{
    fprintf(out, "  enum %s", ed->name);
    if (ed->underlying_type)
        fprintf(out, " (%s)", ed->underlying_type);
    fprintf(out, ":\n");
    for (int i = 0; i < ed->variant_count; i++) {
        const ASTEnumVariant *v = &ed->variants[i];
        indent(out, 2);
        fprintf(out, "%s", v->name);
        if (v->has_value)
            fprintf(out, " = %d", v->value);
        fprintf(out, "\n");
    }
}

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

void ast_dump(const ASTProgram *prog, FILE *out)
{
    fprintf(out, "=== AXIS AST Dump ===\n");
    fprintf(out, "mode: %s\n", prog->mode == MODE_SCRIPT ? "script" : "compile");

    if (prog->enum_count > 0) {
        fprintf(out, "\nEnums (%d):\n", prog->enum_count);
        for (int i = 0; i < prog->enum_count; i++)
            dump_enum_def(out, &prog->enum_defs[i]);
    }

    if (prog->field_count > 0) {
        fprintf(out, "\nFields (%d):\n", prog->field_count);
        for (int i = 0; i < prog->field_count; i++)
            dump_field_def(out, &prog->field_defs[i]);
    }

    if (prog->func_count > 0) {
        fprintf(out, "\nFunctions (%d):\n", prog->func_count);
        for (int i = 0; i < prog->func_count; i++)
            dump_function(out, &prog->functions[i]);
    }

    if (prog->stmt_count > 0) {
        fprintf(out, "\nTop-level statements (%d):\n", prog->stmt_count);
        dump_block(out, prog->statements, prog->stmt_count, 1);
    }

    fprintf(out, "=== END AST ===\n\n");
}
