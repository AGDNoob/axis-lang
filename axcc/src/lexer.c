/*
 * lexer.c – AXIS lexer implementation.
 *
 * Deterministic scanner: no backtracking.
 * Handles INDENT / DEDENT, hex/binary/decimal literals,
 * string escapes, all operators, and keyword recognition.
 */
#include "axis_lexer.h"
#include <ctype.h>

/* ── Keyword table ─────────────────────────────────────────── */
typedef struct { const char *kw; TokenType tt; } KWEntry;

static const KWEntry kw_table[] = {
    /* control flow */
    {"when",     TOK_WHEN},     {"else",      TOK_ELSE},
    {"while",    TOK_WHILE},    {"repeat",    TOK_REPEAT},
    {"loop",     TOK_REPEAT},   /* alias */
    {"for",      TOK_FOR},      {"in",        TOK_IN},
    {"break",    TOK_BREAK},    {"stop",      TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"skip",      TOK_CONTINUE},
    {"match",    TOK_MATCH},
    /* functions */
    {"func",     TOK_FUNC},     {"give",      TOK_GIVE},
    {"return",   TOK_RETURN},
    /* declarations */
    {"mode",     TOK_MODE},     {"script",    TOK_SCRIPT},
    {"compile",  TOK_COMPILE},  {"field",     TOK_FIELD},
    {"enum",     TOK_ENUM},     {"update",    TOK_UPDATE},
    {"copy",     TOK_COPY},
    /* types */
    {"i8",   TOK_I8},   {"i16",  TOK_I16},  {"i32",  TOK_I32},  {"i64",  TOK_I64},
    {"u8",   TOK_U8},   {"u16",  TOK_U16},  {"u32",  TOK_U32},  {"u64",  TOK_U64},
    {"bool", TOK_BOOL},  {"str",  TOK_STR},
    /* booleans */
    {"True",  TOK_TRUE},  {"False", TOK_FALSE},
    /* I/O */
    {"write",    TOK_WRITE},    {"writeln",    TOK_WRITELN},
    {"read",     TOK_READ},     {"readln",     TOK_READLN},
    {"readchar", TOK_READCHAR}, {"read_failed",TOK_READ_FAILED},
    /* logical */
    {"and", TOK_AND}, {"or", TOK_OR}, {"not", TOK_NOT},
    /* syscall */
    {"syscall", TOK_SYSCALL},
};

static TokenType lookup_keyword(const char *text, int len)
{
    for (size_t i = 0; i < AXIS_ARRAY_LEN(kw_table); i++) {
        const char *k = kw_table[i].kw;
        if ((int)strlen(k) == len && memcmp(k, text, (size_t)len) == 0)
            return kw_table[i].tt;
    }
    return TOK_IDENT;
}

/* ── token_type_name ──────────────────────────────────────── */
static const char *_tok_names[] = {
    [TOK_INT_LIT]    = "INT_LIT",    [TOK_STRING_LIT] = "STRING_LIT",
    [TOK_IDENT]      = "IDENT",
    [TOK_TRUE]       = "TRUE",       [TOK_FALSE]      = "FALSE",
    [TOK_I8]  = "I8",  [TOK_I16] = "I16", [TOK_I32] = "I32", [TOK_I64] = "I64",
    [TOK_U8]  = "U8",  [TOK_U16] = "U16", [TOK_U32] = "U32", [TOK_U64] = "U64",
    [TOK_BOOL] = "BOOL", [TOK_STR] = "STR",
    [TOK_WHEN]  = "WHEN",  [TOK_ELSE]   = "ELSE",
    [TOK_WHILE] = "WHILE", [TOK_REPEAT] = "REPEAT",
    [TOK_FOR]   = "FOR",   [TOK_IN]     = "IN",
    [TOK_BREAK] = "BREAK", [TOK_CONTINUE] = "CONTINUE",
    [TOK_MATCH] = "MATCH",
    [TOK_FUNC]   = "FUNC",   [TOK_GIVE]   = "GIVE",  [TOK_RETURN] = "RETURN",
    [TOK_MODE]   = "MODE",   [TOK_SCRIPT] = "SCRIPT", [TOK_COMPILE] = "COMPILE",
    [TOK_FIELD]  = "FIELD",  [TOK_ENUM]   = "ENUM",
    [TOK_UPDATE] = "UPDATE", [TOK_COPY]   = "COPY",
    [TOK_WRITE]  = "WRITE",  [TOK_WRITELN] = "WRITELN",
    [TOK_READ]   = "READ",   [TOK_READLN]  = "READLN",
    [TOK_READCHAR] = "READCHAR", [TOK_READ_FAILED] = "READ_FAILED",
    [TOK_SYSCALL]  = "SYSCALL",
    [TOK_PLUS]    = "+",  [TOK_MINUS]   = "-",  [TOK_STAR]    = "*",
    [TOK_SLASH]   = "/",  [TOK_PERCENT] = "%",
    [TOK_AMP]     = "&",  [TOK_PIPE]    = "|",  [TOK_CARET]  = "^",
    [TOK_LSHIFT]  = "<<", [TOK_RSHIFT]  = ">>",
    [TOK_AND] = "AND", [TOK_OR] = "OR", [TOK_NOT] = "NOT", [TOK_BANG] = "!",
    [TOK_EQ] = "==", [TOK_NE] = "!=",
    [TOK_LT] = "<",  [TOK_LE] = "<=", [TOK_GT] = ">",  [TOK_GE] = ">=",
    [TOK_ASSIGN]         = "=",
    [TOK_PLUS_ASSIGN]    = "+=",  [TOK_MINUS_ASSIGN]   = "-=",
    [TOK_STAR_ASSIGN]    = "*=",  [TOK_SLASH_ASSIGN]   = "/=",
    [TOK_PERCENT_ASSIGN] = "%=",  [TOK_AMP_ASSIGN]     = "&=",
    [TOK_PIPE_ASSIGN]    = "|=",  [TOK_CARET_ASSIGN]   = "^=",
    [TOK_LSHIFT_ASSIGN]  = "<<=", [TOK_RSHIFT_ASSIGN]  = ">>=",
    [TOK_DOTDOT]    = "..",
    [TOK_LPAREN]    = "(",  [TOK_RPAREN]   = ")",
    [TOK_LBRACKET]  = "[",  [TOK_RBRACKET] = "]",
    [TOK_LBRACE]    = "{",  [TOK_RBRACE]   = "}",
    [TOK_COLON]     = ":",  [TOK_SEMICOLON] = ";",
    [TOK_COMMA]     = ",",  [TOK_DOT]       = ".",
    [TOK_ARROW]     = "->", [TOK_UNDERSCORE] = "_",
    [TOK_COLONCOLON] = "::",
    [TOK_INDENT]  = "INDENT",  [TOK_DEDENT]  = "DEDENT",
    [TOK_NEWLINE] = "NEWLINE", [TOK_EOF]     = "EOF",
};

const char *token_type_name(TokenType t)
{
    if (t >= 0 && t < TOK_COUNT_)
        return _tok_names[t];
    return "UNKNOWN";
}

/* ── Helpers ──────────────────────────────────────────────── */

static inline char cur(const Lexer *lex)
{
    return (lex->pos < lex->src_len) ? lex->src[lex->pos] : '\0';
}

static inline char peek(const Lexer *lex, int offset)
{
    size_t p = lex->pos + (size_t)offset;
    return (p < lex->src_len) ? lex->src[p] : '\0';
}

static inline void adv(Lexer *lex)
{
    if (lex->pos < lex->src_len) {
        if (lex->src[lex->pos] == '\n') {
            lex->line++;
            lex->col = 1;
        } else if (lex->src[lex->pos] == '\t') {
            lex->col = ((lex->col - 1) / 4 + 1) * 4 + 1;
        } else {
            lex->col++;
        }
        lex->pos++;
    }
}

static inline Token mktok(TokenType t, int line, int col, const char *start, int len)
{
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type    = t;
    tok.loc.line = line;
    tok.loc.col  = col;
    tok.start   = start;
    tok.length  = len;
    return tok;
}

static void push_pending(Lexer *lex, Token t)
{
    assert(lex->pending_count < LEXER_MAX_INDENT_DEPTH);
    lex->pending[lex->pending_count++] = t;
}

static bool has_pending(const Lexer *lex)
{
    return lex->pending_read < lex->pending_count;
}

static Token pop_pending(Lexer *lex)
{
    assert(has_pending(lex));
    return lex->pending[lex->pending_read++];
}

/* ── Escape map ───────────────────────────────────────────── */

static char escape_char(char c)
{
    switch (c) {
    case 'n':  return '\n';
    case 't':  return '\t';
    case 'r':  return '\r';
    case '\\': return '\\';
    case '"':  return '"';
    case '0':  return '\0';
    default:   return 0;   /* invalid */
    }
}

/* ── Init ─────────────────────────────────────────────────── */

void lexer_init(Lexer *lex, const char *src, size_t len,
                const char *filename, Arena *arena)
{
    memset(lex, 0, sizeof(*lex));
    lex->src      = src;
    lex->src_len  = len;
    lex->filename = filename;
    lex->pos      = 0;
    lex->line     = 1;
    lex->col      = 1;
    lex->indent_stack[0] = 0;
    lex->indent_top      = 0;
    lex->at_line_start   = true;
    lex->pending_count   = 0;
    lex->pending_read    = 0;
    lex->arena           = arena;
}

/* ── Skip helpers ─────────────────────────────────────────── */

static void skip_inline_ws(Lexer *lex)
{
    while (lex->pos < lex->src_len) {
        char c = cur(lex);
        if (c == ' ' || c == '\t' || c == '\r')
            adv(lex);
        else
            break;
    }
}

static void skip_comment(Lexer *lex)
{
    while (lex->pos < lex->src_len && cur(lex) != '\n')
        adv(lex);
}

/* ── Indentation handler ──────────────────────────────────── */

static bool handle_indent(Lexer *lex, Token *out)
{
    if (!lex->at_line_start) return false;
    lex->at_line_start = false;

    int indent = 0;
    bool has_tab = false, has_space = false;
    while (lex->pos < lex->src_len) {
        char c = cur(lex);
        if (c == ' ')       { indent += 1; has_space = true; adv(lex); }
        else if (c == '\t') { indent += 4; has_tab = true; adv(lex); }
        else break;
    }

    /* blank / comment-only line → ignore indentation */
    char c = cur(lex);
    if (c == '\n' || c == '\r' || c == '\0')     return false;
    if (c == '/' && peek(lex,1) == '/') { skip_comment(lex); return false; }
    if (c == '#')                       { skip_comment(lex); return false; }

    if (has_tab && has_space) {
        fprintf(stderr, "%s:%d: error: mixed tabs and spaces in indentation\n",
                lex->filename, lex->line);
        lex->error_count++;
        if (!lex->check_mode) exit(1);
    }

    int current = lex->indent_stack[lex->indent_top];

    if (indent > current) {
        lex->indent_top++;
        if (lex->indent_top >= LEXER_MAX_INDENT_DEPTH) {
            fprintf(stderr, "%s:%d: error: maximum indentation depth (%d) exceeded\n",
                    lex->filename, lex->line, LEXER_MAX_INDENT_DEPTH);
            exit(1);
        }
        lex->indent_stack[lex->indent_top] = indent;
        *out = mktok(TOK_INDENT, lex->line, 1, lex->src + lex->pos, 0);
        return true;
    }
    if (indent < current) {
        /* reset pending queue */
        lex->pending_count = 0;
        lex->pending_read  = 0;

        while (lex->indent_top > 0 && lex->indent_stack[lex->indent_top] > indent) {
            lex->indent_top--;
            push_pending(lex, mktok(TOK_DEDENT, lex->line, 1, lex->src + lex->pos, 0));
        }
        if (lex->indent_stack[lex->indent_top] != indent) {
            fprintf(stderr, "%s:%d: indentation error: level %d doesn't match any outer level\n",
                    lex->filename, lex->line, indent);
            lex->error_count++;
            if (!lex->check_mode) exit(1);
            /* recover: snap to nearest known level */
        }
        /* Return first DEDENT, rest go to pending */
        *out = pop_pending(lex);
        return true;
    }
    return false;
}

/* ── Number literal ───────────────────────────────────────── */

static Token read_number(Lexer *lex)
{
    int sl = lex->line, sc = lex->col;
    const char *start = lex->src + lex->pos;

    /* hex 0x */
    if (cur(lex) == '0' && (peek(lex,1) == 'x' || peek(lex,1) == 'X')) {
        adv(lex); adv(lex);
        while (lex->pos < lex->src_len && (isxdigit((unsigned char)cur(lex)) || cur(lex) == '_'))
            adv(lex);
        int len = (int)(lex->src + lex->pos - start);
        Token tok = mktok(TOK_INT_LIT, sl, sc, start, len);
        /* Parse value (skip underscores) */
        char buf[64]; int bi = 0;
        for (const char *p = start; p < start + len && bi < 62; p++)
            if (*p != '_') buf[bi++] = *p;
        buf[bi] = '\0';
        tok.int_val = (int64_t)strtoull(buf, NULL, 0);
        return tok;
    }

    /* binary 0b */
    if (cur(lex) == '0' && (peek(lex,1) == 'b' || peek(lex,1) == 'B')) {
        adv(lex); adv(lex);
        while (lex->pos < lex->src_len && (cur(lex) == '0' || cur(lex) == '1' || cur(lex) == '_'))
            adv(lex);
        int len = (int)(lex->src + lex->pos - start);
        Token tok = mktok(TOK_INT_LIT, sl, sc, start, len);
        char buf[128]; int bi = 0;
        for (const char *p = start; p < start + len && bi < 126; p++)
            if (*p != '_') buf[bi++] = *p;
        buf[bi] = '\0';
        tok.int_val = (int64_t)strtoull(buf + 2, NULL, 2);
        return tok;
    }

    /* decimal */
    while (lex->pos < lex->src_len && (isdigit((unsigned char)cur(lex)) || cur(lex) == '_'))
        adv(lex);
    int len = (int)(lex->src + lex->pos - start);
    Token tok = mktok(TOK_INT_LIT, sl, sc, start, len);
    char buf[64]; int bi = 0;
    for (const char *p = start; p < start + len && bi < 62; p++)
        if (*p != '_') buf[bi++] = *p;
    buf[bi] = '\0';
    tok.int_val = (int64_t)strtoull(buf, NULL, 10);
    return tok;
}

/* ── String literal ───────────────────────────────────────── */

static Token read_string(Lexer *lex)
{
    int sl = lex->line, sc = lex->col;
    adv(lex); /* skip opening " */

    /* First pass: compute length */
    size_t save_pos = lex->pos;
    int save_line = lex->line, save_col = lex->col;
    size_t slen = 0;
    while (lex->pos < lex->src_len && cur(lex) != '"') {
        if (cur(lex) == '\\') { adv(lex); slen++; adv(lex); }
        else if (cur(lex) == '\n') {
            fprintf(stderr, "%s:%d:%d: unterminated string literal\n",
                    lex->filename, lex->line, lex->col);
            lex->error_count++;
            if (!lex->check_mode) exit(1);
            break;
        }
        else { slen++; adv(lex); }
    }
    if (lex->pos >= lex->src_len) {
        fprintf(stderr, "%s:%d:%d: unterminated string literal\n",
                lex->filename, sl, sc);
        lex->error_count++;
        if (!lex->check_mode) exit(1);
        return mktok(TOK_STRING_LIT, sl, sc, "", 0);
    }

    /* Second pass: copy with escapes */
    lex->pos  = save_pos;
    lex->line = save_line;
    lex->col  = save_col;

    char *buf = (char *)arena_alloc(lex->arena, slen + 1);
    size_t bi = 0;
    while (cur(lex) != '"') {
        if (cur(lex) == '\\') {
            adv(lex);
            char esc = escape_char(cur(lex));
            if (esc == 0 && cur(lex) != '0') {
                fprintf(stderr, "%s:%d:%d: unknown escape sequence: \\%c\n",
                        lex->filename, lex->line, lex->col, cur(lex));
                lex->error_count++;
                if (!lex->check_mode) exit(1);
                esc = cur(lex);  /* use literal char as fallback */
            }
            buf[bi++] = esc;
            adv(lex);
        } else {
            buf[bi++] = cur(lex);
            adv(lex);
        }
    }
    buf[bi] = '\0';
    adv(lex); /* skip closing " */

    Token tok = mktok(TOK_STRING_LIT, sl, sc, buf, (int)bi);
    tok.str_val = buf;
    return tok;
}

/* ── Identifier / keyword ─────────────────────────────────── */

static Token read_ident(Lexer *lex)
{
    int sl = lex->line, sc = lex->col;
    const char *start = lex->src + lex->pos;
    while (lex->pos < lex->src_len && (isalnum((unsigned char)cur(lex)) || cur(lex) == '_'))
        adv(lex);
    int len = (int)(lex->src + lex->pos - start);
    TokenType tt = lookup_keyword(start, len);
    Token tok = mktok(tt, sl, sc, start, len);
    if (tt == TOK_IDENT)
        tok.str_val = arena_strndup(lex->arena, start, (size_t)len);
    else
        tok.str_val = arena_strndup(lex->arena, start, (size_t)len);
    return tok;
}

/* ── Main scan function ───────────────────────────────────── */

Token lexer_next(Lexer *lex)
{
    /* Drain pending (multi-DEDENT) */
    if (has_pending(lex))
        return pop_pending(lex);

    while (lex->pos < lex->src_len) {
        /* Indentation at line start */
        if (lex->at_line_start) {
            Token t;
            if (handle_indent(lex, &t))
                return t;
        }

        char c = cur(lex);

        /* Newline */
        if (c == '\n') {
            int sl = lex->line, sc = lex->col;
            adv(lex);
            lex->at_line_start = true;
            return mktok(TOK_NEWLINE, sl, sc, lex->src + lex->pos - 1, 1);
        }

        /* Inline whitespace */
        if (c == ' ' || c == '\t' || c == '\r') {
            skip_inline_ws(lex);
            continue;
        }

        /* Comments */
        if (c == '/' && peek(lex, 1) == '/') { skip_comment(lex); continue; }
        if (c == '#')                        { skip_comment(lex); continue; }

        int sl = lex->line, sc = lex->col;
        const char *sp = lex->src + lex->pos;

        /* String literal */
        if (c == '"') return read_string(lex);

        /* Number literal */
        if (isdigit((unsigned char)c))
            return read_number(lex);

        /* Negative number (only if next char is digit and previous context suggests expression start) */
        /* NOT handled here – unary minus is handled by the parser */

        /* Identifier / keyword */
        if (isalpha((unsigned char)c) || c == '_')
            return read_ident(lex);

        /* ── Multi-char operators ─────────────────────── */

        /* == */
        if (c == '=' && peek(lex,1) == '=') { adv(lex); adv(lex); return mktok(TOK_EQ, sl, sc, sp, 2); }
        /* != */
        if (c == '!' && peek(lex,1) == '=') { adv(lex); adv(lex); return mktok(TOK_NE, sl, sc, sp, 2); }
        /* ! */
        if (c == '!') { adv(lex); return mktok(TOK_BANG, sl, sc, sp, 1); }

        /* <<, <<=, <=, < */
        if (c == '<') {
            if (peek(lex,1) == '<') {
                adv(lex); adv(lex);
                if (cur(lex) == '=') { adv(lex); return mktok(TOK_LSHIFT_ASSIGN, sl, sc, sp, 3); }
                return mktok(TOK_LSHIFT, sl, sc, sp, 2);
            }
            if (peek(lex,1) == '=') { adv(lex); adv(lex); return mktok(TOK_LE, sl, sc, sp, 2); }
            adv(lex); return mktok(TOK_LT, sl, sc, sp, 1);
        }

        /* >>, >>=, >=, > */
        if (c == '>') {
            if (peek(lex,1) == '>') {
                adv(lex); adv(lex);
                if (cur(lex) == '=') { adv(lex); return mktok(TOK_RSHIFT_ASSIGN, sl, sc, sp, 3); }
                return mktok(TOK_RSHIFT, sl, sc, sp, 2);
            }
            if (peek(lex,1) == '=') { adv(lex); adv(lex); return mktok(TOK_GE, sl, sc, sp, 2); }
            adv(lex); return mktok(TOK_GT, sl, sc, sp, 1);
        }

        /* -> (arrow) */
        if (c == '-' && peek(lex,1) == '>') { adv(lex); adv(lex); return mktok(TOK_ARROW, sl, sc, sp, 2); }

        /* .. (range) */
        if (c == '.' && peek(lex,1) == '.') { adv(lex); adv(lex); return mktok(TOK_DOTDOT, sl, sc, sp, 2); }

        /* :: (enum access) */
        if (c == ':' && peek(lex,1) == ':') { adv(lex); adv(lex); return mktok(TOK_COLONCOLON, sl, sc, sp, 2); }

        /* Compound assignments: +=, -=, *=, /=, %=, &=, |=, ^= */
        if (peek(lex,1) == '=') {
            TokenType ctt = TOK_EOF;
            int clen = 2;
            switch (c) {
            case '+': ctt = TOK_PLUS_ASSIGN;    break;
            case '-': ctt = TOK_MINUS_ASSIGN;   break;
            case '*': ctt = TOK_STAR_ASSIGN;    break;
            case '/': ctt = TOK_SLASH_ASSIGN;   break;
            case '%': ctt = TOK_PERCENT_ASSIGN; break;
            case '&': ctt = TOK_AMP_ASSIGN;     break;
            case '|': ctt = TOK_PIPE_ASSIGN;    break;
            case '^': ctt = TOK_CARET_ASSIGN;   break;
            default: break;
            }
            if (ctt != TOK_EOF) {
                adv(lex); adv(lex);
                return mktok(ctt, sl, sc, sp, clen);
            }
        }

        /* ── Single-char operators ────────────────────── */
        {
            TokenType stt = TOK_EOF;
            switch (c) {
            case '+': stt = TOK_PLUS;      break;
            case '-': stt = TOK_MINUS;     break;
            case '*': stt = TOK_STAR;      break;
            case '/': stt = TOK_SLASH;     break;
            case '%': stt = TOK_PERCENT;   break;
            case '&': stt = TOK_AMP;       break;
            case '|': stt = TOK_PIPE;      break;
            case '^': stt = TOK_CARET;     break;
            case '=': stt = TOK_ASSIGN;    break;
            case '(': stt = TOK_LPAREN;    break;
            case ')': stt = TOK_RPAREN;    break;
            case '[': stt = TOK_LBRACKET;  break;
            case ']': stt = TOK_RBRACKET;  break;
            case '{': stt = TOK_LBRACE;    break;
            case '}': stt = TOK_RBRACE;    break;
            case ':': stt = TOK_COLON;     break;
            case ';': stt = TOK_SEMICOLON; break;
            case ',': stt = TOK_COMMA;     break;
            case '.': stt = TOK_DOT;       break;
            default: break;
            }
            if (stt != TOK_EOF) {
                adv(lex);
                return mktok(stt, sl, sc, sp, 1);
            }
        }

        fprintf(stderr, "%s:%d:%d: unexpected character: '%c' (0x%02x)\n",
                lex->filename, lex->line, lex->col, c, (unsigned char)c);
        lex->error_count++;
        if (!lex->check_mode) exit(1);
        adv(lex);  /* skip bad character */
        continue;
    }

    /* EOF: emit remaining DEDENTs */
    lex->pending_count = 0;
    lex->pending_read  = 0;
    while (lex->indent_top > 0) {
        lex->indent_top--;
        push_pending(lex, mktok(TOK_DEDENT, lex->line, 1, lex->src + lex->pos, 0));
    }
    if (has_pending(lex))
        return pop_pending(lex);

    return mktok(TOK_EOF, lex->line, lex->col, lex->src + lex->pos, 0);
}

/* ── Convenience: tokenize everything ─────────────────────── */

Token *lexer_tokenize_all(Lexer *lex, int *count)
{
    int cap = 256;
    Token *tokens = (Token *)arena_alloc(lex->arena, (size_t)cap * sizeof(Token));
    int n = 0;

    for (;;) {
        if (n >= cap) {
            int newcap = cap * 2;
            Token *newbuf = (Token *)arena_alloc(lex->arena, (size_t)newcap * sizeof(Token));
            memcpy(newbuf, tokens, (size_t)n * sizeof(Token));
            tokens = newbuf;
            cap = newcap;
        }
        tokens[n] = lexer_next(lex);
        if (tokens[n].type == TOK_EOF) {
            n++;
            break;
        }
        n++;
    }
    *count = n;
    return tokens;
}
