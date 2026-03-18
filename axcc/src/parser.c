/*
 * parser.c – Recursive-descent parser for the AXIS language.
 *
 * Operator precedence (lowest → highest):
 *   1. or        (logical)
 *   2. and       (logical)
 *   3. |         (bitwise OR)
 *   4. ^         (bitwise XOR)
 *   5. &         (bitwise AND)
 *   6. == !=     (equality)
 *   7. < <= > >= (relational)
 *   8. << >>     (shift)
 *   9. + -       (additive)
 *  10. * / %     (multiplicative)
 *  11. - ! not   (unary)
 *  12. [] .      (postfix: index, field access)
 */

#include "axis_parser.h"
#include <stdarg.h>
#include <setjmp.h>

/* ═════════════════════════════════════════════════════════════
 * Helpers
 * ═════════════════════════════════════════════════════════════ */

static void parse_error(Parser *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: parse error: ",
            p->filename, p->cur ? p->cur->loc.line : 0,
            p->cur ? p->cur->loc.col : 0);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    p->error_count++;
    if (p->check_mode) {
        longjmp(p->err_jmp, 1);
    }
    exit(1);
}

static inline Token *cur(Parser *p)
{
    return p->cur;
}

static inline bool at_end(Parser *p)
{
    return !p->cur || p->cur->type == TOK_EOF;
}

static inline bool match(Parser *p, TokenType t)
{
    return p->cur && p->cur->type == t;
}

static inline bool match2(Parser *p, TokenType a, TokenType b)
{
    return p->cur && (p->cur->type == a || p->cur->type == b);
}

static inline bool match3(Parser *p, TokenType a, TokenType b, TokenType c)
{
    return p->cur && (p->cur->type == a || p->cur->type == b || p->cur->type == c);
}

static void advance(Parser *p)
{
    p->pos++;
    p->cur = (p->pos < p->token_count) ? &p->tokens[p->pos] : NULL;
}

static Token *expect(Parser *p, TokenType t)
{
    if (!match(p, t)) {
        parse_error(p, "expected %s, got %s",
                    token_type_name(t),
                    p->cur ? token_type_name(p->cur->type) : "EOF");
    }
    Token *tok = p->cur;
    advance(p);
    return tok;
}

static Token *peek(Parser *p, int offset)
{
    int idx = p->pos + offset;
    return (idx >= 0 && idx < p->token_count) ? &p->tokens[idx] : NULL;
}

static void skip_newlines(Parser *p)
{
    while (match(p, TOK_NEWLINE))
        advance(p);
}

static SrcLoc loc(Parser *p)
{
    if (p->cur) return p->cur->loc;
    SrcLoc s = {0, 0};
    return s;
}

/* Arena helpers for node allocation */
#define NEW_EXPR(p)   ((ASTExpr *)arena_alloc((p)->arena, sizeof(ASTExpr)))
#define NEW_STMT(p)   ((ASTStmt *)arena_alloc((p)->arena, sizeof(ASTStmt)))

/* Dynamic list helpers – grow an arena-allocated pointer array.
 * We keep a small local buffer, then copy into the arena at the end. */
typedef struct { void **items; int count; int cap; } PtrVec;

static void ptrvec_init(PtrVec *v)
{
    v->items = NULL;
    v->count = 0;
    v->cap   = 0;
}

static void ptrvec_push(PtrVec *v, void *item, Arena *a)
{
    if (v->count == v->cap) {
        int newcap = v->cap ? v->cap * 2 : 8;
        void **buf = (void **)arena_alloc(a, (size_t)newcap * sizeof(void *));
        if (v->items)
            memcpy(buf, v->items, (size_t)v->count * sizeof(void *));
        v->items = buf;
        v->cap   = newcap;
    }
    v->items[v->count++] = item;
}

/* ═════════════════════════════════════════════════════════════
 * Forward declarations
 * ═════════════════════════════════════════════════════════════ */

static ASTExpr     *parse_expression(Parser *p);
static ASTStmt     *parse_statement(Parser *p);
static ASTTypeNode *parse_type_node(Parser *p);
static void         parse_block(Parser *p, ASTStmt ***out_body, int *out_count);

/* ═════════════════════════════════════════════════════════════
 * Type parsing
 * ═════════════════════════════════════════════════════════════ */

static bool is_type_token(TokenType t)
{
    return t == TOK_I8  || t == TOK_I16 || t == TOK_I32 || t == TOK_I64 ||
           t == TOK_U8  || t == TOK_U16 || t == TOK_U32 || t == TOK_U64 ||
           t == TOK_BOOL || t == TOK_STR;
}

static const char *type_token_str(TokenType t)
{
    switch (t) {
    case TOK_I8:   return "i8";   case TOK_I16:  return "i16";
    case TOK_I32:  return "i32";  case TOK_I64:  return "i64";
    case TOK_U8:   return "u8";   case TOK_U16:  return "u16";
    case TOK_U32:  return "u32";  case TOK_U64:  return "u64";
    case TOK_BOOL: return "bool"; case TOK_STR:  return "str";
    default:       return NULL;
    }
}

static ASTTypeNode *parse_array_type(Parser *p)
{
    TokenType open = cur(p)->type;
    TokenType close = (open == TOK_LPAREN) ? TOK_RPAREN : TOK_RBRACKET;
    advance(p);

    ASTTypeNode *elem = parse_type_node(p);
    int size = 0;
    if (match(p, TOK_SEMICOLON)) {
        advance(p);
        if (!match(p, TOK_INT_LIT))
            parse_error(p, "expected array size");
        size = (int)cur(p)->int_val;
        advance(p);
    }
    expect(p, close);

    ASTTypeNode *n = ARENA_NEW(p->arena, ASTTypeNode);
    n->kind       = TYPE_NODE_ARRAY;
    n->array.elem = elem;
    n->array.size = size;
    return n;
}

static ASTTypeNode *parse_type_node(Parser *p)
{
    /* Array: (elem; size) or [elem; size] */
    if (match2(p, TOK_LPAREN, TOK_LBRACKET))
        return parse_array_type(p);

    /* Scalar or named type */
    if (p->cur && is_type_token(p->cur->type)) {
        ASTTypeNode *n = ARENA_NEW(p->arena, ASTTypeNode);
        n->kind        = TYPE_NODE_SIMPLE;
        n->loc         = loc(p);
        n->simple.name = type_token_str(p->cur->type);
        advance(p);
        return n;
    }
    if (match(p, TOK_IDENT)) {
        ASTTypeNode *n = ARENA_NEW(p->arena, ASTTypeNode);
        n->kind        = TYPE_NODE_SIMPLE;
        n->loc         = loc(p);
        n->simple.name = cur(p)->str_val;
        advance(p);
        return n;
    }
    parse_error(p, "expected type, got %s",
                p->cur ? token_type_name(p->cur->type) : "EOF");
    return NULL; /* unreachable */
}

/* ═════════════════════════════════════════════════════════════
 * Expression parsing (precedence climbing)
 * ═════════════════════════════════════════════════════════════ */

static ASTExpr *parse_unary(Parser *p);

/* ---- primary ---- */
static ASTExpr *parse_primary(Parser *p)
{
    /* Integer literal */
    if (match(p, TOK_INT_LIT)) {
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_INT_LIT;
        e->loc  = loc(p);
        e->int_lit.value = cur(p)->int_val;
        advance(p);
        return e;
    }

    /* String literal */
    if (match(p, TOK_STRING_LIT)) {
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_STRING_LIT;
        e->loc  = loc(p);
        e->string_lit.value = cur(p)->str_val;
        advance(p);
        return e;
    }

    /* Boolean literals */
    if (match(p, TOK_TRUE)) {
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_BOOL_LIT;
        e->loc  = loc(p);
        e->bool_lit.value = true;
        advance(p);
        return e;
    }
    if (match(p, TOK_FALSE)) {
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_BOOL_LIT;
        e->loc  = loc(p);
        e->bool_lit.value = false;
        advance(p);
        return e;
    }

    /* copy [.mode] expr */
    if (match(p, TOK_COPY)) {
        SrcLoc l = loc(p);
        advance(p);
        bool compile_time = false;
        if (match(p, TOK_DOT)) {
            advance(p);
            if (match(p, TOK_IDENT) && strcmp(cur(p)->str_val, "runtime") == 0) {
                advance(p);
            } else if (match(p, TOK_COMPILE)) {
                compile_time = true;
                advance(p);
            } else {
                parse_error(p, "expected 'runtime' or 'compile' after 'copy.'");
            }
        }
        ASTExpr *operand = parse_unary(p);  /* copy applies to a single operand */
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_COPY;
        e->loc  = l;
        e->copy.expr         = operand;
        e->copy.compile_time = compile_time;
        return e;
    }

    /* Array literal [...] */
    if (match(p, TOK_LBRACKET)) {
        SrcLoc l = loc(p);
        advance(p);
        PtrVec elems;
        ptrvec_init(&elems);
        if (!match(p, TOK_RBRACKET)) {
            ptrvec_push(&elems, parse_expression(p), p->arena);
            while (match(p, TOK_COMMA)) {
                advance(p);
                ptrvec_push(&elems, parse_expression(p), p->arena);
            }
        }
        expect(p, TOK_RBRACKET);
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_ARRAY_LIT;
        e->loc  = l;
        e->array_lit.elements = (ASTExpr **)elems.items;
        e->array_lit.count    = elems.count;
        return e;
    }

    /* Built-in read functions: read(), readln(), readchar() → Call */
    if (match3(p, TOK_READ, TOK_READLN, TOK_READCHAR)) {
        SrcLoc l = loc(p);
        const char *name = NULL;
        switch (cur(p)->type) {
        case TOK_READ:     name = "read";     break;
        case TOK_READLN:   name = "readln";   break;
        case TOK_READCHAR: name = "readchar"; break;
        default: break;
        }
        advance(p);
        expect(p, TOK_LPAREN);
        expect(p, TOK_RPAREN);
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_CALL;
        e->loc  = l;
        e->call.name         = name;
        e->call.args         = NULL;
        e->call.update_flags = NULL;
        e->call.arg_count    = 0;
        return e;
    }

    /* read_failed() */
    if (match(p, TOK_READ_FAILED)) {
        SrcLoc l = loc(p);
        advance(p);
        expect(p, TOK_LPAREN);
        expect(p, TOK_RPAREN);
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_READ_FAILED;
        e->loc  = l;
        return e;
    }

    /* Identifier, function call, or enum access (Name::Variant) */
    if (match(p, TOK_IDENT)) {
        SrcLoc l = loc(p);
        const char *name = cur(p)->str_val;
        advance(p);

        /* Enum access: Name::Variant */
        if (match(p, TOK_COLONCOLON)) {
            advance(p);
            if (!match(p, TOK_IDENT))
                parse_error(p, "expected variant name after '::'");
            const char *variant = cur(p)->str_val;
            advance(p);
            ASTExpr *e = NEW_EXPR(p);
            e->kind = EXPR_ENUM_ACCESS;
            e->loc  = l;
            e->enum_access.enum_name = name;
            e->enum_access.variant   = variant;
            return e;
        }

        /* Function call: name(args...) */
        if (match(p, TOK_LPAREN)) {
            advance(p);
            PtrVec args;
            ptrvec_init(&args);
            if (!match(p, TOK_RPAREN)) {
                ptrvec_push(&args, parse_expression(p), p->arena);
                while (match(p, TOK_COMMA)) {
                    advance(p);
                    ptrvec_push(&args, parse_expression(p), p->arena);
                }
            }
            expect(p, TOK_RPAREN);
            ASTExpr *e = NEW_EXPR(p);
            e->kind = EXPR_CALL;
            e->loc  = l;
            e->call.name         = name;
            e->call.args         = (ASTExpr **)args.items;
            e->call.arg_count    = args.count;
            if (args.count > 0) {
                e->call.update_flags = arena_alloc(p->arena,
                    (size_t)args.count * sizeof(bool));
                memset(e->call.update_flags, 0,
                       (size_t)args.count * sizeof(bool));
            } else {
                e->call.update_flags = NULL;
            }
            return e;
        }

        /* Plain identifier */
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_IDENT;
        e->loc  = l;
        e->ident.name = name;
        return e;
    }

    /* Parenthesised expression */
    if (match(p, TOK_LPAREN)) {
        advance(p);
        ASTExpr *e = parse_expression(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    parse_error(p, "unexpected token in expression: %s",
                p->cur ? token_type_name(p->cur->type) : "EOF");
    return NULL; /* unreachable */
}

/* ---- postfix: [index], .member ---- */
static ASTExpr *parse_postfix(Parser *p)
{
    ASTExpr *e = parse_primary(p);
    while (match2(p, TOK_LBRACKET, TOK_DOT)) {
        if (match(p, TOK_LBRACKET)) {
            advance(p);
            ASTExpr *idx = parse_expression(p);
            expect(p, TOK_RBRACKET);
            ASTExpr *n = NEW_EXPR(p);
            n->kind = EXPR_INDEX;
            n->loc  = e->loc;
            n->index.array = e;
            n->index.index = idx;
            e = n;
        } else { /* TOK_DOT */
            advance(p);
            if (!match(p, TOK_IDENT))
                parse_error(p, "expected member name after '.'");
            const char *member = cur(p)->str_val;
            advance(p);
            ASTExpr *n = NEW_EXPR(p);
            n->kind = EXPR_FIELD_ACCESS;
            n->loc  = e->loc;
            n->field_access.object = e;
            n->field_access.member = member;
            e = n;
        }
    }
    return e;
}

/* ---- unary ---- */
static ASTExpr *parse_unary(Parser *p);

static ASTExpr *parse_unary(Parser *p)
{
    if (match(p, TOK_MINUS)) {
        SrcLoc l = loc(p);
        advance(p);
        ASTExpr *operand = parse_unary(p);
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_UNARY;
        e->loc  = l;
        e->unary.op      = TOK_MINUS;
        e->unary.operand = operand;
        return e;
    }
    if (match(p, TOK_BANG)) {
        SrcLoc l = loc(p);
        advance(p);
        ASTExpr *operand = parse_unary(p);
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_UNARY;
        e->loc  = l;
        e->unary.op      = TOK_BANG;
        e->unary.operand = operand;
        return e;
    }
    if (match(p, TOK_NOT)) {
        SrcLoc l = loc(p);
        advance(p);
        ASTExpr *operand = parse_unary(p);
        ASTExpr *e = NEW_EXPR(p);
        e->kind = EXPR_UNARY;
        e->loc  = l;
        e->unary.op      = TOK_NOT;
        e->unary.operand = operand;
        return e;
    }
    return parse_postfix(p);
}

/* ---- binary precedence levels ---- */

/* Helper macro for left-associative binary levels */
#define DEFINE_BINARY_LEVEL(name, next, ...)                              \
static ASTExpr *name(Parser *p)                                           \
{                                                                         \
    ASTExpr *e = next(p);                                                 \
    TokenType _ops[] = { __VA_ARGS__ };                                   \
    for (;;) {                                                            \
        bool found = false;                                               \
        for (int _i = 0; _i < (int)AXIS_ARRAY_LEN(_ops); _i++) {         \
            if (match(p, _ops[_i])) {                                     \
                SrcLoc l = loc(p);                                        \
                TokenType op = cur(p)->type;                              \
                advance(p);                                               \
                ASTExpr *right = next(p);                                 \
                ASTExpr *n = NEW_EXPR(p);                                 \
                n->kind         = EXPR_BINARY;                            \
                n->loc          = l;                                      \
                n->binary.left  = e;                                      \
                n->binary.op    = op;                                     \
                n->binary.right = right;                                  \
                e = n;                                                    \
                found = true;                                             \
                break;                                                    \
            }                                                             \
        }                                                                 \
        if (!found) break;                                                \
    }                                                                     \
    return e;                                                             \
}

/* 10. multiplicative: * / % */
DEFINE_BINARY_LEVEL(parse_multiplicative, parse_unary, TOK_STAR, TOK_SLASH, TOK_PERCENT)

/* 9. additive: + - */
DEFINE_BINARY_LEVEL(parse_additive, parse_multiplicative, TOK_PLUS, TOK_MINUS)

/* 8. shift: << >> */
DEFINE_BINARY_LEVEL(parse_shift, parse_additive, TOK_LSHIFT, TOK_RSHIFT)

/* 7. relational: < <= > >= */
DEFINE_BINARY_LEVEL(parse_comparison, parse_shift, TOK_LT, TOK_LE, TOK_GT, TOK_GE)

/* 6. equality: == != */
DEFINE_BINARY_LEVEL(parse_equality, parse_comparison, TOK_EQ, TOK_NE)

/* 5. bitwise AND: & */
DEFINE_BINARY_LEVEL(parse_bitwise_and, parse_equality, TOK_AMP)

/* 4. bitwise XOR: ^ */
DEFINE_BINARY_LEVEL(parse_bitwise_xor, parse_bitwise_and, TOK_CARET)

/* 3. bitwise OR: | */
DEFINE_BINARY_LEVEL(parse_bitwise_or, parse_bitwise_xor, TOK_PIPE)

/* 2. logical AND */
DEFINE_BINARY_LEVEL(parse_logical_and, parse_bitwise_or, TOK_AND)

/* 1. logical OR */
DEFINE_BINARY_LEVEL(parse_logical_or, parse_logical_and, TOK_OR)

static ASTExpr *parse_expression(Parser *p)
{
    return parse_logical_or(p);
}

/* ═════════════════════════════════════════════════════════════
 * Block parsing
 * ═════════════════════════════════════════════════════════════ */

static void parse_block(Parser *p, ASTStmt ***out_body, int *out_count)
{
    expect(p, TOK_INDENT);
    PtrVec stmts;
    ptrvec_init(&stmts);
    while (!match(p, TOK_DEDENT)) {
        if (at_end(p))
            parse_error(p, "unexpected EOF in block");
        if (match(p, TOK_NEWLINE)) {
            advance(p);
            continue;
        }
        ptrvec_push(&stmts, parse_statement(p), p->arena);
    }
    expect(p, TOK_DEDENT);
    *out_body  = (ASTStmt **)stmts.items;
    *out_count = stmts.count;
}

/* ═════════════════════════════════════════════════════════════
 * Statement parsing
 * ═════════════════════════════════════════════════════════════ */

static ASTStmt *parse_var_decl(Parser *p)
{
    SrcLoc l = loc(p);
    const char *name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_COLON);
    ASTTypeNode *type = parse_type_node(p);
    ASTExpr *value = NULL;
    if (match(p, TOK_ASSIGN)) {
        advance(p);
        value = parse_expression(p);
    }
    skip_newlines(p);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_VAR_DECL;
    s->loc  = l;
    s->var_decl.name         = name;
    s->var_decl.type_node    = type;
    s->var_decl.value        = value;
    s->var_decl.stack_offset = 0;
    s->var_decl.total_size   = 0;
    return s;
}

static ASTStmt *parse_return(Parser *p)
{
    SrcLoc l = loc(p);
    advance(p); /* GIVE or RETURN */
    ASTExpr *value = NULL;
    if (!match3(p, TOK_NEWLINE, TOK_DEDENT, TOK_EOF))
        value = parse_expression(p);
    skip_newlines(p);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_RETURN;
    s->loc  = l;
    s->return_stmt.value = value;
    return s;
}

static ASTStmt *parse_if(Parser *p)
{
    SrcLoc l = loc(p);
    expect(p, TOK_WHEN);
    ASTExpr *cond = parse_expression(p);
    expect(p, TOK_COLON);
    skip_newlines(p);

    ASTStmt **body;
    int body_count;
    parse_block(p, &body, &body_count);

    ASTStmt **else_body  = NULL;
    int       else_count = 0;
    if (match(p, TOK_ELSE)) {
        advance(p);
        if (match(p, TOK_WHEN)) {
            /* else when → chained if */
            ASTStmt *chained = parse_if(p);
            else_body  = (ASTStmt **)arena_alloc(p->arena, sizeof(ASTStmt *));
            else_body[0] = chained;
            else_count = 1;
        } else {
            expect(p, TOK_COLON);
            skip_newlines(p);
            parse_block(p, &else_body, &else_count);
        }
    }

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_IF;
    s->loc  = l;
    s->if_stmt.condition  = cond;
    s->if_stmt.body       = body;
    s->if_stmt.body_count = body_count;
    s->if_stmt.else_body  = else_body;
    s->if_stmt.else_count = else_count;
    return s;
}

static ASTStmt *parse_while(Parser *p)
{
    SrcLoc l = loc(p);
    expect(p, TOK_WHILE);
    ASTExpr *cond = parse_expression(p);
    expect(p, TOK_COLON);
    skip_newlines(p);

    ASTStmt **body;
    int body_count;
    parse_block(p, &body, &body_count);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_WHILE;
    s->loc  = l;
    s->while_loop.condition  = cond;
    s->while_loop.body       = body;
    s->while_loop.body_count = body_count;
    return s;
}

static ASTStmt *parse_repeat(Parser *p)
{
    SrcLoc l = loc(p);
    advance(p); /* REPEAT (or LOOP, both map to TOK_REPEAT) */
    expect(p, TOK_COLON);
    skip_newlines(p);

    ASTStmt **body;
    int body_count;
    parse_block(p, &body, &body_count);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_REPEAT;
    s->loc  = l;
    s->repeat_loop.body       = body;
    s->repeat_loop.body_count = body_count;
    return s;
}

static ASTStmt *parse_match(Parser *p)
{
    SrcLoc l = loc(p);
    advance(p); /* MATCH */
    ASTExpr *expr = parse_expression(p);
    expect(p, TOK_COLON);
    skip_newlines(p);

    expect(p, TOK_INDENT);
    PtrVec arms;
    ptrvec_init(&arms);
    while (!match(p, TOK_DEDENT)) {
        if (at_end(p))
            parse_error(p, "unexpected EOF in match statement");
        if (match(p, TOK_NEWLINE)) { advance(p); continue; }

        ASTMatchArm *arm = ARENA_NEW(p->arena, ASTMatchArm);
        arm->loc = loc(p);
        arm->is_wildcard = false;
        arm->pattern     = NULL;

        /* Wildcard: _ */
        if (match(p, TOK_UNDERSCORE)) {
            arm->is_wildcard = true;
            advance(p);
        } else if (match(p, TOK_IDENT) && strcmp(cur(p)->str_val, "_") == 0) {
            arm->is_wildcard = true;
            advance(p);
        } else {
            arm->pattern = parse_expression(p);
        }
        expect(p, TOK_COLON);
        skip_newlines(p);
        parse_block(p, &arm->body, &arm->body_count);
        ptrvec_push(&arms, arm, p->arena);
    }
    expect(p, TOK_DEDENT);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_MATCH;
    s->loc  = l;
    s->match.expr      = expr;
    s->match.arms       = (arms.count > 0) ? (ASTMatchArm *)arena_alloc(p->arena, (size_t)arms.count * sizeof(ASTMatchArm)) : NULL;
    s->match.arm_count  = arms.count;
    /* Copy from pointer array into contiguous array */
    for (int i = 0; i < arms.count; i++)
        s->match.arms[i] = *(ASTMatchArm *)arms.items[i];
    return s;
}

static ASTStmt *parse_for(Parser *p)
{
    SrcLoc l = loc(p);
    expect(p, TOK_FOR);
    const char *var_name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_IN);

    ASTExpr *iterable;
    /* range(start, end[, step]) */
    if (match(p, TOK_IDENT) && strcmp(cur(p)->str_val, "range") == 0) {
        SrcLoc rl = loc(p);
        advance(p);
        expect(p, TOK_LPAREN);
        ASTExpr *start = parse_expression(p);
        expect(p, TOK_COMMA);
        ASTExpr *end = parse_expression(p);
        ASTExpr *step = NULL;
        if (match(p, TOK_COMMA)) {
            advance(p);
            step = parse_expression(p);
        }
        expect(p, TOK_RPAREN);
        iterable = NEW_EXPR(p);
        iterable->kind = EXPR_RANGE;
        iterable->loc  = rl;
        iterable->range.start = start;
        iterable->range.end   = end;
        iterable->range.step  = step;
    } else {
        iterable = parse_expression(p);
    }

    expect(p, TOK_COLON);
    skip_newlines(p);

    ASTStmt **body;
    int body_count;
    parse_block(p, &body, &body_count);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_FOR;
    s->loc  = l;
    s->for_loop.var_name   = var_name;
    s->for_loop.iterable   = iterable;
    s->for_loop.body       = body;
    s->for_loop.body_count = body_count;
    return s;
}

static ASTStmt *parse_write(Parser *p)
{
    SrcLoc l = loc(p);
    bool newline = (cur(p)->type == TOK_WRITELN);
    advance(p);
    expect(p, TOK_LPAREN);
    ASTExpr *value = parse_expression(p);
    expect(p, TOK_RPAREN);
    skip_newlines(p);

    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_WRITE;
    s->loc  = l;
    s->write.value   = value;
    s->write.newline = newline;
    return s;
}

static ASTStmt *parse_expr_statement(Parser *p)
{
    ASTExpr *expr = parse_expression(p);

    /* Compound assignment operators */
    static const TokenType compound_ops[] = {
        TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN, TOK_STAR_ASSIGN,
        TOK_SLASH_ASSIGN, TOK_PERCENT_ASSIGN,
        TOK_AMP_ASSIGN, TOK_PIPE_ASSIGN, TOK_CARET_ASSIGN,
        TOK_LSHIFT_ASSIGN, TOK_RSHIFT_ASSIGN,
    };
    for (int i = 0; i < (int)AXIS_ARRAY_LEN(compound_ops); i++) {
        if (match(p, compound_ops[i])) {
            TokenType op = cur(p)->type;
            advance(p);
            ASTExpr *val = parse_expression(p);
            skip_newlines(p);
            ASTStmt *s = NEW_STMT(p);
            s->kind = STMT_COMPOUND_ASSIGN;
            s->loc  = expr->loc;
            s->compound_assign.target = expr;
            s->compound_assign.op     = op;
            s->compound_assign.value  = val;
            return s;
        }
    }

    /* Plain assignment */
    if (match(p, TOK_ASSIGN)) {
        advance(p);
        ASTExpr *val = parse_expression(p);
        skip_newlines(p);

        /* Determine target kind */
        if (expr->kind == EXPR_INDEX) {
            ASTStmt *s = NEW_STMT(p);
            s->kind = STMT_INDEX_ASSIGN;
            s->loc  = expr->loc;
            s->index_assign.array = expr->index.array;
            s->index_assign.index = expr->index.index;
            s->index_assign.value = val;
            return s;
        }
        if (expr->kind == EXPR_FIELD_ACCESS) {
            ASTStmt *s = NEW_STMT(p);
            s->kind = STMT_FIELD_ASSIGN;
            s->loc  = expr->loc;
            s->field_assign.object = expr->field_access.object;
            s->field_assign.member = expr->field_access.member;
            s->field_assign.value  = val;
            return s;
        }
        if (expr->kind == EXPR_IDENT) {
            ASTStmt *s = NEW_STMT(p);
            s->kind = STMT_ASSIGN;
            s->loc  = expr->loc;
            s->assign.name  = expr->ident.name;
            s->assign.value = val;
            return s;
        }
        parse_error(p, "invalid assignment target");
    }

    skip_newlines(p);
    ASTStmt *s = NEW_STMT(p);
    s->kind = STMT_EXPR;
    s->loc  = expr->loc;
    s->expr_stmt.expr = expr;
    return s;
}

static ASTStmt *parse_statement(Parser *p)
{
    /* Variable declaration: ident : type [= expr] */
    if (match(p, TOK_IDENT)) {
        Token *nxt = peek(p, 1);
        if (nxt && nxt->type == TOK_COLON) {
            Token *nxt2 = peek(p, 2);
            if (!nxt2 || (nxt2->type != TOK_FIELD && nxt2->type != TOK_ENUM))
                return parse_var_decl(p);
        }
    }

    /* return / give */
    if (match2(p, TOK_GIVE, TOK_RETURN))
        return parse_return(p);

    /* when (if) */
    if (match(p, TOK_WHEN))
        return parse_if(p);

    /* while */
    if (match(p, TOK_WHILE))
        return parse_while(p);

    /* repeat / loop */
    if (match(p, TOK_REPEAT))
        return parse_repeat(p);

    /* match */
    if (match(p, TOK_MATCH))
        return parse_match(p);

    /* for */
    if (match(p, TOK_FOR))
        return parse_for(p);

    /* break / stop */
    if (match(p, TOK_BREAK)) {
        SrcLoc l = loc(p);
        advance(p);
        skip_newlines(p);
        ASTStmt *s = NEW_STMT(p);
        s->kind = STMT_BREAK;
        s->loc  = l;
        return s;
    }

    /* continue / skip */
    if (match(p, TOK_CONTINUE)) {
        SrcLoc l = loc(p);
        advance(p);
        skip_newlines(p);
        ASTStmt *s = NEW_STMT(p);
        s->kind = STMT_CONTINUE;
        s->loc  = l;
        return s;
    }

    /* write / writeln */
    if (match2(p, TOK_WRITE, TOK_WRITELN))
        return parse_write(p);

    /* syscall(args...) */
    if (match(p, TOK_SYSCALL)) {
        SrcLoc l = loc(p);
        advance(p);
        expect(p, TOK_LPAREN);
        PtrVec args;
        ptrvec_init(&args);
        if (!match(p, TOK_RPAREN)) {
            ptrvec_push(&args, parse_expression(p), p->arena);
            while (match(p, TOK_COMMA)) {
                advance(p);
                ptrvec_push(&args, parse_expression(p), p->arena);
            }
        }
        expect(p, TOK_RPAREN);
        skip_newlines(p);
        ASTStmt *s = NEW_STMT(p);
        s->kind = STMT_SYSCALL;
        s->loc  = l;
        s->syscall.args      = (ASTExpr **)args.items;
        s->syscall.arg_count = args.count;
        return s;
    }

    /* Expression-based (assignment, compound assign, bare expr) */
    return parse_expr_statement(p);
}

/* ═════════════════════════════════════════════════════════════
 * Definition parsing
 * ═════════════════════════════════════════════════════════════ */

static ASTParam parse_single_param(Parser *p)
{
    ASTParam param;
    memset(&param, 0, sizeof(param));
    param.loc = loc(p);
    param.is_update = false;

    if (match(p, TOK_UPDATE)) {
        param.is_update = true;
        advance(p);
    } else if (match(p, TOK_COPY)) {
        param.is_copy = true;
        advance(p);
    }

    param.name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_COLON);
    param.type_node = parse_type_node(p);
    return param;
}

static void parse_params(Parser *p, ASTParam **out_params, int *out_count)
{
    PtrVec params;
    ptrvec_init(&params);

    if (match(p, TOK_RPAREN)) {
        *out_params = NULL;
        *out_count  = 0;
        return;
    }
    ASTParam *first = ARENA_NEW(p->arena, ASTParam);
    *first = parse_single_param(p);
    ptrvec_push(&params, first, p->arena);

    while (match(p, TOK_COMMA)) {
        advance(p);
        ASTParam *next = ARENA_NEW(p->arena, ASTParam);
        *next = parse_single_param(p);
        ptrvec_push(&params, next, p->arena);
    }

    /* Copy into contiguous arena array */
    *out_count  = params.count;
    *out_params = (ASTParam *)arena_alloc(p->arena, (size_t)params.count * sizeof(ASTParam));
    for (int i = 0; i < params.count; i++)
        (*out_params)[i] = *(ASTParam *)params.items[i];
}

static ASTFunction parse_function(Parser *p)
{
    ASTFunction fn;
    memset(&fn, 0, sizeof(fn));
    fn.loc = loc(p);

    expect(p, TOK_FUNC);
    fn.name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_LPAREN);
    parse_params(p, &fn.params, &fn.param_count);
    expect(p, TOK_RPAREN);

    /* Return type: -> type   or just type before colon */
    fn.return_type = NULL;
    if (match(p, TOK_ARROW)) {
        advance(p);
        fn.return_type = parse_type_node(p);
    } else if (!match(p, TOK_COLON)) {
        fn.return_type = parse_type_node(p);
    }

    expect(p, TOK_COLON);
    skip_newlines(p);
    parse_block(p, &fn.body, &fn.body_count);
    return fn;
}

/* ---- Field definitions ---- */

static ASTFieldMember parse_field_member(Parser *p);  /* forward */

static void parse_field_members(Parser *p, ASTFieldMember **out, int *out_count)
{
    expect(p, TOK_INDENT);
    PtrVec members;
    ptrvec_init(&members);
    while (!match(p, TOK_DEDENT)) {
        if (at_end(p))
            parse_error(p, "unexpected EOF in field definition");
        if (match(p, TOK_NEWLINE)) { advance(p); continue; }
        ASTFieldMember *m = ARENA_NEW(p->arena, ASTFieldMember);
        *m = parse_field_member(p);
        ptrvec_push(&members, m, p->arena);
    }
    expect(p, TOK_DEDENT);

    *out_count = members.count;
    *out = (ASTFieldMember *)arena_alloc(p->arena, (size_t)members.count * sizeof(ASTFieldMember));
    for (int i = 0; i < members.count; i++)
        (*out)[i] = *(ASTFieldMember *)members.items[i];
}

static ASTFieldMember parse_field_member(Parser *p)
{
    ASTFieldMember m;
    memset(&m, 0, sizeof(m));
    m.loc  = loc(p);
    m.name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_COLON);

    /* Inline nested field: name: field: */
    if (match(p, TOK_FIELD)) {
        advance(p);
        expect(p, TOK_COLON);
        skip_newlines(p);
        ASTTypeNode *t = ARENA_NEW(p->arena, ASTTypeNode);
        t->kind = TYPE_NODE_SIMPLE;
        t->simple.name = "field";
        m.type_node = t;
        parse_field_members(p, &m.inline_members, &m.inline_count);
        return m;
    }

    /* Array type in field: (type; size) */
    if (match(p, TOK_LPAREN)) {
        /* Check for array-of-inline-fields: (field; N): [...] */
        Token *nxt = peek(p, 1);
        if (nxt && nxt->type == TOK_FIELD) {
            advance(p); /* ( */
            advance(p); /* field */
            expect(p, TOK_SEMICOLON);
            int size = (int)expect(p, TOK_INT_LIT)->int_val;
            expect(p, TOK_RPAREN);
            expect(p, TOK_COLON);

            /* Parse inline [...] block */
            expect(p, TOK_LBRACKET);
            skip_newlines(p);
            PtrVec inline_m;
            ptrvec_init(&inline_m);
            while (!match(p, TOK_RBRACKET)) {
                if (at_end(p))
                    parse_error(p, "unexpected EOF in inline field array");
                if (match3(p, TOK_NEWLINE, TOK_INDENT, TOK_DEDENT)) { advance(p); continue; }
                ASTFieldMember *im = ARENA_NEW(p->arena, ASTFieldMember);
                *im = parse_field_member(p);
                ptrvec_push(&inline_m, im, p->arena);
            }
            expect(p, TOK_RBRACKET);
            skip_newlines(p);

            ASTTypeNode *elem_t = ARENA_NEW(p->arena, ASTTypeNode);
            elem_t->kind = TYPE_NODE_SIMPLE;
            elem_t->simple.name = "field";
            ASTTypeNode *arr_t = ARENA_NEW(p->arena, ASTTypeNode);
            arr_t->kind = TYPE_NODE_ARRAY;
            arr_t->array.elem = elem_t;
            arr_t->array.size = size;
            m.type_node = arr_t;
            m.inline_count = inline_m.count;
            m.inline_members = (ASTFieldMember *)arena_alloc(p->arena, (size_t)inline_m.count * sizeof(ASTFieldMember));
            for (int i = 0; i < inline_m.count; i++)
                m.inline_members[i] = *(ASTFieldMember *)inline_m.items[i];
            return m;
        }

        /* Regular array member */
        m.type_node = parse_array_type(p);
        if (match(p, TOK_ASSIGN)) {
            advance(p);
            m.default_value = parse_expression(p);
        }
        skip_newlines(p);
        return m;
    }

    /* Scalar member */
    m.type_node = parse_type_node(p);
    if (match(p, TOK_ASSIGN)) {
        advance(p);
        m.default_value = parse_expression(p);
    }
    skip_newlines(p);
    return m;
}

static ASTFieldDef parse_field_def(Parser *p)
{
    ASTFieldDef def;
    memset(&def, 0, sizeof(def));
    def.loc = loc(p);
    def.name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_COLON);
    expect(p, TOK_FIELD);
    expect(p, TOK_COLON);
    skip_newlines(p);
    parse_field_members(p, &def.members, &def.member_count);
    return def;
}

/* ---- Enum definitions ---- */

static ASTEnumDef parse_enum_def(Parser *p)
{
    ASTEnumDef def;
    memset(&def, 0, sizeof(def));
    def.loc = loc(p);
    def.name = expect(p, TOK_IDENT)->str_val;
    expect(p, TOK_COLON);
    expect(p, TOK_ENUM);

    /* Optional underlying type */
    def.underlying_type = "i32";
    if (p->cur && is_type_token(p->cur->type)) {
        def.underlying_type = type_token_str(p->cur->type);
        advance(p);
    }

    expect(p, TOK_COLON);
    skip_newlines(p);

    /* Parse variants */
    expect(p, TOK_INDENT);
    PtrVec variants;
    ptrvec_init(&variants);
    int next_val = 0;
    while (!match(p, TOK_DEDENT)) {
        if (at_end(p))
            parse_error(p, "unexpected EOF in enum definition");
        if (match(p, TOK_NEWLINE)) { advance(p); continue; }

        ASTEnumVariant *v = ARENA_NEW(p->arena, ASTEnumVariant);
        v->loc  = loc(p);
        v->name = expect(p, TOK_IDENT)->str_val;
        v->has_value = false;

        if (match(p, TOK_ASSIGN)) {
            advance(p);
            if (!match(p, TOK_INT_LIT))
                parse_error(p, "expected integer literal for enum variant value");
            v->value     = (int)cur(p)->int_val;
            v->has_value = true;
            next_val = v->value + 1;
            advance(p);
        } else {
            v->value = next_val++;
            v->has_value = false;
        }

        /* Check for duplicate variant names and values */
        for (int j = 0; j < variants.count; j++) {
            ASTEnumVariant *prev = (ASTEnumVariant *)variants.items[j];
            if (strcmp(prev->name, v->name) == 0)
                parse_error(p, "duplicate enum variant name '%s'", v->name);
            if (prev->value == v->value)
                parse_error(p, "duplicate enum variant value %d (in '%s' and '%s')",
                            v->value, prev->name, v->name);
        }

        skip_newlines(p);
        ptrvec_push(&variants, v, p->arena);
    }
    expect(p, TOK_DEDENT);

    def.variant_count = variants.count;
    def.variants = (ASTEnumVariant *)arena_alloc(p->arena, (size_t)variants.count * sizeof(ASTEnumVariant));
    for (int i = 0; i < variants.count; i++)
        def.variants[i] = *(ASTEnumVariant *)variants.items[i];
    return def;
}

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

void parser_init(Parser *p, Token *tokens, int token_count,
                 Arena *arena, const char *filename, const char *source)
{
    memset(p, 0, sizeof(*p));
    p->tokens      = tokens;
    p->token_count = token_count;
    p->pos         = 0;
    p->cur         = (token_count > 0) ? &tokens[0] : NULL;
    p->arena       = arena;
    p->filename    = filename;
    p->source      = source;
}

ASTProgram *parser_parse(Parser *p)
{
    skip_newlines(p);

    ASTProgram *prog = ARENA_NEW(p->arena, ASTProgram);
    prog->mode = MODE_COMPILE;
    prog->loc  = loc(p);

    /* Optional mode declaration */
    if (match(p, TOK_MODE)) {
        advance(p);
        if (match(p, TOK_SCRIPT)) {
            prog->mode = MODE_SCRIPT;
            advance(p);
        } else if (match(p, TOK_COMPILE)) {
            prog->mode = MODE_COMPILE;
            advance(p);
        } else {
            parse_error(p, "expected 'script' or 'compile' after 'mode'");
        }
        skip_newlines(p);
    }

    /* Collect top-level items */
    PtrVec funcs, stmts;
    ptrvec_init(&funcs);
    ptrvec_init(&stmts);

    /* Field and enum defs stored separately */
    PtrVec fields, enums;
    ptrvec_init(&fields);
    ptrvec_init(&enums);

    while (!at_end(p)) {
        if (match(p, TOK_NEWLINE)) { advance(p); continue; }

        /* In check mode, wrap each top-level item in setjmp so we can
           recover from parse errors and keep going.                   */
        if (p->check_mode && setjmp(p->err_jmp)) {
            /* Panic-mode recovery: skip tokens until top-level newline */
            while (!at_end(p) && !match(p, TOK_NEWLINE))
                advance(p);
            skip_newlines(p);
            continue;
        }

        /* Function definition */
        if (match(p, TOK_FUNC)) {
            ASTFunction *fn = ARENA_NEW(p->arena, ASTFunction);
            *fn = parse_function(p);
            ptrvec_push(&funcs, fn, p->arena);
            continue;
        }

        /* Field or enum definition: Ident : field/enum : ... */
        if (match(p, TOK_IDENT)) {
            Token *nxt  = peek(p, 1);
            Token *nxt2 = peek(p, 2);
            if (nxt && nxt->type == TOK_COLON && nxt2) {
                if (nxt2->type == TOK_FIELD) {
                    ASTFieldDef *fd = ARENA_NEW(p->arena, ASTFieldDef);
                    *fd = parse_field_def(p);
                    ptrvec_push(&fields, fd, p->arena);
                    skip_newlines(p);
                    continue;
                }
                if (nxt2->type == TOK_ENUM) {
                    ASTEnumDef *ed = ARENA_NEW(p->arena, ASTEnumDef);
                    *ed = parse_enum_def(p);
                    ptrvec_push(&enums, ed, p->arena);
                    skip_newlines(p);
                    continue;
                }
            }
        }

        /* Script mode: top-level statements */
        if (prog->mode == MODE_SCRIPT) {
            ptrvec_push(&stmts, parse_statement(p), p->arena);
            skip_newlines(p);
            continue;
        }

        parse_error(p, "unexpected token in compile mode (only func, field, enum allowed)");
    }

    /* Compile mode requires main() — skip in check mode if we had errors,
       since functions may have been lost during error recovery.           */
    if (prog->mode == MODE_COMPILE && !(p->check_mode && p->error_count > 0)) {
        bool has_main = false;
        for (int i = 0; i < funcs.count; i++) {
            ASTFunction *fn = (ASTFunction *)funcs.items[i];
            if (strcmp(fn->name, "main") == 0) { has_main = true; break; }
        }
        if (!has_main)
            parse_error(p, "compile mode requires a 'func main()' definition");
    }

    /* Flatten into program */
    prog->func_count = funcs.count;
    prog->functions = (ASTFunction *)arena_alloc(p->arena, (size_t)funcs.count * sizeof(ASTFunction));
    for (int i = 0; i < funcs.count; i++)
        prog->functions[i] = *(ASTFunction *)funcs.items[i];

    prog->stmt_count  = stmts.count;
    prog->statements  = (ASTStmt **)stmts.items;

    prog->field_count = fields.count;
    prog->field_defs  = (ASTFieldDef *)arena_alloc(p->arena, (size_t)fields.count * sizeof(ASTFieldDef));
    for (int i = 0; i < fields.count; i++)
        prog->field_defs[i] = *(ASTFieldDef *)fields.items[i];

    prog->enum_count = enums.count;
    prog->enum_defs  = (ASTEnumDef *)arena_alloc(p->arena, (size_t)enums.count * sizeof(ASTEnumDef));
    for (int i = 0; i < enums.count; i++)
        prog->enum_defs[i] = *(ASTEnumDef *)enums.items[i];

    return prog;
}
