/*
 * axis_pe.h – Minimal PE32+ (64-bit) executable writer for AXIS.
 *
 * Takes the output of the x86-64 code generator (X64Ctx) and
 * produces a standalone Windows console executable.
 *
 * Layout:
 *   DOS header + stub
 *   PE signature
 *   COFF file header
 *   Optional header (PE32+)
 *   Section headers (.text, .rdata, .data, .idata)
 *   Section data
 */
#ifndef AXIS_PE_H
#define AXIS_PE_H

#include "axis_common.h"
#include "axis_x64.h"

/* ═════════════════════════════════════════════════════════════
 * PE constants
 * ═════════════════════════════════════════════════════════════ */

#define PE_FILE_ALIGNMENT     0x200   /* 512 bytes */
#define PE_SECTION_ALIGNMENT  0x1000  /* 4096 bytes */
#define PE_IMAGE_BASE         0x00400000ULL

/* ═════════════════════════════════════════════════════════════
 * Import table – functions we import from Windows DLLs
 * (kernel32.dll for I/O, heap, process exit)
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    const char *dll_name;       /* e.g. "kernel32.dll"         */
    const char **func_names;    /* NULL-terminated array        */
    int         func_count;
} PEImportDll;

typedef struct {
    PEImportDll *dlls;
    int          dll_count;
} PEImports;

/* ═════════════════════════════════════════════════════════════
 * PE writer context
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    /* Input */
    const X64Ctx *x64;

    /* Output buffer */
    uint8_t *buf;
    int      len;
    int      cap;

    /* Section layout (RVAs computed at write time) */
    uint32_t text_rva;
    uint32_t text_raw;      /* file offset */
    uint32_t text_size;     /* virtual size */

    uint32_t rdata_rva;
    uint32_t rdata_raw;
    uint32_t rdata_size;

    uint32_t idata_rva;     /* import directory */
    uint32_t idata_raw;
    uint32_t idata_size;

    uint32_t bss_rva;       /* .data / uninitialized */
    uint32_t bss_size;

    /* Entry point RVA within .text */
    uint32_t entry_rva;

    /* Import address table (filled at write time) */
    uint32_t iat_rva;

    /* Image size */
    uint32_t image_size;
} PECtx;

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

/*
 * pe_write – Generate a PE32+ executable from x64 codegen output.
 *            Writes the complete .exe into ctx->buf / ctx->len.
 *            Returns 0 on success, nonzero on error.
 */
int pe_write(PECtx *ctx, const X64Ctx *x64);

/*
 * pe_save – Write the PE buffer to a file.
 *           Returns 0 on success,nonzero on error.
 */
int pe_save(const PECtx *ctx, const char *path);

/*
 * pe_free – Free the PE output buffer.
 */
void pe_free(PECtx *ctx);

#endif /* AXIS_PE_H */
