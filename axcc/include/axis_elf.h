/*
 * axis_elf.h – Minimal ELF64 executable writer for AXIS (Linux x86-64).
 *
 * Takes the output of the x86-64 code generator (X64Ctx) and
 * produces a standalone Linux console executable using syscalls.
 *
 * Layout:
 *   ELF64 header
 *   Program headers (PT_LOAD × 2)
 *   .text  – generated x86-64 machine code + runtime stubs
 *   .data  – string literals + runtime buffers (RW)
 */
#ifndef AXIS_ELF_H
#define AXIS_ELF_H

#include "axis_common.h"
#include "axis_x64.h"

/* ═════════════════════════════════════════════════════════════
 * ELF constants
 * ═════════════════════════════════════════════════════════════ */

#define ELF_PAGE_SIZE   0x1000          /* 4096 bytes              */
#define ELF_BASE_VA     0x400000ULL     /* conventional load addr  */
#define ELF_EHDR_SIZE   64              /* sizeof(Elf64_Ehdr)      */
#define ELF_PHDR_SIZE   56              /* sizeof(Elf64_Phdr)      */

/* ═════════════════════════════════════════════════════════════
 * ELF writer context
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    /* Input */
    const X64Ctx *x64;

    /* Output buffer */
    uint8_t *buf;
    int      len;
    int      cap;

    /* Section layout */
    uint64_t text_va;       /* virtual address of .text     */
    uint32_t text_off;      /* file offset of .text         */
    uint32_t text_size;     /* size of .text in bytes       */

    uint64_t data_va;       /* virtual address of .data     */
    uint32_t data_off;      /* file offset of .data         */
    uint32_t data_size;     /* size of .data in bytes       */

    /* Entry point */
    uint64_t entry_va;
} ELFCtx;

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

/*
 * elf_write – Generate an ELF64 executable from x64 codegen output.
 *             Writes the complete binary into ctx->buf / ctx->len.
 *             Returns 0 on success, nonzero on error.
 */
int elf_write(ELFCtx *ctx, const X64Ctx *x64);

/*
 * elf_save – Write the ELF buffer to a file (sets executable permission).
 *            Returns 0 on success, nonzero on error.
 */
int elf_save(const ELFCtx *ctx, const char *path);

/*
 * elf_free – Free the ELF output buffer.
 */
void elf_free(ELFCtx *ctx);

#endif /* AXIS_ELF_H */
