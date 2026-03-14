/*
 * axis_lexer.h – AXIS lexer (tokenizer) interface.
 *
 * Produces a token stream from AXIS source code, including
 * INDENT / DEDENT tokens for indentation-based block structure.
 */
#ifndef AXIS_LEXER_H
#define AXIS_LEXER_H

#include "axis_token.h"
#include "axis_arena.h"

#define LEXER_MAX_INDENT_DEPTH 128

typedef struct {
    /* Source */
    const char *src;
    size_t      src_len;
    const char *filename;

    /* Position */
    size_t  pos;
    int     line;
    int     col;

    /* Indentation tracking */
    int  indent_stack[LEXER_MAX_INDENT_DEPTH];
    int  indent_top;          /* index of TOS in indent_stack */
    bool at_line_start;

    /* Pending token queue (for multi-DEDENT) */
    Token  pending[LEXER_MAX_INDENT_DEPTH];
    int    pending_count;
    int    pending_read;

    /* Arena for string values */
    Arena *arena;

    /* Check mode: collect errors instead of aborting */
    bool check_mode;
    int  error_count;
} Lexer;

void   lexer_init(Lexer *lex, const char *src, size_t len,
                   const char *filename, Arena *arena);
Token  lexer_next(Lexer *lex);

/* Tokenize the entire source; returns arena-allocated array, sets *count. */
Token *lexer_tokenize_all(Lexer *lex, int *count);

#endif /* AXIS_LEXER_H */
