/*
 * axis_ast.h – Abstract Syntax Tree node definitions for AXIS.
 *
 * All AST nodes are arena-allocated.  Lists use (pointer, count) pairs.
 * Expression / statement nodes are tagged unions keyed by ExprKind / StmtKind.
 */
#ifndef AXIS_AST_H
#define AXIS_AST_H

#include "axis_common.h"
#include "axis_token.h"

/* ── Forward declarations ─────────────────────────────────── */
typedef struct ASTExpr       ASTExpr;
typedef struct ASTStmt       ASTStmt;
typedef struct ASTTypeNode   ASTTypeNode;
typedef struct ASTMatchArm   ASTMatchArm;
typedef struct ASTFieldMember ASTFieldMember;

/* ═════════════════════════════════════════════════════════════
 * Type Nodes
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    TYPE_NODE_SIMPLE,       /* i32, bool, str, or named type */
    TYPE_NODE_ARRAY,        /* (element_type; size)           */
} TypeNodeKind;

struct ASTTypeNode {
    TypeNodeKind kind;
    SrcLoc       loc;
    union {
        struct { const char *name; }                simple;
        struct { ASTTypeNode *elem; int size; }     array;
    };
};

/* ═════════════════════════════════════════════════════════════
 * Expression Nodes
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    EXPR_INT_LIT,           /* 42, 0xFF, 0b1010             */
    EXPR_STRING_LIT,        /* "hello"                      */
    EXPR_BOOL_LIT,          /* true / false                 */
    EXPR_IDENT,             /* variable or function name    */
    EXPR_BINARY,            /* left op right                */
    EXPR_UNARY,             /* op operand                   */
    EXPR_CALL,              /* name(args...)                */
    EXPR_INDEX,             /* array[index]                 */
    EXPR_FIELD_ACCESS,      /* obj.member                   */
    EXPR_ENUM_ACCESS,       /* Enum::Variant                */
    EXPR_ARRAY_LIT,         /* [1, 2, 3]                    */
    EXPR_COPY,              /* copy expr                    */
    EXPR_RANGE,             /* start..end  (or with step)   */
    EXPR_READ_FAILED,       /* read_failed                  */
} ExprKind;

struct ASTExpr {
    ExprKind    kind;
    SrcLoc      loc;
    const char *inferred_type;  /* filled by semantic pass (e.g. "i32") */
    union {
        /* EXPR_INT_LIT */
        struct { int64_t value; }                               int_lit;

        /* EXPR_STRING_LIT */
        struct { const char *value; }                           string_lit;

        /* EXPR_BOOL_LIT */
        struct { bool value; }                                  bool_lit;

        /* EXPR_IDENT */
        struct { const char *name; }                            ident;

        /* EXPR_BINARY – op stored as TokenType (TOK_PLUS, etc.) */
        struct { ASTExpr *left; TokenType op; ASTExpr *right; } binary;

        /* EXPR_UNARY – op is TOK_MINUS, TOK_NOT, TOK_BANG */
        struct { TokenType op; ASTExpr *operand; }              unary;

        /* EXPR_CALL */
        struct {
            const char *name;
            ASTExpr   **args;
            bool       *update_flags;   /* per-arg "update" flag */
            int         arg_count;
        } call;

        /* EXPR_INDEX */
        struct { ASTExpr *array; ASTExpr *index; }              index;

        /* EXPR_FIELD_ACCESS */
        struct { ASTExpr *object; const char *member; }         field_access;

        /* EXPR_ENUM_ACCESS */
        struct { const char *enum_name; const char *variant; }  enum_access;

        /* EXPR_ARRAY_LIT */
        struct { ASTExpr **elements; int count; }               array_lit;

        /* EXPR_COPY  (compile_time: false = runtime, true = compile) */
        struct { ASTExpr *expr; bool compile_time; }            copy;

        /* EXPR_RANGE  (step may be NULL) */
        struct { ASTExpr *start; ASTExpr *end; ASTExpr *step; } range;

        /* EXPR_READ_FAILED – no payload */
    };
};

/* ═════════════════════════════════════════════════════════════
 * Statement Nodes
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    STMT_VAR_DECL,          /* name: type = value           */
    STMT_ASSIGN,            /* name = value                 */
    STMT_INDEX_ASSIGN,      /* array[idx] = value           */
    STMT_FIELD_ASSIGN,      /* obj.member = value           */
    STMT_COMPOUND_ASSIGN,   /* target op= value             */
    STMT_EXPR,              /* expression as statement      */
    STMT_WRITE,             /* write / writeln              */
    STMT_READ,              /* read / readln / readchar     */
    STMT_IF,                /* when ... else ...            */
    STMT_WHILE,             /* while condition: body        */
    STMT_REPEAT,            /* repeat: body (infinite loop) */
    STMT_FOR,               /* for var in iter: body        */
    STMT_BREAK,             /* break / stop                 */
    STMT_CONTINUE,          /* continue / skip              */
    STMT_RETURN,            /* give / return value          */
    STMT_MATCH,             /* match expr: arms...          */
    STMT_SYSCALL,           /* syscall(args...)             */
} StmtKind;

typedef enum {
    READ_READ,
    READ_READLN,
    READ_READCHAR,
} ReadKind;

/* Match arm (used inside STMT_MATCH) */
struct ASTMatchArm {
    ASTExpr  *pattern;      /* NULL → wildcard (_)  */
    ASTStmt **body;
    int       body_count;
    bool      is_wildcard;
    SrcLoc    loc;
};

struct ASTStmt {
    StmtKind kind;
    SrcLoc   loc;
    union {
        /* STMT_VAR_DECL */
        struct {
            const char  *name;
            ASTTypeNode *type_node;     /* NULL if inferred          */
            ASTExpr     *value;         /* NULL if no initializer    */
            int          stack_offset;  /* filled by semantic pass   */
            int          total_size;    /* filled by semantic pass   */
        } var_decl;

        /* STMT_ASSIGN */
        struct {
            const char *name;
            ASTExpr    *value;
        } assign;

        /* STMT_INDEX_ASSIGN */
        struct {
            ASTExpr *array;
            ASTExpr *index;
            ASTExpr *value;
        } index_assign;

        /* STMT_FIELD_ASSIGN */
        struct {
            ASTExpr    *object;
            const char *member;
            ASTExpr    *value;
        } field_assign;

        /* STMT_COMPOUND_ASSIGN – op is TOK_PLUS_ASSIGN etc. */
        struct {
            ASTExpr  *target;
            TokenType op;
            ASTExpr  *value;
        } compound_assign;

        /* STMT_EXPR */
        struct {
            ASTExpr *expr;
        } expr_stmt;

        /* STMT_WRITE */
        struct {
            ASTExpr *value;
            bool     newline;       /* true = writeln, false = write */
        } write;

        /* STMT_READ */
        struct {
            const char *target;     /* variable name to read into */
            ReadKind    read_kind;
        } read;

        /* STMT_IF */
        struct {
            ASTExpr  *condition;
            ASTStmt **body;
            int       body_count;
            ASTStmt **else_body;    /* NULL if no else clause */
            int       else_count;
        } if_stmt;

        /* STMT_WHILE */
        struct {
            ASTExpr  *condition;
            ASTStmt **body;
            int       body_count;
        } while_loop;

        /* STMT_REPEAT */
        struct {
            ASTStmt **body;
            int       body_count;
        } repeat_loop;

        /* STMT_FOR */
        struct {
            const char *var_name;
            ASTExpr    *iterable;   /* RangeExpr or array */
            ASTStmt   **body;
            int         body_count;
            /* Populated by semantic pass for array iteration */
            int         array_elem_size;  /* element size in bytes  */
            int         array_count;      /* number of elements     */
        } for_loop;

        /* STMT_RETURN */
        struct {
            ASTExpr *value;         /* NULL for bare return */
        } return_stmt;

        /* STMT_MATCH */
        struct {
            ASTExpr     *expr;
            ASTMatchArm *arms;      /* arena-allocated array */
            int          arm_count;
        } match;

        /* STMT_SYSCALL */
        struct {
            ASTExpr **args;
            int       arg_count;
        } syscall;

        /* STMT_BREAK, STMT_CONTINUE – no payload */
    };
};

/* ═════════════════════════════════════════════════════════════
 * Definition Nodes (top-level constructs)
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    const char  *name;
    ASTTypeNode *type_node;
    bool         is_update;     /* "update" modifier on parameter */
    int          stack_offset;  /* filled by semantic pass */
    SrcLoc       loc;
} ASTParam;

typedef struct {
    const char   *name;
    ASTParam     *params;       /* arena-allocated array */
    int           param_count;
    ASTTypeNode  *return_type;  /* NULL → void */
    ASTStmt     **body;
    int           body_count;
    int           stack_size;   /* filled by semantic pass */
    SrcLoc        loc;
} ASTFunction;

struct ASTFieldMember {
    const char      *name;
    ASTTypeNode     *type_node;
    ASTExpr         *default_value;     /* NULL if none */
    ASTFieldMember  *inline_members;    /* for nested inline fields */
    int              inline_count;
    SrcLoc           loc;
};

typedef struct {
    const char     *name;
    ASTFieldMember *members;    /* arena-allocated array */
    int             member_count;
    SrcLoc          loc;
} ASTFieldDef;

typedef struct {
    const char *name;
    int         value;
    bool        has_value;      /* false → auto-assigned */
    SrcLoc      loc;
} ASTEnumVariant;

typedef struct {
    const char     *name;
    const char     *underlying_type;  /* e.g. "i32" (default) */
    ASTEnumVariant *variants;         /* arena-allocated array */
    int             variant_count;
    SrcLoc          loc;
} ASTEnumDef;

/* ═════════════════════════════════════════════════════════════
 * Program (root node)
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    MODE_SCRIPT,
    MODE_COMPILE,
} ProgramMode;

typedef struct {
    ProgramMode    mode;
    ASTFunction   *functions;   /* arena-allocated array */
    int            func_count;
    ASTStmt      **statements;  /* top-level statements */
    int            stmt_count;
    ASTFieldDef   *field_defs;  /* arena-allocated array */
    int            field_count;
    ASTEnumDef    *enum_defs;   /* arena-allocated array */
    int            enum_count;
    SrcLoc         loc;
} ASTProgram;

#endif /* AXIS_AST_H */
