/*
 * x64.c â€“ x86-64 native code generator for the AXIS compiler.
 *
 * Strategy:
 *   â€¢ Variables live at [rbp + stack_off] (stack_off is negative).
 *   â€¢ Temps are assigned physical registers via linear-scan regalloc.
 *     Spilled temps live below variables: [rbp - var_area - (temp_id+1)*8].
 *   â€¢ RAX, RCX, RDX are scratch registers for instruction lowering.
 *   â€¢ R8, R9 are reserved for function call arguments.
 *   â€¢ Allocatable: RBX, RSI, RDI, R10, R11, R12â€“R15.
 *   â€¢ Calling convention: Windows x64 (rcx, rdx, r8, r9 + shadow space).
 *   â€¢ The linker/PE-writer patches RELOC_REL32 for function calls
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

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Helpers / Forward declarations
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void resolve_label_relocs(X64Ctx *ctx);

_Noreturn static void x64_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\n  \033[1;31merror:\033[0m ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* â”€â”€ Code buffer operations â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â”€â”€ Relocations â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â”€â”€ Label management â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * x86-64 register encoding
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

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

/* Emit REX for 32-bit operation: only needed when any reg > 7 */
static void emit_rex32(CodeBuf *cb, int r, int x, int b)
{
    if (r > 7 || x > 7 || b > 7)
        cb_emit8(cb, rex(0, r, x, b));
}

/* ModRM byte builder */
static uint8_t modrm(int mod, int reg, int rm)
{
    return (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Stack slot helpers
 *
 * Variables occupy [rbp + stack_off] where stack_off is negative.
 * Temps live below the variable area (spilled temps only):
 *   spill slot 0 â†’ [rbp - var_area - 8], slot 1 â†’ [rbp - var_area - 16], etc.
 *   Register-allocated temps have no stack slot.
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static int temp_off(const X64Ctx *ctx, int temp_id)
{
    int slot = ctx->spill_map[temp_id];
    return ctx->frame_size - ctx->var_area_size - (slot + 1) * 8;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Instruction emission helpers (commonly used patterns)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/*
 * emit_mov_reg_reg(cb, dst, src)  â€“  mov dst, src  (32-bit)
 * Encoding: [REX] 89 /r  (mov r/m32, r32)
 */
static void emit_mov_reg_reg(CodeBuf *cb, int dst, int src)
{
    emit_rex32(cb, src, 0, dst);
    cb_emit8(cb, 0x89);
    cb_emit8(cb, modrm(3, src, dst));
}

/* 64-bit variant for prologue/epilogue pointer operations */
static void emit_mov_reg_reg64(CodeBuf *cb, int dst, int src)
{
    cb_emit8(cb, rex(1, src, 0, dst));
    cb_emit8(cb, 0x89);
    cb_emit8(cb, modrm(3, src, dst));
}

/*
 * emit_mov_reg_imm64(cb, reg, val)  â€“  movabs reg, imm64
 * Encoding: REX.W + B8+rd  imm64
 */
static void emit_mov_reg_imm64(CodeBuf *cb, int reg, int64_t val)
{
    cb_emit8(cb, rex(1, 0, 0, reg));
    cb_emit8(cb, (uint8_t)(0xB8 + (reg & 7)));
    cb_emit64(cb, (uint64_t)val);
}

/*
 * emit_mov_reg_imm32(cb, reg, val)  â€“  mov reg, imm32 (zero-extend)
 * Encoding: B8+rd imm32  (no REX.W â†’ 32-bit op, upper 32 bits zeroed)
 */
static void emit_mov_reg_imm32(CodeBuf *cb, int reg, int32_t val)
{
    if (reg >= 8)
        cb_emit8(cb, (uint8_t)(0x41));  /* REX.B for extended register */
    cb_emit8(cb, (uint8_t)(0xB8 + (reg & 7)));
    cb_emit32(cb, (uint32_t)val);
}

/*
 * emit_load_imm(cb, reg, val) â€“ load immediate, shortest encoding
 */
static void emit_load_imm(CodeBuf *cb, int reg, int64_t val)
{
    if (val == 0) {
        /* xor reg32, reg32 (clears upper 32 bits too) */
        if (reg >= 8)
            cb_emit8(cb, rex(0, reg, 0, reg));
        cb_emit8(cb, 0x31);
        cb_emit8(cb, modrm(3, reg, reg));
    } else if (val >= -0x80000000LL && val <= 0x7FFFFFFFLL) {
        emit_mov_reg_imm32(cb, reg, (int32_t)(uint32_t)val);
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
static void __attribute__((unused)) emit_load_rbp(CodeBuf *cb, int dst, int off)
{
    emit_rex32(cb, dst, 0, RSP);
    cb_emit8(cb, 0x8B);                    /* mov r32, [rsp + disp32] */
    cb_emit8(cb, modrm(2, dst, RSP));
    cb_emit8(cb, 0x24);                    /* SIB: base=RSP, no index */
    cb_emit32(cb, (uint32_t)off);
}

/* 64-bit variant for callee-save / pointer loads */
static void emit_load_rbp64(CodeBuf *cb, int dst, int off)
{
    cb_emit8(cb, rex(1, dst, 0, RSP));
    cb_emit8(cb, 0x8B);                    /* mov r64, [rsp + disp32] */
    cb_emit8(cb, modrm(2, dst, RSP));
    cb_emit8(cb, 0x24);                    /* SIB: base=RSP, no index */
    cb_emit32(cb, (uint32_t)off);
}

/*
 * emit_store_rbp_off(cb, offset, src_reg)
 *   mov [rbp + offset], src_reg   (64-bit store)
 */
static void emit_store_rbp(CodeBuf *cb, int off, int src)
{
    emit_rex32(cb, src, 0, RSP);
    cb_emit8(cb, 0x89);                    /* mov [rsp + disp32], r32 */
    cb_emit8(cb, modrm(2, src, RSP));
    cb_emit8(cb, 0x24);                    /* SIB: base=RSP, no index */
    cb_emit32(cb, (uint32_t)off);
}

/* 64-bit variant for callee-save / pointer stores */
static void emit_store_rbp64(CodeBuf *cb, int off, int src)
{
    cb_emit8(cb, rex(1, src, 0, RSP));
    cb_emit8(cb, 0x89);                    /* mov [rsp + disp32], r64 */
    cb_emit8(cb, modrm(2, src, RSP));
    cb_emit8(cb, 0x24);                    /* SIB: base=RSP, no index */
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
        cb_emit8(cb, rex(1, dst, 0, RSP));  /* REX.W */
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xBE);                  /* movsx r64, r/m8 */
        cb_emit8(cb, modrm(2, dst, RSP));
        cb_emit8(cb, 0x24);                  /* SIB: base=RSP */
        cb_emit32(cb, (uint32_t)off);
        break;
    case 2:
        cb_emit8(cb, rex(1, dst, 0, RSP));  /* REX.W */
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xBF);                  /* movsx r64, r/m16 */
        cb_emit8(cb, modrm(2, dst, RSP));
        cb_emit8(cb, 0x24);                  /* SIB: base=RSP */
        cb_emit32(cb, (uint32_t)off);
        break;
    case 4:
        /* movsxd r64, dword [rbp+off] â€” sign-extend i32 to i64 */
        cb_emit8(cb, rex(1, dst, 0, RSP));  /* REX.W */
        cb_emit8(cb, 0x63);                  /* movsxd r64, r/m32 */
        cb_emit8(cb, modrm(2, dst, RSP));
        cb_emit8(cb, 0x24);                  /* SIB: base=RSP */
        cb_emit32(cb, (uint32_t)off);
        break;
    default: /* 8 or unknown */
        emit_load_rbp64(cb, dst, off);
        break;
    }
}

/* Zero-extending load from [rbp+off] (for unsigned types u8, u16, u32). */
static void emit_load_rbp_zx(CodeBuf *cb, int dst, int off, int size)
{
    switch (size) {
    case 1:
        /* movzx r32, byte [rbp+off]  â€” no REX.W so result zero-extends to r64 */
        if (dst >= 8 || RSP >= 8)
            cb_emit8(cb, rex(0, dst, 0, RSP));
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xB6);
        cb_emit8(cb, modrm(2, dst, RSP));
        cb_emit8(cb, 0x24);  /* SIB: [RSP + disp32] */
        cb_emit32(cb, (uint32_t)off);
        break;
    case 2:
        /* movzx r32, word [rsp+off] */
        if (dst >= 8 || RSP >= 8)
            cb_emit8(cb, rex(0, dst, 0, RSP));
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0xB7);
        cb_emit8(cb, modrm(2, dst, RSP));
        cb_emit8(cb, 0x24);  /* SIB: [RSP + disp32] */
        cb_emit32(cb, (uint32_t)off);
        break;
    case 4:
        /* mov r32, dword [rbp+off] â€” writing to r32 auto-zero-extends to r64 */
        if (dst >= 8 || RSP >= 8)
            cb_emit8(cb, rex(0, dst, 0, RSP));
        cb_emit8(cb, 0x8B);
        cb_emit8(cb, modrm(2, dst, RSP));
        cb_emit8(cb, 0x24);  /* SIB: [RSP + disp32] */
        cb_emit32(cb, (uint32_t)off);
        break;
    default:
        emit_load_rbp64(cb, dst, off);
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
            cb_emit8(cb, rex(0, src, 0, RSP));
        else {
            cb_emit8(cb, rex(0, src, 0, RSP));
        }
        cb_emit8(cb, 0x88);                  /* mov r/m8, r8 */
        cb_emit8(cb, modrm(2, src, RSP));
        cb_emit8(cb, 0x24);                  /* SIB: [RSP + disp32] */
        cb_emit32(cb, (uint32_t)off);
        break;
    case 2:
        cb_emit8(cb, 0x66);                  /* operand size prefix â†’ 16-bit */
        if (src >= 8)
            cb_emit8(cb, rex(0, src, 0, RSP));
        else
            cb_emit8(cb, rex(0, src, 0, RSP));
        cb_emit8(cb, 0x89);                  /* mov r/m16, r16 */
        cb_emit8(cb, modrm(2, src, RSP));
        cb_emit8(cb, 0x24);                  /* SIB: [RSP + disp32] */
        cb_emit32(cb, (uint32_t)off);
        break;
    case 4:
        /* No REX.W â†’ 32-bit operation */
        if (src >= 8)
            cb_emit8(cb, rex(0, src, 0, RSP));
        cb_emit8(cb, 0x89);                  /* mov r/m32, r32 */
        cb_emit8(cb, modrm(2, src, RSP));
        cb_emit8(cb, 0x24);                  /* SIB: [RSP + disp32] */
        cb_emit32(cb, (uint32_t)off);
        break;
    default: /* 8 or unknown */
        emit_store_rbp64(cb, off, src);
        break;
    }
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Spill-Reload Cache
 *
 * Tracks which physical register currently caches a recently-
 * spilled temp, allowing subsequent load_oper calls to skip
 * the memory load if the value is still in a register.
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static int src_reg_temp[16];  /* src_reg_temp[phys_reg] = temp_id or -1 */

static void src_flush(void)
{
    for (int i = 0; i < 16; i++) src_reg_temp[i] = -1;
}

static void src_invalidate(int reg)
{
    if (reg >= 0 && reg < 16) src_reg_temp[reg] = -1;
}

/* Record that 'reg' now holds spilled 'temp_id'. */
static void src_set(int reg, int temp_id)
{
    for (int i = 0; i < 16; i++)
        if (src_reg_temp[i] == temp_id) src_reg_temp[i] = -1;
    src_reg_temp[reg] = temp_id;
}

/* Find the physical register caching 'temp_id', or -1. */
static int src_find(int temp_id)
{
    for (int i = 0; i < 16; i++)
        if (src_reg_temp[i] == temp_id) return i;
    return -1;
}

/*
 * Load an IR operand's value into a register.
 * If the operand is a temp allocated to a physical register,
 * emits a reg-to-reg move (or nothing if already in the target).
 */
static void load_oper(X64Ctx *ctx, int reg, const IROper *op)
{
    CodeBuf *cb = &ctx->code;
    switch (op->kind) {
    case OPER_TEMP: {
        const RegAlloc *ra = &ctx->cur_ra;
        int phys = (ra->temp_reg && op->temp_id < ra->temp_count)
                   ? ra->temp_reg[op->temp_id] : REG_SPILLED;
        int narrow = (op->size > 0 && op->size <= 4);
        if (phys != REG_SPILLED) {
            src_invalidate(reg);
            if (phys != reg) {
                if (narrow)
                    emit_mov_reg_reg(cb, reg, phys);
                else
                    emit_mov_reg_reg64(cb, reg, phys);
            }
        } else {
            /* Check spill-reload cache */
            int cached = src_find(op->temp_id);
            if (cached >= 0) {
                if (cached != reg) {
                    src_invalidate(reg);
                    if (narrow)
                        emit_mov_reg_reg(cb, reg, cached);
                    else
                        emit_mov_reg_reg64(cb, reg, cached);
                }
                /* else: already in the right register, skip */
            } else {
                src_invalidate(reg);
                if (narrow)
                    emit_load_rbp(cb, reg, temp_off(ctx, op->temp_id));
                else
                    emit_load_rbp64(cb, reg, temp_off(ctx, op->temp_id));
            }
            src_set(reg, op->temp_id);
        }
        break;
    }
    case OPER_IMM:
        src_invalidate(reg);
        emit_load_imm(cb, reg, op->imm);
        break;
    case OPER_STACK:
        src_invalidate(reg);
        emit_load_rbp_sx(cb, reg, ctx->frame_size + op->stack_off, op->size);
        break;
    case OPER_NONE:
        src_invalidate(reg);
        /* load 0 */
        emit_load_imm(cb, reg, 0);
        break;
    default:
        x64_error("load_oper: unsupported operand kind %d", op->kind);
    }
}

/*
 * Store a register value to an IR temp's allocated location.
 * If the temp is in a physical register, emit reg-to-reg move
 * (or nothing if already there). Otherwise store to stack.
 * size: operand byte-width (1â€“8); 0 falls back to 64-bit.
 */
static void store_temp(X64Ctx *ctx, int temp_id, int reg, int size)
{
    const RegAlloc *ra = &ctx->cur_ra;
    int phys = (ra->temp_reg && temp_id < ra->temp_count)
               ? ra->temp_reg[temp_id] : REG_SPILLED;
    int narrow = (size > 0 && size <= 4);
    if (phys != REG_SPILLED) {
        src_invalidate(phys);
        src_invalidate(reg);
        if (phys != reg) {
            if (narrow)
                emit_mov_reg_reg(&ctx->code, phys, reg);
            else
                emit_mov_reg_reg64(&ctx->code, phys, reg);
        }
    } else {
        if (narrow)
            emit_store_rbp(&ctx->code, temp_off(ctx, temp_id), reg);
        else
            emit_store_rbp64(&ctx->code, temp_off(ctx, temp_id), reg);
        src_set(reg, temp_id);
    }
}

/* â”€â”€ Register-aware codegen helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/* Get the physical register for a temp, or REG_SPILLED. */
static int temp_phys(const X64Ctx *ctx, int temp_id)
{
    const RegAlloc *ra = &ctx->cur_ra;
    if (ra->temp_reg && temp_id >= 0 && temp_id < ra->temp_count)
        return ra->temp_reg[temp_id];
    return REG_SPILLED;
}

/* Get the physical register for an operand (OPER_TEMP only), or -1. */
static int oper_phys(const X64Ctx *ctx, const IROper *op)
{
    if (op->kind == OPER_TEMP)
        return temp_phys(ctx, op->temp_id);
    return -1;
}

/* Get the dest physical register for a temp, or fallback. */
static int dest_reg(const X64Ctx *ctx, const IROper *dest, int fallback)
{
    if (dest->kind == OPER_TEMP) {
        int r = temp_phys(ctx, dest->temp_id);
        if (r != REG_SPILLED) return r;
    }
    return fallback;
}

/* â”€â”€ ALU helpers (reg = reg OP reg) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/* Two-operand ALU: op eax, ecx (32-bit) */
static void emit_alu_rr(CodeBuf *cb, uint8_t opcode, int dst, int src)
{
    emit_rex32(cb, src, 0, dst);
    cb_emit8(cb, opcode);
    cb_emit8(cb, modrm(3, src, dst));
}

/* ALU immediate: op r/m32, imm32  (83 /ext imm8 or 81 /ext imm32) */
static void emit_alu_ri32(CodeBuf *cb, int reg, uint8_t ext, int32_t imm)
{
    emit_rex32(cb, 0, 0, reg);
    if (imm >= -128 && imm <= 127) {
        cb_emit8(cb, 0x83);
        cb_emit8(cb, modrm(3, ext, reg));
        cb_emit8(cb, (uint8_t)(int8_t)imm);
    } else {
        cb_emit8(cb, 0x81);
        cb_emit8(cb, modrm(3, ext, reg));
        cb_emit32(cb, (uint32_t)imm);
    }
}

/* Two-operand ALU: op rax, rcx (64-bit) */
static void emit_alu_rr64(CodeBuf *cb, uint8_t opcode, int dst, int src)
{
    cb_emit8(cb, rex(1, src, 0, dst));
    cb_emit8(cb, opcode);
    cb_emit8(cb, modrm(3, src, dst));
}

/* neg eax (32-bit) */
static void emit_neg_reg(CodeBuf *cb, int reg)
{
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0xF7);
    cb_emit8(cb, modrm(3, 3, reg));     /* /3 = neg */
}

/* not eax (32-bit) */
__attribute__((unused))
static void emit_not_reg(CodeBuf *cb, int reg)
{
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0xF7);
    cb_emit8(cb, modrm(3, 2, reg));     /* /2 = not */
}

/* cmp rax, rcx (64-bit) */
static void emit_cmp_rr(CodeBuf *cb, int a, int b)
{
    emit_alu_rr(cb, 0x39, a, b);   /* cmp r/m64, r64 */
}

/* test reg, reg (32-bit) */
static void emit_test_rr(CodeBuf *cb, int a, int b)
{
    emit_rex32(cb, b, 0, a);
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

/* movzx eax, al â€” no REX needed (RAX = reg 0) */
static void emit_movzx_rax_al(CodeBuf *cb)
{
    cb_emit8(cb, 0x0F);
    cb_emit8(cb, 0xB6);
    cb_emit8(cb, modrm(3, RAX, RAX));
}

/*
 * emit_jmp_rel32(cb) â€“ jmp rel32; returns offset of the rel32 for patching
 */
static int emit_jmp_rel32(CodeBuf *cb)
{
    cb_emit8(cb, 0xE9);
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);  /* placeholder */
    return patch;
}

/*
 * emit_jcc_rel32(cb, cc) â€“ jCC rel32; returns offset of rel32
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

/* â”€â”€ PUSH / POP â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â”€â”€ RET â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_ret(CodeBuf *cb)
{
    cb_emit8(cb, 0xC3);
}

/* â”€â”€ CALL rel32 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int emit_call_rel32(CodeBuf *cb)
{
    cb_emit8(cb, 0xE8);
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);
    return patch;
}

/* â”€â”€ sub rsp, imm32 / add rsp, imm32 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_sub_rsp_imm32(CodeBuf *cb, int32_t val)
{
    cb_emit8(cb, rex(1, 0, 0, RSP));
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 5, RSP));    /* /5 = sub */
    cb_emit32(cb, (uint32_t)val);
}

static void __attribute__((unused)) emit_add_rsp_imm32(CodeBuf *cb, int32_t val)
{
    cb_emit8(cb, rex(1, 0, 0, RSP));
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 0, RSP));    /* /0 = add */
    cb_emit32(cb, (uint32_t)val);
}

/* â”€â”€ sub reg, imm32  (32-bit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_sub_reg_imm32(CodeBuf *cb, int reg, int32_t val)
{
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 5, reg));    /* /5 = sub */
    cb_emit32(cb, (uint32_t)val);
}

/* â”€â”€ cmp reg, imm32  (32-bit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_cmp_reg_imm32(CodeBuf *cb, int reg, int32_t val)
{
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 7, reg));    /* /7 = cmp */
    cb_emit32(cb, (uint32_t)val);
}

/* â”€â”€ add reg, imm32  (32-bit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_add_reg_imm32(CodeBuf *cb, int reg, int32_t val)
{
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0x81);
    cb_emit8(cb, modrm(3, 0, reg));    /* /0 = add */
    cb_emit32(cb, (uint32_t)val);
}

/* â”€â”€ LEA reg, [rbp + disp32] â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_lea_rbp(CodeBuf *cb, int dst, int off)
{
    cb_emit8(cb, rex(1, dst, 0, RSP));
    cb_emit8(cb, 0x8D);                    /* lea r64, [rsp + disp32] */
    cb_emit8(cb, modrm(2, dst, RSP));
    cb_emit8(cb, 0x24);                    /* SIB: base=RSP, no index */
    cb_emit32(cb, (uint32_t)off);
}

/* â”€â”€ Shift: sal/shr reg, cl â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_shift_cl(CodeBuf *cb, int reg, uint8_t ext)
{
    /* D3 /ext  â†’ shift r32 by CL */
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0xD3);
    cb_emit8(cb, modrm(3, ext, reg));
}

/* â”€â”€ Shift: sal/shr reg, imm8 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_shift_imm(CodeBuf *cb, int reg, uint8_t ext, uint8_t imm)
{
    emit_rex32(cb, 0, 0, reg);
    if (imm == 1) {
        /* D1 /ext â†’ shift r32 by 1 (shorter encoding) */
        cb_emit8(cb, 0xD1);
        cb_emit8(cb, modrm(3, ext, reg));
    } else {
        /* C1 /ext imm8 â†’ shift r32 by imm8 */
        cb_emit8(cb, 0xC1);
        cb_emit8(cb, modrm(3, ext, reg));
        cb_emit8(cb, imm);
    }
}

/* â”€â”€ imul rax, rcx (signed 64-bit multiply) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_imul_rr(CodeBuf *cb, int dst, int src)
{
    emit_rex32(cb, dst, 0, src);
    cb_emit8(cb, 0x0F);
    cb_emit8(cb, 0xAF);
    cb_emit8(cb, modrm(3, dst, src));
}

/* â”€â”€ 3-operand imul: imul $imm, src, dst â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_imul_ri3(CodeBuf *cb, int dst, int src, int32_t imm)
{
    emit_rex32(cb, dst, 0, src);
    if (imm >= -128 && imm <= 127) {
        cb_emit8(cb, 0x6B);
        cb_emit8(cb, modrm(3, dst, src));
        cb_emit8(cb, (uint8_t)(int8_t)imm);
    } else {
        cb_emit8(cb, 0x69);
        cb_emit8(cb, modrm(3, dst, src));
        cb_emit32(cb, (uint32_t)imm);
    }
}

/* â”€â”€ LEA-multiply: lea dst, [src + src*scale] â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
/* Computes dst = src * (1 + scale) where scale âˆˆ {2,4,8}.
 * Used for Ã—3, Ã—5, Ã—9 to replace IMUL with a single LEA. */

static void emit_lea_scale(CodeBuf *cb, int dst, int src, int scale)
{
    int ss;
    switch (scale) {
    case 2: ss = 1; break;
    case 4: ss = 2; break;
    case 8: ss = 3; break;
    default: return;
    }
    /* 8D /r with SIB: [base + index*scale] (32-bit) */
    emit_rex32(cb, dst, src, src);
    cb_emit8(cb, 0x8D);
    /* ModRM: mod depends on base (RBP/R13 need mod=01+disp8) */
    int mod = ((src & 7) == 5) ? 1 : 0;
    cb_emit8(cb, modrm(mod, dst, 4));  /* rm=4 â†’ SIB follows */
    cb_emit8(cb, (uint8_t)((ss << 6) | ((src & 7) << 3) | (src & 7)));
    if (mod == 1) cb_emit8(cb, 0);     /* disp8=0 for RBP/R13 base */
}

/* â”€â”€ LEA dst, [base + index*scale] (32-bit, base â‰  index) â”€â”€ */
/* Computes dst = base + index * scale where scale âˆˆ {1,2,4,8}.
 * Unlike emit_lea_scale, base and index are separate registers.
 * Used for fused MUL+ADD patterns (e.g. Ã—7+add â†’ lea+sub). */

static void emit_lea_base_idx_scale(CodeBuf *cb, int dst, int base,
                                     int idx, int scale)
{
    int ss;
    switch (scale) {
    case 1: ss = 0; break;
    case 2: ss = 1; break;
    case 4: ss = 2; break;
    case 8: ss = 3; break;
    default: return;
    }
    /* SIB index field 0b100 means 'no index' â€” caller must avoid
     * passing RSP/R12 (reg & 7 == 4) as the index register.       */
    emit_rex32(cb, dst, idx, base);
    cb_emit8(cb, 0x8D);
    int mod = ((base & 7) == 5) ? 1 : 0;  /* RBP/R13 base needs mod=01+disp8 */
    cb_emit8(cb, modrm(mod, dst, 4));       /* rm=4 â†’ SIB follows */
    cb_emit8(cb, (uint8_t)((ss << 6) | ((idx & 7) << 3) | (base & 7)));
    if (mod == 1) cb_emit8(cb, 0);          /* disp8=0 for RBP/R13 */
}

/* â”€â”€ LEA dst, [base + index] (32-bit, no scale) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_lea_rr(CodeBuf *cb, int dst, int base, int idx)
{
    /* SIB can't encode RSP as index; swap if needed (commutative) */
    if ((idx & 7) == 4) { int t = base; base = idx; idx = t; }
    emit_rex32(cb, dst, idx, base);
    cb_emit8(cb, 0x8D);
    int mod = ((base & 7) == 5) ? 1 : 0;  /* RBP/R13 needs mod=01+disp8 */
    cb_emit8(cb, modrm(mod, dst, 4));       /* rm=4 â†’ SIB follows */
    cb_emit8(cb, (uint8_t)(((idx & 7) << 3) | (base & 7)));
    if (mod == 1) cb_emit8(cb, 0);
}

/* â”€â”€ LEA dst, [base + disp] (32-bit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_lea_ri(CodeBuf *cb, int dst, int base, int32_t disp)
{
    emit_rex32(cb, dst, 0, base);
    cb_emit8(cb, 0x8D);
    if (disp >= -128 && disp <= 127) {
        cb_emit8(cb, modrm(1, dst, base));
        if ((base & 7) == 4) cb_emit8(cb, 0x24);  /* SIB for RSP/R12 */
        cb_emit8(cb, (uint8_t)(int8_t)disp);
    } else {
        cb_emit8(cb, modrm(2, dst, base));
        if ((base & 7) == 4) cb_emit8(cb, 0x24);
        cb_emit32(cb, (uint32_t)disp);
    }
}

/* â”€â”€ idiv rcx (signed 64-bit divide: rdx:rax / rcx) â”€â”€â”€â”€â”€â”€ */

static void emit_cdq(CodeBuf *cb)
{
    cb_emit8(cb, 0x99);             /* CDQ: sign-extend eax â†’ edx:eax */
}

static void emit_idiv_reg(CodeBuf *cb, int reg)
{
    emit_rex32(cb, 0, 0, reg);
    cb_emit8(cb, 0xF7);
    cb_emit8(cb, modrm(3, 7, reg)); /* /7 = idiv */
}

/* â”€â”€ mov [reg + disp32], src (64-bit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void emit_store_mem(CodeBuf *cb, int base, int32_t disp, int src)
{
    cb_emit8(cb, rex(1, src, 0, base));
    cb_emit8(cb, 0x89);
    cb_emit8(cb, modrm(2, src, base));
    if ((base & 7) == RSP) cb_emit8(cb, 0x24); /* SIB for RSP base */
    cb_emit32(cb, (uint32_t)disp);
}

/* â”€â”€ mov dst, [reg + disp32] (64-bit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void __attribute__((unused)) emit_load_mem(CodeBuf *cb, int dst, int base, int32_t disp)
{
    cb_emit8(cb, rex(1, dst, 0, base));
    cb_emit8(cb, 0x8B);
    cb_emit8(cb, modrm(2, dst, base));
    if ((base & 7) == RSP) cb_emit8(cb, 0x24); /* SIB for RSP base */
    cb_emit32(cb, (uint32_t)disp);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * rdata section builder (string literals)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

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

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Per-function code generation
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

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
static const char *RT_READ_FAILED = "__axis_read_failed";
static const char *RT_MEMCPY      = "__axis_memcpy";
static const char *RT_DIV_ZERO    = "__axis_div_zero";
static const char *RT_STR_CONCAT  = "__axis_str_concat";
static const char *RT_STR_EQ      = "__axis_str_eq";

/* â”€â”€ Emit a call to a named function (relocation-based) â”€â”€ */

static void emit_call_sym(X64Ctx *ctx, const char *name)
{
    int patch = emit_call_rel32(&ctx->code);
    add_reloc(ctx, RELOC_REL32, patch, name, 0, 0);
}

/* â”€â”€ Emit a LEA for a string literal (RIP-relative) â”€â”€â”€â”€â”€â”€ */

static void emit_lea_string(X64Ctx *ctx, int reg, int str_idx)
{
    CodeBuf *cb = &ctx->code;
    /* lea reg, [rip + disp32] */
    cb_emit8(cb, rex(1, reg, 0, 0));
    cb_emit8(cb, 0x8D);
    cb_emit8(cb, modrm(0, reg, 5));    /* mod=00, rm=5 â†’ RIP+disp32 */
    int patch = cb_pos(cb);
    cb_emit32(cb, 0);                  /* placeholder disp32 */

    /* Relocation: patch this to point to strings[str_idx] in .rdata */
    add_reloc(ctx, RELOC_RIP_REL32, patch, NULL, str_idx, 0);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * IR instruction â†’ x86-64 lowering
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/* Forward declaration â€“ defined after gen_instr */
static void emit_epilogue(X64Ctx *ctx);

/*
 * gen_instr â€“ Lower a single IR instruction to x86-64 machine code.
 *
 * Returns the number of IR instructions consumed (normally 1, but
 * CMP+Branch fusion may consume 2).  idx is the current instruction
 * index within fn->instrs.
 */
static int gen_instr(X64Ctx *ctx, const IRFunc *fn, int idx)
{
    const IRInstr *ins = &fn->instrs[idx];
    CodeBuf *cb = &ctx->code;

    /* Flush spill-reload cache for instructions that clobber registers
     * outside of load_oper/store_temp (calls, divs, memory ops, etc.).
     * Simple ALU/MOV/CMP/branch instructions are safe â€” their register
     * usage is fully tracked through load_oper and store_temp.
     *
     * We flush both BEFORE and AFTER non-safe instructions.  The pre-flush
     * prevents stale entries from being consumed by load_oper calls inside
     * the instruction.  The post-flush prevents entries populated by
     * load_oper from surviving past internal clobbers (e.g. IMUL in
     * INDEX_LOAD trashes RCX after load_oper cached it). */
    int need_post_flush = 0;
    switch (ins->op) {
    case IR_NOP: case IR_MOV: case IR_LOAD_IMM: case IR_LOAD_STR:
    case IR_LOAD_VAR: case IR_STORE_VAR:
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_NEG:
    case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
    case IR_SHL: case IR_SHR:
    case IR_CMP_EQ: case IR_CMP_NE: case IR_CMP_LT:
    case IR_CMP_LE: case IR_CMP_GT: case IR_CMP_GE:
    case IR_LOG_NOT: case IR_ARG:
    case IR_JMP: case IR_JZ: case IR_JNZ:
    case IR_SEXT: case IR_ZEXT: case IR_TRUNC:
    case IR_CMOV:
        break;
    default:
        src_flush();
        need_post_flush = 1;
        break;
    }

    switch (ins->op) {

    /* â”€â”€ NOP â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_NOP:
        cb_emit8(cb, 0x90);
        break;

    /* â”€â”€ MOV: dest = src1 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_MOV: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        load_oper(ctx, dr, &ins->src1);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        else if (ins->dest.kind == OPER_STACK)
            emit_store_rbp(cb, ins->dest.stack_off, dr);
        break;
    }

    /* â”€â”€ LOAD_IMM: dest = imm â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_LOAD_IMM: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        emit_load_imm(cb, dr, ins->src1.imm);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    /* â”€â”€ LOAD_STR: dest = &string[idx] â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_LOAD_STR: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        emit_lea_string(ctx, dr, ins->src1.str_idx);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    /* â”€â”€ LOAD_VAR: dest = [rbp + stack_off] â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_LOAD_VAR: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        if (ins->extra)   /* unsigned â†’ zero-extend */
            emit_load_rbp_zx(cb, dr, ins->src1.stack_off, ins->src1.size);
        else              /* signed   â†’ sign-extend */
            emit_load_rbp_sx(cb, dr, ins->src1.stack_off, ins->src1.size);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    /* â”€â”€ STORE_VAR: [rbp + stack_off] = src1 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_STORE_VAR: {
        int s1r = oper_phys(ctx, &ins->src1);
        int r = (s1r >= 0) ? s1r : RAX;
        if (s1r < 0) load_oper(ctx, RAX, &ins->src1);
        emit_store_rbp_sz(cb, ins->dest.stack_off, r, ins->dest.size);
        break;
    }

    /* â”€â”€ Arithmetic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_ADD: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        /* Check if either operand is an immediate (commutative) */
        const IROper *imm_op = NULL, *reg_op = NULL;
        if (ins->src2.kind == OPER_IMM) { imm_op = &ins->src2; reg_op = &ins->src1; }
        else if (ins->src1.kind == OPER_IMM) { imm_op = &ins->src1; reg_op = &ins->src2; }
        if (imm_op && imm_op->imm >= INT32_MIN && imm_op->imm <= INT32_MAX) {
            int sr = oper_phys(ctx, reg_op);
            if (sr >= 0) {
                /* LEA: non-destructive reg + imm */
                emit_lea_ri(cb, dr, sr, (int32_t)imm_op->imm);
            } else {
                load_oper(ctx, dr, reg_op);
                emit_add_reg_imm32(cb, dr, (int32_t)imm_op->imm);
            }
        } else {
            int s1r = oper_phys(ctx, &ins->src1);
            int s2r = oper_phys(ctx, &ins->src2);
            if (s1r >= 0 && s2r >= 0) {
                /* LEA: non-destructive reg + reg */
                emit_lea_rr(cb, dr, s1r, s2r);
            } else {
                const IROper *a = &ins->src1, *b = &ins->src2;
                if (s2r == dr) { const IROper *t = a; a = b; b = t; s2r = oper_phys(ctx, b); }
                load_oper(ctx, dr, a);
                int s2 = (s2r >= 0 && s2r != dr) ? s2r : RCX;
                if (s2 != s2r) load_oper(ctx, s2, b);
                emit_alu_rr(cb, 0x01, dr, s2);
            }
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_SUB: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        if (ins->src2.kind == OPER_IMM) {
            int64_t v = ins->src2.imm;
            if (v >= INT32_MIN && v <= INT32_MAX && v != (int64_t)INT32_MIN) {
                /* Try LEA for non-destructive reg - imm */
                int sr = oper_phys(ctx, &ins->src1);
                if (sr >= 0) {
                    emit_lea_ri(cb, dr, sr, -(int32_t)v);
                } else {
                    load_oper(ctx, dr, &ins->src1);
                    emit_sub_reg_imm32(cb, dr, (int32_t)v);
                }
            } else if (v >= INT32_MIN && v <= INT32_MAX) {
                load_oper(ctx, dr, &ins->src1);
                emit_sub_reg_imm32(cb, dr, (int32_t)v);
            } else {
                load_oper(ctx, dr, &ins->src1);
                load_oper(ctx, RCX, &ins->src2);
                emit_alu_rr(cb, 0x29, dr, RCX);
            }
        } else {
            int s2r = oper_phys(ctx, &ins->src2);
            if (s2r == dr) dr = RAX;
            load_oper(ctx, dr, &ins->src1);
            int s2 = (s2r >= 0 && s2r != dr) ? s2r : RCX;
            if (s2 != s2r) load_oper(ctx, s2, &ins->src2);
            emit_alu_rr(cb, 0x29, dr, s2);
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_MUL: {
        /* LEA-multiply for small constants: Ã—3, Ã—5, Ã—9 */
        const IROper *var_op = NULL;
        int64_t cval = 0;
        if (ins->src2.kind == OPER_IMM) {
            var_op = &ins->src1; cval = ins->src2.imm;
        } else if (ins->src1.kind == OPER_IMM) {
            var_op = &ins->src2; cval = ins->src1.imm;
        }
        if (var_op && (cval == 3 || cval == 5 || cval == 9)) {
            int dr = dest_reg(ctx, &ins->dest, RAX);
            load_oper(ctx, dr, var_op);
            int scale = (cval == 3) ? 2 : (cval == 5) ? 4 : 8;
            emit_lea_scale(cb, dr, dr, scale);
            if (ins->dest.kind == OPER_TEMP)
                store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
            break;
        }
        /* Ã—7 via mov+shl+sub: x*8-x (2c latency vs 4c for two complex LEAs) */
        if (var_op && cval == 7) {
            /* â”€â”€ MULÃ—7 + ADD fusion â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
             * Pattern: t1 = src * 7;  t2 = t1 + other;
             * Fused:   lea t2, [other + src*8]; sub t2, src
             * Saves 2 instructions vs non-fused (4 â†’ 2 insns). */
            if (ins->dest.kind == OPER_TEMP && idx + 1 < fn->instr_count) {
                const IRInstr *nx = &fn->instrs[idx + 1];
                if (nx->op == IR_ADD) {
                    const IROper *add_other = NULL;
                    if (nx->src1.kind == OPER_TEMP &&
                        nx->src1.temp_id == ins->dest.temp_id)
                        add_other = &nx->src2;
                    else if (nx->src2.kind == OPER_TEMP &&
                             nx->src2.temp_id == ins->dest.temp_id)
                        add_other = &nx->src1;
                    if (add_other && add_other->kind != OPER_IMM) {
                        int sr = oper_phys(ctx, var_op);
                        if (sr < 0 || sr == REG_SPILLED) {
                            sr = RCX;
                            load_oper(ctx, sr, var_op);
                        }
                        /* SIB can't encode R12/RSP as index */
                        if ((sr & 7) != 4) {
                            int ar = oper_phys(ctx, add_other);
                            if (ar < 0 || ar == REG_SPILLED) {
                                ar = (sr == RDX) ? RAX : RDX;
                                load_oper(ctx, ar, add_other);
                            }
                            int dr = dest_reg(ctx, &nx->dest, RAX);
                            if (dr != sr) {
                                /* Normal: lea dr,[other+src*8]; sub dr,src */
                                emit_lea_base_idx_scale(cb, dr, ar, sr, 8);
                                emit_alu_rr(cb, 0x29, dr, sr);
                            } else {
                                /* dr==sr collision: use scratch to avoid
                                 * clobbering src before the SUB */
                                int tmp = RAX;
                                if (tmp == sr || tmp == ar) tmp = RCX;
                                if (tmp == sr || tmp == ar) tmp = RDX;
                                emit_lea_base_idx_scale(cb, tmp, ar, sr, 8);
                                emit_alu_rr(cb, 0x29, tmp, sr);
                                emit_mov_reg_reg64(cb, dr, tmp);
                            }
                            if (nx->dest.kind == OPER_TEMP)
                                store_temp(ctx, nx->dest.temp_id, dr, nx->dest.size);
                            return 2; /* consumed MUL + ADD */
                        }
                    }
                }
            }
            /* â”€â”€ Non-fused Ã—7: mov+shl+sub â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
            int dr = dest_reg(ctx, &ins->dest, RAX);
            int sr = oper_phys(ctx, var_op);
            if (sr < 0 || sr == REG_SPILLED) {
                sr = (dr == RCX) ? RDX : RCX;
                load_oper(ctx, sr, var_op);
            }
            if (sr == dr) {
                /* src and dest share register; stash src first */
                int scratch = (dr == RCX) ? RDX : RCX;
                emit_mov_reg_reg64(cb, scratch, sr);
                emit_shift_imm(cb, dr, 4, 3);        /* dr = src*8 */
                emit_alu_rr(cb, 0x29, dr, scratch);   /* dr -= src  */
            } else {
                emit_mov_reg_reg64(cb, dr, sr);       /* dr = src   */
                emit_shift_imm(cb, dr, 4, 3);         /* dr = src*8 */
                emit_alu_rr(cb, 0x29, dr, sr);        /* dr -= src  */
            }
            if (ins->dest.kind == OPER_TEMP)
                store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
            break;
        }
        /* 3-operand IMUL with immediate: imul $imm, src, dst */
        if (var_op) {
            int dr = dest_reg(ctx, &ins->dest, RAX);
            int sr = oper_phys(ctx, var_op);
            if (sr < 0 || sr == REG_SPILLED) {
                sr = RCX;
                load_oper(ctx, sr, var_op);
            }
            emit_imul_ri3(cb, dr, sr, (int32_t)cval);
            if (ins->dest.kind == OPER_TEMP)
                store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
            break;
        }
        /* General case: IMUL */
        int dr = dest_reg(ctx, &ins->dest, RAX);
        int s2r = oper_phys(ctx, &ins->src2);
        const IROper *a = &ins->src1, *b = &ins->src2;
        if (s2r == dr) { const IROper *t = a; a = b; b = t; s2r = oper_phys(ctx, b); }
        load_oper(ctx, dr, a);
        int s2 = (s2r >= 0 && s2r != dr) ? s2r : RCX;
        if (s2 != s2r) load_oper(ctx, s2, b);
        emit_imul_rr(cb, dr, s2);        /* imul dr, s2 */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_DIV:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_test_rr(cb, RCX, RCX);          /* test ecx, ecx */
        cb_emit8(cb, 0x75); cb_emit8(cb, 5); /* jnz +5 (skip call) */
        emit_call_sym(ctx, RT_DIV_ZERO);
        if (ins->extra) {
            emit_alu_rr(cb, 0x31, RDX, RDX); /* xor edx, edx */
            emit_rex32(cb, 0, 0, RCX);
            cb_emit8(cb, 0xF7);
            cb_emit8(cb, modrm(3, 6, RCX));   /* div ecx */
        } else {
            emit_cdq(cb);
            emit_idiv_reg(cb, RCX);
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;

    case IR_MOD:
        load_oper(ctx, RAX, &ins->src1);
        load_oper(ctx, RCX, &ins->src2);
        emit_test_rr(cb, RCX, RCX);          /* test ecx, ecx */
        cb_emit8(cb, 0x75); cb_emit8(cb, 5); /* jnz +5 (skip call) */
        emit_call_sym(ctx, RT_DIV_ZERO);
        if (ins->extra) {
            emit_alu_rr(cb, 0x31, RDX, RDX); /* xor edx, edx */
            emit_rex32(cb, 0, 0, RCX);
            cb_emit8(cb, 0xF7);
            cb_emit8(cb, modrm(3, 6, RCX));   /* div ecx */
        } else {
            emit_cdq(cb);
            emit_idiv_reg(cb, RCX);
        }
        emit_mov_reg_reg(cb, RAX, RDX);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;

    case IR_NEG: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        load_oper(ctx, dr, &ins->src1);
        emit_neg_reg(cb, dr);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    /* â”€â”€ Bitwise â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_BIT_AND: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        /* immediate path: and reg, imm32 */
        const IROper *imm_op = NULL, *reg_op = NULL;
        if (ins->src2.kind == OPER_IMM) { imm_op = &ins->src2; reg_op = &ins->src1; }
        else if (ins->src1.kind == OPER_IMM) { imm_op = &ins->src1; reg_op = &ins->src2; }
        if (imm_op && imm_op->imm >= INT32_MIN && imm_op->imm <= INT32_MAX) {
            load_oper(ctx, dr, reg_op);
            emit_alu_ri32(cb, dr, 4, (int32_t)imm_op->imm);  /* and dr, imm */
        } else {
            int s2r = oper_phys(ctx, &ins->src2);
            const IROper *a = &ins->src1, *b = &ins->src2;
            if (s2r == dr) { const IROper *t = a; a = b; b = t; s2r = oper_phys(ctx, b); }
            load_oper(ctx, dr, a);
            int s2 = (s2r >= 0 && s2r != dr) ? s2r : RCX;
            if (s2 != s2r) load_oper(ctx, s2, b);
            emit_alu_rr(cb, 0x21, dr, s2);  /* and dr, s2 */
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_BIT_OR: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        /* immediate path: or reg, imm32 */
        const IROper *imm_op = NULL, *reg_op = NULL;
        if (ins->src2.kind == OPER_IMM) { imm_op = &ins->src2; reg_op = &ins->src1; }
        else if (ins->src1.kind == OPER_IMM) { imm_op = &ins->src1; reg_op = &ins->src2; }
        if (imm_op && imm_op->imm >= INT32_MIN && imm_op->imm <= INT32_MAX) {
            load_oper(ctx, dr, reg_op);
            emit_alu_ri32(cb, dr, 1, (int32_t)imm_op->imm);  /* or dr, imm */
        } else {
            int s2r = oper_phys(ctx, &ins->src2);
            const IROper *a = &ins->src1, *b = &ins->src2;
            if (s2r == dr) { const IROper *t = a; a = b; b = t; s2r = oper_phys(ctx, b); }
            load_oper(ctx, dr, a);
            int s2 = (s2r >= 0 && s2r != dr) ? s2r : RCX;
            if (s2 != s2r) load_oper(ctx, s2, b);
            emit_alu_rr(cb, 0x09, dr, s2);  /* or dr, s2 */
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_BIT_XOR: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        /* immediate path: xor reg, imm32 */
        const IROper *imm_op = NULL, *reg_op = NULL;
        if (ins->src2.kind == OPER_IMM) { imm_op = &ins->src2; reg_op = &ins->src1; }
        else if (ins->src1.kind == OPER_IMM) { imm_op = &ins->src1; reg_op = &ins->src2; }
        if (imm_op && imm_op->imm >= INT32_MIN && imm_op->imm <= INT32_MAX) {
            load_oper(ctx, dr, reg_op);
            emit_alu_ri32(cb, dr, 6, (int32_t)imm_op->imm);  /* xor dr, imm */
        } else {
            int s2r = oper_phys(ctx, &ins->src2);
            const IROper *a = &ins->src1, *b = &ins->src2;
            if (s2r == dr) { const IROper *t = a; a = b; b = t; s2r = oper_phys(ctx, b); }
            load_oper(ctx, dr, a);
            int s2 = (s2r >= 0 && s2r != dr) ? s2r : RCX;
            if (s2 != s2r) load_oper(ctx, s2, b);
            emit_alu_rr(cb, 0x31, dr, s2);  /* xor dr, s2 */
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_SHL: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        load_oper(ctx, dr, &ins->src1);
        if (ins->src2.kind == OPER_IMM) {
            emit_shift_imm(cb, dr, 4, (uint8_t)(ins->src2.imm & 31));
        } else {
            if (dr == RCX) dr = RAX;
            load_oper(ctx, dr, &ins->src1);
            load_oper(ctx, RCX, &ins->src2);
            emit_shift_cl(cb, dr, 4);
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    case IR_SHR: {
        int dr = dest_reg(ctx, &ins->dest, RAX);
        uint8_t ext = ins->extra ? 5 : 7; /* shr or sar */
        load_oper(ctx, dr, &ins->src1);
        if (ins->src2.kind == OPER_IMM) {
            emit_shift_imm(cb, dr, ext, (uint8_t)(ins->src2.imm & 31));
        } else {
            if (dr == RCX) dr = RAX;
            load_oper(ctx, dr, &ins->src1);
            load_oper(ctx, RCX, &ins->src2);
            emit_shift_cl(cb, dr, ext);
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, dr, ins->dest.size);
        break;
    }

    /* â”€â”€ Comparison â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_CMP_EQ:
    case IR_CMP_NE:
    case IR_CMP_LT:
    case IR_CMP_LE:
    case IR_CMP_GT:
    case IR_CMP_GE: {
        /* Load operands using register-aware approach */
        int s1r = oper_phys(ctx, &ins->src1);
        int r1 = (s1r >= 0) ? s1r : RAX;
        if (s1r < 0 || s1r != r1) load_oper(ctx, r1, &ins->src1);
        if (ins->src2.kind == OPER_IMM &&
            ins->src2.imm >= INT32_MIN && ins->src2.imm <= INT32_MAX) {
            emit_cmp_reg_imm32(cb, r1, (int32_t)ins->src2.imm);
        } else {
            int s2r = oper_phys(ctx, &ins->src2);
            int r2 = (s2r >= 0 && s2r != r1) ? s2r : RCX;
            if (r1 == r2) { r1 = RAX; r2 = RCX; load_oper(ctx, r1, &ins->src1); }
            if (s2r < 0 || s2r != r2) load_oper(ctx, r2, &ins->src2);
            emit_cmp_rr(cb, r1, r2);
        }

        /* Map opcode to condition code */
        uint8_t cc;
        if (ins->extra) {
            switch (ins->op) {
            case IR_CMP_EQ: cc = 0x04; break;
            case IR_CMP_NE: cc = 0x05; break;
            case IR_CMP_LT: cc = 0x02; break;
            case IR_CMP_LE: cc = 0x06; break;
            case IR_CMP_GT: cc = 0x07; break;
            case IR_CMP_GE: cc = 0x03; break;
            default: cc = 0x04; break;
            }
        } else {
            switch (ins->op) {
            case IR_CMP_EQ: cc = 0x04; break;
            case IR_CMP_NE: cc = 0x05; break;
            case IR_CMP_LT: cc = 0x0C; break;
            case IR_CMP_LE: cc = 0x0E; break;
            case IR_CMP_GT: cc = 0x0F; break;
            case IR_CMP_GE: cc = 0x0D; break;
            default: cc = 0x04; break;
            }
        }

        /* â”€â”€ CMP+Branch fusion â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
        /* If the next IR instruction is JZ/JNZ on the same temp,
         * emit CMP + Jcc directly instead of SETCC+MOVZX+store
         * then load+TEST+Jcc (saves ~5 instructions).           */
        if (ins->dest.kind == OPER_TEMP && idx + 1 < fn->instr_count) {
            const IRInstr *next = &fn->instrs[idx + 1];
            if ((next->op == IR_JZ || next->op == IR_JNZ) &&
                next->src1.kind == OPER_TEMP &&
                next->src1.temp_id == ins->dest.temp_id) {
                /* JNZ = jump when cmp is true  â†’ use cc as-is
                 * JZ  = jump when cmp is false â†’ invert cc (XOR 1) */
                uint8_t bcc = (next->op == IR_JNZ) ? cc : (uint8_t)(cc ^ 1);
                int lbl = next->dest.label_id;
                int target = get_label(ctx, lbl);
                if (target >= 0) {
                    cb_emit8(cb, 0x0F);
                    cb_emit8(cb, (uint8_t)(0x80 + bcc));
                    int from = cb_pos(cb) + 4;
                    cb_emit32(cb, (uint32_t)(target - from));
                } else {
                    int patch = emit_jcc_rel32(cb, bcc);
                    add_reloc(ctx, RELOC_REL32, patch, NULL, lbl, 0);
                }
                return 2;  /* consumed CMP + JZ/JNZ */
            }
        }

        /* Non-fused fallback: materialize boolean result */
        emit_setcc(cb, cc);
        emit_movzx_rax_al(cb);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ Logical NOT â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_LOG_NOT:
        load_oper(ctx, RAX, &ins->src1);
        emit_test_rr(cb, RAX, RAX);
        emit_setcc(cb, 0x04);              /* sete al (ZF=1 when RAX==0) */
        emit_movzx_rax_al(cb);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;

    /* â”€â”€ Conditional move â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_CMOV: {
        /* if (src2 != 0) dest = src1 */
        load_oper(ctx, RAX, &ins->dest);   /* current default value */
        load_oper(ctx, RCX, &ins->src1);   /* alternative value */
        load_oper(ctx, RDX, &ins->src2);   /* condition (0 or 1) */
        emit_test_rr(cb, RDX, RDX);
        /* CMOVne rax, rcx â€” REX.W 0F 45 ModRM(3,rax,rcx) */
        cb_emit8(cb, rex(1, RAX, 0, RCX));
        cb_emit8(cb, 0x0F);
        cb_emit8(cb, 0x45);               /* CMOVne */
        cb_emit8(cb, modrm(3, RAX, RCX));
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ Labels â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_LABEL:
        src_flush();  /* branch target â€” register state unknown */
        set_label(ctx, ins->dest.label_id, cb_pos(cb));
        break;

    /* â”€â”€ Unconditional jump â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_JMP: {
        int lbl = ins->dest.label_id;
        int target = get_label(ctx, lbl);
        if (target >= 0) {
            /* Backward jump â€“ target known */
            cb_emit8(cb, 0xE9);
            int from = cb_pos(cb) + 4;
            cb_emit32(cb, (uint32_t)(target - from));
        } else {
            /* Forward jump â€“ need to patch later */
            int patch = emit_jmp_rel32(cb);
            /* Store patch info associated with label */
            add_reloc(ctx, RELOC_REL32, patch, NULL, lbl, 0);
        }
        break;
    }

    /* â”€â”€ Conditional jumps â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
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

    /* â”€â”€ ARG: handled by IR_CALL below â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_ARG:
        /* IR_ARGs are processed directly by the IR_CALL handler
         * which scans backwards to find them.  Nothing to emit here. */
        break;

    /* â”€â”€ CALL â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_CALL: {
        /* Shadow + stack-arg space is pre-allocated in the frame.
         * Just load arguments and emit the call. */

        /* Walk backwards through the preceding IR_ARG instructions
         * and emit register loads / stack stores now. */
        {
            int call_idx = (int)(ins - fn->instrs);
            for (int a = call_idx - 1; a >= 0 && fn->instrs[a].op == IR_ARG; a--) {
                const IRInstr *ai = &fn->instrs[a];
                int arg_idx = (int)ai->dest.imm;
                if (arg_idx < 4) {
                    load_oper(ctx, win64_arg_regs[arg_idx], &ai->src1);
                } else {
                    load_oper(ctx, RAX, &ai->src1);
                    emit_store_mem(cb, RSP, (int32_t)((arg_idx - 4) * 8), RAX);
                }
            }
        }

        /* Emit call */
        if (ins->src1.kind == OPER_FUNC) {
            emit_call_sym(ctx, ins->src1.func_name);
        } else {
            x64_error("IR_CALL with non-function operand");
        }

        /* Result in RAX â†’ store to dest */
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ RET â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_RET:
        load_oper(ctx, RAX, &ins->src1);
        __attribute__((fallthrough));
    case IR_RET_VOID: {
        /* Dead-jump elimination: if all remaining IR instructions are
         * NOP/LABEL, the epilogue follows directly â€” no jmp needed. */
        bool at_end = true;
        for (int j = idx + 1; j < fn->instr_count; j++) {
            IROpcode op2 = fn->instrs[j].op;
            if (op2 != IR_NOP && op2 != IR_LABEL) {
                at_end = false;
                break;
            }
        }
        if (!at_end) {
            int lbl = ctx->epilogue_label;
            int target = get_label(ctx, lbl);
            if (target >= 0) {
                cb_emit8(cb, 0xE9);
                int from = cb_pos(cb) + 4;
                cb_emit32(cb, (uint32_t)(target - from));
            } else {
                int patch = emit_jmp_rel32(cb);
                add_reloc(ctx, RELOC_REL32, patch, NULL, lbl, 0);
            }
        }
        break;
    }

    /* â”€â”€ WRITE (built-in I/O) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_WRITE: {
        /* src1 = value to write, dest.imm = newline flag
         * extra = write-type hint (0=int, 1=str, 2=bool, 3=char) */
        load_oper(ctx, RAX, &ins->src1);

        /* Prepare argument: value in RCX (first arg, Win64) */
        emit_mov_reg_reg64(cb, RCX, RAX);

        /* Shadow space is pre-allocated in the frame */
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
        break;
    }

    /* â”€â”€ READ (built-in I/O) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_READ: {
        /* dest = result temp, src1.imm = read kind */
        /* Shadow space is pre-allocated in the frame */
        switch ((int)ins->src1.imm) {
        case 1:  emit_call_sym(ctx, RT_READ_LINE);   break; /* read line */
        case 3:  emit_call_sym(ctx, RT_READ_FAILED); break; /* read_failed flag */
        default: emit_call_sym(ctx, RT_READ_I64);    break; /* read i64 */
        }

        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ INDEX_LOAD: dest = base[idx] â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_INDEX_LOAD: {
        /* dest = result, src1 = base (stack off), src2 = index
         * extra = element size | 0x100 if unsigned */
        bool is_unsig = (ins->extra & 0x100) != 0;
        int esz = (ins->extra & 0xFF) ? (ins->extra & 0xFF) : 8;

        /* Load index into RCX */
        load_oper(ctx, RCX, &ins->src2);

        /* Multiply index by element size â†’ RCX */
        emit_load_imm(cb, RDX, esz);
        emit_imul_rr(cb, RCX, RDX);

        /* LEA base address â†’ RAX (stack_off is already negative) */
        if (ins->src1.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->src1.stack_off);
        else
            load_oper(ctx, RAX, &ins->src1);

        /* Add offset (array grows upward in our layout) */
        emit_alu_rr64(cb, 0x01, RAX, RCX);  /* add rax, rcx */

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
                /* mov eax, [rax] â€” auto zero-extends to rax */
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
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ INDEX_STORE: base[idx] = val â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_INDEX_STORE: {
        /* dest = base (stack off), src1 = index, src2 = value
         * extra = element size */
        int esz = ins->extra ? ins->extra : 8;

        load_oper(ctx, RCX, &ins->src1);   /* index */
        emit_load_imm(cb, RDX, esz);
        emit_imul_rr(cb, RCX, RDX);         /* RCX = index * esz */

        /* LEA base address â†’ RAX (stack_off is already negative) */
        if (ins->dest.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->dest.stack_off);
        else
            load_oper(ctx, RAX, &ins->dest);

        emit_alu_rr64(cb, 0x01, RAX, RCX);   /* RAX = base + offset */

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

    /* â”€â”€ FIELD_LOAD: dest = *(base + offset), width-aware â”€â”€ */
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
            emit_alu_rr64(cb, 0x01, RAX, RCX);  /* add rax, rcx */
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
                /* mov eax, [rax] â€” auto zero-extends to rax */
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
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ FIELD_STORE: *(base + offset) = val, width-aware â”€â”€ */
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
            emit_alu_rr64(cb, 0x01, RAX, RDX);  /* add rax, rdx */
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

    /* â”€â”€ LEA: dest = address of stack slot â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_LEA:
        if (ins->src1.kind == OPER_STACK)
            emit_lea_rbp(cb, RAX, ins->src1.stack_off);
        else
            load_oper(ctx, RAX, &ins->src1);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;

    /* â”€â”€ MEMCPY â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_MEMCPY: {
        /* dest = dst_addr (temp), src1 = src_addr (temp),
         * src2.imm = byte count, extra: 0=runtime, 1=compile */
        load_oper(ctx, RCX, &ins->dest);   /* arg0: dst */
        load_oper(ctx, RDX, &ins->src1);   /* arg1: src */
        emit_load_imm(cb, R8, ins->src2.imm); /* arg2: count */
        if (ins->extra) {
            /* copy.compile â€” inline byte-copy loop (no call overhead) */
            /* test r8, r8 */
            cb_emit8(cb, 0x4D); cb_emit8(cb, 0x85); cb_emit8(cb, 0xC0);
            /* jz skip (patch offset below) */
            cb_emit8(cb, 0x74);
            int jz_patch = cb->len;
            cb_emit8(cb, 0x00); /* placeholder */
            /* loop: movzx eax, byte [rdx] */
            int loop_top = cb->len;
            cb_emit8(cb, 0x0F); cb_emit8(cb, 0xB6); cb_emit8(cb, 0x02);
            /* mov [rcx], al */
            cb_emit8(cb, 0x88); cb_emit8(cb, 0x01);
            /* inc rcx */
            cb_emit8(cb, 0x48); cb_emit8(cb, 0xFF); cb_emit8(cb, 0xC1);
            /* inc rdx */
            cb_emit8(cb, 0x48); cb_emit8(cb, 0xFF); cb_emit8(cb, 0xC2);
            /* dec r8 */
            cb_emit8(cb, 0x49); cb_emit8(cb, 0xFF); cb_emit8(cb, 0xC8);
            /* jnz loop_top */
            cb_emit8(cb, 0x75);
            cb_emit8(cb, (uint8_t)(loop_top - (cb->len + 1)));
            /* patch jz to skip past loop */
            cb->data[jz_patch] = (uint8_t)(cb->len - (jz_patch + 1));
        } else {
            /* copy.runtime â€” REP MOVSB via runtime stub */
            /* Shadow space is pre-allocated in the frame */
            emit_call_sym(ctx, RT_MEMCPY);
        }
        break;
    }

    /* â”€â”€ STORE_IND: *dest = src1 (store through pointer) â”€â”€ */
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

    /* â”€â”€ Sign extend â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_SEXT: {
        load_oper(ctx, RAX, &ins->src1);
        int src_sz = ins->src1.size;
        if (src_sz == 1) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, 0xBE);                 /* movsx rax, al    */
            cb_emit8(cb, modrm(3, RAX, RAX));
        } else if (src_sz == 2) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, 0xBF);                 /* movsx rax, ax    */
            cb_emit8(cb, modrm(3, RAX, RAX));
        } else if (src_sz == 4) {
            cb_emit8(cb, rex(1, RAX, 0, RAX));
            cb_emit8(cb, 0x63);                 /* movsxd rax, eax  */
            cb_emit8(cb, modrm(3, RAX, RAX));
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ Zero extend â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_ZEXT: {
        load_oper(ctx, RAX, &ins->src1);
        int src_sz = ins->src1.size;
        if (src_sz == 1) {
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, 0xB6);                 /* movzx eax, al    */
            cb_emit8(cb, modrm(3, RAX, RAX));
        } else if (src_sz == 2) {
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, 0xB7);                 /* movzx eax, ax    */
            cb_emit8(cb, modrm(3, RAX, RAX));
        } else if (src_sz == 4) {
            cb_emit8(cb, 0x89);                 /* mov eax, eax     */
            cb_emit8(cb, modrm(3, RAX, RAX));   /* clears upper 32  */
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ Truncate â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_TRUNC: {
        load_oper(ctx, RAX, &ins->src1);
        int dst_sz = ins->dest.size;
        if (dst_sz == 1) {
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, 0xB6);                 /* movzx eax, al    */
            cb_emit8(cb, modrm(3, RAX, RAX));
        } else if (dst_sz == 2) {
            cb_emit8(cb, 0x0F);
            cb_emit8(cb, 0xB7);                 /* movzx eax, ax    */
            cb_emit8(cb, modrm(3, RAX, RAX));
        } else if (dst_sz == 4) {
            cb_emit8(cb, 0x89);                 /* mov eax, eax     */
            cb_emit8(cb, modrm(3, RAX, RAX));   /* clears upper 32  */
        }
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ String concat: dest = str_concat(src1, src2) â”€â”€â”€â”€â”€ */
    case IR_STR_CONCAT:
        load_oper(ctx, RCX, &ins->src1);
        load_oper(ctx, RDX, &ins->src2);
        emit_call_sym(ctx, RT_STR_CONCAT);
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;

    /* â”€â”€ String equality: dest = str_eq(src1, src2) â”€â”€â”€â”€â”€â”€ */
    case IR_STR_EQ: {
        load_oper(ctx, RCX, &ins->src1);
        load_oper(ctx, RDX, &ins->src2);
        emit_call_sym(ctx, RT_STR_EQ);
        /* RAX = 0 if equal.  extra==0 â†’ want eq (sete), extra==1 â†’ want ne (setne) */
        /* test eax, eax */
        cb_emit8(cb, 0x85);
        cb_emit8(cb, modrm(3, RAX, RAX));
        if (ins->extra == 0) {
            /* sete al */
            cb_emit8(cb, 0x0F); cb_emit8(cb, 0x94); cb_emit8(cb, 0xC0);
        } else {
            /* setne al */
            cb_emit8(cb, 0x0F); cb_emit8(cb, 0x95); cb_emit8(cb, 0xC0);
        }
        /* movzx eax, al */
        cb_emit8(cb, 0x0F); cb_emit8(cb, 0xB6); cb_emit8(cb, modrm(3, RAX, RAX));
        if (ins->dest.kind == OPER_TEMP)
            store_temp(ctx, ins->dest.temp_id, RAX, ins->dest.size);
        break;
    }

    /* â”€â”€ SYSCALL â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    case IR_SYSCALL: {
        /* Arguments pre-loaded via IR_ARG.
         * syscall on Windows: not directly used; on Linux the
         * ELF backend would remap for System V ABI.
         * For now emit INT3 as placeholder. */
        cb_emit8(cb, 0xCC);  /* int3 â€“ breakpoint / placeholder */
        break;
    }

    default:
        x64_error("unhandled IR opcode %d", ins->op);
        break;
    }

    /* Post-flush: clear cache entries that load_oper may have set during
     * a non-safe instruction whose internal operations clobbered the
     * scratch registers (e.g. INDEX_LOAD does IMUL after load_oper). */
    if (need_post_flush)
        src_flush();

    return 1;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Function prologue / epilogue + instruction loop
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/*
 * Emit function epilogue: restore callee-saved registers, then
 * mov rsp, rbp; pop rbp; ret.
 * Used by IR_RET, IR_RET_VOID, and the implicit safety-net epilogue.
 */
static void emit_epilogue(X64Ctx *ctx)
{
    CodeBuf *cb = &ctx->code;

    /* Restore callee-saved registers (reverse order) */
    for (int i = ctx->callee_save_count - 1; i >= 0; i--) {
        int off = -(ctx->callee_save_base + (i + 1) * 8);
        emit_load_rbp64(cb, ctx->callee_save_regs[i], off);
    }

    emit_mov_reg_reg64(cb, RSP, RBP);
    emit_pop(cb, RBP);
    emit_ret(cb);
}

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

    /* â”€â”€ Register allocation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    opt_regalloc(&ctx->cur_ra, fn, ctx->arena);

    /* Build spill_map: maps temp_id â†’ compact spill slot index.
     * Only spilled temps get a slot; register-allocated temps get -1. */
    {
        int tc = ctx->cur_ra.temp_count;
        ctx->spill_map = (int *)arena_alloc(ctx->arena, tc * sizeof(int));
        int slot = 0;
        for (int t = 0; t < tc; t++) {
            if (ctx->cur_ra.temp_reg[t] == REG_SPILLED)
                ctx->spill_map[t] = slot++;
            else
                ctx->spill_map[t] = -1;
        }
    }

    /* Build list of callee-saved registers used by the allocator */
    ctx->callee_save_count = 0;
    for (int r = 0; r < 16; r++) {
        if (ctx->cur_ra.callee_used[r])
            ctx->callee_save_regs[ctx->callee_save_count++] = r;
    }

    /* Compute frame size (RSP-relative, no frame pointer).
     * Variables occupy [RSP + frame_size - 1] down to [RSP + frame_size - stack_size].
     * Spill slots below variables.  Call space at bottom.
     * Callee-saved GPRs are PUSHed before SUB RSP, so NOT included here.
     *
     * Only SPILLED temps need stack slots; register-allocated temps skip. */
    ctx->var_area_size = fn->stack_size;
    int spill_count = ctx->cur_ra.spill_count;
    int temps_space = spill_count * 8;

    /* Scan instructions to find maximum call-site allocation needed.
     * AXIS callees store params to their own frame (not the caller's
     * shadow space), and runtime stubs manage their own stack, so
     * no 32-byte shadow space is reserved.  Only extra stack args
     * for >4-parameter calls need space. */
    int max_call_alloc = 0;
    for (int i = 0; i < fn->instr_count; i++) {
        int needed = 0;
        if (fn->instrs[i].op == IR_CALL) {
            int nargs = (int)fn->instrs[i].src2.imm;
            if (nargs > 4) {
                needed = AXIS_ALIGN((nargs - 4) * 8, 16);
            }
        }
        if (needed > max_call_alloc) max_call_alloc = needed;
    }

    int raw = fn->stack_size + temps_space + max_call_alloc;
    int frame = AXIS_ALIGN(raw, 16);
    /* After CALL, RSP is 8-misaligned (return address).
     * Each callee-save PUSH subtracts 8:
     *   odd  push count -> RSP re-aligned (0 mod 16) -> frame 0 mod 16 OK
     *   even push count -> RSP still 8 mod 16        -> need +8          */
    if (!(ctx->callee_save_count & 1))
        frame += 8;
    ctx->frame_size = frame;
    xf->stack_size = frame;

    /* â”€â”€ Shrink-wrapping analysis â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
     * Scan the function's entry IR for a compare-and-branch on
     * a parameter that leads to a simple return.  Two cases:
     *   (a) BRANCH TARGET is a simple return â†’ post-prologue
     *       fast path (cmp+jcc after prologue; fast ret after
     *       epilogue, skipping callee-save restore).
     *   (b) FALL-THROUGH is a simple return â†’ pre-prologue
     *       fast path (cmp+jcc+ret BEFORE prologue; zero frame
     *       overhead, matching GCC -O1 shrink-wrapping).       */
    int  sw_patch = -1;       /* code-buf offset of Jcc's rel32 field  */
    bool sw_has_retval = false;
    int64_t sw_retval = 0;
    bool sw_branch_target = false;  /* (a) post-prologue fast path */
    bool sw_fallthrough   = false;  /* (b) pre-prologue fast path  */
    bool sw_ret_param     = false;  /* fall-through returns param  */
    int  sw_param_reg     = -1;
    int  sw_param_size    = 0;
    uint8_t sw_bcc        = 0;
    int  sw_preg          = -1;
    int32_t sw_fv         = 0;
    IROpcode sw_cop       = (IROpcode)0;
    int  sw_skip_start    = -1;   /* first IR index to skip (inclusive) */
    int  sw_skip_end      = -1;   /* last IR index to skip (exclusive)  */

    if (ctx->opt_level >= 1 && ctx->callee_save_count > 0 &&
        fn->param_count >= 1 && fn->param_count <= 4 &&
        fn->instr_count >= 3)
    {
        const IRInstr *ir = fn->instrs;
        int ic = fn->instr_count;
        int k = 0;
        while (k < ic && ir[k].op == IR_NOP) k++;

        /* Expect LOAD_VAR loading a parameter (first 4 only) */
        if (k < ic && ir[k].op == IR_LOAD_VAR &&
            ir[k].src1.kind == OPER_STACK)
        {
            int poff = ir[k].src1.stack_off;
            int pt   = ir[k].dest.temp_id;
            int pidx = -1;
            for (int p = 0; p < fn->param_count && p < 4; p++) {
                if (fn->param_info[p].offset == poff &&
                    !fn->param_info[p].is_field) {
                    pidx = p; break;
                }
            }
            if (pidx >= 0) {
                int j = k + 1;
                while (j < ic && ir[j].op == IR_NOP) j++;

                /* Optional LOAD_IMM for comparison constant */
                int64_t cval = 0;  int ct = -1;
                if (j < ic && ir[j].op == IR_LOAD_IMM) {
                    cval = ir[j].src1.imm;
                    ct   = ir[j].dest.temp_id;
                    j++;
                    while (j < ic && ir[j].op == IR_NOP) j++;
                }

                /* Expect CMP_xx */
                if (j < ic && ir[j].op >= IR_CMP_EQ && ir[j].op <= IR_CMP_GE) {
                    const IRInstr *cmp = &ir[j];
                    bool ok = false;
                    int64_t fv = 0;
                    bool swapped = false;

                    if (cmp->src1.kind == OPER_TEMP && cmp->src1.temp_id == pt) {
                        if (cmp->src2.kind == OPER_IMM) {
                            ok = true; fv = cmp->src2.imm;
                        } else if (ct >= 0 && cmp->src2.kind == OPER_TEMP &&
                                   cmp->src2.temp_id == ct) {
                            ok = true; fv = cval;
                        }
                    } else if (cmp->src2.kind == OPER_TEMP && cmp->src2.temp_id == pt) {
                        if (cmp->src1.kind == OPER_IMM) {
                            ok = true; fv = cmp->src1.imm; swapped = true;
                        } else if (ct >= 0 && cmp->src1.kind == OPER_TEMP &&
                                   cmp->src1.temp_id == ct) {
                            ok = true; fv = cval; swapped = true;
                        }
                    }

                    if (ok && fv >= INT32_MIN && fv <= INT32_MAX) {
                        int cmpt = cmp->dest.temp_id;
                        int b = j + 1;
                        while (b < ic && ir[b].op == IR_NOP) b++;

                        /* Expect JZ or JNZ on the CMP result */
                        if (b < ic &&
                            (ir[b].op == IR_JZ || ir[b].op == IR_JNZ) &&
                            ir[b].src1.kind == OPER_TEMP &&
                            ir[b].src1.temp_id == cmpt)
                        {
                            int blabel = ir[b].dest.label_id;
                            bool is_jnz = (ir[b].op == IR_JNZ);

                            /* Build x86 condition code */
                            IROpcode cop = cmp->op;
                            if (swapped) {
                                if (cop == IR_CMP_LT) cop = IR_CMP_GT;
                                else if (cop == IR_CMP_LE) cop = IR_CMP_GE;
                                else if (cop == IR_CMP_GT) cop = IR_CMP_LT;
                                else if (cop == IR_CMP_GE) cop = IR_CMP_LE;
                            }
                            uint8_t cc;
                            if (cmp->extra) { /* unsigned */
                                switch (cop) {
                                case IR_CMP_EQ: cc=0x04; break;
                                case IR_CMP_NE: cc=0x05; break;
                                case IR_CMP_LT: cc=0x02; break;
                                case IR_CMP_LE: cc=0x06; break;
                                case IR_CMP_GT: cc=0x07; break;
                                case IR_CMP_GE: cc=0x03; break;
                                default:        cc=0x04; break;
                                }
                            } else { /* signed */
                                switch (cop) {
                                case IR_CMP_EQ: cc=0x04; break;
                                case IR_CMP_NE: cc=0x05; break;
                                case IR_CMP_LT: cc=0x0C; break;
                                case IR_CMP_LE: cc=0x0E; break;
                                case IR_CMP_GT: cc=0x0F; break;
                                case IR_CMP_GE: cc=0x0D; break;
                                default:        cc=0x04; break;
                                }
                            }
                            uint8_t bcc = is_jnz ? cc : (uint8_t)(cc ^ 1);
                            int preg = win64_arg_regs[pidx];

                            /* Save analysis results for emission */
                            sw_bcc  = bcc;
                            sw_preg = preg;
                            sw_fv   = (int32_t)fv;
                            sw_cop  = cop;

                            /* (a) Check branch TARGET â†’ simple return */
                            int li = -1;
                            for (int i2 = 0; i2 < ic; i2++) {
                                if (ir[i2].op == IR_LABEL &&
                                    ir[i2].dest.label_id == blabel) {
                                    li = i2 + 1; break;
                                }
                            }
                            if (li >= 0) {
                                while (li < ic && ir[li].op == IR_NOP) li++;
                                if (li < ic && ir[li].op == IR_RET_VOID) {
                                    sw_branch_target = true;
                                } else if (li < ic && ir[li].op == IR_RET &&
                                           ir[li].src1.kind == OPER_IMM) {
                                    sw_branch_target = true;
                                    sw_has_retval = true;
                                    sw_retval = ir[li].src1.imm;
                                } else if (li < ic && ir[li].op == IR_LOAD_IMM) {
                                    int rt = ir[li].dest.temp_id;
                                    int64_t rv = ir[li].src1.imm;
                                    int ri = li + 1;
                                    while (ri < ic && ir[ri].op == IR_NOP) ri++;
                                    if (ri < ic && ir[ri].op == IR_RET &&
                                        ir[ri].src1.kind == OPER_TEMP &&
                                        ir[ri].src1.temp_id == rt) {
                                        sw_branch_target = true;
                                        sw_has_retval = true;
                                        sw_retval = rv;
                                    }
                                }
                            }

                            /* (b) Check FALL-THROUGH â†’ simple return */
                            if (!sw_branch_target) {
                                int ft = b + 1;
                                while (ft < ic && ir[ft].op == IR_NOP) ft++;
                                if (ft < ic && ir[ft].op == IR_RET_VOID) {
                                    sw_fallthrough = true;
                                    sw_skip_start = k + 1;
                                    sw_skip_end   = ft + 1;
                                } else if (ft < ic && ir[ft].op == IR_RET) {
                                    if (ir[ft].src1.kind == OPER_IMM) {
                                        sw_fallthrough = true;
                                        sw_has_retval = true;
                                        sw_retval = ir[ft].src1.imm;
                                        sw_skip_start = k + 1;
                                        sw_skip_end   = ft + 1;
                                    } else if (ir[ft].src1.kind == OPER_TEMP &&
                                               ir[ft].src1.temp_id == pt) {
                                        sw_fallthrough = true;
                                        sw_ret_param = true;
                                        sw_param_reg = preg;
                                        sw_param_size = fn->param_info[pidx].size;
                                        sw_skip_start = k + 1;
                                        sw_skip_end   = ft + 1;
                                    }
                                } else if (ft < ic && ir[ft].op == IR_LOAD_IMM) {
                                    int rt = ir[ft].dest.temp_id;
                                    int64_t rv = ir[ft].src1.imm;
                                    int ri = ft + 1;
                                    while (ri < ic && ir[ri].op == IR_NOP) ri++;
                                    if (ri < ic && ir[ri].op == IR_RET &&
                                        ir[ri].src1.kind == OPER_TEMP &&
                                        ir[ri].src1.temp_id == rt) {
                                        sw_fallthrough = true;
                                        sw_has_retval = true;
                                        sw_retval = rv;
                                        sw_skip_start = k + 1;
                                        sw_skip_end   = ri + 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* â”€â”€ Pre-prologue fast return (fall-through case) â”€â”€â”€â”€â”€
     * Emit cmp + jcc + retval + ret BEFORE the prologue.
     * Base-case callers pay zero frame overhead.              */
    if (sw_fallthrough) {
        if (sw_fv == 0 && (sw_cop == IR_CMP_EQ || sw_cop == IR_CMP_NE)) {
            emit_test_rr(cb, sw_preg, sw_preg);
        } else {
            emit_cmp_reg_imm32(cb, sw_preg, sw_fv);
        }
        int skip_patch = emit_jcc_rel32(cb, sw_bcc);
        if (sw_ret_param) {
            if (sw_param_size > 4)
                emit_mov_reg_reg64(cb, RAX, sw_param_reg);
            else
                emit_mov_reg_reg(cb, RAX, sw_param_reg);
        } else if (sw_has_retval) {
            emit_load_imm(cb, RAX, sw_retval);
        }
        emit_ret(cb);
        patch_jmp(cb, skip_patch);
    }

    /* â”€â”€ Prologue â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    emit_push(cb, RBP);
    emit_mov_reg_reg64(cb, RBP, RSP);
    if (frame > 0)
        emit_sub_rsp_imm32(cb, frame);

    /* â”€â”€ Post-prologue fast path (branch-target case) â”€â”€â”€â”€â”€â”€
     * CMP + Jcc emitted after prologue; the fast return itself
     * is placed after the epilogue (patched via sw_patch).     */
    if (sw_branch_target) {
        if (sw_fv == 0 && (sw_cop == IR_CMP_EQ || sw_cop == IR_CMP_NE)) {
            emit_test_rr(cb, sw_preg, sw_preg);
        } else {
            emit_cmp_reg_imm32(cb, sw_preg, sw_fv);
        }
        sw_patch = emit_jcc_rel32(cb, sw_bcc);
    }

    /* Save callee-saved registers to their stack slots */
    for (int i = 0; i < ctx->callee_save_count; i++) {
        int off = -(ctx->callee_save_base + (i + 1) * 8);
        emit_store_rbp64(cb, off, ctx->callee_save_regs[i]);
    }

    /* Spill incoming register parameters to their stack slots.
     * Win64 ABI: first 4 integer args in RCX, RDX, R8, R9.
     * Parameters beyond 4 are already on the caller's stack and
     * need to be copied to the callee's local slots.
     * Field/struct params are passed by pointer â€” we first spill
     * ALL register args as raw 8-byte values (preserving pointers),
     * then memcpy field params in a second pass. This avoids
     * clobbering registers before all args are saved. */

    /* â”€â”€ Param-spill elimination analysis (O1+) â”€â”€â”€â”€â”€â”€â”€â”€â”€
     * For each register param: if it is NOT a field param, has no
     * STORE_VARs to its slot, no IR_LEA taking its address, and
     * exactly one IR_LOAD_VAR whose dest temp is register-allocated,
     * we can skip the memory spill and emit a direct reg-to-reg
     * transfer in the prologue, then skip the LOAD_VAR at codegen. */
    int  param_skip_ir[4]  = {-1, -1, -1, -1};  /* IR index to skip */
    bool param_skip[4]     = {false};             /* skip spill?      */
    int  param_dest[4]     = {0};                 /* dest phys reg    */
    int  param_src_size[4] = {0};                 /* src operand size */
    bool param_is_zx[4]    = {false};             /* zero-extend?     */

    {
        int n = fn->param_count < 4 ? fn->param_count : 4;

        if (ctx->opt_level >= 1) {
            for (int p = 0; p < n; p++) {
                if (fn->param_info[p].is_field)  continue;
                if (fn->param_info[p].is_update) continue;
                int poff = fn->param_info[p].offset;
                int load_count = 0, store_count = 0, lea_count = 0;
                int load_idx = -1;
                for (int j = 0; j < fn->instr_count; j++) {
                    const IRInstr *ins = &fn->instrs[j];
                    if (ins->op == IR_LOAD_VAR &&
                        ins->src1.kind == OPER_STACK &&
                        ins->src1.stack_off == poff) {
                        load_count++;
                        load_idx = j;
                    }
                    if (ins->op == IR_STORE_VAR &&
                        ins->dest.kind == OPER_STACK &&
                        ins->dest.stack_off == poff) {
                        store_count++;
                    }
                    if (ins->op == IR_LEA &&
                        ins->src1.kind == OPER_STACK &&
                        ins->src1.stack_off == poff) {
                        lea_count++;
                    }
                }
                if (load_count != 1 || store_count != 0 || lea_count != 0)
                    continue;
                /* Check that the LOAD_VAR dest temp is register-allocated */
                const IRInstr *li = &fn->instrs[load_idx];
                if (li->dest.kind != OPER_TEMP) continue;
                int dreg = temp_phys(ctx, li->dest.temp_id);
                if (dreg < 0 || dreg == REG_SPILLED) continue;
                param_skip[p]     = true;
                param_skip_ir[p]  = load_idx;
                param_dest[p]     = dreg;
                param_src_size[p] = li->src1.size;
                param_is_zx[p]    = (li->extra != 0);
            }
            /* Conflict check: if an optimised param's dest_reg equals
             * another param's ABI source reg, disable the former to
             * avoid clobbering before the source is consumed. */
            for (int p = 0; p < n; p++) {
                if (!param_skip[p]) continue;
                for (int q = 0; q < n; q++) {
                    if (q == p) continue;
                    if (param_dest[p] == win64_arg_regs[q]) {
                        param_skip[p] = false;
                        break;
                    }
                }
            }
        }

        /* Pass 1: spill all register args (pointers or scalars) */
        for (int i = 0; i < n; i++) {
            if (param_skip[i]) continue;   /* optimised â€” no spill needed */
            int off  = fn->param_info[i].offset;
            int size = fn->param_info[i].size;
            if (fn->param_info[i].is_field) {
                /* Save the pointer (8 bytes) to the local slot.
                 * The slot is field_size bytes wide, so 8 bytes is fine. */
                emit_store_rbp64(cb, off, win64_arg_regs[i]);
            } else {
                emit_store_rbp_sz(cb, off, win64_arg_regs[i], size);
            }
        }

        /* Pass 1b: emit direct reg-to-reg transfers for optimised params.
         * This runs AFTER all non-optimised spills so we don't clobber
         * any ABI registers before they are saved. */
        for (int p = 0; p < n; p++) {
            if (!param_skip[p]) continue;
            int src = win64_arg_regs[p];
            int dst = param_dest[p];
            int sz  = param_src_size[p];
            if (param_is_zx[p]) {
                /* zero-extend */
                switch (sz) {
                case 1: /* movzx r32, r8  */
                    emit_rex32(cb, dst, 0, src);
                    cb_emit8(cb, 0x0F); cb_emit8(cb, 0xB6);
                    cb_emit8(cb, modrm(3, dst, src));
                    break;
                case 2: /* movzx r32, r16 */
                    emit_rex32(cb, dst, 0, src);
                    cb_emit8(cb, 0x0F); cb_emit8(cb, 0xB7);
                    cb_emit8(cb, modrm(3, dst, src));
                    break;
                case 4: /* mov r32, r32 â€” implicit zero-ext */
                    emit_mov_reg_reg(cb, dst, src);
                    break;
                default: /* 8-byte: plain 64-bit mov */
                    if (dst != src) emit_mov_reg_reg64(cb, dst, src);
                    break;
                }
            } else {
                /* sign-extend */
                switch (sz) {
                case 1: /* movsx r64, r8  */
                    cb_emit8(cb, rex(1, dst, 0, src));
                    cb_emit8(cb, 0x0F); cb_emit8(cb, 0xBE);
                    cb_emit8(cb, modrm(3, dst, src));
                    break;
                case 2: /* movsx r64, r16 */
                    cb_emit8(cb, rex(1, dst, 0, src));
                    cb_emit8(cb, 0x0F); cb_emit8(cb, 0xBF);
                    cb_emit8(cb, modrm(3, dst, src));
                    break;
                case 4: /* movsxd r64, r32 */
                    cb_emit8(cb, rex(1, dst, 0, src));
                    cb_emit8(cb, 0x63);
                    cb_emit8(cb, modrm(3, dst, src));
                    break;
                default: /* 8-byte: plain 64-bit mov */
                    if (dst != src) emit_mov_reg_reg64(cb, dst, src);
                    break;
                }
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
            emit_load_rbp64(cb, RDX, off);
            emit_lea_rbp(cb, RCX, off);             /* dst */
            emit_load_imm(cb, R8, fsz);             /* count */
            /* Shadow space is pre-allocated in the frame */
            emit_call_sym(ctx, RT_MEMCPY);
        }

        /* Args 5..N: caller placed them at [old_rsp + 8 + (i-4)*8].
         * After push rbp, that's [rbp + 16 + (i-4)*8].             */
        for (int i = 4; i < fn->param_count; i++) {
            int caller_off = 16 + (i - 4) * 8;
            emit_load_rbp64(cb, RAX, caller_off);
            int off  = fn->param_info[i].offset;
            int size = fn->param_info[i].size;
            if (fn->param_info[i].is_field) {
                int fsz = fn->param_info[i].field_size;
                emit_mov_reg_reg64(cb, RDX, RAX);     /* src ptr */
                emit_lea_rbp(cb, RCX, off);           /* dst */
                emit_load_imm(cb, R8, fsz);           /* count */
                /* Shadow space is pre-allocated in the frame */
                emit_call_sym(ctx, RT_MEMCPY);
            } else {
                emit_store_rbp_sz(cb, off, RAX, size);
            }
        }
    }

    /* Reset label table for this function */
    for (int i = 0; i < ctx->label_cap; i++)
        ctx->label_offsets[i] = -1;

    /* Reserve a label ID for the shared epilogue (max label + 1) */
    int max_lbl = -1;
    {
        for (int i = 0; i < fn->instr_count; i++)
            if (fn->instrs[i].op == IR_LABEL && fn->instrs[i].dest.label_id > max_lbl)
                max_lbl = fn->instrs[i].dest.label_id;
        ctx->epilogue_label = max_lbl + 1;
    }

    /* â”€â”€ Identify loop header labels (back-edge targets) for alignment â”€â”€ */
    int lbl_count = max_lbl + 2;  /* +1 for epilogue, +1 for size */
    bool *loop_headers = (bool *)calloc((size_t)lbl_count, sizeof(bool));
    {
        bool *label_seen = (bool *)calloc((size_t)lbl_count, sizeof(bool));
        for (int i = 0; i < fn->instr_count; i++) {
            const IRInstr *ins = &fn->instrs[i];
            if (ins->op == IR_LABEL)
                label_seen[ins->dest.label_id] = true;
            else if (ins->op == IR_JMP || ins->op == IR_JZ || ins->op == IR_JNZ) {
                int lbl = ins->dest.label_id;
                if (lbl < lbl_count && label_seen[lbl])
                    loop_headers[lbl] = true;
            }
        }
        free(label_seen);
    }

    /* â”€â”€ Instruction loop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    src_flush();  /* start with clean spill-reload cache */
    for (int i = 0; i < fn->instr_count; ) {
        /* Skip IR instructions already handled by pre-prologue shrink-wrap */
        if (sw_skip_start >= 0 && i >= sw_skip_start && i < sw_skip_end) {
            i++;
            continue;
        }
        /* Skip LOAD_VARs replaced by param-spill elimination */
        if (param_skip_ir[0] == i || param_skip_ir[1] == i ||
            param_skip_ir[2] == i || param_skip_ir[3] == i) {
            i++;
            continue;
        }
        /* Align loop header labels to 16-byte boundary (DSB optimization, O2+ only) */
        if (ctx->opt_level >= 2 && fn->instrs[i].op == IR_LABEL && loop_headers[fn->instrs[i].dest.label_id]) {
            while (cb_pos(cb) & 15)
                cb_emit8(cb, 0x90);  /* NOP padding */
        }
        i += gen_instr(ctx, fn, i);
    }
    free(loop_headers);

    /* â”€â”€ Shared epilogue â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    set_label(ctx, ctx->epilogue_label, cb_pos(cb));
    emit_epilogue(ctx);

    /* â”€â”€ Shrink-wrap fast return â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    if (sw_patch >= 0) {
        uint32_t fast_ret_pos = cb_pos(cb);
        if (sw_has_retval)
            emit_load_imm(cb, RAX, sw_retval);
        emit_mov_reg_reg64(cb, RSP, RBP);
        emit_pop(cb, RBP);
        emit_ret(cb);
        /* Patch the Jcc rel32 to point here */
        cb_patch32(&ctx->code, sw_patch,
                   (uint32_t)(fast_ret_pos - (sw_patch + 4)));
    }

    xf->text_size = cb_pos(cb) - xf->text_offset;

    /* Resolve label relocs for THIS function while labels are still valid */
    resolve_label_relocs(ctx);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Label relocation resolution (second pass within .text)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

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

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Resolve function call relocations (intra-module)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static int find_func_offset(const X64Ctx *ctx, const char *name)
{
    for (int i = 0; i < ctx->func_count; i++)
        if (strcmp(ctx->funcs[i].name, name) == 0)
            return ctx->funcs[i].text_offset;
    return -1; /* external / runtime â€“ leave for PE/ELF linker */
}

static void resolve_func_relocs(X64Ctx *ctx)
{
    for (int i = 0; i < ctx->reloc_count; i++) {
        Reloc *r = &ctx->relocs[i];
        if (r->target_sym == NULL) continue;
        if (r->kind != RELOC_REL32) continue;

        int target = find_func_offset(ctx, r->target_sym);
        if (target < 0) continue;  /* external â€“ keep for linker */

        int from = r->offset + 4;
        int rel = target - from + r->addend;
        cb_patch32(&ctx->code, r->offset, (uint32_t)rel);
        r->kind = (RelocKind)-1; /* resolved */
    }
    compact_relocs(ctx);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Public API
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

void x64_codegen(X64Ctx *ctx, const IRProgram *ir, Arena *arena)
{
    int saved_opt = ctx->opt_level;
    memset(ctx, 0, sizeof(*ctx));
    ctx->opt_level = saved_opt;
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

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Debug dump
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

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
        fprintf(out, "  [%d] %s at .text+0x%04X â†’ %s (addend=%d)\n",
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
