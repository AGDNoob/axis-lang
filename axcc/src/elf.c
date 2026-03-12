/*
 * elf.c – Minimal ELF64 executable writer for the AXIS compiler (Linux x86-64).
 *
 * Produces a standalone static executable with:
 *   .text  – generated x86-64 machine code + runtime stubs
 *   .data  – string literals + runtime buffers
 *
 * Runtime stubs (__axis_write_i64, etc.) use Linux syscalls directly
 * (no libc dependency).  The code generator passes arguments using the
 * Windows x64 ABI internally (RCX, RDX, R8, R9); stubs remap to
 * System V as needed for syscalls.
 *
 * Syscalls used:
 *   SYS_read  (0)  – stdin input
 *   SYS_write (1)  – stdout output
 *   SYS_exit  (60) – process termination
 */

#include "axis_elf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

/* x86-64 register indices (matching x64.c) */
enum {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11_ = 11
};

/* Linux syscall numbers */
#define SYS_READ   0
#define SYS_WRITE  1
#define SYS_EXIT   60

/* ═════════════════════════════════════════════════════════════
 * ELF64 header structures
 * ═════════════════════════════════════════════════════════════ */

#pragma pack(push, 1)

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#pragma pack(pop)

/* ELF constants */
#define ET_EXEC     2
#define EM_X86_64   0x3E
#define PT_LOAD     1
#define PF_X        1
#define PF_W        2
#define PF_R        4

/* ═════════════════════════════════════════════════════════════
 * Stub buffer – growable byte array for emitting runtime stubs
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t *data;
    int      len;
    int      cap;
} StubBuf;

static void sb_init(StubBuf *sb)
{
    sb->cap = 2048;
    sb->len = 0;
    sb->data = (uint8_t *)calloc(1, (size_t)sb->cap);
}

static void sb_emit8(StubBuf *sb, uint8_t v)
{
    if (sb->len >= sb->cap) {
        sb->cap *= 2;
        sb->data = (uint8_t *)realloc(sb->data, (size_t)sb->cap);
    }
    sb->data[sb->len++] = v;
}

static void sb_emit32(StubBuf *sb, uint32_t v)
{
    for (int i = 0; i < 4; i++) sb_emit8(sb, (uint8_t)(v >> (i * 8)));
}

static void sb_free(StubBuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

/* ═════════════════════════════════════════════════════════════
 * Runtime data appended to .data (after x64 string literals)
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    int true_off;       /* offset within .data of "true\0"   */
    int false_off;      /* offset within .data of "false\0"  */
    int buf_off;        /* offset within .data of 256B input buffer */
    int flag_off;       /* offset within .data of read_failed flag  */
    int total;          /* total .data size including additions     */
} RtData;

static void init_rt_data(RtData *rt, int rdata_len)
{
    int off = rdata_len;
    rt->true_off  = off; off += 5;    /* "true\0"  */
    rt->false_off = off; off += 6;    /* "false\0" */
    rt->buf_off   = off; off += 256;  /* input buffer */
    rt->flag_off  = off; off += 1;    /* read_failed flag */
    rt->total     = off;
}

/* ═════════════════════════════════════════════════════════════
 * Stub offsets (section-relative, within .text)
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    int write_i64_off;
    int write_str_off;
    int write_bool_off;
    int write_char_off;
    int write_nl_off;
    int read_i64_off;
    int read_line_off;
    int read_char_off;
    int read_failed_off;
    int memcpy_off;
} StubOffsets;

/* ═════════════════════════════════════════════════════════════
 * RIP-relative emit helpers
 *
 * All helpers take (text_va, stub_text_off) where:
 *   stub_text_off = user_code_len (stubs start after user code)
 *   actual VA = text_va + stub_text_off + sb->len
 * ═════════════════════════════════════════════════════════════ */

/* LEA reg, [rip + disp32]  →  7 bytes */
static void sb_emit_lea_rip(StubBuf *sb, int reg,
                            uint64_t data_va, int data_off,
                            uint64_t text_va, int stub_text_off)
{
    uint64_t rip = text_va + (uint64_t)stub_text_off
                 + (uint64_t)sb->len + 7;
    int32_t disp = (int32_t)((int64_t)(data_va + (uint64_t)data_off)
                           - (int64_t)rip);
    sb_emit8(sb, (uint8_t)(0x48 | ((reg >= 8) ? 0x04 : 0)));  /* REX.W [+R] */
    sb_emit8(sb, 0x8D);
    sb_emit8(sb, (uint8_t)(0x05 | ((reg & 7) << 3)));
    sb_emit32(sb, (uint32_t)disp);
}

/* MOV byte [rip + disp32], imm8  →  7 bytes */
static void sb_emit_mov_rip_byte(StubBuf *sb, uint8_t val,
                                 uint64_t data_va, int data_off,
                                 uint64_t text_va, int stub_text_off)
{
    uint64_t rip = text_va + (uint64_t)stub_text_off
                 + (uint64_t)sb->len + 7;
    int32_t disp = (int32_t)((int64_t)(data_va + (uint64_t)data_off)
                           - (int64_t)rip);
    sb_emit8(sb, 0xC6);          /* MOV r/m8, imm8 */
    sb_emit8(sb, 0x05);          /* modrm: [rip+disp32] */
    sb_emit32(sb, (uint32_t)disp);
    sb_emit8(sb, val);
}

/* MOVZX eax, byte [rip + disp32]  →  7 bytes */
static void sb_emit_movzx_eax_rip(StubBuf *sb,
                                  uint64_t data_va, int data_off,
                                  uint64_t text_va, int stub_text_off)
{
    uint64_t rip = text_va + (uint64_t)stub_text_off
                 + (uint64_t)sb->len + 7;
    int32_t disp = (int32_t)((int64_t)(data_va + (uint64_t)data_off)
                           - (int64_t)rip);
    sb_emit8(sb, 0x0F);
    sb_emit8(sb, 0xB6);
    sb_emit8(sb, 0x05);          /* modrm: eax, [rip+disp32] */
    sb_emit32(sb, (uint32_t)disp);
}

/* ═════════════════════════════════════════════════════════════
 * Convenience stub emitters
 * ═════════════════════════════════════════════════════════════ */

static void sb_emit_push_rbp(StubBuf *sb)   { sb_emit8(sb, 0x55); }
static void sb_emit_mov_rbp_rsp(StubBuf *sb) {
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xE5);
}
static void sb_emit_leave(StubBuf *sb)       { sb_emit8(sb, 0xC9); }
static void sb_emit_ret(StubBuf *sb)         { sb_emit8(sb, 0xC3); }
static void sb_emit_syscall(StubBuf *sb)     { sb_emit8(sb, 0x0F); sb_emit8(sb, 0x05); }

/* sub rsp, imm8 */
static void sb_emit_sub_rsp(StubBuf *sb, uint8_t imm)
{
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x83);
    sb_emit8(sb, 0xEC); sb_emit8(sb, imm);
}

/* ═════════════════════════════════════════════════════════════
 * Generate all runtime stubs using Linux syscalls
 * ═════════════════════════════════════════════════════════════ */

static StubOffsets gen_stubs(StubBuf *sb,
                             const RtData *rt,
                             uint64_t text_va,
                             uint64_t data_va,
                             int user_code_len)
{
    StubOffsets so;
    sb_init(sb);

    int base = user_code_len;

    /* ────────────────────────────────────────────────────────
     * __axis_write_i64(value in RCX)
     * Convert int64 to decimal string on stack, write to stdout.
     * ──────────────────────────────────────────────────────── */
    so.write_i64_off = base + sb->len;

    sb_emit_push_rbp(sb);                                     /* push rbp          */
    sb_emit_mov_rbp_rsp(sb);                                  /* mov rbp, rsp      */
    sb_emit_sub_rsp(sb, 48);                                  /* sub rsp, 48       */

    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xC8); /* mov rax, rcx   */
    sb_emit8(sb, 0x45); sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0); /* xor r8d, r8d   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0); /* test rax, rax  */
    sb_emit8(sb, 0x79); sb_emit8(sb, 0x09);                     /* jns .pos (+9)  */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0xF7); sb_emit8(sb, 0xD8); /* neg rax        */
    sb_emit8(sb, 0x41); sb_emit8(sb, 0xB8);                     /* mov r8d, 1     */
    sb_emit32(sb, 1);
    /* .pos: */
    sb_emit8(sb, 0x4C); sb_emit8(sb, 0x8D); sb_emit8(sb, 0x5D); /* lea r11,[rbp+0]*/
    sb_emit8(sb, 0x00);
    sb_emit8(sb, 0x49); sb_emit8(sb, 0xC7); sb_emit8(sb, 0xC1); /* mov r9, 10     */
    sb_emit32(sb, 10);

    /* .loop: */
    int loop_pos = sb->len;
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xD2);                     /* xor edx, edx   */
    sb_emit8(sb, 0x49); sb_emit8(sb, 0xF7); sb_emit8(sb, 0xF1); /* div r9         */
    sb_emit8(sb, 0x80); sb_emit8(sb, 0xC2); sb_emit8(sb, 0x30); /* add dl, '0'    */
    sb_emit8(sb, 0x49); sb_emit8(sb, 0xFF); sb_emit8(sb, 0xCB); /* dec r11        */
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x88); sb_emit8(sb, 0x13); /* mov [r11], dl  */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0); /* test rax, rax  */
    /* jnz .loop */
    sb_emit8(sb, 0x75);
    sb_emit8(sb, (uint8_t)(loop_pos - (sb->len + 1)));

    sb_emit8(sb, 0x45); sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0); /* test r8d, r8d  */
    sb_emit8(sb, 0x74); sb_emit8(sb, 0x07);                     /* jz .write (+7) */
    sb_emit8(sb, 0x49); sb_emit8(sb, 0xFF); sb_emit8(sb, 0xCB); /* dec r11        */
    sb_emit8(sb, 0x41); sb_emit8(sb, 0xC6); sb_emit8(sb, 0x03); /* mov byte[r11], */
    sb_emit8(sb, 0x2D);                                          /*   '-'          */

    /* .write: write(1, r11, len) */
    sb_emit8(sb, 0xB8); sb_emit32(sb, SYS_WRITE);               /* mov eax, 1     */
    sb_emit8(sb, 0xBF); sb_emit32(sb, 1);                       /* mov edi, 1     */
    sb_emit8(sb, 0x4C); sb_emit8(sb, 0x89); sb_emit8(sb, 0xDE); /* mov rsi, r11   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x8D); sb_emit8(sb, 0x55); /* lea rdx,[rbp+0]*/
    sb_emit8(sb, 0x00);
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x29); sb_emit8(sb, 0xF2); /* sub rdx, rsi   */
    sb_emit_syscall(sb);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_write_str(pointer in RCX)
     * strlen + write to stdout.
     * ──────────────────────────────────────────────────────── */
    so.write_str_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xCF); /* mov rdi, rcx   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xCE); /* mov rsi, rcx   */

    /* .strlen_loop: */
    int str_loop = sb->len;
    sb_emit8(sb, 0x80); sb_emit8(sb, 0x3F); sb_emit8(sb, 0x00); /* cmp byte[rdi],0*/
    sb_emit8(sb, 0x74); sb_emit8(sb, 0x05);                     /* je .done (+5)  */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0xFF); sb_emit8(sb, 0xC7); /* inc rdi        */
    /* jmp .strlen_loop */
    sb_emit8(sb, 0xEB);
    sb_emit8(sb, (uint8_t)(str_loop - (sb->len + 1)));

    /* .done: rdi = past end, rsi = start */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xFA); /* mov rdx, rdi   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x29); sb_emit8(sb, 0xF2); /* sub rdx, rsi   */
    sb_emit8(sb, 0xB8); sb_emit32(sb, SYS_WRITE);               /* mov eax, 1     */
    sb_emit8(sb, 0xBF); sb_emit32(sb, 1);                       /* mov edi, 1     */
    sb_emit_syscall(sb);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_write_bool(value in RCX)
     * Write "true" or "false" to stdout.
     * ──────────────────────────────────────────────────────── */
    so.write_bool_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit8(sb, 0x85); sb_emit8(sb, 0xC9);                     /* test ecx, ecx  */

    int jz_patch = sb->len;
    sb_emit8(sb, 0x74); sb_emit8(sb, 0x00);                     /* jz .false (patch) */

    /* "true" path */
    sb_emit_lea_rip(sb, RSI, data_va, rt->true_off, text_va, base);
    sb_emit8(sb, 0xBA); sb_emit32(sb, 4);                       /* mov edx, 4     */
    int jmp_patch = sb->len;
    sb_emit8(sb, 0xEB); sb_emit8(sb, 0x00);                     /* jmp .write (patch) */

    /* .false: */
    sb->data[jz_patch + 1] = (uint8_t)(sb->len - (jz_patch + 2));
    sb_emit_lea_rip(sb, RSI, data_va, rt->false_off, text_va, base);
    sb_emit8(sb, 0xBA); sb_emit32(sb, 5);                       /* mov edx, 5     */

    /* .write: */
    sb->data[jmp_patch + 1] = (uint8_t)(sb->len - (jmp_patch + 2));
    sb_emit8(sb, 0xB8); sb_emit32(sb, SYS_WRITE);               /* mov eax, 1     */
    sb_emit8(sb, 0xBF); sb_emit32(sb, 1);                       /* mov edi, 1     */
    sb_emit_syscall(sb);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_write_char(char in CL)
     * Push char, write 1 byte to stdout.
     * ──────────────────────────────────────────────────────── */
    so.write_char_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit8(sb, 0x51);                                          /* push rcx       */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xE6); /* mov rsi, rsp   */
    sb_emit8(sb, 0xBA); sb_emit32(sb, 1);                       /* mov edx, 1     */
    sb_emit8(sb, 0xB8); sb_emit32(sb, SYS_WRITE);               /* mov eax, 1     */
    sb_emit8(sb, 0xBF); sb_emit32(sb, 1);                       /* mov edi, 1     */
    sb_emit_syscall(sb);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_write_nl()
     * Write newline character.
     * ──────────────────────────────────────────────────────── */
    so.write_nl_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit8(sb, 0x6A); sb_emit8(sb, 0x0A);                     /* push 0x0A      */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xE6); /* mov rsi, rsp   */
    sb_emit8(sb, 0xBA); sb_emit32(sb, 1);                       /* mov edx, 1     */
    sb_emit8(sb, 0xB8); sb_emit32(sb, SYS_WRITE);               /* mov eax, 1     */
    sb_emit8(sb, 0xBF); sb_emit32(sb, 1);                       /* mov edi, 1     */
    sb_emit_syscall(sb);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_read_i64()
     * Read line from stdin, parse decimal integer, return in RAX.
     * Sets read_failed flag on error.
     * ──────────────────────────────────────────────────────── */
    so.read_i64_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit_sub_rsp(sb, 48);

    /* read(0, rbp-32, 31) */
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xFF);                     /* xor edi, edi   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x8D); sb_emit8(sb, 0x75); /* lea rsi,[rbp-32]*/
    sb_emit8(sb, 0xE0);
    sb_emit8(sb, 0xBA); sb_emit32(sb, 31);                      /* mov edx, 31    */
    sb_emit_syscall(sb);

    /* test eax, eax; jle .error */
    sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0);                     /* test eax, eax  */
    int jle_patch_ri = sb->len;
    sb_emit8(sb, 0x7E); sb_emit8(sb, 0x00);                     /* jle .error (patch) */

    /* Parse decimal: r10 = pointer, rax = result, r8d = neg flag */
    sb_emit8(sb, 0x4C); sb_emit8(sb, 0x8D); sb_emit8(sb, 0x55); /* lea r10,[rbp-32]*/
    sb_emit8(sb, 0xE0);
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit8(sb, 0x45); sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0); /* xor r8d, r8d   */

    /* Check for '-' */
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x0F); sb_emit8(sb, 0xB6); /* movzx ecx,[r10]*/
    sb_emit8(sb, 0x0A);
    sb_emit8(sb, 0x80); sb_emit8(sb, 0xF9); sb_emit8(sb, 0x2D); /* cmp cl, '-'    */
    sb_emit8(sb, 0x75); sb_emit8(sb, 0x09);                     /* jne .digit (+9)*/
    sb_emit8(sb, 0x41); sb_emit8(sb, 0xB8); sb_emit32(sb, 1);   /* mov r8d, 1     */
    sb_emit8(sb, 0x49); sb_emit8(sb, 0xFF); sb_emit8(sb, 0xC2); /* inc r10        */

    /* .digit: */
    int digit_loop = sb->len;
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x0F); sb_emit8(sb, 0xB6); /* movzx ecx,[r10]*/
    sb_emit8(sb, 0x0A);
    sb_emit8(sb, 0x80); sb_emit8(sb, 0xE9); sb_emit8(sb, 0x30); /* sub cl, '0'    */
    sb_emit8(sb, 0x80); sb_emit8(sb, 0xF9); sb_emit8(sb, 0x09); /* cmp cl, 9      */
    /* ja .parse_done (skip imul+movzx+add+inc+jmp = 4+3+3+3+2 = 15) */
    sb_emit8(sb, 0x77); sb_emit8(sb, 0x0F);
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x6B); sb_emit8(sb, 0xC0); /* imul rax,rax,10*/
    sb_emit8(sb, 0x0A);
    sb_emit8(sb, 0x0F); sb_emit8(sb, 0xB6); sb_emit8(sb, 0xC9); /* movzx ecx, cl  */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x01); sb_emit8(sb, 0xC8); /* add rax, rcx   */
    sb_emit8(sb, 0x49); sb_emit8(sb, 0xFF); sb_emit8(sb, 0xC2); /* inc r10        */
    /* jmp .digit */
    sb_emit8(sb, 0xEB);
    sb_emit8(sb, (uint8_t)(digit_loop - (sb->len + 1)));

    /* .parse_done: negate if needed */
    sb_emit8(sb, 0x45); sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0); /* test r8d, r8d  */
    sb_emit8(sb, 0x74); sb_emit8(sb, 0x03);                     /* jz .ok (+3)    */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0xF7); sb_emit8(sb, 0xD8); /* neg rax        */

    /* .ok: clear flag, return */
    sb_emit_mov_rip_byte(sb, 0, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* .error: set flag, return 0 */
    sb->data[jle_patch_ri + 1] = (uint8_t)(sb->len - (jle_patch_ri + 2));
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit_mov_rip_byte(sb, 1, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_read_line()
     * Read line from stdin into static buffer.
     * Strips trailing newline, null-terminates.
     * Returns pointer to buffer in RAX.
     * ──────────────────────────────────────────────────────── */
    so.read_line_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);

    /* read(0, buf, 255) */
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xFF);                     /* xor edi, edi   */
    sb_emit_lea_rip(sb, RSI, data_va, rt->buf_off, text_va, base);
    sb_emit8(sb, 0xBA); sb_emit32(sb, 255);                     /* mov edx, 255   */
    sb_emit_syscall(sb);

    sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0);                     /* test eax, eax  */
    int jle_patch_rl = sb->len;
    sb_emit8(sb, 0x7E); sb_emit8(sb, 0x00);                     /* jle .error     */

    /* Save byte count in r9d */
    sb_emit8(sb, 0x41); sb_emit8(sb, 0x89); sb_emit8(sb, 0xC1); /* mov r9d, eax   */

    /* Scan for newline: rdi = buf, ecx = count */
    sb_emit_lea_rip(sb, RDI, data_va, rt->buf_off, text_va, base);
    sb_emit8(sb, 0x44); sb_emit8(sb, 0x89); sb_emit8(sb, 0xC9); /* mov ecx, r9d   */

    /* .scan: */
    int scan_loop = sb->len;
    sb_emit8(sb, 0x85); sb_emit8(sb, 0xC9);                     /* test ecx, ecx  */
    sb_emit8(sb, 0x74); sb_emit8(sb, 0x0C);                     /* jz .done (+12) */
    sb_emit8(sb, 0x80); sb_emit8(sb, 0x3F); sb_emit8(sb, 0x0A); /* cmp byte[rdi],\n */
    sb_emit8(sb, 0x74); sb_emit8(sb, 0x07);                     /* je .done (+7)  */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0xFF); sb_emit8(sb, 0xC7); /* inc rdi        */
    sb_emit8(sb, 0xFF); sb_emit8(sb, 0xC9);                     /* dec ecx        */
    /* jmp .scan */
    sb_emit8(sb, 0xEB);
    sb_emit8(sb, (uint8_t)(scan_loop - (sb->len + 1)));

    /* .done: null-terminate at rdi */
    sb_emit8(sb, 0xC6); sb_emit8(sb, 0x07); sb_emit8(sb, 0x00); /* mov byte[rdi],0*/

    /* Return buffer pointer in rax */
    sb_emit_lea_rip(sb, RAX, data_va, rt->buf_off, text_va, base);
    /* Clear flag */
    sb_emit_mov_rip_byte(sb, 0, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* .error: */
    sb->data[jle_patch_rl + 1] = (uint8_t)(sb->len - (jle_patch_rl + 2));
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit_mov_rip_byte(sb, 1, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_read_char()
     * Read 1 byte from stdin, return in RAX.
     * ──────────────────────────────────────────────────────── */
    so.read_char_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit_sub_rsp(sb, 16);

    /* read(0, rbp-1, 1) */
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xFF);                     /* xor edi, edi   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x8D); sb_emit8(sb, 0x75); /* lea rsi,[rbp-1]*/
    sb_emit8(sb, 0xFF);
    sb_emit8(sb, 0xBA); sb_emit32(sb, 1);                       /* mov edx, 1     */
    sb_emit_syscall(sb);

    sb_emit8(sb, 0x85); sb_emit8(sb, 0xC0);                     /* test eax, eax  */
    int jle_patch_rc = sb->len;
    sb_emit8(sb, 0x7E); sb_emit8(sb, 0x00);                     /* jle .error     */

    sb_emit8(sb, 0x0F); sb_emit8(sb, 0xB6); sb_emit8(sb, 0x45); /* movzx eax,[rbp-1]*/
    sb_emit8(sb, 0xFF);
    sb_emit_mov_rip_byte(sb, 0, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* .error: */
    sb->data[jle_patch_rc + 1] = (uint8_t)(sb->len - (jle_patch_rc + 2));
    sb_emit8(sb, 0x31); sb_emit8(sb, 0xC0);                     /* xor eax, eax   */
    sb_emit_mov_rip_byte(sb, 1, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_read_failed()
     * Return read_failed flag (0 or 1) in RAX.
     * ──────────────────────────────────────────────────────── */
    so.read_failed_off = base + sb->len;

    sb_emit_push_rbp(sb);
    sb_emit_mov_rbp_rsp(sb);
    sb_emit_movzx_eax_rip(sb, data_va, rt->flag_off, text_va, base);
    sb_emit_leave(sb);
    sb_emit_ret(sb);

    /* ────────────────────────────────────────────────────────
     * __axis_memcpy(dst=RCX, src=RDX, count=R8)
     * ──────────────────────────────────────────────────────── */
    so.memcpy_off = base + sb->len;

    sb_emit8(sb, 0x57);                                          /* push rdi       */
    sb_emit8(sb, 0x56);                                          /* push rsi       */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xCF); /* mov rdi, rcx   */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x89); sb_emit8(sb, 0xD6); /* mov rsi, rdx   */
    sb_emit8(sb, 0x4C); sb_emit8(sb, 0x89); sb_emit8(sb, 0xC1); /* mov rcx, r8    */
    sb_emit8(sb, 0xF3); sb_emit8(sb, 0xA4);                     /* rep movsb      */
    sb_emit8(sb, 0x5E);                                          /* pop rsi        */
    sb_emit8(sb, 0x5F);                                          /* pop rdi        */
    sb_emit_ret(sb);

    return so;
}

/* ═════════════════════════════════════════════════════════════
 * Generate entry point stub
 *
 * Calls __top_level, then exit(retval) or exit(0).
 * Returns the .text-section-relative offset of the entry stub.
 * ═════════════════════════════════════════════════════════════ */

static int gen_entry_stub(StubBuf *sb, const X64Ctx *x64)
{
    /* Find entry function */
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

    int entry_stub_off = x64->code.len + sb->len;

    /* sub rsp, 40C (shadow space + alignment) */
    sb_emit8(sb, 0x48); sb_emit8(sb, 0x83);
    sb_emit8(sb, 0xEC); sb_emit8(sb, 0x28);               /* 4 bytes */

    /* call rel32 → __top_level */
    sb_emit8(sb, 0xE8);                                    /* 1 byte  */
    int patch_pos = sb->len;
    sb_emit32(sb, 0);                                      /* 4-byte placeholder */

    /* Patch call displacement:
     * RIP after call = entry_stub_off + 4(sub) + 1(E8) + 4(rel32) = +9
     * disp = top_off - abs_from */
    if (top_off >= 0) {
        int abs_from = entry_stub_off + 9;
        int32_t rel = top_off - abs_from;
        sb->data[patch_pos + 0] = (uint8_t)(rel);
        sb->data[patch_pos + 1] = (uint8_t)(rel >> 8);
        sb->data[patch_pos + 2] = (uint8_t)(rel >> 16);
        sb->data[patch_pos + 3] = (uint8_t)(rel >> 24);
    }

    /* Exit code: main() → use return value; else → 0 */
    if (entry_name && strcmp(entry_name, "main") == 0) {
        sb_emit8(sb, 0x89); sb_emit8(sb, 0xC7);           /* mov edi, eax */
    } else {
        sb_emit8(sb, 0x31); sb_emit8(sb, 0xFF);           /* xor edi, edi */
    }

    /* mov eax, 60 (SYS_exit) */
    sb_emit8(sb, 0xB8); sb_emit32(sb, SYS_EXIT);
    /* syscall */
    sb_emit_syscall(sb);

    return entry_stub_off;
}

/* ═════════════════════════════════════════════════════════════
 * Patch runtime relocations (RELOC_REL32 → stub offsets)
 *
 * Both target and offset are section-relative (within .text),
 * so VAs cancel out.
 * ═════════════════════════════════════════════════════════════ */

static void patch_runtime_relocs(X64Ctx *x64_mut, const StubOffsets *so)
{
    for (int i = 0; i < x64_mut->reloc_count; i++) {
        Reloc *r = &x64_mut->relocs[i];
        if (r->kind != RELOC_REL32 || r->target_sym == NULL)
            continue;

        int target = -1;
        if      (strcmp(r->target_sym, "__axis_write_i64") == 0)   target = so->write_i64_off;
        else if (strcmp(r->target_sym, "__axis_write_str") == 0)   target = so->write_str_off;
        else if (strcmp(r->target_sym, "__axis_write_bool") == 0)  target = so->write_bool_off;
        else if (strcmp(r->target_sym, "__axis_write_char") == 0)  target = so->write_char_off;
        else if (strcmp(r->target_sym, "__axis_write_nl") == 0)    target = so->write_nl_off;
        else if (strcmp(r->target_sym, "__axis_read_i64") == 0)    target = so->read_i64_off;
        else if (strcmp(r->target_sym, "__axis_read_line") == 0)   target = so->read_line_off;
        else if (strcmp(r->target_sym, "__axis_read_char") == 0)   target = so->read_char_off;
        else if (strcmp(r->target_sym, "__axis_read_failed") == 0) target = so->read_failed_off;
        else if (strcmp(r->target_sym, "__axis_memcpy") == 0)      target = so->memcpy_off;
        else continue;  /* user function – already resolved */

        int from = r->offset + 4;
        int32_t rel = target - from + r->addend;
        memcpy(&x64_mut->code.data[r->offset], &rel, 4);
        r->kind = (RelocKind)-1;  /* mark resolved */
    }
}

/* ═════════════════════════════════════════════════════════════
 * Patch string relocations (RELOC_RIP_REL32 → .data offsets)
 *
 * These cross section boundaries, so we use full VAs.
 * ═════════════════════════════════════════════════════════════ */

static void patch_string_relocs(X64Ctx *x64_mut,
                                uint64_t text_va,
                                uint64_t data_va)
{
    for (int i = 0; i < x64_mut->reloc_count; i++) {
        Reloc *r = &x64_mut->relocs[i];
        if (r->kind != RELOC_RIP_REL32) continue;

        int str_idx = r->target_label;
        if (str_idx < 0 || str_idx >= x64_mut->string_count) continue;

        uint64_t str_va = data_va
                        + (uint64_t)x64_mut->strings[str_idx].rdata_offset;
        uint64_t rip    = text_va + (uint64_t)r->offset + 4;
        int32_t  disp   = (int32_t)((int64_t)str_va - (int64_t)rip)
                        + r->addend;
        memcpy(&x64_mut->code.data[r->offset], &disp, 4);
        r->kind = (RelocKind)-1;
    }
}

/* ═════════════════════════════════════════════════════════════
 * Main ELF writer
 * ═════════════════════════════════════════════════════════════ */

int elf_write(ELFCtx *ctx, const X64Ctx *x64)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->x64 = x64;

    /* ── Mutable copy of code + relocs for in-place patching ─── */
    X64Ctx x64_mut = *x64;  /* shallow copy */
    x64_mut.code.data = (uint8_t *)malloc((size_t)x64->code.len + 4096);
    if (!x64_mut.code.data) return -1;
    memcpy(x64_mut.code.data, x64->code.data, (size_t)x64->code.len);
    x64_mut.code.len = x64->code.len;
    x64_mut.code.cap = x64->code.len + 4096;

    /* ── Runtime data layout ─────────────────────────────────── */
    RtData rt;
    init_rt_data(&rt, x64->rdata_len);

    int user_code_len = x64->code.len;

    /* ── Pass 1: generate stubs with dummy VAs to measure size ── */
    StubBuf sb1;
    StubOffsets so1 = gen_stubs(&sb1, &rt, 0, 0, user_code_len);
    (void)so1;
    gen_entry_stub(&sb1, x64);
    int stubs_size = sb1.len;
    sb_free(&sb1);

    /* ── Compute layout ──────────────────────────────────────── */
    uint32_t text_off  = ELF_EHDR_SIZE + 2 * ELF_PHDR_SIZE;  /* 176 = 0xB0 */
    uint32_t text_size = (uint32_t)user_code_len + (uint32_t)stubs_size;
    uint64_t text_va   = ELF_BASE_VA + text_off;

    uint32_t data_off  = AXIS_ALIGN(text_off + text_size, ELF_PAGE_SIZE);
    uint64_t data_va   = ELF_BASE_VA + data_off;
    uint32_t data_size = (uint32_t)rt.total;

    /* ── Pass 2: generate stubs with real VAs ────────────────── */
    StubBuf sb;
    StubOffsets so = gen_stubs(&sb, &rt, text_va, data_va, user_code_len);
    int entry_stub_off = gen_entry_stub(&sb, x64);

    /* ── Patch relocations ───────────────────────────────────── */
    patch_runtime_relocs(&x64_mut, &so);
    patch_string_relocs(&x64_mut, text_va, data_va);

    /* ── Build .data content ─────────────────────────────────── */
    uint8_t *data_buf = (uint8_t *)calloc(1, (size_t)data_size);
    if (!data_buf) { sb_free(&sb); free(x64_mut.code.data); return -1; }
    if (x64->rdata_len > 0)
        memcpy(data_buf, x64->rdata, (size_t)x64->rdata_len);
    memcpy(data_buf + rt.true_off,  "true",  5);
    memcpy(data_buf + rt.false_off, "false", 6);
    /* buffer and flag are already zero from calloc */

    /* ── Entry point VA ──────────────────────────────────────── */
    uint64_t entry_va = text_va + (uint64_t)entry_stub_off;

    /* ── Build the ELF file ──────────────────────────────────── */

    /* Total file size: data_off + data_size */
    int file_size = (int)data_off + (int)data_size;
    ctx->buf = (uint8_t *)calloc(1, (size_t)file_size);
    if (!ctx->buf) { sb_free(&sb); free(x64_mut.code.data); free(data_buf); return -1; }
    ctx->cap = file_size;
    int pos = 0;

    /* ── ELF header ──────────────────────────────────────────── */
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0]  = 0x7F;
    ehdr.e_ident[1]  = 'E';
    ehdr.e_ident[2]  = 'L';
    ehdr.e_ident[3]  = 'F';
    ehdr.e_ident[4]  = 2;   /* ELFCLASS64   */
    ehdr.e_ident[5]  = 1;   /* ELFDATA2LSB  */
    ehdr.e_ident[6]  = 1;   /* EV_CURRENT   */
    ehdr.e_ident[7]  = 0;   /* ELFOSABI_NONE */
    ehdr.e_type      = ET_EXEC;
    ehdr.e_machine   = EM_X86_64;
    ehdr.e_version   = 1;
    ehdr.e_entry     = entry_va;
    ehdr.e_phoff     = ELF_EHDR_SIZE;
    ehdr.e_shoff     = 0;
    ehdr.e_flags     = 0;
    ehdr.e_ehsize    = ELF_EHDR_SIZE;
    ehdr.e_phentsize = ELF_PHDR_SIZE;
    ehdr.e_phnum     = 2;
    ehdr.e_shentsize = 0;
    ehdr.e_shnum     = 0;
    ehdr.e_shstrndx  = 0;
    memcpy(ctx->buf + pos, &ehdr, sizeof(ehdr));
    pos += sizeof(ehdr);

    /* ── Program header 1: text segment (RX) ─────────────────── */
    Elf64_Phdr ph_text;
    memset(&ph_text, 0, sizeof(ph_text));
    ph_text.p_type   = PT_LOAD;
    ph_text.p_flags  = PF_R | PF_X;
    ph_text.p_offset = 0;                              /* includes ELF hdr + phdrs */
    ph_text.p_vaddr  = ELF_BASE_VA;
    ph_text.p_paddr  = ELF_BASE_VA;
    ph_text.p_filesz = (uint64_t)text_off + text_size;
    ph_text.p_memsz  = (uint64_t)text_off + text_size;
    ph_text.p_align  = ELF_PAGE_SIZE;
    memcpy(ctx->buf + pos, &ph_text, sizeof(ph_text));
    pos += sizeof(ph_text);

    /* ── Program header 2: data segment (RW) ─────────────────── */
    Elf64_Phdr ph_data;
    memset(&ph_data, 0, sizeof(ph_data));
    ph_data.p_type   = PT_LOAD;
    ph_data.p_flags  = PF_R | PF_W;
    ph_data.p_offset = data_off;
    ph_data.p_vaddr  = data_va;
    ph_data.p_paddr  = data_va;
    ph_data.p_filesz = data_size;
    ph_data.p_memsz  = data_size;
    ph_data.p_align  = ELF_PAGE_SIZE;
    memcpy(ctx->buf + pos, &ph_data, sizeof(ph_data));
    pos += sizeof(ph_data);

    /* ── .text section data (at text_off) ────────────────────── */
    /* pos should be at text_off = 0xB0; zero-padding already from calloc */
    memcpy(ctx->buf + text_off, x64_mut.code.data, (size_t)user_code_len);
    memcpy(ctx->buf + text_off + user_code_len, sb.data, (size_t)sb.len);

    /* ── .data section data (at data_off) ────────────────────── */
    memcpy(ctx->buf + data_off, data_buf, (size_t)data_size);

    /* ── Store context ───────────────────────────────────────── */
    ctx->len       = file_size;
    ctx->text_va   = text_va;
    ctx->text_off  = text_off;
    ctx->text_size = text_size;
    ctx->data_va   = data_va;
    ctx->data_off  = data_off;
    ctx->data_size = data_size;
    ctx->entry_va  = entry_va;

    /* ── Cleanup ─────────────────────────────────────────────── */
    free(x64_mut.code.data);
    free(data_buf);
    sb_free(&sb);

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 * Save ELF file + set executable permission
 * ═════════════════════════════════════════════════════════════ */

int elf_save(const ELFCtx *ctx, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return -1;
    }
    size_t written = fwrite(ctx->buf, 1, (size_t)ctx->len, f);
    fclose(f);

    if ((int)written != ctx->len) {
        fprintf(stderr, "error: incomplete write to '%s'\n", path);
        return -1;
    }

#ifndef _WIN32
    chmod(path, 0755);
#endif
    return 0;
}

/* ═════════════════════════════════════════════════════════════
 * Free ELF output buffer
 * ═════════════════════════════════════════════════════════════ */

void elf_free(ELFCtx *ctx)
{
    free(ctx->buf);
    ctx->buf = NULL;
    ctx->len = ctx->cap = 0;
}
