/*
 * axis_semantic.h – AXIS semantic analyser interface.
 *
 * Multi-pass analysis:
 *   Pass 0a: Collect field definitions
 *   Pass 0b: Collect enum definitions
 *   Pass 1 : Collect function signatures
 *   Pass 2 : Analyse function bodies (type-check, annotate AST)
 *   Pass 3 : Analyse top-level statements (script mode)
 *
 * Annotates AST nodes with inferred types and stack layout information.
 */
#ifndef AXIS_SEMANTIC_H
#define AXIS_SEMANTIC_H

#include "axis_ast.h"
#include "axis_arena.h"

/* ── Symbol ─────────────────────────────────────────────────── */

typedef struct Symbol Symbol;
struct Symbol {
    const char   *name;
    const char   *type_name;      /* "i32", "bool", "array", field/enum name */
    bool          mutable;
    int           stack_offset;
    bool          is_param;
    bool          is_update;      /* parameter "update" modifier */
    ASTTypeNode  *array_type;     /* non-NULL for array variables  */
    Symbol       *next;           /* linked list within scope      */
};

typedef struct FuncSig FuncSig;
struct FuncSig {
    const char   *name;
    ASTParam     *params;
    int           param_count;
    const char   *return_type;    /* NULL → void */
    FuncSig      *next;
};

/* ── Scope ──────────────────────────────────────────────────── */

typedef struct Scope Scope;
struct Scope {
    Scope  *parent;
    Symbol *symbols;              /* singly-linked list */
};

/* ── Analyser ───────────────────────────────────────────────── */

typedef struct {
    Arena       *arena;
    const char  *filename;

    Scope       *global_scope;
    Scope       *current_scope;

    /* Registered type definitions */
    ASTFieldDef *field_defs;
    int          field_count;
    ASTEnumDef  *enum_defs;
    int          enum_count;

    /* Function signature table */
    FuncSig     *func_sigs;       /* linked list */

    /* Current function being analysed (NULL at top level) */
    ASTFunction *current_func;
    int          stack_offset;    /* grows positive, stored as negative */
    int          loop_depth;      /* for break/continue validation     */
} Semantic;

/* ── Public API ─────────────────────────────────────────────── */

void semantic_init(Semantic *s, Arena *arena, const char *filename);

/* Returns 0 on success, non-zero on first error (printed to stderr). */
int  semantic_analyze(Semantic *s, ASTProgram *prog);

#endif /* AXIS_SEMANTIC_H */
