/*
 * axis_token.h – Token types and Token struct for the AXIS language.
 */
#ifndef AXIS_TOKEN_H
#define AXIS_TOKEN_H

#include "axis_common.h"

typedef enum {
    /* ── Literals ─────────────────────────────────────── */
    TOK_INT_LIT,
    TOK_STRING_LIT,
    TOK_IDENT,

    /* ── Boolean literals ─────────────────────────────── */
    TOK_TRUE,
    TOK_FALSE,

    /* ── Type keywords ────────────────────────────────── */
    TOK_I8, TOK_I16, TOK_I32, TOK_I64,
    TOK_U8, TOK_U16, TOK_U32, TOK_U64,
    TOK_BOOL, TOK_STR,

    /* ── Control flow ─────────────────────────────────── */
    TOK_WHEN,           /* if */
    TOK_ELSE,
    TOK_WHILE,
    TOK_REPEAT,         /* infinite loop ("repeat" / "loop") */
    TOK_FOR,
    TOK_IN,
    TOK_BREAK,          /* "break" / "stop"     */
    TOK_CONTINUE,       /* "continue" / "skip"  */
    TOK_MATCH,

    /* ── Function keywords ────────────────────────────── */
    TOK_FUNC,
    TOK_GIVE,           /* return (primary)  */
    TOK_RETURN,         /* return (alias)    */

    /* ── Declaration / modifier ───────────────────────── */
    TOK_MODE,
    TOK_SCRIPT,
    TOK_COMPILE,
    TOK_FIELD,
    TOK_ENUM,
    TOK_UPDATE,
    TOK_COPY,

    /* ── Built-in I/O ─────────────────────────────────── */
    TOK_WRITE,
    TOK_WRITELN,
    TOK_READ,
    TOK_READLN,
    TOK_READCHAR,
    TOK_READ_FAILED,

    /* ── Syscall ──────────────────────────────────────── */
    TOK_SYSCALL,

    /* ── Arithmetic operators ─────────────────────────── */
    TOK_PLUS,           /* +  */
    TOK_MINUS,          /* -  */
    TOK_STAR,           /* *  */
    TOK_SLASH,          /* /  */
    TOK_PERCENT,        /* %  */

    /* ── Bitwise operators ────────────────────────────── */
    TOK_AMP,            /* &  */
    TOK_PIPE,           /* |  */
    TOK_CARET,          /* ^  */
    TOK_LSHIFT,         /* << */
    TOK_RSHIFT,         /* >> */

    /* ── Logical operators ────────────────────────────── */
    TOK_AND,            /* and */
    TOK_OR,             /* or  */
    TOK_NOT,            /* not */
    TOK_BANG,           /* !   */

    /* ── Comparison operators ─────────────────────────── */
    TOK_EQ,             /* == */
    TOK_NE,             /* != */
    TOK_LT,             /* <  */
    TOK_LE,             /* <= */
    TOK_GT,             /* >  */
    TOK_GE,             /* >= */

    /* ── Assignment operators ─────────────────────────── */
    TOK_ASSIGN,         /* =   */
    TOK_PLUS_ASSIGN,    /* +=  */
    TOK_MINUS_ASSIGN,   /* -=  */
    TOK_STAR_ASSIGN,    /* *=  */
    TOK_SLASH_ASSIGN,   /* /=  */
    TOK_PERCENT_ASSIGN, /* %=  */
    TOK_AMP_ASSIGN,     /* &=  */
    TOK_PIPE_ASSIGN,    /* |=  */
    TOK_CARET_ASSIGN,   /* ^=  */
    TOK_LSHIFT_ASSIGN,  /* <<= */
    TOK_RSHIFT_ASSIGN,  /* >>= */

    /* ── Range ────────────────────────────────────────── */
    TOK_DOTDOT,         /* .. */

    /* ── Punctuation ──────────────────────────────────── */
    TOK_LPAREN,         /* (  */
    TOK_RPAREN,         /* )  */
    TOK_LBRACKET,       /* [  */
    TOK_RBRACKET,       /* ]  */
    TOK_LBRACE,         /* {  */
    TOK_RBRACE,         /* }  */
    TOK_COLON,          /* :  */
    TOK_SEMICOLON,      /* ;  */
    TOK_COMMA,          /* ,  */
    TOK_DOT,            /* .  */
    TOK_ARROW,          /* -> */
    TOK_UNDERSCORE,     /* _  */
    TOK_COLONCOLON,     /* :: */

    /* ── Structural ───────────────────────────────────── */
    TOK_INDENT,
    TOK_DEDENT,
    TOK_NEWLINE,
    TOK_EOF,

    TOK_COUNT_
} TokenType;

typedef struct {
    TokenType   type;
    SrcLoc      loc;
    const char *start;      /* pointer into source text */
    int         length;
    int64_t     int_val;    /* for TOK_INT_LIT          */
    const char *str_val;    /* arena-owned, for TOK_STRING_LIT / TOK_IDENT */
} Token;

/* Human-readable name for debugging */
const char *token_type_name(TokenType t);

#endif /* AXIS_TOKEN_H */
