# Lexer

The lexer (`lexer.c`) converts source text into a flat array of tokens. It is a single-pass scanner with no backtracking.

## Token Types

AXCC defines 117 token types in `axis_token.h`:

- **Literals**: `INT_LIT` (decimal, hex `0xFF`, binary `0b1010`), `STRING_LIT`, `BOOL` (`true`/`false`)
- **Keywords**: 57 keywords covering control flow (`when`, `else`, `while`, `repeat`, `for`, `in`, `stop`, `skip`, `match`), types (`i8`–`i64`, `u8`–`u64`, `bool`, `str`), I/O (`write`, `writeln`, `read`, `readln`, `readchar`), functions (`func`, `give`, `update`), and declarations (`var`, `field`, `enum`, `mode`)
- **Operators**: Arithmetic, comparison, logical (`and`, `or`, `not`), bitwise, compound assignment
- **Structural**: `INDENT`, `DEDENT`, `NEWLINE`, `EOF`, parentheses, brackets, colons, commas

## Indentation Handling

AXIS uses indentation to define blocks (like Python). The lexer manages this by:

1. Maintaining an indent stack (max depth 128) tracking the current indentation level
2. At the start of each line, counting leading spaces
3. If the indent increases: push onto the stack, emit an `INDENT` token
4. If the indent decreases: pop the stack (possibly multiple levels), emit as many `DEDENT` tokens as needed

Multiple `DEDENT` tokens can be emitted for a single line. These are queued in a pending buffer and returned one at a time on subsequent `next_token()` calls.

## Number Literals

The lexer recognizes three integer formats:

| Format | Prefix | Example |
|--------|--------|---------|
| Decimal | none | `42`, `1_000_000` |
| Hexadecimal | `0x` | `0xFF`, `0xDEAD` |
| Binary | `0b` | `0b1010`, `0b11111111` |

Underscores are allowed in decimal literals for readability.

## String Literals

Strings are double-quoted with these escape sequences:

| Escape | Meaning |
|--------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\\` | Backslash |
| `\"` | Double quote |
| `\0` | Null byte |

## Line Tracking

Every token stores its source line and column number. This information is passed through all stages and used for error messages.

## Next

[Parser](03-parser.md)
