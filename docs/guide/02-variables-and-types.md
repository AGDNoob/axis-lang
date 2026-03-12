# Variables and Types

## Declaring Variables

Variables are declared with a name, type, and optional initial value:

```axis
x: i32 = 42
name: str = "Alice"
flag: bool = True
```

If you omit the value, the variable is zero-initialized:

```axis
count: i32       # 0
active: bool     # False
```

## Integer Types

AXIS has signed and unsigned integers in 8, 16, 32, and 64-bit widths:

| Type  | Size    | Range |
|-------|---------|-------|
| `i8`  | 8-bit   | -128 to 127 |
| `i16` | 16-bit  | -32,768 to 32,767 |
| `i32` | 32-bit  | -2,147,483,648 to 2,147,483,647 |
| `i64` | 64-bit  | -9.2×10¹⁸ to 9.2×10¹⁸ |
| `u8`  | 8-bit   | 0 to 255 |
| `u16` | 16-bit  | 0 to 65,535 |
| `u32` | 32-bit  | 0 to 4,294,967,295 |
| `u64` | 64-bit  | 0 to 18.4×10¹⁸ |

## Other Types

| Type   | Description |
|--------|-------------|
| `bool` | `True` or `False` |
| `str`  | String (script mode only) |

## Type Annotations

Types are always explicit. There is no type inference:

```axis
a: i32 = 10
b: i64 = 1000000
c: u8 = 255
d: bool = True
e: str = "hello"
```

## Comments

```axis
// C-style comment
# Python-style comment
```

Both styles work everywhere.

## Next

[Control Flow](03-control-flow.md)
