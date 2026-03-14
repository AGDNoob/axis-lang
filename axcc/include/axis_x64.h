/*
 * axis_x64.h – x86-64 native code generator for AXIS.
 *
 * Translates IRProgram → raw machine code + relocation info.
 * The output is consumed by PE/ELF writers to produce executables.
 *
 * Calling convention: Windows x64 (rcx, rdx, r8, r9, stack).
 * System V is handled by the ELF writer swapping arg registers.
 */
#ifndef AXIS_X64_H
#define AXIS_X64_H

#include "axis_common.h"
#include "axis_arena.h"
#include "axis_ir.h"
#include "axis_opt.h"

/* ═════════════════════════════════════════════════════════════
 * Code buffer – growable byte array for emitted machine code
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t *data;
    int      len;
    int      cap;
} CodeBuf;

/* ═════════════════════════════════════════════════════════════
 * Relocation – patches needed after layout is finalized
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    RELOC_REL32,        /* 32-bit PC-relative (call, jmp, jcc)         */
    RELOC_ABS64,        /* 64-bit absolute (data pointers / lea rip)   */
    RELOC_RIP_REL32,    /* RIP-relative 32-bit (lea, mov from .rdata)  */
} RelocKind;

typedef struct {
    RelocKind   kind;
    int         offset;         /* byte offset in .text where patch goes */
    const char *target_sym;     /* function name or NULL for label       */
    int         target_label;   /* label id (if target_sym == NULL)      */
    int         addend;         /* extra offset to add                   */
} Reloc;

/* ═════════════════════════════════════════════════════════════
 * Function code info – output per compiled function
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    const char *name;
    int         text_offset;    /* start offset within code buf       */
    int         text_size;      /* code length in bytes               */
    int         stack_size;     /* aligned stack frame (from IR)      */
} X64Func;

/* ═════════════════════════════════════════════════════════════
 * String literal entry for .rdata section
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    const char *data;           /* NUL-terminated string content   */
    int         rdata_offset;   /* offset within .rdata section    */
} X64String;

/* ═════════════════════════════════════════════════════════════
 * Code-generation context
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    /* Input */
    const IRProgram *ir;

    /* Output: machine code (.text) */
    CodeBuf  code;

    /* Output: read-only data (.rdata) */
    uint8_t *rdata;
    int      rdata_len;
    int      rdata_cap;

    /* Relocation table */
    Reloc   *relocs;
    int      reloc_count;
    int      reloc_cap;

    /* Per-function info */
    X64Func *funcs;
    int      func_count;
    int      func_cap;

    /* String literals */
    X64String *strings;
    int        string_count;

    /* Label → code offset mapping (resolved during codegen) */
    int    *label_offsets;   /* indexed by label_id */
    int     label_cap;

    /* Temp → location mapping */
    int     var_area_size;   /* fn->stack_size: variables occupy [rbp-1] .. [rbp-var_area_size] */

    /* Register allocation for current function */
    RegAlloc cur_ra;         /* maps temp_id → physical register or REG_SPILLED */

    /* Callee-saved register save/restore info for current function */
    int callee_save_regs[16]; /* which phys regs to push/pop */
    int callee_save_count;    /* how many callee-saved regs are used */
    int callee_save_base;     /* rbp offset base: first saved reg at -(base+8) */

    /* Memory */
    Arena *arena;
} X64Ctx;

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

/*
 * x64_codegen  – Generate x86-64 machine code from an IR program.
 *                Populates ctx with code, relocs, string data.
 *                Arena is used for all allocations.
 */
void x64_codegen(X64Ctx *ctx, const IRProgram *ir, Arena *arena);

/*
 * x64_dump – Debug: disassemble generated code to FILE* in hex+text form.
 */
void x64_dump(const X64Ctx *ctx, FILE *out);

#endif /* AXIS_X64_H */
