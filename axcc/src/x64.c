/*
 * x64.c – x86-64 native code generator for the AXIS compiler.
 *
 * Strategy:
 *   • Variables live at [rbp + stack_off] (stack_off is negative).
 *   • Temps live below variables: [rbp - var_area - (temp_id+1)*8].
 *   • We use RAX, RCX, RDX, R8, R9, R10, R11 as scratch.
 *   • Each IR instruction is lowered independently (no global regalloc).
 *   • Calling convention: Windows x64 (rcx, rdx, r8, r9 + shadow space).
 *   • The linker/PE-writer patches RELOC_REL32 for function calls
 *     and RELOC_RIP_REL32 for string literal references.
 *
 * Encoding reference:
 *   REX prefix = 0x40 | W(3) R(2) X(1) B(0)
 *   ModRM      = mod(7:6) reg(5:3) rm(2:0)
 *   SIB        = scale(7:6) index(5:3) base(2:0)
 */

#include "axis_x64.h"
#include <inttypes.h>
#include <stdarg.h>

/* ═════════════════════════════════════════════════════════════
 * Helpers / Forward declarations
 * ═════════════════════════════════════════════════════════════ */

static void resolve_label_relocs(X64Ctx *ctx);

_Noreturn static void x64_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "axisc: x64 codegen error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* ── Code buffer operations ──────────────────────────────── */

static void cb_init(CodeBuf *cb)
{
    cb->cap  = 4096;
    cb->len  = 0;
    cb->data = (uint8_t *)malloc(cb->cap);
    if (!cb->data) x64_error("out of memory for code buffer");
}

static void cb_grow(CodeBuf *cb, int need)
{
    while (cb->len + need > cb->cap) {
        cb->cap *= 2;
        cb->data = (uint8_t *)realloc(cb->data, cb->cap);
        if (!cb->data) x64_error("out of memory growing code buffer");
    }
}

static void cb_emit8(CodeBuf *cb, uint8_t b)
{
    cb_grow(cb, 1);
    cb->data[cb->len++] = b;
}

__attribute__((unused))
static void cb_emit16(CodeBuf *cb, uint16_t v)
{
    cb_grow(cb, 2);
    memcpy(&cb->data[cb->len], &v, 2);
    cb->len += 2;
}

static void cb_emit32(CodeBuf *cb, uint32_t v)
{
    cb_grow(cb, 4);
    memcpy(&cb->data[cb->len], &v, 4);
    cb->len += 4;
}

static void cb_emit64(CodeBuf *cb, uint64_t v)
{
    cb_grow(cb, 8);
    memcpy(&cb->data[cb->len], &v, 8);
    cb->len += 8;
}

static int cb_pos(const CodeBuf *cb) { return cb->len; }

/* Write a 32-bit value at a specific offset (for patching). */
static void cb_patch32(CodeBuf *cb, int offset, uint32_t v)
{
    assert(offset >= 0 && offset + 4 <= cb->len);
    memcpy(&cb->data[offset], &v, 4);
}

/* ── Relocations ─────────────────────────────────────────── */

static void add_reloc(X64Ctx *ctx, RelocKind kind, int offset,
                      const char *sym, int label, int addend)
{
    if (ctx->reloc_count >= ctx->reloc_cap) {
        ctx->reloc_cap = ctx->reloc_cap ? ctx->reloc_cap * 2 : 64;
        ctx->relocs = (Reloc *)realloc(ctx->relocs,
                                       ctx->reloc_cap * sizeof(Reloc));
    }
    Reloc *r       = &ctx->relocs[ctx->reloc_count++];
    r->kind        = kind;
    r->offset      = offset;
    r->target_sym  = sym;
    r->target_label = label;
    r->addend      = addend;
}

/* ── Label management ────────────────────────────────────── */

static void ensure_label(X64Ctx *ctx, int id)
{
    if (id >= ctx->label_cap) {
        int old = ctx->label_cap;
        ctx->label_cap = (id + 64) & ~63;
        ctx->label_offsets = (int *)realloc(ctx->label_offsets,
                                            ctx->label_cap * sizeof(int));
        for (int i = old; i < ctx->label_cap; i++)
            ctx->label_offsets[i] = -1;
    }
}

static void set_label(X64Ctx *ctx, int id, int offset)
{
    ensure_label(ctx, id);
    ctx->label_offsets[id] = offset;
}

static int get_label(X64Ctx *ctx, int id)
{
    ensure_label(ctx, id);
    return ctx->label_offsets[id];
}

/* ═════════════════════════════════════════════════════════════
 * x86-64 register encoding
 * ═════════════════════════════════════════════════════════════ */

enum {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

/* REX prefix builder */
static uint8_t rex(int w, int r, int x, int b)
{
    return (uint8_t)(0x40 | (w ? 8 : 0) | (r > 7 ? 4 : 0) |
                     (x > 7 ? 2 : 0) | (b > 7 ? 1 : 0));
}

/* ModRM byte builder */
static uint8_t modrm(int mod, int reg, int rm)
{
    return (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* ═════════════════════════════════════════════════════════════
 * Stack slot helpers
 *
 * Variables occupy [rbp + stack_off] where stack_off is negative.
 * Temps live below the variable area:
 *   temp 0 → [rbp - var_area - 8], temp 1 → [rbp - var_area - 16], etc.
 * ═════════════════════════════════════════════════════════════ */

static int temp_rbp_off(const X64Ctx *ctx, int temp_id)
{
    return -(ctx->var_area_size + (temp_id + 1) * 8);
}

/* ═════════════════════════════════════════════════════════════
 * Instruction emission helpers (commonly used patterns)
 * ═════════════════════════════════════════════════════════════ */

/*
 * emit_mov_reg_reg(cb, dst, src)  –  mov dst, src  (64-bit)
 * Encoding: REX.W + 89 /r  (mov r/m64, r64)
 */
static void emit_mov_reg_reg(CodeBuf *cb, int dst, int src)
{
    cb_emit8(cb, rex(1, src, 0, dst));
    cb_emit8(cb, 0x89);
    cb_emit8(cb, modrm(3, src, dst));
}

/*
 * emit_mov_reg_imm64(cb, reg, val)  –  movabs reg, imm64
 * Encoding: REX.W + B8+rd  imm64
 */
static void emit_mov_reg_imm64(CodeBuf *cb, int reg, int64_t val)
{
    cb_emit8(cb, rex(1, 0, 0, reg));
    cb_emit8(cb, (uint8_t)(0xB8 + (reg & 7)));
    cb_emit64(cb, (uint64_t)val);
}

/*
 * emit_mov_reg_imm32(cb, reg, val)  –  mov reg, imm32 (zero-extend)
 * Encoding: B8+rd imm32  (no REX.W → 32-bit op, upper 32 bits zeroed)
 */
static void emit_mov_reg_imm32(CodeBuf *cb, int reg, int32_t val)
{
    if (reg >= 8)
        cb_emit8(cb, (uint8_t)(0x41));  /* REX.B for extended register */
    cb_emit8(cb, (uint8_t)(0xB8 + (reg & 7)));
    cb_emit32(cb, (uint32_t)val);
}

/*
 * emit_load_imm(cb, reg, val) – load immediate, shortest encoding
 */
static void emit_load_imm(CodeBuf *cb, int reg, int64_t val)
{
    if (val == 0) {
        /* xor reg32, reg32 (clears upper 32 bits too) */
        if (reg >= 8)
            cb_emit8(cb, rex(0, reg, 0, reg));
        cb_emit8(cb, 0x31);
        cb_emit8(cb, modrm(3, reg, reg));
    } else if (val > 0 && val <= 0x7FFFFFFF) {
        emit_mov_reg_imm32(cb, reg, (int32_t)val);
    } else {
        emit_mov_reg_imm64(cb, reg, val);
    }
}

/*
 * emit_load_rbp_off(cb, dst_reg, offset)
 *   mov dst_reg, [rbp + offset]   (64-bit load)
 *
 * offset is always negative for our usage.
 * Uses disp32 encoding with RBP base.
 */
static void emit_load_rbp(CodeBuf *cb, int dst, int off)
{
    cb_emit8(cb, rex(1, dst, 0, RBP));
    cb_emit8(cb, 0x8B);                    /* mov r64, r/m64 */
    cb_emit8(cb, modrm(2, dst, RBP));      /* mod=10 (disp32), rm=rbp */
    cb_emit32(cb, (uint32_t)off);
}

/*
 * emit_store_rbp_off(cb, offset, src_reg)
 *   mov [rbp + offset], src_reg   (64-bit store)
 */
static void emit_store_rbp(CodeBuf *cb, int off, int src)
{
    cb_emit8(cb, rex(1, src, 0, RBP));
    cb_emit8(cb, 0x89);                    /* mov r/m64, r64 */
    cb_emit8(cb, modrm(2, src, RBP));
    cb_emit32(cb, (uint32_t)off);
}

/*
 * Width-aware sign-extending load from [rbp + off] into a 64-bit register.
 * For i8:  movsx  rax, byte  [rbp+off]   (REX.W 0F BE)
 * For i16: movsx  rax, word  [rbp+off]   (REX.W 0F BF)
 * For i32: movsxd rax, dword [rbp+off]   (REX.W 63)
 * For i64: mov    rax, qword [rbp+off]   (existing 64-bit load)
 */
static void emit_load_rbp_sx(CodeBuf *cb, int dst, int off, int size)
{
    switch (size) {
    case 1:
        cb_emit8(cb, rex(1, dst, 0, RBP));  /* REX.W */
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xBE);                  /* movsx r64, r/m8 */
        cb_emit8(cb, modrm(2, dst, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    case 2:
        cb_emit8(cb, rex(1, dst, 0, RBP));  /* REX.W */
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xBF);                  /* movsx r64, r/m16 */
        cb_emit8(cb, modrm(2, dst, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    case 4:
        cb_emit8(cb, rex(1, dst, 0, RBP));  /* REX.W */
        cb_emit8(cb, 0x63);                  /* movsxd r64, r/m32 */
        cb_emit8(cb, modrm(2, dst, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    default: /* 8 or unknown */
        emit_load_rbp(cb, dst, off);
        break;
    }
}

/* Zero-extending load from [rbp+off] (for unsigned types u8, u16, u32). */
static void emit_load_rbp_zx(CodeBuf *cb, int dst, int off, int size)
{
    switch (size) {
    case 1:
        /* movzx r32, byte [rbp+off]  — no REX.W so result zero-extends to r64 */
        if (dst >= 8 || RBP >= 8)
            cb_emit8(cb, rex(0, dst, 0, RBP));
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xB6);
        cb_emit8(cb, modrm(2, dst, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    case 2:
        /* movzx r32, word [rbp+off] */
        if (dst >= 8 || RBP >= 8)
            cb_emit8(cb, rex(0, dst, 0, RBP));
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xB7);
        cb_emit8(cb, modrm(2, dst, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    case 4:
        /* mov r32, dword [rbp+off] — writing to r32 auto-zero-extends to r64 */
        if (dst >= 8 || RBP >= 8)
            cb_emit8(cb, rex(0, dst, 0, RBP));
        cb_emit8(cb, 0x8B);
        cb_emit8(cb, modrm(2, dst, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    default:
        emit_load_rbp(cb, dst, off);
        break;
    }
}

/*
 * Width-aware store: write only the appropriate number of bytes.
 * For i8:  mov byte  [rbp+off], al    (88, no REX.W)
 * For i16: mov word  [rbp+off], ax    (66 89, no REX.W)
 * For i32: mov dword [rbp+off], eax   (89, no REX.W)
 * For i64: mov qword [rbp+off], rax   (existing 64-bit store)
 */
static void emit_store_rbp_sz(CodeBuf *cb, int off, int src, int size)
{
    switch (size) {
    case 1:
        if (src >= 4) /* need REX for spl/bpl/sil/dil or r8b-r15b */
            cb_emit8(cb, rex(0, src, 0, RBP));
        else {
            /* For al/cl/dl/bl we still need REX if RBP is base (for mod=10) */
            /* Actually RBP base doesn't need REX.B; but emit it for safety */
            cb_emit8(cb, rex(0, src, 0, RBP));
        }
        cb_emit8(cb, 0x88);                  /* mov r/m8, r8 */
        cb_emit8(cb, modrm(2, src, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    case 2:
        cb_emit8(cb, 0x66);                  /* operand size prefix → 16-bit */
        if (src >= 8)
            cb_emit8(cb, rex(0, src, 0, RBP));
        else
            cb_emit8(cb, rex(0, src, 0, RBP));
        cb_emit8(cb, 0x89);                  /* mov r/m16, r16 */
        cb_emit8(cb, modrm(2, src, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    case 4:
        /* No REX.W → 32-bit operation */
        if (src >= 8)
            cb_emit8(cb, rex(0, src, 0, RBP));
        cb_emit8(cb, 0x89);                  /* mov r/m32, r32 */
        cb_emit8(cb, modrm(2, src, RBP));
        cb_emit32(cb, (uint32_t)off);
        break;
    default: /* 8 or unknown */
        emit_store_rbp(cb, off, src);
        break;
    }
}

/*
 * Load an IR operand's value into a register.
 */
static void load_oper(X64Ctx *ctx, int reg, const IROper *op)
{
    CodeBuf *cb = &ctx->code;
    switch (op->kind) {
    case OPER_TEMP:
        emit_load_rbp(cb, reg, temp_rbp_off(ctx, op->temp_id));
        break;
    case OPER_IMM:
        emit_load_imm(cb, reg, op->imm);
        break;
    case OPER_STACK:
        emit_load_rbp_sx(cb, reg, op->stack_off, op->size);
        break;
    case OPER_NONE:
        /* load 0 */
        emit_load_imm(cb, reg, 0);
        break;
    default:
        x64_error("load_oper: unsupported operand kind %d", op->kind);
    }
}

/*
 * Store a register value to an IR temp's stack slot.
 */
static void store_temp(X64Ctx *ctx, int temp_id, int reg)
{
    emit_store_rbp(&ctx->code, temp_rbp_off(ctx, temp_id), reg);
}

/* ── ALU helpers (reg = reg OP reg) ──────────────────────── */

/* Two-operand ALU: op rax, rcx (64-bit) */
static void emit_alu_rr(CodeBuf *cb, uint8_t opcode, int dst, int src)
{
    cb_emit8(cb, rex(1, src, 0, dst));
    cb_emit8(cb, opcode);
    cb_emit8(cb, modrm(3, src, dst));
}

/* neg rax (64-bit) */
static void emit_neg_reg(CodeBuf *cb, int reg)
{
    cb_emit8(cb, rex(1, 0, 0, reg));
    cb_emit8(cb, 0xF7);
    cb_emit8(cb, modrm(3, 3, reg));     /* /3 = neg */
}

/* not rax (64-bit) */
__attribute__((unused))
static void emit_not_reg(CodeBuf *cb, int reg)
{
    cb_emit8(cb, rex(1, 0, 0, reg));
    cb_emit8(cb, 0xF7);
    cb_emit8(cb, modrm(3, 2, reg));     /* /2 = not */
}

/* cmp rax, rcx (64-bit) */
static void emit_cmp_rr(CodeBuf *cb, int a, int b)
{
    emit_alu_rr(cb, 0x39, a, b);   /* cmp r/m64, r64 */
}

/* test reg, reg (64-bit) */
static void emit_test_rr(CodeBuf *cb, int a, int b)
{
    cb_emit8(cb, rex(1, b, 0, a));
    cb_emit8(cb, 0x85);
    cb_emit8(cb, modrm(3, b, a));
}

/* setCC al */
static void emit_setcc(CodeBuf *cb, uint8_t cc)
{
    cb_emit8(cb, 0x0F);
    cb_emit8(cb, (uint8_t)(0x90 + cc));
    cb_emit8(cb, modrm(3, 0, RAX));     /* ModRM for al */
}

/* movzx rax, al */
static void emit_movzx_rax_al(CodeBuf *cb)
{
    cb_emit8(cb, rex(1, RAX, 0, RAX));
    cb_emit8(cb, 0x0F);
    cb_emit8(cb, 0xB6);
    cb_emit8(cb, modrm(3, RAX, RAX));
}

/*
 * emit_jmp_rel32(cb) – jmp rel32; returns offset of the rel32 for patching
 */
static int emit_jmp_rel32(CodeBuf *cb)
{
    cb_emit8(cb, 0xE9);
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);  /* placeholder */
    return patch;
}

/*
 * emit_jcc_rel32(cb, cc) – jCC rel32; returns offset of rel32
 */
static int emit_jcc_rel32(CodeBuf *cb, uint8_t cc)
{
    cb_emit8(cb, 0x0F);
    cb_emit8(cb, (uint8_t)(0x80 + cc));
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);
    return patch;
}

/* Patch a rel32 at 'patch_offset' to jump to current position. */
__attribute__((unused))
static void patch_jmp(CodeBuf *cb, int patch_offset)
{
    int target = cb_pos(cb);
    int rel = target - (patch_offset + 4);  /* rel is from end of imm32 */
    cb_patch32(cb, patch_offset, (uint32_t)rel);
}

/* ── PUSH / POP ──────────────────────────────────────────── */

static void emit_push(CodeBuf *cb, int reg)
{
    if (reg >= 8) cb_emit8(cb, (uint8_t)(0x41));
    cb_emit8(cb, (uint8_t)(0x50 + (reg & 7)));
}

static void emit_pop(CodeBuf *cb, int reg)
{
    if (reg >= 8) cb_emit8(cb, (uint8_t)(0x41));
    cb_emit8(cb, (uint8_t)(0x58 + (reg & 7)));
}

/* ── RET ─────────────────────────────────────────────────── */

static void emit_ret(CodeBuf *cb)
{
    cb_emit8(cb, 0xC3);
}

/* ── CALL rel32 ──────────────────────────────────────────── */

static int emit_call_rel32(CodeBuf *cb)
{
    cb_emit8(cb, 0xE8);
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);
    return patch;
}

/* ── sub rsp, imm32 / add rsp, imm32 ───────────────────── */

static void emit_sub_rsp_imm32(CodeBuf *cb, int32_t val)
{
    cb_emit8(cb, rex(1, 0, 0, RSP));
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 5, RSP));    /* /5 = sub */
    cb_emit32(cb, (uint32_t)val);
}

static void emit_add_rsp_imm32(CodeBuf *cb, int32_t val)
{
    cb_emit8(cb, rex(1, 0, 0, RSP));
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 0, RSP));    /* /0 = add */
    cb_emit32(cb, (uint32_t)val);
}

/* ── LEA reg, [rbp + disp32] ────────────────────────────── */

static void emit_lea_rbp(CodeBuf *cb, int dst, int off)
{
    cb_emit8(cb, rex(1, dst, 0, RBP));
    cb_emit8(cb, 0x8D);
    cb_emit8(cb, modrm(2, dst, RBP));
    cb_emit32(cb, (uint32_t)off);
}

/* ── Shift: sal/shr reg, cl ─────────────────────────────── */

static void emit_shift_cl(CodeBuf *cb, int reg, uint8_t ext)
{
    /* REX.W + D3 /ext  → shift r64 by CL */
    cb_emit8(cb, rex(1, 0, 0, reg));
    cb_emit8(cb, 0xD3);
    cb_emit8(cb, modrm(3, ext, reg));
}

/* ── imul rax, rcx (signed 64-bit multiply) ─────────────── */

static void emit_imul_rr(CodeBuf *cb, int dst, int src)
{
    cb_emit8(cb, rex(1, dst, 0, src));
    cb_emit8(cb, 0x0F);
    cb_emit8(cb, 0xAF);
    cb_emit8(cb, modrm(3, dst, src));
}

/* ── idiv rcx (signed 64-bit divide: rdx:rax / rcx) ────── */

static void emit_cqo(CodeBuf *cb)
{
    cb_emit8(cb, rex(1, 0, 0, 0));  /* REX.W */
    cb_emit8(cb, 0x99);             /* CQO: sign-extend rax → rdx:rax */
}

static void emit_idiv_reg(CodeBuf *cb, int reg)
{
    cb_emit8(cb, rex(1, 0, 0, reg));
    cb_emit8(cb, 0xF7);
    cb_emit8(cb, modrm(3, 7, reg)); /* /7 = idiv */
}

/* ── mov [reg + disp32], src (64-bit) ───────────────────── */

static void emit_store_mem(CodeBuf *cb, int base, int32_t disp, int src)
{
    cb_emit8(cb, rex(1, src, 0, base));
    cb_emit8(cb, 0x89);
    cb_emit8(cb, modrm(2, src, base));
    if ((base & 7) == RSP) cb_emit8(cb, 0x24); /* SIB for RSP base */
    cb_emit32(cb, (uint32_t)disp);
}

/* ── mov dst, [reg + disp32] (64-bit) ───────────────────── */

static void emit_load_mem(CodeBuf *cb, int dst, int base, int32_t disp)
{
    cb_emit8(cb, rex(1, dst, 0, base));
    cb_emit8(cb, 0x8B);
    cb_emit8(cb, modrm(2, dst, base));
    if ((base & 7) == RSP) cb_emit8(cb, 0x24); /* SIB for RSP base */
    cb_emit32(cb, (uint32_t)disp);
}

/* ═════════════════════════════════════════════════════════════
 * rdata section builder (string literals)
 * ═════════════════════════════════════════════════════════════ */

static void rdata_init(X64Ctx *ctx)
{
    ctx->rdata_cap = 1024;
    ctx->rdata_len = 0;
    ctx->rdata = (uint8_t *)malloc(ctx->rdata_cap);
}

static int rdata_add_string(X64Ctx *ctx, const char *s)
{
    int slen = (int)strlen(s) + 1;  /* include NUL */
    while (ctx->rdata_len + slen > ctx->rdata_cap) {
        ctx->rdata_cap *= 2;
        ctx->rdata = (uint8_t *)realloc(ctx->rdata, ctx->rdata_cap);
    }
    int off = ctx->rdata_len;
    memcpy(&ctx->rdata[ctx->rdata_len], s, slen);
    ctx->rdata_len += slen;
    return off;
}

/* ═════════════════════════════════════════════════════════════
 * Per-function code generation
 * ═════════════════════════════════════════════════════════════ */

/*
 * Windows x64 calling convention:
 *   args 1-4 in RCX, RDX, R8, R9
 *   caller must allocate 32 bytes shadow space
 *   stack 16-byte aligned before CALL
 *
 * Our frame layout (callee):
 *   [rbp+16]   = return address (pushed by call)
 *   [rbp+8]    = saved rbp
 *   [rbp]      = ...
 *   [rbp-8]    = temp 0
 *   [rbp-16]   = temp 1
 *   ...
 *   [rbp-N]    = last temp / local variable area
 *
 * We compute frame_size = max(temp_count * 8, stack_size) aligned to 16.
 * On entry: push rbp; mov rbp, rsp; sub rsp, frame_size
 * On exit:  mov rsp, rbp; pop rbp; ret
 */

/* Arg registers for Windows x64 calling convention */
static const int win64_arg_regs[4] = { RCX, RDX, R8, R9 };

static void gen_function(X64Ctx *ctx, const IRFunc *fn);

/* Forward declare runtime helpers we'll emit calls to */
static const char *RT_WRITE_I64   = "__axis_write_i64";
static const char *RT_WRITE_STR   = "__axis_write_str";
static const char *RT_WRITE_BOOL  = "__axis_write_bool";
static const char *RT_WRITE_CHAR  = "__axis_write_char";
static const char *RT_WRITE_NL    = "__axis_write_nl";
static const char *RT_READ_I64    = "__axis_read_i64";
static const char *RT_READ_LINE   = "__axis_read_line";
static const char *RT_READ_CHAR   = "__axis_read_char";
static const char *RT_READ_FAILED = "__axis_read_failed";
static const char *RT_MEMCPY      = "__axis_memcpy";

/* ── Emit a call to a named function (relocation-based) ── */

static void emit_call_sym(X64Ctx *ctx, const char *name)
{
    int patch = emit_call_rel32(&ctx->code);
    add_reloc(ctx, RELOC_REL32, patch, name, 0, 0);
}

/* ── Emit a LEA for a string literal (RIP-relative) ────── */

static void emit_lea_string(X64Ctx *ctx, int reg, int str_idx)
{
    CodeBuf *cb = &ctx->code;
    /* lea reg, [rip + disp32] */
    cb_emit8(cb, rex(1, reg, 0, 0));
    cb_emit8(cb, 0x8D);
    cb_emit8(cb, modrm(0, reg, 5));    /* mod=00, rm=5 → RIP+disp32 */
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);                  /* placeholder disp32 */

    /* Relocation: patch this to point to strings[str_idx] in .rdata */
    add_reloc(ctx, RELOC_RIP_REL32, patch, NULL, str_idx, 0);
}

/* ═════════════════════════════════════════════════════════════
 * IR instruction → x86-64 lowering
 * ═════════════════════════════════════════════════════════════ */

static void gen_instr(X64Ctx *ctx, const IRFunc *fn, const IRInstr *ins)
{
    CodeBuf *cb = &ctx->code;

    switch (ins->op) {

    /* ── NOP ─────────────────────────────────────────────── */
    case IR_NOP:
        cb_emit8(cb, 0x90);
        break;

    /* ── MOV: dest = src1 ────────────────────────────────── */
    case IR_MOV:
        load_oper(ctx, RAX, &ins->src1);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        else if (ins->dest.kind == OPER_STACK)
            emit_store_rbp(cb, ins->dest.stack_off, RAX);
        break;

    /* ── LOAD_IMM: dest = imm ────────────────────────────── */
    case IR_LOAD_IMM:
        emit_load_imm(cb, RAX, ins->src1.imm);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── LOAD_STR: dest = &string[idx] ───────────────────── */
    case IR_LOAD_STR:
        emit_lea_string(ctx, RAX, ins->src1.str_idx);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── LOAD_VAR: dest = [rbp + stack_off] ─────────────── */
    case IR_LOAD_VAR:
        if (ins->extra)   /* unsigned → zero-extend */
            emit_load_rbp_zx(cb, RAX, ins->src1.stack_off, ins->src1.size);
        else              /* signed   → sign-extend */
            emit_load_rbp_sx(cb, RAX, ins->src1.stack_off, ins->src1.size);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── STORE_VAR: [rbp + stack_off] = src1 ────────────── */
    case IR_STORE_VAR:
        load_oper(ctx, RAX, &ins->src1);
        emit_store_rbp_sz(cb, ins->dest.stack_off, RAX, ins->dest.size);
        break;

    /* ── Arithmetic ──────────────────────────────────────── */
    case IR_ADD:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_alu_rr(cb, 0x01, RAX, RCX);  /* add rax, rcx */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_SUB:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_alu_rr(cb, 0x29, RAX, RCX);  /* sub rax, rcx */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_MUL:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_imul_rr(cb, RAX, RCX);        /* imul rax, rcx */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_DIV:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        if (ins->extra) {
            emit_alu_rr(cb, 0x31, RDX, RDX); /* xor rdx, rdx */
            cb_emit8(cb, rex(1, 0, 0, RCX));
            cb_emit8(cb, 0xF7);
            cb_emit8(cb, modrm(3, 6, RCX));   /* div rcx */
        } else {
            emit_cqo(cb);
            emit_idiv_reg(cb, RCX);
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_MOD:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        if (ins->extra) {
            emit_alu_rr(cb, 0x31, RDX, RDX); /* xor rdx, rdx */
            cb_emit8(cb, rex(1, 0, 0, RCX));
            cb_emit8(cb, 0xF7);
            cb_emit8(cb, modrm(3, 6, RCX));   /* div rcx */
        } else {
            emit_cqo(cb);
            emit_idiv_reg(cb, RCX);
        }
        emit_mov_reg_reg(cb, RAX, RDX);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_NEG:
        load_oper(ctx, RAX, &ins->src1);
        emit_neg_reg(cb, RAX);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── Bitwise ─────────────────────────────────────────── */
    case IR_BIT_AND:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_alu_rr(cb, 0x21, RAX, RCX);  /* and rax, rcx */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_BIT_OR:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_alu_rr(cb, 0x09, RAX, RCX);  /* or rax, rcx */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_BIT_XOR:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_alu_rr(cb, 0x31, RAX, RCX);  /* xor rax, rcx */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_SHL:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);   /* shift count must be in CL */
        emit_shift_cl(cb, RAX, 4);         /* sal rax, cl (/4) */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    case IR_SHR:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_shift_cl(cb, RAX, ins->extra ? 5 : 7); /* shr or sar */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── Comparison ──────────────────────────────────────── */
    case IR_CMP_EQ:
    case IR_CMP_NE:
    case IR_CMP_LT:
    case IR_CMP_LE:
    case IR_CMP_GT:
    case IR_CMP_GE: {
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_cmp_rr(cb, RAX, RCX);
        /* Map opcode to SETcc condition code */
        uint8_t cc;
        if (ins->extra) {
            /* unsigned condition codes */
            switch (ins->op) {
            case IR_CMP_EQ: cc = 0x04; break; /* sete   */
            case IR_CMP_NE: cc = 0x05; break; /* setne  */
            case IR_CMP_LT: cc = 0x02; break; /* setb   */
            case IR_CMP_LE: cc = 0x06; break; /* setbe  */
            case IR_CMP_GT: cc = 0x07; break; /* seta   */
            case IR_CMP_GE: cc = 0x03; break; /* setae  */
            default: cc = 0x04; break;
            }
        } else {
            switch (ins->op) {
            case IR_CMP_EQ: cc = 0x04; break; /* sete  (ZF=1)  */
            case IR_CMP_NE: cc = 0x05; break; /* setne (ZF=0)  */
            case IR_CMP_LT: cc = 0x0C; break; /* setl  (SF≠OF) */
            case IR_CMP_LE: cc = 0x0E; break; /* setle         */
            case IR_CMP_GT: cc = 0x0F; break; /* setg          */
            case IR_CMP_GE: cc = 0x0D; break; /* setge         */
            default: cc = 0x04; break;
            }
        }
        emit_setcc(cb, cc);
        emit_movzx_rax_al(cb);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;
    }

    /* ── Logical NOT ─────────────────────────────────────── */
    case IR_LOG_NOT:
        load_oper(ctx, RAX, &ins->src1);
        emit_test_rr(cb, RAX, RAX);
        emit_setcc(cb, 0x04);              /* sete al (ZF=1 when RAX==0) */
        emit_movzx_rax_al(cb);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── Labels ──────────────────────────────────────────── */
    case IR_LABEL:
        set_label(ctx, ins->dest.label_id, cb_pos(cb));
        break;

    /* ── Unconditional jump ──────────────────────────────── */
    case IR_JMP: {
        int lbl = ins->dest.label_id;
        int target = get_label(ctx, lbl);
        if (target >= 0) {
            /* Backward jump – target known */
            cb_emit8(cb, 0xE9);
            int from = cb_pos(cb) + 4;
            cb_emit32(cb, (uint32_t)(target - from));
        } else {
            /* Forward jump – need to patch later */
            int patch = emit_jmp_rel32(cb);
            /* Store patch info associated with label */
            add_reloc(ctx, RELOC_REL32, patch, NULL, lbl, 0);
        }
        break;
    }

    /* ── Conditional jumps ───────────────────────────────── */
    case IR_JZ:
    case IR_JNZ: {
        load_oper(ctx, RAX, &ins->src1);
        emit_test_rr(cb, RAX, RAX);
        uint8_t cc = (ins->op == IR_JZ) ? 0x04 : 0x05; /* je / jne */
        int lbl = ins->dest.label_id;
        int target = get_label(ctx, lbl);
        if (target >= 0) {
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, (uint8_t)(0x80 + cc));
            int from = cb_pos(cb) + 4;
            cb_emit32(cb, (uint32_t)(target - from));
        } else {
            int patch = emit_jcc_rel32(cb, cc);
            add_reloc(ctx, RELOC_REL32, patch, NULL, lbl, 0);
        }
        break;
    }

    /* ── ARG: handled by IR_CALL below ──────────────────── */
    case IR_ARG:
        /* IR_ARGs are processed directly by the IR_CALL handler
         * which scans backwards to find them.  Nothing to emit here. */
        break;

    /* ── CALL ────────────────────────────────────────────── */
    case IR_CALL: {
        int nargs = (int)ins->src2.imm;
        int shadow = 32;
        int extra_args = (nargs > 4) ? (nargs - 4) * 8 : 0;
        int alloc = AXIS_ALIGN(shadow + extra_args, 16);

        /* Allocate shadow + stack-arg space FIRST so that stores
         * to [rsp+32..] land in the correct caller-frame area. */
        emit_sub_rsp_imm32(cb, alloc);

        /* Walk backwards through the preceding IR_ARG instructions
         * and emit register loads / stack stores now. */
        {
            /* Find index of this IR_CALL in the function's instruction array */
            int call_idx = (int)(ins - fn->instrs);
            for (int a = call_idx - 1; a >= 0 && fn->instrs[a].op == IR_ARG; a--) {
                const IRInstr *ai = &fn->instrs[a];
                int arg_idx = (int)ai->dest.imm;
                load_oper(ctx, RAX, &ai->src1);
                if (arg_idx < 4) {
                    emit_mov_reg_reg(cb, win64_arg_regs[arg_idx], RAX);
                } else {
                    /* Store at [rsp + arg_idx*8] inside the allocated area */
                    emit_store_mem(cb, RSP, (int32_t)(arg_idx * 8), RAX);
                }
            }
        }

        /* Emit call */
        if (ins->src1.kind == OPER_FUNC) {
            emit_call_sym(ctx, ins->src1.func_name);
        } else {
            x64_error("IR_CALL with non-function operand");
        }

        emit_add_rsp_imm32(cb, alloc);

        /* Result in RAX → store to dest */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;
    }

    /* ── RET ─────────────────────────────────────────────── */
    case IR_RET:
        load_oper(ctx, RAX, &ins->src1);
        /* Epilogue */
        emit_mov_reg_reg(cb, RSP, RBP);
        emit_pop(cb, RBP);
        emit_ret(cb);
        break;

    case IR_RET_VOID:
        emit_mov_reg_reg(cb, RSP, RBP);
        emit_pop(cb, RBP);
        emit_ret(cb);
        break;

    /* ── WRITE (built-in I/O) ────────────────────────────── */
    case IR_WRITE: {
        /* src1 = value to write, dest.imm = newline flag
         * extra = write-type hint (0=int, 1=str, 2=bool, 3=char) */
        load_oper(ctx, RAX, &ins->src1);

        /* Prepare argument: value in RCX (first arg, Win64) */
        emit_mov_reg_reg(cb, RCX, RAX);

        /* Shadow space */
        emit_sub_rsp_imm32(cb, 32);

        switch (ins->extra) {
        case 1:  emit_call_sym(ctx, RT_WRITE_STR);  break;
        case 2:  emit_call_sym(ctx, RT_WRITE_BOOL); break;
        case 3:  emit_call_sym(ctx, RT_WRITE_CHAR); break;
        default: emit_call_sym(ctx, RT_WRITE_I64);  break;
        }

        /* Newline? */
        if (ins->dest.imm) {
            emit_call_sym(ctx, RT_WRITE_NL);
        }

        emit_add_rsp_imm32(cb, 32);
        break;
    }

    /* ── READ (built-in I/O) ─────────────────────────────── */
    case IR_READ: {
        /* dest = result temp, src1.imm = read kind */
        emit_sub_rsp_imm32(cb, 32);

        switch ((int)ins->src1.imm) {
        case 1:  emit_call_sym(ctx, RT_READ_LINE);   break; /* readln */
        case 2:  emit_call_sym(ctx, RT_READ_CHAR);   break; /* readchar */
        case 3:  emit_call_sym(ctx, RT_READ_FAILED); break; /* read_failed */
        default: emit_call_sym(ctx, RT_READ_I64);    break; /* read */
        }

        emit_add_rsp_imm32(cb, 32);

        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;
    }

    /* ── INDEX_LOAD: dest = base[idx] ────────────────────── */
    case IR_INDEX_LOAD: {
        /* dest = result, src1 = base (stack off), src2 = index
         * extra = element size | 0x100 if unsigned */
        bool is_unsig = (ins->extra & 0x100) != 0;
        int esz = (ins->extra & 0xFF) ? (ins->extra & 0xFF) : 8;

        /* Load index into RCX */
        load_oper(ctx, RCX, &ins->src2);

        /* Multiply index by element size → RCX */
        emit_load_imm(cb, RDX, esz);
        emit_imul_rr(cb, RCX, RDX);

        /* LEA base address → RAX (stack_off is already negative) */
        if (ins->src1.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->src1.stack_off);
        else
            load_oper(ctx, RAX, &ins->src1);

        /* Add offset (array grows upward in our layout) */
        emit_alu_rr(cb, 0x01, RAX, RCX);  /* add rax, rcx */

        /* Width-aware load from [rax] */
        if (esz == 1) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, is_unsig ? 0xB6 : 0xBE);  /* movzx / movsx byte */
            cb_emit8(cb, modrm(0, RAX, RAX));
        } else if (esz == 2) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, is_unsig ? 0xB7 : 0xBF);  /* movzx / movsx word */
            cb_emit8(cb, modrm(0, RAX, RAX));
        } else if (esz <= 4) {
            if (is_unsig) {
                /* mov eax, [rax] — auto zero-extends to rax */
                cb_emit8(cb, 0x8B);
                cb_emit8(cb, modrm(0, RAX, RAX));
            } else {
                /* movsxd rax, dword [rax] */
                cb_emit8(cb, rex(1, RAX, 0, RAX));
                cb_emit8(cb, 0x63);
                cb_emit8(cb, modrm(0, RAX, RAX));
            }
        } else {
            /* mov rax, [rax] */
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x8B);
            cb_emit8(cb, modrm(0, RAX, RAX));
        }

        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;
    }

    /* ── INDEX_STORE: base[idx] = val ────────────────────── */
    case IR_INDEX_STORE: {
        /* dest = base (stack off), src1 = index, src2 = value
         * extra = element size */
        int esz = ins->extra ? ins->extra : 8;

        load_oper(ctx, RCX, &ins->src1);   /* index */
        emit_load_imm(cb, RDX, esz);
        emit_imul_rr(cb, RCX, RDX);         /* RCX = index * esz */

        /* LEA base address → RAX (stack_off is already negative) */
        if (ins->dest.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->dest.stack_off);
        else
            load_oper(ctx, RAX, &ins->dest);

        emit_alu_rr(cb, 0x01, RAX, RCX);   /* RAX = base + offset */

        load_oper(ctx, RDX, &ins->src2);    /* value */

        /* Width-aware store: [rax] = rdx */
        if (esz == 1) {
            /* mov byte [rax], dl */
            cb_emit8(cb, 0x88);
            cb_emit8(cb, modrm(0, RDX, RAX));
        } else if (esz == 2) {
            /* mov word [rax], dx */
            cb_emit8(cb, 0x66);             /* operand-size prefix */
            cb_emit8(cb, 0x89);
            cb_emit8(cb, modrm(0, RDX, RAX));
        } else if (esz <= 4) {
            /* mov dword [rax], edx */
            cb_emit8(cb, 0x89);
            cb_emit8(cb, modrm(0, RDX, RAX));
        } else {
            /* mov qword [rax], rdx */
            cb_emit8(cb, rex(1, RDX, 0, RAX));
            cb_emit8(cb, 0x89);
            cb_emit8(cb, modrm(0, RDX, RAX));
        }

        break;
    }

    /* ── FIELD_LOAD: dest = *(base + offset), width-aware ── */
    case IR_FIELD_LOAD: {
        /* src1 = base, src2 = oper_imm(field_offset),
         * extra = member size | 0x100 if unsigned */
        bool is_unsig = (ins->extra & 0x100) != 0;
        int esz = (ins->extra & 0xFF) ? (ins->extra & 0xFF) : 8;

        if (ins->src1.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->src1.stack_off);
        else
            load_oper(ctx, RAX, &ins->src1);

        int foff = (int)ins->src2.imm;
        if (foff != 0) {
            emit_load_imm(cb, RCX, foff);
            emit_alu_rr(cb, 0x01, RAX, RCX);  /* add rax, rcx */
        }

        /* Width-aware load from [rax] */
        if (esz == 1) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, is_unsig ? 0xB6 : 0xBE);  /* movzx / movsx byte */
            cb_emit8(cb, modrm(0, RAX, RAX));
        } else if (esz == 2) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, is_unsig ? 0xB7 : 0xBF);  /* movzx / movsx word */
            cb_emit8(cb, modrm(0, RAX, RAX));
        } else if (esz <= 4) {
            if (is_unsig) {
                /* mov eax, [rax] — auto zero-extends to rax */
                cb_emit8(cb, 0x8B);
                cb_emit8(cb, modrm(0, RAX, RAX));
            } else {
                /* movsxd rax, dword [rax] */
                cb_emit8(cb, rex(1, RAX, 0, RAX));
                cb_emit8(cb, 0x63);
                cb_emit8(cb, modrm(0, RAX, RAX));
            }
        } else {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x8B);
            cb_emit8(cb, modrm(0, RAX, RAX));  /* mov rax, [rax] */
        }

        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;
    }

    /* ── FIELD_STORE: *(base + offset) = val, width-aware ── */
    case IR_FIELD_STORE: {
        /* dest = base, src1 = oper_imm(field_offset), src2 = value,
         * extra = member size */
        int esz = ins->extra ? ins->extra : 8;

        if (ins->dest.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->dest.stack_off);
        else
            load_oper(ctx, RAX, &ins->dest);

        int foff = (int)ins->src1.imm;
        if (foff != 0) {
            emit_load_imm(cb, RDX, foff);
            emit_alu_rr(cb, 0x01, RAX, RDX);  /* add rax, rdx */
        }

        load_oper(ctx, RCX, &ins->src2);  /* value */

        /* Width-aware store: [rax] = rcx */
        if (esz == 1) {
            cb_emit8(cb, 0x88);
            cb_emit8(cb, modrm(0, RCX, RAX));  /* mov byte [rax], cl */
        } else if (esz == 2) {
            cb_emit8(cb, 0x66);                 /* operand-size prefix */
            cb_emit8(cb, 0x89);
            cb_emit8(cb, modrm(0, RCX, RAX));  /* mov word [rax], cx */
        } else if (esz <= 4) {
            cb_emit8(cb, 0x89);
            cb_emit8(cb, modrm(0, RCX, RAX));  /* mov dword [rax], ecx */
        } else {
            cb_emit8(cb, rex(1, RCX, 0, RAX));
            cb_emit8(cb, 0x89);
            cb_emit8(cb, modrm(0, RCX, RAX));  /* mov qword [rax], rcx */
        }
        break;
    }

    /* ── LEA: dest = address of stack slot ───────────────── */
    case IR_LEA:
        if (ins->src1.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->src1.stack_off);
        else
            load_oper(ctx, RAX, &ins->src1);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── MEMCPY ──────────────────────────────────────────── */
    case IR_MEMCPY: {
        /* dest = dst_addr (temp), src1 = src_addr (temp),
         * src2.imm = byte count */
        load_oper(ctx, RCX, &ins->dest);   /* arg0: dst */
        load_oper(ctx, RDX, &ins->src1);   /* arg1: src */
        emit_load_imm(cb, R8, ins->src2.imm); /* arg2: count */
        emit_sub_rsp_imm32(cb, 32);
        emit_call_sym(ctx, RT_MEMCPY);
        emit_add_rsp_imm32(cb, 32);
        break;
    }

    /* ── STORE_IND: *dest = src1 (store through pointer) ── */
    case IR_STORE_IND: {
        /* dest = address (temp), src1 = value (temp), extra = size */
        load_oper(ctx, RAX, &ins->dest);   /* address */
        load_oper(ctx, RCX, &ins->src1);   /* value */
        int sz = ins->extra;
        if (sz == 1) {
            cb_emit8(cb, 0x88);                     /* mov [rax], cl */
            cb_emit8(cb, modrm(0, RCX, RAX));
        } else if (sz == 2) {
            cb_emit8(cb, 0x66);                     /* operand prefix */
            cb_emit8(cb, 0x89);                     /* mov [rax], cx */
            cb_emit8(cb, modrm(0, RCX, RAX));
        } else if (sz == 4) {
            cb_emit8(cb, 0x89);                     /* mov [rax], ecx */
            cb_emit8(cb, modrm(0, RCX, RAX));
        } else {
            cb_emit8(cb, rex(1, RCX, 0, RAX));      /* REX.W */
            cb_emit8(cb, 0x89);                     /* mov [rax], rcx */
            cb_emit8(cb, modrm(0, RCX, RAX));
        }
        break;
    }

    /* ── Sign/Zero extend, Truncate ──────────────────────── */
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
        /* For our simple codegen, all temps are 64-bit on stack.
         * Extension/truncation is effectively a NOP since values
         * are already stored as 64-bit. */
        load_oper(ctx, RAX, &ins->src1);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX);
        break;

    /* ── SYSCALL ─────────────────────────────────────────── */
    case IR_SYSCALL: {
        /* Arguments pre-loaded via IR_ARG.
         * syscall on Windows: not directly used; on Linux the
         * ELF backend would remap for System V ABI.
         * For now emit INT3 as placeholder. */
        cb_emit8(cb, 0xCC);  /* int3 – breakpoint / placeholder */
        break;
    }

    default:
        x64_error("unhandled IR opcode %d", ins->op);
        break;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Function prologue / epilogue + instruction loop
 * ═════════════════════════════════════════════════════════════ */

static void gen_function(X64Ctx *ctx, const IRFunc *fn)
{
    CodeBuf *cb = &ctx->code;

    /* Record function start */
    if (ctx->func_count >= ctx->func_cap) {
        ctx->func_cap = ctx->func_cap ? ctx->func_cap * 2 : 16;
        ctx->funcs = (X64Func *)realloc(ctx->funcs,
                                        ctx->func_cap * sizeof(X64Func));
    }
    X64Func *xf = &ctx->funcs[ctx->func_count++];
    xf->name = fn->name;
    xf->text_offset = cb_pos(cb);

    /* Compute frame size:
     * Variables occupy [rbp-1] down to [rbp - stack_size].
     * Temps start below variables: temp i at [rbp - stack_size - (i+1)*8].
     * Total frame = stack_size + temps_space, aligned to 16. */
    ctx->var_area_size = fn->stack_size;
    int temps_space = fn->temp_count * 8;
    int frame = fn->stack_size + temps_space;
    frame = AXIS_ALIGN(frame, 16);
    /* After push rbp (8 bytes), RSP is 8-misaligned; sub by 16-aligned
     * frame re-aligns to 16.  If frame is 0, stack is still 8-misaligned,
     * so bump it to 16 for any function that might call out. */
    if (frame == 0 && fn->instr_count > 0) frame = 16;
    xf->stack_size = frame;

    /* ── Prologue ──────────────────────────────────── */
    emit_push(cb, RBP);
    emit_mov_reg_reg(cb, RBP, RSP);
    if (frame > 0)
        emit_sub_rsp_imm32(cb, frame);

    /* Spill incoming register parameters to their stack slots.
     * Win64 ABI: first 4 integer args in RCX, RDX, R8, R9.
     * Parameters beyond 4 are already on the caller's stack and
     * need to be copied to the callee's local slots.
     * Field/struct params are passed by pointer — we first spill
     * ALL register args as raw 8-byte values (preserving pointers),
     * then memcpy field params in a second pass. This avoids
     * clobbering registers before all args are saved. */
    {
        int n = fn->param_count < 4 ? fn->param_count : 4;

        /* Pass 1: spill all register args (pointers or scalars) */
        for (int i = 0; i < n; i++) {
            int off  = fn->param_info[i].offset;
            int size = fn->param_info[i].size;
            if (fn->param_info[i].is_field) {
                /* Save the pointer (8 bytes) to the local slot.
                 * The slot is field_size bytes wide, so 8 bytes is fine. */
                emit_store_rbp(cb, off, win64_arg_regs[i]);
            } else {
                emit_store_rbp_sz(cb, off, win64_arg_regs[i], size);
            }
        }

        /* Pass 2: for field params, memcpy from saved pointer to slot.
         * The pointer is at [rbp + off], and we overwrite the same
         * slot with the actual struct data. */
        for (int i = 0; i < n; i++) {
            if (!fn->param_info[i].is_field) continue;
            int off = fn->param_info[i].offset;
            int fsz = fn->param_info[i].field_size;
            /* Load the saved pointer into RDX (src) */
            emit_load_rbp(cb, RDX, off);
            emit_lea_rbp(cb, RCX, off);             /* dst */
            emit_load_imm(cb, R8, fsz);             /* count */
            emit_sub_rsp_imm32(cb, 32);
            emit_call_sym(ctx, RT_MEMCPY);
            emit_add_rsp_imm32(cb, 32);
        }

        /* Args 5..N: caller placed them at [old_rsp + 8 + i*8].
         * After push rbp, that's [rbp + 16 + i*8].               */
        for (int i = 4; i < fn->param_count; i++) {
            int caller_off = 16 + i * 8;
            emit_load_rbp(cb, RAX, caller_off);
            int off  = fn->param_info[i].offset;
            int size = fn->param_info[i].size;
            if (fn->param_info[i].is_field) {
                int fsz = fn->param_info[i].field_size;
                emit_mov_reg_reg(cb, RDX, RAX);     /* src ptr */
                emit_lea_rbp(cb, RCX, off);           /* dst */
                emit_load_imm(cb, R8, fsz);           /* count */
                emit_sub_rsp_imm32(cb, 32);
                emit_call_sym(ctx, RT_MEMCPY);
                emit_add_rsp_imm32(cb, 32);
            } else {
                emit_store_rbp_sz(cb, off, RAX, size);
            }
        }
    }

    /* Reset label table for this function */
    for (int i = 0; i < ctx->label_cap; i++)
        ctx->label_offsets[i] = -1;

    /* ── Instruction loop ────────────────────────── */
    for (int i = 0; i < fn->instr_count; i++) {
        gen_instr(ctx, fn, &fn->instrs[i]);
    }

    /* ── Implicit epilogue (safety net) ──────────── */
    /* If the function didn't end with RET, add one */
    if (fn->instr_count == 0 ||
        (fn->instrs[fn->instr_count - 1].op != IR_RET &&
         fn->instrs[fn->instr_count - 1].op != IR_RET_VOID))
    {
        emit_load_imm(cb, RAX, 0);
        emit_mov_reg_reg(cb, RSP, RBP);
        emit_pop(cb, RBP);
        emit_ret(cb);
    }

    xf->text_size = cb_pos(cb) - xf->text_offset;

    /* Resolve label relocs for THIS function while labels are still valid */
    resolve_label_relocs(ctx);
}

/* ═════════════════════════════════════════════════════════════
 * Label relocation resolution (second pass within .text)
 * ═════════════════════════════════════════════════════════════ */

static void resolve_label_relocs(X64Ctx *ctx)
{
    for (int i = 0; i < ctx->reloc_count; i++) {
        Reloc *r = &ctx->relocs[i];
        if (r->target_sym != NULL) continue; /* function reloc, not label */
        if (r->kind != RELOC_REL32) continue;

        int target = get_label(ctx, r->target_label);
        if (target < 0) {
            x64_error("unresolved label %d", r->target_label);
        }
        int from = r->offset + 4; /* rel32 is measured from end of imm */
        int rel = target - from + r->addend;
        cb_patch32(&ctx->code, r->offset, (uint32_t)rel);

        /* Mark as resolved by clearing target_sym / setting a sentinel */
        r->kind = (RelocKind)-1; /* resolved */
    }
}

/* Remove resolved (label) relocs, keep only function/string relocs. */
static void compact_relocs(X64Ctx *ctx)
{
    int w = 0;
    for (int i = 0; i < ctx->reloc_count; i++) {
        if ((int)ctx->relocs[i].kind >= 0) {
            if (w != i) ctx->relocs[w] = ctx->relocs[i];
            w++;
        }
    }
    ctx->reloc_count = w;
}

/* ═════════════════════════════════════════════════════════════
 * Resolve function call relocations (intra-module)
 * ═════════════════════════════════════════════════════════════ */

static int find_func_offset(const X64Ctx *ctx, const char *name)
{
    for (int i = 0; i < ctx->func_count; i++)
        if (strcmp(ctx->funcs[i].name, name) == 0)
            return ctx->funcs[i].text_offset;
    return -1; /* external / runtime – leave for PE/ELF linker */
}

static void resolve_func_relocs(X64Ctx *ctx)
{
    for (int i = 0; i < ctx->reloc_count; i++) {
        Reloc *r = &ctx->relocs[i];
        if (r->target_sym == NULL) continue;
        if (r->kind != RELOC_REL32) continue;

        int target = find_func_offset(ctx, r->target_sym);
        if (target < 0) continue;  /* external – keep for linker */

        int from = r->offset + 4;
        int rel = target - from + r->addend;
        cb_patch32(&ctx->code, r->offset, (uint32_t)rel);
        r->kind = (RelocKind)-1; /* resolved */
    }
    compact_relocs(ctx);
}

/* ═════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════ */

void x64_codegen(X64Ctx *ctx, const IRProgram *ir, Arena *arena)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ir    = ir;
    ctx->arena = arena;

    cb_init(&ctx->code);
    rdata_init(ctx);

    /* Prepare label table */
    ctx->label_cap     = 256;
    ctx->label_offsets = (int *)malloc(ctx->label_cap * sizeof(int));
    for (int i = 0; i < ctx->label_cap; i++)
        ctx->label_offsets[i] = -1;

    /* Build string table in .rdata */
    ctx->string_count = ir->str_count;
    ctx->strings = (X64String *)malloc(ir->str_count * sizeof(X64String));
    for (int i = 0; i < ir->str_count; i++) {
        ctx->strings[i].data = ir->strings[i];
        ctx->strings[i].rdata_offset = rdata_add_string(ctx, ir->strings[i]);
    }

    /* Generate code for all user-defined functions */
    for (int i = 0; i < ir->func_count; i++) {
        gen_function(ctx, &ir->funcs[i]);
    }

    /* Generate code for top-level statements (entry point) */
    if (ir->top_level.name != NULL)
        gen_function(ctx, &ir->top_level);

    /* Resolve intra-module jumps are now done per-function in gen_function */
    compact_relocs(ctx);

    /* Resolve intra-module function calls */
    resolve_func_relocs(ctx);
}

/* ═════════════════════════════════════════════════════════════
 * Debug dump
 * ═════════════════════════════════════════════════════════════ */

void x64_dump(const X64Ctx *ctx, FILE *out)
{
    fprintf(out, "=== x86-64 Code Generation Summary ===\n\n");

    fprintf(out, "Functions (%d):\n", ctx->func_count);
    for (int i = 0; i < ctx->func_count; i++) {
        const X64Func *f = &ctx->funcs[i];
        fprintf(out, "  [%d] %-20s offset=0x%04X size=%d stack=%d\n",
                i, f->name, f->text_offset, f->text_size, f->stack_size);
    }

    fprintf(out, "\nString literals (%d):\n", ctx->string_count);
    for (int i = 0; i < ctx->string_count; i++) {
        fprintf(out, "  [%d] rdata+0x%04X \"%s\"\n",
                i, ctx->strings[i].rdata_offset, ctx->strings[i].data);
    }

    fprintf(out, "\nUnresolved relocations (%d):\n", ctx->reloc_count);
    for (int i = 0; i < ctx->reloc_count; i++) {
        const Reloc *r = &ctx->relocs[i];
        const char *kind_s = "???";
        switch (r->kind) {
        case RELOC_REL32:     kind_s = "REL32";     break;
        case RELOC_ABS64:     kind_s = "ABS64";     break;
        case RELOC_RIP_REL32: kind_s = "RIP_REL32"; break;
        default: break;
        }
        fprintf(out, "  [%d] %s at .text+0x%04X → %s (addend=%d)\n",
                i, kind_s, r->offset,
                r->target_sym ? r->target_sym : "(string)",
                r->addend);
    }

    fprintf(out, "\n.text raw (%d bytes):\n", ctx->code.len);
    for (int i = 0; i < ctx->code.len; i++) {
        if (i % 16 == 0) fprintf(out, "  %04X: ", i);
        fprintf(out, "%02X ", ctx->code.data[i]);
        if (i % 16 == 15 || i == ctx->code.len - 1) fprintf(out, "\n");
    }

    fprintf(out, "\n.rdata raw (%d bytes):\n", ctx->rdata_len);
    for (int i = 0; i < ctx->rdata_len; i++) {
        if (i % 16 == 0) fprintf(out, "  %04X: ", i);
        fprintf(out, "%02X ", ctx->rdata[i]);
        if (i % 16 == 15 || i == ctx->rdata_len - 1) fprintf(out, "\n");
    }
}
