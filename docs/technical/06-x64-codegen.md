# x64 Code Generation

The x64 code generator (`x64.c`) translates IR instructions into x86-64 machine code.

## Register Allocation Strategy

AXCC v1.2.1 uses a linear-scan register allocator across all 16 x86-64 general-purpose registers. Local variables and IR temporaries are mapped to physical registers; stack spills occur only when registers are exhausted.

A spill-reload cache tracks which spilled values are currently in registers. When a spilled temporary is accessed again, the code generator emits a register-to-register `MOV` instead of a stack load. The cache is invalidated at branch targets and register clobbers.

See [Optimizations](08-optimizations.md) for the full optimization pipeline.

## Calling Conventions

AXCC supports two calling conventions, selected based on the target platform:

### Windows x64
Arguments passed in: `rcx`, `rdx`, `r8`, `r9`, then stack.
32 bytes of shadow space reserved on the stack for the callee.

### System V (Linux)
Arguments passed in: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`, then stack.
No shadow space.

The code generator picks the correct convention based on whether the output is PE (Windows) or ELF (Linux).

## Function Prologue and Epilogue

Every function gets a standard prologue:

```asm
push rbp
mov  rbp, rsp
sub  rsp, <frame_size>    ; space for locals + temporaries
```

And epilogue:

```asm
mov  rsp, rbp
pop  rbp
ret
```

The frame size is calculated from the semantic analyzer's stack layout plus the number of temporaries used in the function's IR.

## Operand Size Handling

The code generator matches instruction operand sizes to the AXIS type being operated on:

| AXIS type | Size | x64 registers used |
|-----------|------|-------------------|
| `i8`, `u8`, `bool` | 1 byte | `al`, `cl`, etc. |
| `i16`, `u16` | 2 bytes | `ax`, `cx`, etc. |
| `i32`, `u32` | 4 bytes | `eax`, `ecx`, etc. |
| `i64`, `u64`, `str` | 8 bytes | `rax`, `rcx`, etc. |

MOV, ADD, CMP, and other instructions all select the appropriate encoding based on operand size.

## Relocation Table

Not all addresses are known at code generation time. The x64 backend emits placeholder values and records relocations that are resolved later by the PE/ELF writer:

| Relocation type | Description |
|----------------|-------------|
| `RELOC_REL32` | 32-bit PC-relative offset (for CALL and JMP) |
| `RELOC_ABS64` | 64-bit absolute address (for data pointers) |
| `RELOC_RIP_REL32` | 32-bit RIP-relative offset (for `.rdata` references) |

When the PE or ELF writer lays out sections with known addresses, it patches every relocation site with the final computed values.

## Output

The code generator produces an `X64Ctx` structure containing:

- `.text` buffer: Raw machine code bytes
- `.rdata` buffer: Read-only data (string literals)
- Relocation list: Sites to patch with final addresses
- Symbol table: Function names and their offsets in `.text`

This structure is passed to the PE or ELF writer for final executable generation.

## Next

[Binary Formats](07-binary-formats.md)
