# IR Generation

The IR generator (`irgen.c`) transforms the type-checked AST into a flat list of three-address instructions per function.

## IR Design

The IR uses virtual temporaries (unlimited virtual registers) rather than physical registers. Each instruction operates on at most three operands: a destination and up to two sources.

Operand types (`IROperKind`):
- `NONE` — unused operand slot
- `TEMP` — virtual register (numbered sequentially)
- `IMM` — immediate integer value
- `STACK` — stack-relative address
- `LABEL` — branch target
- `FUNC` — function reference
- `STR` — index into the string table

## IR Opcodes

42 opcodes organized by category:

### Data Movement
| Opcode | Description |
|--------|-------------|
| `MOV` | Copy between temporaries |
| `LOAD_IMM` | Load immediate value |
| `LOAD_STR` | Load string pointer (by string table index) |
| `LOAD_VAR` | Load from stack variable |
| `STORE_VAR` | Store to stack variable |
| `STORE_IND` | Store through pointer (indirect) |

### Arithmetic
| Opcode | Description |
|--------|-------------|
| `ADD`, `SUB`, `MUL`, `DIV`, `MOD` | Binary arithmetic |
| `NEG` | Unary negation |

### Bitwise
| Opcode | Description |
|--------|-------------|
| `BIT_AND`, `BIT_OR`, `BIT_XOR` | Bitwise logic |
| `SHL`, `SHR` | Shift left, arithmetic shift right |

### Comparison
| Opcode | Description |
|--------|-------------|
| `CMP_EQ`, `CMP_NE` | Equality |
| `CMP_LT`, `CMP_LE`, `CMP_GT`, `CMP_GE` | Relational |

Each comparison produces 0 or 1 in the destination temporary.

### Control Flow
| Opcode | Description |
|--------|-------------|
| `LABEL` | Branch target |
| `JMP` | Unconditional jump |
| `JZ` | Jump if zero |
| `JNZ` | Jump if not zero |

### Functions
| Opcode | Description |
|--------|-------------|
| `ARG` | Push function argument |
| `CALL` | Call function |
| `RET` | Return with value |
| `RET_VOID` | Return without value |

### I/O
| Opcode | Description |
|--------|-------------|
| `WRITE` | Output value |
| `READ` | Input value (read/readln/readchar variant stored in instruction) |

### Memory
| Opcode | Description |
|--------|-------------|
| `INDEX_LOAD` | Load array element |
| `INDEX_STORE` | Store array element |
| `FIELD_LOAD` | Load field member |
| `FIELD_STORE` | Store field member |
| `LEA` | Load effective address |
| `MEMCPY` | Copy memory block |

### Type Conversion
| Opcode | Description |
|--------|-------------|
| `SEXT` | Sign-extend to larger type |
| `ZEXT` | Zero-extend to larger type |
| `TRUNC` | Truncate to smaller type |

### System
| Opcode | Description |
|--------|-------------|
| `SYSCALL` | System call with argument count |

## Translation Patterns

### Expressions

Binary operations are decomposed into three-address form:

```
a + b * c
→
  t0 = LOAD_VAR b
  t1 = LOAD_VAR c
  t2 = MUL t0, t1
  t3 = LOAD_VAR a
  t4 = ADD t3, t2
```

Short-circuit evaluation for `and`/`or` generates conditional jumps:

```
x and y
→
  t0 = <evaluate x>
  JZ t0, label_false
  t1 = <evaluate y>
  JZ t1, label_false
  t2 = LOAD_IMM 1
  JMP label_end
label_false:
  t2 = LOAD_IMM 0
label_end:
```

### Control Flow

`when`/`else` maps to conditional jumps:

```
when condition:
    body
else:
    alt
→
  t0 = <condition>
  JZ t0, else_label
  <body>
  JMP end_label
else_label:
  <alt>
end_label:
```

`while` loops use a back-edge jump:

```
while condition:
    body
→
loop_start:
  t0 = <condition>
  JZ t0, loop_end
  <body>
  JMP loop_start
loop_end:
```

`for` loops over ranges are decomposed into equivalent `while` loops with an iterator variable.

### Function Calls

Arguments are emitted as `ARG` instructions, then a `CALL`:

```
result = foo(a, b)
→
  t0 = LOAD_VAR a
  ARG t0
  t1 = LOAD_VAR b
  ARG t1
  t2 = CALL foo
```

### String Table

String literals are collected into a string table during IR generation. Each string gets an index, and `LOAD_STR` references it by index. The x64 backend later emits these strings into the `.rdata` section.

## Next

[x64 Code Generation](06-x64-codegen.md)
