/*
 * axis_parser.h – AXIS recursive-descent parser interface.
 *
 * Consumes an array of Token and produces an ASTProgram (arena-allocated).
 */
#ifndef AXIS_PARSER_H
#define AXIS_PARSER_H

#include "axis_ast.h"
#include "axis_arena.h"
#include "axis_token.h"

#include <setjmp.h>

typedef struct {
    Token      *tokens;
    int         token_count;
    int         pos;
    Token      *cur;            /* points to tokens[pos] */
    Arena      *arena;
    const char *filename;
    const char *source;         /* original source text (for error context) */

    /* Check mode: collect errors and recover instead of aborting */
    bool        check_mode;
    int         error_count;
    jmp_buf     err_jmp;        /* recovery point (only used when check_mode) */
} Parser;

void        parser_init(Parser *p, Token *tokens, int token_count,
                         Arena *arena, const char *filename, const char *source);
ASTProgram *parser_parse(Parser *p);

#endif /* AXIS_PARSER_H */
