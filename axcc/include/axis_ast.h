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
    EXPR_COPY_CAST,         /* copy expr as type            */
    EXPR_RANGE,             /* start..end  (or with step)   */
    EXPR_INPUT,             /* input(prompt?)               */
    EXPR_INPUT_FAILED,      /* var_input_failed()           */
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

        /* EXPR_COPY_CAST  (copy value + cast to target type) */
        struct {
            ASTExpr     *expr;
            ASTTypeNode *target_type;
            int          old_size, new_size;
            bool         is_signed_src;
        } copy_cast;

        /* EXPR_RANGE  (step may be NULL) */
        struct { ASTExpr *start; ASTExpr *end; ASTExpr *step; } range;

        /* EXPR_INPUT  (prompt may be NULL) */
        struct { ASTExpr *prompt; }                             input;

        /* EXPR_INPUT_FAILED  (var_name = prefix before "_input_failed") */
        struct { const char *var_name; int input_flag_offset; } input_failed;
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
    STMT_IF,                /* when ... else ...            */
    STMT_WHILE,             /* while condition: body        */
    STMT_REPEAT,            /* repeat: body (infinite loop) */
    STMT_FOR,               /* for var in iter: body        */
    STMT_BREAK,             /* break / stop                 */
    STMT_CONTINUE,          /* continue / skip              */
    STMT_RETURN,            /* return value                 */
    STMT_MATCH,             /* match expr: arms...          */
    STMT_SYSCALL,           /* syscall(args...)             */
    STMT_UPDATE_CAST,       /* update name as type          */
} StmtKind;

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
            int          input_flag_offset; /* stack offset for input flag (0 if not input) */
            bool         is_const;      /* true if declared with const */
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
            ASTExpr    *condition;
            ASTStmt   **body;
            int         body_count;
            const char *flag;       /* optional @flag name (NULL if none) */
        } while_loop;

        /* STMT_REPEAT */
        struct {
            ASTStmt   **body;
            int         body_count;
            const char *flag;       /* optional @flag name (NULL if none) */
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
            const char *flag;       /* optional @flag name (NULL if none) */
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

        /* STMT_BREAK */
        struct {
            const char *flag;   /* NULL = innermost loop */
        } break_stmt;

        /* STMT_CONTINUE */
        struct {
            const char *flag;   /* NULL = innermost loop */
        } continue_stmt;

        /* STMT_UPDATE_CAST */
        struct {
            const char  *var_name;
            ASTTypeNode *target_type;
            int          old_offset;    /* filled by semantic pass */
            int          new_offset;    /* filled by semantic pass */
            int          old_size;      /* filled by semantic pass */
            int          new_size;      /* filled by semantic pass */
            bool         is_signed_src; /* filled by semantic pass */
        } update_cast;
    };
};

/* ═════════════════════════════════════════════════════════════
 * Definition Nodes (top-level constructs)
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    const char  *name;
    ASTTypeNode *type_node;
    bool         is_update;     /* "update" modifier on parameter */
    bool         is_copy;       /* "copy" modifier on parameter   */
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
