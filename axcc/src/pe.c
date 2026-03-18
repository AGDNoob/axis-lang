/*
 * pe.c – Minimal PE32+ (64-bit) executable writer for the AXIS compiler.
 *
 * Produces a standalone Windows console executable with:
 *   .text   – generated x86-64 machine code + runtime stubs
 *   .rdata  – string literals
 *   .idata  – import directory (kernel32.dll, msvcrt.dll)
 *
 * The runtime stubs (__axis_write_i64, etc.) are small thunks that
 * call into msvcrt.dll's printf/scanf/putchar/getchar.
 */

#include "axis_pe.h"
#include <string.h>

/* x86-64 register indices (matching x64.c) */
enum {
    RAX = 0, RCX = 1, RDX = 2, R11 = 11
};

/* ═════════════════════════════════════════════════════════════
 * PE structure definitions (minimal subset)
 * ═════════════════════════════════════════════════════════════ */

#pragma pack(push, 1)

typedef struct {
    uint16_t e_magic;           /* "MZ" = 0x5A4D */
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;          /* offset to PE signature */
} DOSHeader;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFFHeader;

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} DataDirectory;

typedef struct {
    uint16_t Magic;                 /* 0x020B for PE32+ */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    DataDirectory DataDirectory[16];
} OptionalHeader64;

typedef struct {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} SectionHeader;

/* Import directory entry */
typedef struct {
    uint32_t OriginalFirstThunk; /* RVA to Import Lookup Table (ILT) */
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;               /* RVA to DLL name string */
    uint32_t FirstThunk;         /* RVA to Import Address Table (IAT) */
} ImportDirectoryEntry;

#pragma pack(pop)

/* ═════════════════════════════════════════════════════════════
 * Output buffer helpers
 * ═════════════════════════════════════════════════════════════ */

static void buf_init(PECtx *ctx)
{
    ctx->cap = 64 * 1024;
    ctx->len = 0;
    ctx->buf = (uint8_t *)calloc(1, ctx->cap);
}

static void buf_grow(PECtx *ctx, int need)
{
    while (ctx->len + need > ctx->cap) {
        ctx->cap *= 2;
        ctx->buf = (uint8_t *)realloc(ctx->buf, ctx->cap);
    }
}

static void buf_write(PECtx *ctx, const void *data, int size)
{
    buf_grow(ctx, size);
    memcpy(&ctx->buf[ctx->len], data, size);
    ctx->len += size;
}

__attribute__((unused))
static void buf_write8(PECtx *ctx, uint8_t v)  { buf_write(ctx, &v, 1); }
__attribute__((unused))
static void buf_write16(PECtx *ctx, uint16_t v) { buf_write(ctx, &v, 2); }
static void buf_write32(PECtx *ctx, uint32_t v) { buf_write(ctx, &v, 4); }
__attribute__((unused))
static void buf_write64(PECtx *ctx, uint64_t v) { buf_write(ctx, &v, 8); }

static void buf_pad_to(PECtx *ctx, int offset)
{
    if (offset > ctx->len) {
        buf_grow(ctx, offset - ctx->len);
        memset(&ctx->buf[ctx->len], 0, offset - ctx->len);
        ctx->len = offset;
    }
}

__attribute__((unused))
static void buf_patch32(PECtx *ctx, int offset, uint32_t v)
{
    memcpy(&ctx->buf[offset], &v, 4);
}

__attribute__((unused))
static void buf_patch64(PECtx *ctx, int offset, uint64_t v)
{
    memcpy(&ctx->buf[offset], &v, 8);
}

/* ═════════════════════════════════════════════════════════════
 * Alignment helpers
 * ═════════════════════════════════════════════════════════════ */

static uint32_t align_up(uint32_t v, uint32_t align)
{
    return (v + align - 1) & ~(align - 1);
}

/* ═════════════════════════════════════════════════════════════
 * Runtime stubs
 *
 * We generate small assembly thunks that bridge the AXIS runtime
 * calls (__axis_write_i64 etc.) to msvcrt functions.
 * These are appended to .text after the user code.
 *
 * Import functions we need:
 *   msvcrt.dll:  printf, scanf, putchar, getchar, _write
 *   kernel32.dll: ExitProcess, GetStdHandle, WriteConsoleA,
 *                 ReadConsoleA
 *
 * For simplicity we'll route through msvcrt's printf/scanf.
 * ═════════════════════════════════════════════════════════════ */

/* Import function indices (for IAT lookup) */
enum {
    IMP_PRINTF = 0,
    IMP_SCANF,
    IMP_PUTCHAR,
    IMP_GETCHAR,
    IMP_EXIT,
    IMP_COUNT
};

static const char *import_names[IMP_COUNT] = {
    "printf", "scanf", "putchar", "getchar", "exit"
};

/* We'll store the IAT entry offsets (RVA) for each import.
 * The runtime stubs do: call [rip + iat_entry_rva - rip_here] */

typedef struct {
    uint32_t iat_entry_rva[IMP_COUNT];
} ImportInfo;

/*
 * Emit a runtime stub that:
 *   1. Already has the argument in RCX (Win64 1st arg).
 *   2. Loads format string address into RCX (for printf) or just calls.
 *   3. call [rip + disp32]  →  calls through IAT.
 *   4. ret
 *
 * We allocate format strings in .rdata.
 */

typedef struct {
    const char *name;       /* __axis_write_i64 etc. */
    int         text_off;   /* offset within .text */
} StubInfo;

/* ═════════════════════════════════════════════════════════════
 * Build the import section (.idata)
 *
 * Structure:
 *   Import Directory Table (array of ImportDirectoryEntry, NULL-terminated)
 *   Import Lookup Table (ILT) – array of 64-bit RVAs to Hint/Name
 *   Import Address Table (IAT) – duplicate of ILT, patched by loader
 *   Hint/Name entries
 *   DLL name strings
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t *data;
    int      len;
    int      cap;

    /* Layout offsets (within .idata) */
    int idt_off;            /* Import Directory Table */
    int ilt_off;            /* Import Lookup Table */
    int iat_off;            /* Import Address Table */
    int hints_off;          /* Hint/Name entries */
    int names_off;          /* DLL name strings */

    /* IAT entry RVAs (absolute, after idata_rva is known) */
    uint32_t iat_entry_rva[IMP_COUNT];
} IdataBuilder;

static void idata_init(IdataBuilder *ib)
{
    ib->cap = 4096;
    ib->len = 0;
    ib->data = (uint8_t *)calloc(1, ib->cap);
}

static void idata_grow(IdataBuilder *ib, int need)
{
    while (ib->len + need > ib->cap) {
        ib->cap *= 2;
        ib->data = (uint8_t *)realloc(ib->data, ib->cap);
    }
}

static int idata_write(IdataBuilder *ib, const void *data, int sz)
{
    idata_grow(ib, sz);
    int off = ib->len;
    memcpy(&ib->data[ib->len], data, sz);
    ib->len += sz;
    return off;
}

__attribute__((unused))
static int idata_write32(IdataBuilder *ib, uint32_t v)
{
    return idata_write(ib, &v, 4);
}

static int idata_write64(IdataBuilder *ib, uint64_t v)
{
    return idata_write(ib, &v, 8);
}

static uint16_t idata_write16(IdataBuilder *ib, uint16_t v)
{
    idata_write(ib, &v, 2);
    return v;
}

static void idata_pad(IdataBuilder *ib, int to)
{
    if (to > ib->len) {
        idata_grow(ib, to - ib->len);
        memset(&ib->data[ib->len], 0, to - ib->len);
        ib->len = to;
    }
}

static void idata_patch32(IdataBuilder *ib, int off, uint32_t v)
{
    memcpy(&ib->data[off], &v, 4);
}

static void idata_patch64(IdataBuilder *ib, int off, uint64_t v)
{
    memcpy(&ib->data[off], &v, 8);
}

/*
 * Build the .idata section data.
 * We have one DLL: msvcrt.dll, importing printf, scanf, putchar, getchar, exit.
 */
static void build_idata(IdataBuilder *ib, uint32_t idata_rva)
{
    idata_init(ib);

    /*
     * Layout plan:
     *   IDT: 2 entries (1 for msvcrt.dll + 1 null terminator) = 40 bytes
     *   ILT: IMP_COUNT + 1 entries (8 bytes each)
     *   IAT: IMP_COUNT + 1 entries (8 bytes each) – must be separate from ILT
     *   Hint/Name: for each import
     *   DLL name: "msvcrt.dll\0"
     */

    /* --- Import Directory Table --- */
    ib->idt_off = ib->len;
    /* msvcrt.dll entry (20 bytes) – will patch RVAs later */
    int idt_msvcrt = ib->len;
    ImportDirectoryEntry ide;
    memset(&ide, 0, sizeof(ide));
    idata_write(ib, &ide, sizeof(ide));
    /* Null terminator entry */
    idata_write(ib, &ide, sizeof(ide));

    /* --- Import Lookup Table (ILT) for msvcrt --- */
    ib->ilt_off = ib->len;
    int ilt_entries[IMP_COUNT];
    for (int i = 0; i < IMP_COUNT; i++) {
        ilt_entries[i] = ib->len;
        idata_write64(ib, 0); /* placeholder – will patch to Hint/Name RVA */
    }
    idata_write64(ib, 0); /* null terminator */

    /* --- Import Address Table (IAT) – same structure as ILT --- */
    ib->iat_off = ib->len;
    int iat_entries[IMP_COUNT];
    for (int i = 0; i < IMP_COUNT; i++) {
        iat_entries[i] = ib->len;
        idata_write64(ib, 0);
    }
    idata_write64(ib, 0);

    /* --- Hint/Name entries --- */
    ib->hints_off = ib->len;
    int hint_rvas[IMP_COUNT];
    for (int i = 0; i < IMP_COUNT; i++) {
        hint_rvas[i] = ib->len;
        idata_write16(ib, (uint16_t)i);    /* hint */
        int nlen = (int)strlen(import_names[i]) + 1;
        idata_write(ib, import_names[i], nlen);
        /* Pad to even boundary */
        if (nlen % 2) idata_write(ib, "\0", 1);
    }

    /* --- DLL name --- */
    ib->names_off = ib->len;
    int dll_name_off = ib->len;
    const char *dll = "msvcrt.dll";
    idata_write(ib, dll, (int)strlen(dll) + 1);
    /* Align to 4 bytes */
    idata_pad(ib, (int)align_up(ib->len, 4));

    /* --- Patch RVAs --- */
    /* IDT entry for msvcrt */
    idata_patch32(ib, idt_msvcrt + 0,  idata_rva + (uint32_t)ib->ilt_off);     /* OriginalFirstThunk → ILT */
    idata_patch32(ib, idt_msvcrt + 12, idata_rva + (uint32_t)dll_name_off);    /* Name → DLL name */
    idata_patch32(ib, idt_msvcrt + 16, idata_rva + (uint32_t)ib->iat_off);     /* FirstThunk → IAT */

    /* ILT and IAT entries → Hint/Name RVAs */
    for (int i = 0; i < IMP_COUNT; i++) {
        uint64_t hn_rva = idata_rva + (uint32_t)hint_rvas[i];
        idata_patch64(ib, ilt_entries[i], hn_rva);
        idata_patch64(ib, iat_entries[i], hn_rva);
    }

    /* Store IAT entry RVAs for stub generation */
    for (int i = 0; i < IMP_COUNT; i++) {
        ib->iat_entry_rva[i] = idata_rva + (uint32_t)iat_entries[i];
    }
}

/* ═════════════════════════════════════════════════════════════
 * Runtime stub generation
 *
 * Each stub is a small sequence:
 *   For __axis_write_i64:
 *     lea rdx, [rcx]       ; value already in rcx, move to rdx (2nd arg)
 *     lea rcx, [rip+disp]  ; format string "%lld" in .rdata
 *     sub rsp, 40           ; shadow space + alignment
 *     call [rip+disp]       ; printf via IAT
 *     add rsp, 40
 *     ret
 *
 * Format strings are appended to .rdata.
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    /* format string offsets in .rdata */
    int fmt_lld;    /* "%lld"  */
    int fmt_s;      /* "%s"    */
    int fmt_true;   /* "true"  */
    int fmt_false;  /* "false" */
    int fmt_c;      /* "%c"    */
    int fmt_nl;     /* "\n"    */
    int fmt_lld_in; /* "%lld"  (for scanf) */
    int fmt_buf;    /* 256-byte input buffer */
    int read_failed_flag; /* 1-byte flag for read_failed() */
    int fmt_div_zero;       /* "runtime error: division by zero\n" */
} RtFormats;

/* Add runtime format strings to the rdata provided by x64 codegen.
 * We append after existing string literals. */
static RtFormats add_rt_formats(uint8_t **rdata, int *rdata_len, int *rdata_cap)
{
    RtFormats rf;

#define RT_ADD_STR(field, s) do {                               \
    int slen = (int)sizeof(s);                                  \
    while (*rdata_len + slen > *rdata_cap) {                    \
        *rdata_cap *= 2;                                        \
        *rdata = (uint8_t *)realloc(*rdata, *rdata_cap);        \
    }                                                           \
    rf.field = *rdata_len;                                      \
    memcpy(*rdata + *rdata_len, s, slen);                       \
    *rdata_len += slen;                                         \
} while(0)

    RT_ADD_STR(fmt_lld,    "%lld");
    RT_ADD_STR(fmt_s,      "%s");
    RT_ADD_STR(fmt_true,   "true");
    RT_ADD_STR(fmt_false,  "false");
    RT_ADD_STR(fmt_c,      "%c");
    RT_ADD_STR(fmt_nl,     "\n");
    RT_ADD_STR(fmt_lld_in, "%lld");

    /* 256-byte input buffer */
    while (*rdata_len + 256 > *rdata_cap) {
        *rdata_cap *= 2;
        *rdata = (uint8_t *)realloc(*rdata, *rdata_cap);
    }
    rf.fmt_buf = *rdata_len;
    memset(*rdata + *rdata_len, 0, 256);
    *rdata_len += 256;

    /* 1-byte read_failed flag (0 = ok, 1 = failed) */
    while (*rdata_len + 1 > *rdata_cap) {
        *rdata_cap *= 2;
        *rdata = (uint8_t *)realloc(*rdata, *rdata_cap);
    }
    rf.read_failed_flag = *rdata_len;
    (*rdata)[*rdata_len] = 0;
    *rdata_len += 1;

    RT_ADD_STR(fmt_div_zero, "runtime error: division by zero\n");

#undef RT_ADD_STR
    return rf;
}

/* Emit bytes into a temporary buffer for stubs, to be later appended
 * into the main .text section. */
typedef struct {
    uint8_t *data;
    int      len;
    int      cap;
} StubBuf;

static void sb_init(StubBuf *sb)
{
    sb->cap = 1024;
    sb->len = 0;
    sb->data = (uint8_t *)calloc(1, sb->cap);
}

static void sb_emit8(StubBuf *sb, uint8_t v)
{
    if (sb->len >= sb->cap) {
        sb->cap *= 2;
        sb->data = (uint8_t *)realloc(sb->data, sb->cap);
    }
    sb->data[sb->len++] = v;
}

static void sb_emit32(StubBuf *sb, uint32_t v)
{
    for (int i = 0; i < 4; i++) sb_emit8(sb, (uint8_t)(v >> (i * 8)));
}

/* Emit: call [rip + disp32]  (FF 15 disp32) */
static void sb_emit_call_iat(StubBuf *sb, uint32_t iat_rva,
                             uint32_t text_rva, int stub_text_off)
{
    /* The call [rip+disp32] instruction is 6 bytes.
     * RIP at execution = text_rva + stub_text_off + sb->len + 6
     * disp32 = iat_rva - RIP */
    int rip = (int)(text_rva + stub_text_off + sb->len + 6);
    int disp = (int)iat_rva - rip;
    sb_emit8(sb, 0xFF);
    sb_emit8(sb, 0x15); /* ModRM: mod=00 reg=010 rm=101 → [rip+d32] */
    sb_emit32(sb, (uint32_t)disp);
}

/* Emit: lea reg, [rip + disp32]  to load a .rdata address */
static void sb_emit_lea_rip(StubBuf *sb, int reg,
                            uint32_t rdata_rva, int rdata_off,
                            uint32_t text_rva, int stub_text_off)
{
    /* lea r64, [rip+disp32]: REX.W + 8D modrm(00, reg, 101) disp32
     * Instruction is 7 bytes. RIP = text_rva + stub_text_off + sb->len + 7 */
    int rip = (int)(text_rva + stub_text_off + sb->len + 7);
    int disp = (int)(rdata_rva + rdata_off) - rip;
    sb_emit8(sb, (uint8_t)(0x48 | ((reg >= 8) ? 0x04 : 0)));  /* REX.W + R */
    sb_emit8(sb, 0x8D);
    sb_emit8(sb, (uint8_t)(0x05 | ((reg & 7) << 3)));  /* modrm(00, reg, 5) */
    sb_emit32(sb, (uint32_t)disp);
}

/* Emit: mov rdx, rcx (pass value as 2nd arg, push format to 1st) */
static void sb_emit_mov_rdx_rcx(StubBuf *sb)
{
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xCA); /* mov rdx, rcx */
}

/* sub rsp, 40 ; add rsp, 40 */
static void sb_emit_sub_rsp_40(StubBuf *sb)
{
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x83); sb_emit8(sb, 0xEC); sb_emit8(sb, 0x28);
}
static void sb_emit_add_rsp_40(StubBuf *sb)
{
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x83); sb_emit8(sb, 0xC4); sb_emit8(sb, 0x28);
}

static void sb_emit_ret(StubBuf *sb)
{
    sb_emit8(sb, 0xC3);
}

/* xor eax, eax */
static void sb_emit_xor_eax_eax(StubBuf *sb)
{
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);
}

/*
 * Generate all runtime stubs.  Returns stub info (name + text offset)
 * for each stub so we can patch call relocations in the user code.
 *
 * Stubs are emitted into sb; their text_offset is relative to the
 * start of the generated code's .text section (user_code_len is the
 * byte offset where stubs begin in .text).
 */
typedef struct {
    int write_i64_off;
    int write_str_off;
    int write_bool_off;
    int write_char_off;
    int write_nl_off;
    int read_i64_off;
    int read_line_off;
    int read_char_off;
    int memcpy_off;
    int read_failed_off;
    int div_zero_off;
} StubOffsets;

static StubOffsets gen_stubs(StubBuf *sb,
                             const IdataBuilder *idata,
                             const RtFormats *rf,
                             uint32_t text_rva,
                             uint32_t rdata_rva,
                             int user_code_len)
{
    StubOffsets so;
    sb_init(sb);

    int base = user_code_len; /* stubs start after user code in .text */

    /* ── __axis_write_i64: printf("%lld", val) ──────────── */
    so.write_i64_off = base + sb->len;
    sb_emit_mov_rdx_rcx(sb);   /* value → rdx (arg2) */
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_lld,
                    text_rva, base);   /* fmt → rcx (arg1) */
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_PRINTF],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    sb_emit_ret(sb);

    /* ── __axis_write_str: printf("%s", str) ────────────── */
    so.write_str_off = base + sb->len;
    sb_emit_mov_rdx_rcx(sb);
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_s,
                    text_rva, base);
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_PRINTF],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    sb_emit_ret(sb);

    /* ── __axis_write_bool: print "true"/"false" ────────── */
    so.write_bool_off = base + sb->len;
    /* test ecx, ecx → jz print_false */
    sb_emit8(sb, 0x85); sb_emit8(sb, 0xC9); /* test ecx, ecx */
    sb_emit8(sb, 0x74);                       /* jz +N (short) */
    /* We need to skip the "true" path. Calculate relative. */
    int jz_patch = sb->len;
    sb_emit8(sb, 0);                          /* placeholder offset */
    /* true path */
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_true,
                    text_rva, base);
    sb_emit8(sb, 0xEB);                       /* jmp short +N */
    int jmp_patch = sb->len;
    sb_emit8(sb, 0);
    /* false path */
    int false_off = sb->len;
    sb->data[jz_patch] = (uint8_t)(false_off - (jz_patch + 1));
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_false,
                    text_rva, base);
    int after_false = sb->len;
    sb->data[jmp_patch] = (uint8_t)(after_false - (jmp_patch + 1));
    /* Common: call printf */
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_PRINTF],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    sb_emit_ret(sb);

    /* ── __axis_write_char: putchar(val) ────────────────── */
    so.write_char_off = base + sb->len;
    /* RCX already has the char value */
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_PUTCHAR],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    sb_emit_ret(sb);

    /* ── __axis_write_nl: putchar('\n') ─────────────────── */
    so.write_nl_off = base + sb->len;
    /* mov ecx, 10 ('\n') */
    sb_emit8(sb, 0xB9); sb_emit32(sb, 10);
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_PUTCHAR],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    sb_emit_ret(sb);

    /* ── __axis_read_i64: scanf("%lld", &buf) → return buf ── */
    so.read_i64_off = base + sb->len;
    sb_emit_lea_rip(sb, RDX, rdata_rva, rf->fmt_buf,
                    text_rva, base);  /* &buf → rdx */
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_lld_in,
                    text_rva, base);  /* fmt → rcx */
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_SCANF],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    /* eax = scanf return (1=success). Set read_failed flag. */
    sb_emit8(sb, 0x83); sb_emit8(sb, 0xF8); sb_emit8(sb, 0x01); /* cmp eax, 1 */
    sb_emit8(sb, 0x0F); sb_emit8(sb, 0x95); sb_emit8(sb, 0xC1); /* setne cl */
    sb_emit_lea_rip(sb, R11, rdata_rva, rf->read_failed_flag,
                    text_rva, base);  /* lea r11, [rip+flag] */
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x88); sb_emit8(sb, 0x0B); /* mov byte [r11], cl */
    /* Load result: mov rax, [buf_addr] */
    sb_emit_lea_rip(sb, RAX, rdata_rva, rf->fmt_buf,
                    text_rva, base);
    /* mov rax, [rax] */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x8B); sb_emit8(sb, 0x00);
    sb_emit_ret(sb);

    /* ── __axis_read_line: fgets-like → return ptr to buffer ── */
    so.read_line_off = base + sb->len;
    /* Simple: read chars with getchar until newline, store in buf */
    /* For now: return pointer to static buffer (filled by scanf) */
    sb_emit_lea_rip(sb, RDX, rdata_rva, rf->fmt_buf,
                    text_rva, base);
    /* Use scanf("%255s", buf) – simplified */
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_s,
                    text_rva, base);
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_SCANF],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    /* eax = scanf return (1=success). Set read_failed flag. */
    sb_emit8(sb, 0x83); sb_emit8(sb, 0xF8); sb_emit8(sb, 0x01); /* cmp eax, 1 */
    sb_emit8(sb, 0x0F); sb_emit8(sb, 0x95); sb_emit8(sb, 0xC1); /* setne cl */
    sb_emit_lea_rip(sb, R11, rdata_rva, rf->read_failed_flag,
                    text_rva, base);
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x88); sb_emit8(sb, 0x0B); /* mov byte [r11], cl */
    sb_emit_lea_rip(sb, RAX, rdata_rva, rf->fmt_buf,
                    text_rva, base);
    sb_emit_ret(sb);

    /* ── __axis_read_char: getchar() ────────────────────── */
    so.read_char_off = base + sb->len;
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_GETCHAR],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    /* eax = getchar return (-1 = EOF). Set read_failed flag. */
    sb_emit8(sb, 0x83); sb_emit8(sb, 0xF8); sb_emit8(sb, 0xFF); /* cmp eax, -1 */
    sb_emit8(sb, 0x0F); sb_emit8(sb, 0x94); sb_emit8(sb, 0xC1); /* sete cl */
    sb_emit_lea_rip(sb, R11, rdata_rva, rf->read_failed_flag,
                    text_rva, base);
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x88); sb_emit8(sb, 0x0B); /* mov byte [r11], cl */
    /* Result already in eax, zero-extend to rax */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x0F); sb_emit8(sb, 0xB7);
    sb_emit8(sb, 0xC0); /* movzx rax, ax */
    sb_emit_ret(sb);

    /* ── __axis_read_failed: return the flag byte ──────── */
    so.read_failed_off = base + sb->len;
    sb_emit_lea_rip(sb, RAX, rdata_rva, rf->read_failed_flag,
                    text_rva, base);
    sb_emit8(sb, 0x0F); sb_emit8(sb, 0xB6); sb_emit8(sb, 0x00); /* movzx eax, byte [rax] */
    sb_emit_ret(sb);

    /* ── __axis_memcpy: trivial byte copy ───────────────── */
    so.memcpy_off = base + sb->len;
    /* rcx=dst, rdx=src, r8=count; use rep movsb */
    /* Push rdi, rsi */
    sb_emit8(sb, 0x57);  /* push rdi */
    sb_emit8(sb, 0x56);  /* push rsi */
    /* mov rdi, rcx; mov rsi, rdx; mov rcx, r8 */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xCF); /* mov rdi, rcx */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xD6); /* mov rsi, rdx */
    sb_emit8(sb, 0x4C); sb_emit8(sb, 0x89); sb_emit8(sb, 0xC1); /* mov rcx, r8 */
    /* rep movsb */
    sb_emit8(sb, 0xF3); sb_emit8(sb, 0xA4);
    /* Pop rsi, rdi */
    sb_emit8(sb, 0x5E);  /* pop rsi */
    sb_emit8(sb, 0x5F);  /* pop rdi */
    sb_emit_ret(sb);

    /* ── __axis_div_zero: print error message and exit(1) ──── */
    so.div_zero_off = base + sb->len;
    sb_emit_lea_rip(sb, RCX, rdata_rva, rf->fmt_div_zero,
                    text_rva, base);   /* error msg → rcx (arg1) */
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_PRINTF],
                     text_rva, base);
    sb_emit_add_rsp_40(sb);
    sb_emit8(sb, 0xB9); sb_emit8(sb, 0x01); sb_emit8(sb, 0x00);
    sb_emit8(sb, 0x00); sb_emit8(sb, 0x00); /* mov ecx, 1 */
    sb_emit_sub_rsp_40(sb);
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_EXIT],
                     text_rva, base);

    /* ── entry point stub: call __axis_top_level, then exit(0) ── */
    /* This is NOT in StubOffsets; we handle it separately. */

    return so;
}

/* ═════════════════════════════════════════════════════════════
 * Patch user code relocations to point at runtime stubs
 *
 * After stubs are generated, we know their .text offsets and
 * can resolve the remaining RELOC_REL32 entries that target
 * runtime function names.
 * ═════════════════════════════════════════════════════════════ */

static void patch_runtime_relocs(X64Ctx *x64_mut, const StubOffsets *so)
{
    /* NOTE: we take a mutable copy pointer to fix up code bytes.
     * This happens before writing to PE. */
    for (int i = 0; i < x64_mut->reloc_count; i++) {
        Reloc *r = &x64_mut->relocs[i];
        if (r->kind != RELOC_REL32 || r->target_sym == NULL) continue;

        int target = -1;
        if (strcmp(r->target_sym, "__axis_write_i64") == 0)  target = so->write_i64_off;
        else if (strcmp(r->target_sym, "__axis_write_str") == 0)  target = so->write_str_off;
        else if (strcmp(r->target_sym, "__axis_write_bool") == 0) target = so->write_bool_off;
        else if (strcmp(r->target_sym, "__axis_write_char") == 0) target = so->write_char_off;
        else if (strcmp(r->target_sym, "__axis_write_nl") == 0)   target = so->write_nl_off;
        else if (strcmp(r->target_sym, "__axis_read_i64") == 0)   target = so->read_i64_off;
        else if (strcmp(r->target_sym, "__axis_read_line") == 0)  target = so->read_line_off;
        else if (strcmp(r->target_sym, "__axis_read_char") == 0)  target = so->read_char_off;
        else if (strcmp(r->target_sym, "__axis_read_failed") == 0) target = so->read_failed_off;
        else if (strcmp(r->target_sym, "__axis_memcpy") == 0)     target = so->memcpy_off;
        else if (strcmp(r->target_sym, "__axis_div_zero") == 0)    target = so->div_zero_off;
        else continue; /* user function – should already be resolved */

        int from = r->offset + 4;
        int rel = target - from + r->addend;
        memcpy(&x64_mut->code.data[r->offset], &rel, 4);
        r->kind = (RelocKind)-1;  /* mark resolved */
    }
}

/* ═════════════════════════════════════════════════════════════
 * Also patch remaining RIP-relative string relocations
 * (RELOC_RIP_REL32) once we know rdata_rva and text_rva.
 * ═════════════════════════════════════════════════════════════ */

static void patch_string_relocs(X64Ctx *x64_mut,
                                uint32_t text_rva,
                                uint32_t rdata_rva)
{
    for (int i = 0; i < x64_mut->reloc_count; i++) {
        Reloc *r = &x64_mut->relocs[i];
        if (r->kind != RELOC_RIP_REL32) continue;

        /* target_label is string index */
        int str_idx = r->target_label;
        if (str_idx < 0 || str_idx >= x64_mut->string_count) continue;

        uint32_t str_rva = rdata_rva + (uint32_t)x64_mut->strings[str_idx].rdata_offset;
        /* RIP = text_rva + r->offset + 4 */
        uint32_t rip = text_rva + (uint32_t)r->offset + 4;
        int32_t disp = (int32_t)(str_rva - rip) + r->addend;
        memcpy(&x64_mut->code.data[r->offset], &disp, 4);
        r->kind = (RelocKind)-1;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Generate entry point stub: call __top_level then exit(0)
 * ═════════════════════════════════════════════════════════════ */

static int gen_entry_stub(StubBuf *sb, const X64Ctx *x64,
                          const IdataBuilder *idata,
                          uint32_t text_rva)
{
    /* Find entry function offset: prefer __top_level, then main, then _start,
     * finally fall back to the last function. */
    int top_off = -1;
    const char *entry_name = NULL;
    for (int i = 0; i < x64->func_count; i++) {
        if (strcmp(x64->funcs[i].name, "__top_level") == 0) {
            top_off = x64->funcs[i].text_offset;
            entry_name = x64->funcs[i].name;
            break;
        }
    }
    if (top_off < 0) {
        for (int i = 0; i < x64->func_count; i++) {
            if (strcmp(x64->funcs[i].name, "main") == 0 ||
                strcmp(x64->funcs[i].name, "_start") == 0)
            {
                top_off = x64->funcs[i].text_offset;
                entry_name = x64->funcs[i].name;
                break;
            }
        }
    }
    if (top_off < 0 && x64->func_count > 0) {
        top_off = x64->funcs[x64->func_count - 1].text_offset;
        entry_name = x64->funcs[x64->func_count - 1].name;
    }

    /* entry_stub_off = absolute .text offset where entry begins */
    int entry_stub_off = x64->code.len + sb->len;

    /* sub rsp, 40 (shadow space + alignment) */
    sb_emit_sub_rsp_40(sb);                        /* 4 bytes */
    /* call rel32 → top-level */
    sb_emit8(sb, 0xE8);                            /* 1 byte  */
    int patch_pos = sb->len;
    sb_emit32(sb, 0);                              /* 4 bytes placeholder */

    /* Patch the rel32: RIP is at entry_stub_off + 4(sub) + 1(E8) + 4(rel32) = +9 */
    if (top_off >= 0) {
        int abs_from = entry_stub_off + 9;
        int r = top_off - abs_from;
        sb->data[patch_pos + 0] = (uint8_t)(r);
        sb->data[patch_pos + 1] = (uint8_t)(r >> 8);
        sb->data[patch_pos + 2] = (uint8_t)(r >> 16);
        sb->data[patch_pos + 3] = (uint8_t)(r >> 24);
    }

    /* Set exit code: if entry is main(), use its return value (in eax);
     * otherwise (script mode) exit with 0. */
    if (entry_name && strcmp(entry_name, "main") == 0) {
        /* mov ecx, eax — main's return value becomes exit code */
        sb_emit8(sb, 0x89); sb_emit8(sb, 0xC1);
    } else {
        /* xor ecx, ecx — exit(0) */
        sb_emit8(sb, 0x31); sb_emit8(sb, 0xC9);
    }
    sb_emit_call_iat(sb, idata->iat_entry_rva[IMP_EXIT],
                     text_rva, x64->code.len);
    sb_emit8(sb, 0xCC);                            /* int3 */

    return entry_stub_off;
}

/* ═════════════════════════════════════════════════════════════
 * Main PE writer
 * ═════════════════════════════════════════════════════════════ */

int pe_write(PECtx *ctx, const X64Ctx *x64)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->x64 = x64;
    buf_init(ctx);

    /* We'll work with a mutable copy of the code buffer + relocs
     * so we can patch in-place. */
    X64Ctx x64_mut = *x64;                    /* shallow copy */
    x64_mut.code.data = (uint8_t *)malloc(x64->code.len + 4096);
    memcpy(x64_mut.code.data, x64->code.data, x64->code.len);
    x64_mut.code.len = x64->code.len;
    x64_mut.code.cap = x64->code.len + 4096;

    /* Copy rdata so we can append runtime format strings */
    int rdata_cap = x64->rdata_len + 1024;
    int rdata_len = x64->rdata_len;
    uint8_t *rdata_buf = (uint8_t *)malloc(rdata_cap);
    if (x64->rdata_len > 0)
        memcpy(rdata_buf, x64->rdata, x64->rdata_len);

    /* Copy relocs */
    x64_mut.relocs = (Reloc *)malloc(x64->reloc_cap * sizeof(Reloc));
    memcpy(x64_mut.relocs, x64->relocs, x64->reloc_count * sizeof(Reloc));
    x64_mut.reloc_count = x64->reloc_count;
    x64_mut.reloc_cap = x64->reloc_cap;

    /* Copy strings */
    x64_mut.strings = (X64String *)malloc(x64->string_count * sizeof(X64String));
    memcpy(x64_mut.strings, x64->strings, x64->string_count * sizeof(X64String));

    /* Add runtime format strings to rdata */
    RtFormats rf = add_rt_formats(&rdata_buf, &rdata_len, &rdata_cap);

    /* ── Section layout ────────────────────────────── */
    const int NUM_SECTIONS = 3;  /* .text, .rdata, .idata */

    int headers_size = (int)(sizeof(DOSHeader) +
                             4 +  /* PE signature */
                             sizeof(COFFHeader) +
                             sizeof(OptionalHeader64) +
                             NUM_SECTIONS * sizeof(SectionHeader));
    headers_size = (int)align_up(headers_size, PE_FILE_ALIGNMENT);

    ctx->text_rva  = PE_SECTION_ALIGNMENT;
    ctx->text_raw  = (uint32_t)headers_size;

    /* ── Two-pass stub generation ─────────────────────
     *
     * Stubs contain RIP-relative addresses to .rdata (format strings)
     * and .idata (IAT entries).  Computing those RVAs requires knowing
     * the total .text size, which in turn includes the stubs.
     *
     * Pass 1: generate stubs with dummy RVAs purely to measure their
     *         total byte count (which is RVA-independent).
     * Pass 2: compute the real section layout, rebuild idata, and
     *         regenerate stubs with the correct RVAs.
     * ──────────────────────────────────────────────── */

    /* --- Pass 1: measure stub size --- */
    StubBuf sb_measure;
    IdataBuilder idata_measure;
    build_idata(&idata_measure, /* dummy idata_rva */ 0x10000);
    gen_stubs(&sb_measure, &idata_measure, &rf,
              ctx->text_rva, /* dummy rdata_rva */ 0x10000,
              x64->code.len);
    gen_entry_stub(&sb_measure, x64, &idata_measure, ctx->text_rva);
    int total_text_len = x64->code.len + sb_measure.len;

    /* --- Compute real section layout using actual text size --- */
    uint32_t text_raw_size = align_up((uint32_t)total_text_len, PE_FILE_ALIGNMENT);

    ctx->rdata_rva = align_up(ctx->text_rva + (uint32_t)total_text_len,
                              PE_SECTION_ALIGNMENT);
    ctx->rdata_raw = ctx->text_raw + text_raw_size;
    ctx->rdata_size = (uint32_t)rdata_len;
    uint32_t rdata_raw_size = align_up((uint32_t)rdata_len, PE_FILE_ALIGNMENT);

    ctx->idata_rva = align_up(ctx->rdata_rva + (uint32_t)rdata_len,
                              PE_SECTION_ALIGNMENT);
    ctx->idata_raw = ctx->rdata_raw + rdata_raw_size;

    /* --- Pass 2: generate stubs with correct RVAs --- */
    IdataBuilder idata;
    build_idata(&idata, ctx->idata_rva);
    ctx->idata_size = (uint32_t)idata.len;
    ctx->iat_rva = ctx->idata_rva + (uint32_t)idata.iat_off;
    uint32_t idata_raw_size = align_up((uint32_t)idata.len, PE_FILE_ALIGNMENT);

    StubBuf sb;
    StubOffsets so = gen_stubs(&sb, &idata, &rf,
                               ctx->text_rva, ctx->rdata_rva,
                               x64->code.len);
    int entry_stub_off = gen_entry_stub(&sb, x64, &idata, ctx->text_rva);

    total_text_len = x64->code.len + sb.len;
    ctx->text_size = (uint32_t)total_text_len;
    ctx->entry_rva = ctx->text_rva + (uint32_t)entry_stub_off;

    ctx->image_size = align_up(ctx->idata_rva + ctx->idata_size,
                               PE_SECTION_ALIGNMENT);

    /* Patch relocations in user code */
    patch_runtime_relocs(&x64_mut, &so);
    patch_string_relocs(&x64_mut, ctx->text_rva, ctx->rdata_rva);

    /* ═══ Write PE file ═══ */

    /* ── DOS Header ────────────────────────────────── */
    DOSHeader dos;
    memset(&dos, 0, sizeof(dos));
    dos.e_magic  = 0x5A4D;     /* "MZ" */
    dos.e_lfanew = sizeof(DOSHeader);
    buf_write(ctx, &dos, sizeof(dos));

    /* ── PE Signature ──────────────────────────────── */
    buf_write32(ctx, 0x00004550);  /* "PE\0\0" */

    /* ── COFF Header ───────────────────────────────── */
    COFFHeader coff;
    memset(&coff, 0, sizeof(coff));
    coff.Machine              = 0x8664;    /* AMD64 */
    coff.NumberOfSections     = NUM_SECTIONS;
    coff.SizeOfOptionalHeader = sizeof(OptionalHeader64);
    coff.Characteristics      = 0x0022;    /* EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE */
    buf_write(ctx, &coff, sizeof(coff));

    /* ── Optional Header (PE32+) ───────────────────── */
    OptionalHeader64 opt;
    memset(&opt, 0, sizeof(opt));
    opt.Magic                       = 0x020B;  /* PE32+ */
    opt.MajorLinkerVersion          = 1;
    opt.SizeOfCode                  = ctx->text_size;
    opt.SizeOfInitializedData       = ctx->rdata_size + ctx->idata_size;
    opt.AddressOfEntryPoint         = ctx->entry_rva;
    opt.BaseOfCode                  = ctx->text_rva;
    opt.ImageBase                   = PE_IMAGE_BASE;
    opt.SectionAlignment            = PE_SECTION_ALIGNMENT;
    opt.FileAlignment               = PE_FILE_ALIGNMENT;
    opt.MajorOperatingSystemVersion = 6;
    opt.MinorOperatingSystemVersion = 0;
    opt.MajorSubsystemVersion       = 6;
    opt.MinorSubsystemVersion       = 0;
    opt.SizeOfImage                 = ctx->image_size;
    opt.SizeOfHeaders               = (uint32_t)headers_size;
    opt.Subsystem                   = 3;       /* IMAGE_SUBSYSTEM_WINDOWS_CUI (console) */
    opt.DllCharacteristics          = 0x8160;  /* NX_COMPAT|DYNAMIC_BASE|HIGH_ENTROPY_VA|TERMINAL_SERVER_AWARE */
    opt.SizeOfStackReserve          = 0x100000;
    opt.SizeOfStackCommit           = 0x1000;
    opt.SizeOfHeapReserve           = 0x100000;
    opt.SizeOfHeapCommit            = 0x1000;
    opt.NumberOfRvaAndSizes         = 16;

    /* Data directories */
    opt.DataDirectory[1].VirtualAddress = ctx->idata_rva;           /* Import Table */
    opt.DataDirectory[1].Size           = ctx->idata_size;
    opt.DataDirectory[12].VirtualAddress = ctx->idata_rva + (uint32_t)idata.iat_off; /* IAT */
    opt.DataDirectory[12].Size           = (IMP_COUNT + 1) * 8;
    buf_write(ctx, &opt, sizeof(opt));

    /* ── Section Headers ───────────────────────────── */
    SectionHeader sh;

    /* .text */
    memset(&sh, 0, sizeof(sh));
    memcpy(sh.Name, ".text\0\0\0", 8);
    sh.VirtualSize     = ctx->text_size;
    sh.VirtualAddress  = ctx->text_rva;
    sh.SizeOfRawData   = text_raw_size;
    sh.PointerToRawData = ctx->text_raw;
    sh.Characteristics = 0x60000020; /* CODE | EXECUTE | READ */
    buf_write(ctx, &sh, sizeof(sh));

    /* .rdata */
    memset(&sh, 0, sizeof(sh));
    memcpy(sh.Name, ".rdata\0\0", 8);
    sh.VirtualSize     = ctx->rdata_size;
    sh.VirtualAddress  = ctx->rdata_rva;
    sh.SizeOfRawData   = rdata_raw_size;
    sh.PointerToRawData = ctx->rdata_raw;
    sh.Characteristics = 0x40000040; /* INITIALIZED_DATA | READ */
    buf_write(ctx, &sh, sizeof(sh));

    /* .idata */
    memset(&sh, 0, sizeof(sh));
    memcpy(sh.Name, ".idata\0\0", 8);
    sh.VirtualSize     = ctx->idata_size;
    sh.VirtualAddress  = ctx->idata_rva;
    sh.SizeOfRawData   = idata_raw_size;
    sh.PointerToRawData = ctx->idata_raw;
    sh.Characteristics = 0xC0000040; /* INITIALIZED_DATA | READ | WRITE */
    buf_write(ctx, &sh, sizeof(sh));

    /* Pad headers to file alignment */
    buf_pad_to(ctx, headers_size);

    /* ── Section Data: .text ───────────────────────── */
    buf_pad_to(ctx, (int)ctx->text_raw);
    buf_write(ctx, x64_mut.code.data, x64_mut.code.len);
    buf_write(ctx, sb.data, sb.len);
    buf_pad_to(ctx, (int)(ctx->text_raw + text_raw_size));

    /* ── Section Data: .rdata ──────────────────────── */
    buf_pad_to(ctx, (int)ctx->rdata_raw);
    buf_write(ctx, rdata_buf, rdata_len);
    buf_pad_to(ctx, (int)(ctx->rdata_raw + rdata_raw_size));

    /* ── Section Data: .idata ──────────────────────── */
    buf_pad_to(ctx, (int)ctx->idata_raw);
    buf_write(ctx, idata.data, idata.len);
    buf_pad_to(ctx, (int)(ctx->idata_raw + idata_raw_size));

    /* ── Cleanup temp buffers ──────────────────────── */
    free(x64_mut.code.data);
    free(x64_mut.relocs);
    free(x64_mut.strings);
    free(rdata_buf);
    free(sb.data);
    free(idata.data);

    return 0;
}

int pe_save(const PECtx *ctx, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(ctx->buf, 1, ctx->len, f);
    fclose(f);
    return (written == (size_t)ctx->len) ? 0 : -1;
}

void pe_free(PECtx *ctx)
{
    free(ctx->buf);
    ctx->buf = NULL;
    ctx->len = 0;
    ctx->cap = 0;
}
