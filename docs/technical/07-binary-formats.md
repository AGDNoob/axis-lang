# Binary Formats

AXCC produces standalone executables in two formats: Windows PE32+ and Linux ELF64. Both are generated directly from the `X64Ctx` output of the code generator — no external linker or assembler is involved.

## PE32+ (Windows)

Written by `pe.c`. The output is a valid Windows executable (`.exe`).

### Layout

```
DOS Header (stub)
PE Signature ("PE\0\0")
COFF Header
Optional Header (PE32+)
Section Headers
─────────────────
.text   (code, execute/read)
.rdata  (string literals, read-only)
.idata  (import table)
.data   (writable data)
```

### Key Parameters

| Parameter | Value |
|-----------|-------|
| Image base | `0x00400000` |
| File alignment | 512 bytes |
| Section alignment | 4096 bytes |
| Stack size | Default |

### Import Table

AXIS programs use Windows API functions for I/O, memory, and process management. The import table in `.idata` references `kernel32.dll` and lists the needed functions (e.g., `WriteFile`, `ReadFile`, `GetStdHandle`, `ExitProcess`, `HeapAlloc`).

The PE writer builds the import directory, import lookup table, and import address table so the Windows loader can resolve these at load time.

### Entry Point

The PE entry point is a small stub that:
1. Calls the generated top-level code (or `main()` in compile mode)
2. Calls `ExitProcess` with the return value

## ELF64 (Linux)

Written by `elf.c`. The output is a statically linked Linux executable.

### Layout

```
ELF64 Header
Program Headers (2 × PT_LOAD)
─────────────────
Segment 1: .text + stubs  (read + execute)
Segment 2: .data           (read + write)
```

### Key Parameters

| Parameter | Value |
|-----------|-------|
| Load address | `0x400000` |
| Entry point | `_start` |

### System Calls

Linux executables use direct syscalls instead of shared libraries:

| Syscall | Number | Purpose |
|---------|--------|---------|
| `write` | 1 | Output to stdout/stderr |
| `read` | 0 | Input from stdin |
| `exit` | 60 | Terminate process |
| `brk` | 12 | Heap memory allocation |

### Entry Point

The ELF entry point (`_start`) is a minimal stub:

```asm
; call generated code
call main_or_toplevel
; exit with return value
mov  edi, eax
mov  eax, 60     ; sys_exit
syscall
```

## Relocation Resolution

Both writers resolve the relocations recorded by the x64 backend:

1. **Lay out sections** at their final virtual addresses
2. **Walk the relocation list** and compute the target address for each entry
3. **Patch the machine code** with the resolved values:
   - `RELOC_REL32`: `target - (site + 4)` (PC-relative)
   - `RELOC_ABS64`: absolute target address
   - `RELOC_RIP_REL32`: `target - (site + 4)` adjusted for `.rdata` offset

## Binary Size

Typical AXIS executables are very small because there is no runtime, no standard library, and no dynamic linking overhead (on Linux). A minimal compile-mode program produces:

- Windows PE: ~3 KB
- Linux ELF: ~2–3 KB
