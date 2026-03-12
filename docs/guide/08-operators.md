# Operators

## Arithmetic

| Operator | Description |
|----------|-------------|
| `+`  | Addition |
| `-`  | Subtraction |
| `*`  | Multiplication |
| `/`  | Division |
| `%`  | Modulo |

```axis
a: i32 = 10
b: i32 = 3
writeln(a + b)    # 13
writeln(a - b)    # 7
writeln(a * b)    # 30
writeln(a / b)    # 3
writeln(a % b)    # 1
```

## Comparison

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<`  | Less than |
| `<=` | Less or equal |
| `>`  | Greater than |
| `>=` | Greater or equal |

Returns `bool`.

## Logical

| Operator | Description |
|----------|-------------|
| `and` | Logical AND (short-circuit) |
| `or`  | Logical OR (short-circuit) |
| `not` / `!` | Logical NOT |

```axis
x: i32 = 5
when x > 0 and x < 10:
    writeln("single digit positive")
```

## Bitwise

| Operator | Description |
|----------|-------------|
| `&`  | AND |
| `\|`  | OR |
| `^`  | XOR |
| `<<` | Left shift |
| `>>` | Right shift |

```axis
a: i32 = 0b1100
b: i32 = 0b1010
writeln(a & b)     # 8  (0b1000)
writeln(a \| b)    # 14 (0b1110)
writeln(a ^ b)     # 6  (0b0110)
writeln(a << 1)    # 24 (0b11000)
writeln(a >> 1)    # 6  (0b0110)
```

## Compound Assignment

All arithmetic and bitwise operators have compound variants:

```axis
x: i32 = 10
x += 5     # 15
x -= 3     # 12
x *= 2     # 24
x /= 4     # 6
x %= 5     # 1

y: i32 = 0xFF
y &= 0x0F  # 15
y |= 0xF0  # 255
y ^= 0xFF  # 0
y <<= 4
y >>= 2
```

Compound assignment works on variables, array elements, and field members.

## Next

[I/O](09-io.md)
