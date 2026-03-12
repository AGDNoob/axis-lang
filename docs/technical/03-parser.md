# Parser

The parser (`parser.c`) is a recursive descent parser that transforms the token stream into an Abstract Syntax Tree (AST).

## Operator Precedence

From lowest to highest:

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 | `or` | Left |
| 2 | `and` | Left |
| 3 | `\|` (bitwise OR) | Left |
| 4 | `^` (bitwise XOR) | Left |
| 5 | `&` (bitwise AND) | Left |
| 6 | `==` `!=` | Left |
| 7 | `<` `<=` `>` `>=` | Left |
| 8 | `<<` `>>` | Left |
| 9 | `+` `-` | Left |
| 10 | `*` `/` `%` | Left |
| 11 | `-` `not` (unary) | Right (prefix) |
| 12 | `[]` `.` (index, field access) | Left (postfix) |

## AST Node Types

### Expressions (14 kinds)

| Kind | Description |
|------|-------------|
| `INT_LIT` | Integer constant |
| `STRING_LIT` | String constant |
| `BOOL_LIT` | `true` or `false` |
| `IDENT` | Variable reference |
| `BINARY` | Binary operation (op, left, right) |
| `UNARY` | Unary operation (op, operand) |
| `CALL` | Function call (name, arguments) |
| `INDEX` | Array index `arr[i]` |
| `FIELD_ACCESS` | Field member access `obj.member` |
| `ENUM_ACCESS` | Enum variant access `Color.Red` |
| `ARRAY_LIT` | Array literal |
| `RANGE` | Range expression `start..end` with optional step |
| `COPY` | Array copy expression |
| `READ_FAILED` | Error flag for failed reads |

### Statements (17 kinds)

| Kind | Description |
|------|-------------|
| `VAR_DECL` | Variable declaration (with optional type inference) |
| `ASSIGN` | Variable assignment |
| `INDEX_ASSIGN` | Array element assignment |
| `FIELD_ASSIGN` | Field member assignment |
| `COMPOUND_ASSIGN` | `+=`, `-=`, `*=`, etc. |
| `IF` | Conditional with optional else |
| `WHILE` | While loop |
| `REPEAT` | Infinite loop |
| `FOR` | For loop (range or array iteration) |
| `BREAK` | Exit loop |
| `CONTINUE` | Skip to next iteration |
| `RETURN` | Return from function |
| `MATCH` | Pattern matching statement |
| `WRITE` | Output statement |
| `READ` | Input statement (read/readln/readchar) |
| `EXPR` | Expression used as statement |
| `SYSCALL` | System call |

### Top-Level Definitions

- **Functions** (`ASTFunction`): Name, typed parameter list, return type, body statements
- **Fields** (`ASTFieldDef`): Named struct type with members and optional default values
- **Enums** (`ASTEnumDef`): Named enum with variants, optional explicit values, optional underlying type

## Block Parsing

Blocks are delimited by `INDENT`/`DEDENT` tokens from the lexer. The parser expects:

```
statement:
    NEWLINE
    INDENT
    statement+
    DEDENT
```

This mirrors Python's indentation rules.

## Next

[Semantic Analysis](04-semantic-analysis.md)
