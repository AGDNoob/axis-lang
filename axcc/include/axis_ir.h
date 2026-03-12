/*
 * axis_ir.h – Three-address-code Intermediate Representation for AXIS.
 *
 * Each function is lowered to a flat list of IR instructions.
 * Operands are virtual (temporaries), immediates, stack offsets, labels,
 * or function/string references.  The x86-64 back-end maps these to
 * physical registers and real stack frames.
 */
#ifndef AXIS_IR_H
#define AXIS_IR_H

#include "axis_common.h"
#include "axis_arena.h"
#include "axis_ast.h"

/* ═════════════════════════════════════════════════════════════
 * IR Opcode
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    /* ── Move / load ─────────────────────────────────────── */
    IR_NOP,
    IR_MOV,             /* dest = src1                       */
    IR_LOAD_IMM,        /* dest = imm (integer)              */
    IR_LOAD_STR,        /* dest = &strings[src1.imm]         */
    IR_LOAD_VAR,        /* dest = [rbp + src1.stack_off]     */
    IR_STORE_VAR,       /* [rbp + dest.stack_off] = src1     */

    /* ── Arithmetic ──────────────────────────────────────── */
    IR_ADD,             /* dest = src1 + src2                */
    IR_SUB,             /* dest = src1 - src2                */
    IR_MUL,             /* dest = src1 * src2                */
    IR_DIV,             /* dest = src1 / src2                */
    IR_MOD,             /* dest = src1 % src2                */
    IR_NEG,             /* dest = -src1                      */

    /* ── Bitwise ─────────────────────────────────────────── */
    IR_BIT_AND,         /* dest = src1 & src2                */
    IR_BIT_OR,          /* dest = src1 | src2                */
    IR_BIT_XOR,         /* dest = src1 ^ src2                */
    IR_SHL,             /* dest = src1 << src2               */
    IR_SHR,             /* dest = src1 >> src2 (arithmetic)  */

    /* ── Comparison (result → 0 / 1) ────────────────────── */
    IR_CMP_EQ,          /* dest = (src1 == src2)             */
    IR_CMP_NE,          /* dest = (src1 != src2)             */
    IR_CMP_LT,          /* dest = (src1 <  src2)             */
    IR_CMP_LE,          /* dest = (src1 <= src2)             */
    IR_CMP_GT,          /* dest = (src1 >  src2)             */
    IR_CMP_GE,          /* dest = (src1 >= src2)             */

    /* ── Logical (bool → bool) ───────────────────────────── */
    IR_LOG_NOT,         /* dest = !src1                      */
    /* AND/OR lowered to short-circuit jumps during IR gen   */

    /* ── Control flow ────────────────────────────────────── */
    IR_LABEL,           /* label dest.label_id               */
    IR_JMP,             /* goto dest.label_id                */
    IR_JZ,              /* if src1 == 0  goto dest.label_id  */
    IR_JNZ,             /* if src1 != 0  goto dest.label_id  */

    /* ── Call / return ───────────────────────────────────── */
    IR_ARG,             /* arg[dest.imm] = src1              */
    IR_CALL,            /* dest = call src1.func(nargs=src2) */
    IR_RET,             /* return src1 (src1 may be NONE)    */
    IR_RET_VOID,        /* return (no value)                 */

    /* ── I/O (built-in) ─────────────────────────────────── */
    IR_WRITE,           /* write src1 (dest.imm = newline?)  */
    IR_READ,            /* dest = read(src1.imm = kind)      */

    /* ── Array / field ───────────────────────────────────── */
    IR_INDEX_LOAD,      /* dest = base[idx], elem_size in src2.imm */
    IR_INDEX_STORE,     /* base[idx] = src: dest=base, src1=idx, src2=val; size via extra */
    IR_FIELD_LOAD,      /* dest = *(base + offset)           */
    IR_FIELD_STORE,     /* *(base + offset) = src1           */
    IR_LEA,             /* dest = effective address of stack slot (src1) */

    /* ── Array copy / indirect store ────────────────────── */
    IR_MEMCPY,          /* memcpy(dest_addr, src1_addr, src2.imm bytes) */
    IR_STORE_IND,       /* *dest = src1  (store src1 through pointer dest, extra=size) */

    /* ── Conversion ──────────────────────────────────────── */
    IR_SEXT,            /* dest = sign-extend src1 to dest.size */
    IR_ZEXT,            /* dest = zero-extend src1 to dest.size */
    IR_TRUNC,           /* dest = truncate src1 to dest.size    */

    /* ── Syscall ─────────────────────────────────────────── */
    IR_SYSCALL,         /* syscall with src1.imm args (regs loaded via IR_ARG) */

    IR_OPCODE_COUNT
} IROpcode;

/* ═════════════════════════════════════════════════════════════
 * IR Operand
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    OPER_NONE,          /* unused operand slot */
    OPER_TEMP,          /* virtual register: temp_id         */
    OPER_IMM,           /* immediate integer constant        */
    OPER_STACK,         /* stack slot at [rbp + stack_off]   */
    OPER_LABEL,         /* label identifier                  */
    OPER_FUNC,          /* function name (string pointer)    */
    OPER_STR,           /* string table index                */
} IROperKind;

typedef struct {
    IROperKind kind;
    int        size;        /* operand size in bytes (1,2,4,8) */
    union {
        int         temp_id;    /* OPER_TEMP  */
        int64_t     imm;        /* OPER_IMM   */
        int         stack_off;  /* OPER_STACK */
        int         label_id;   /* OPER_LABEL */
        const char *func_name;  /* OPER_FUNC  */
        int         str_idx;    /* OPER_STR   */
    };
} IROper;

/* ═════════════════════════════════════════════════════════════
 * IR Instruction
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    IROpcode op;
    IROper   dest;
    IROper   src1;
    IROper   src2;
    int      extra;         /* misc: elem_size, write-type, etc. */
    SrcLoc   loc;
} IRInstr;

/* ═════════════════════════════════════════════════════════════
 * IR Function / Program
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    int  offset;     /* rbp-relative offset (negative) */
    int  size;       /* byte width (1, 2, 4, 8)        */
    bool is_update;  /* true for "update" params        */
    int  wb_offset;  /* rbp-relative offset of hidden writeback-address slot */
    bool is_field;   /* true for aggregate (field/struct) params passed by ptr */
    int  field_size; /* total byte size of the aggregate (for memcpy in prologue) */
} IRParamInfo;

typedef struct {
    const char   *name;
    IRInstr      *instrs;
    int           instr_count;
    int           instr_cap;
    int           temp_count;     /* total virtual registers used */
    int           stack_size;     /* aligned frame size (from semantic) */
    int           param_count;    /* total params incl. hidden update-addr args */
    int           visible_param_count; /* original user-visible param count */
    IRParamInfo  *param_info;     /* array[param_count]: offset/size per param */
} IRFunc;

typedef struct {
    IRFunc     *funcs;
    int         func_count;
    int         func_cap;

    IRFunc      top_level;      /* IR for top-level statements */

    const char **strings;       /* string literal table */
    int          str_count;
    int          str_cap;

    Arena       *arena;
} IRProgram;

/* ═════════════════════════════════════════════════════════════
 * IR–generation API
 * ═════════════════════════════════════════════════════════════ */

void      ir_program_init(IRProgram *p, Arena *arena);
void      ir_generate(IRProgram *p, ASTProgram *ast, const char *filename);

/* Debug: dump IR to FILE* in text form */
void      ir_dump(const IRProgram *p, FILE *out);

#endif /* AXIS_IR_H */
