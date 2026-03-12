# Enums and Match

## Enums

Enums define a set of named constants:

```axis
Color: enum:
    Red
    Green
    Blue
```

Values auto-increment from 0 (`Red = 0`, `Green = 1`, `Blue = 2`).

### Using Enums

```axis
c: Color = Color.Red

when c == Color.Green:
    writeln("green")
```

### Explicit Values

```axis
Status: enum:
    Pending = 0
    Active = 1
    Completed = 100
```

### Custom Underlying Type

By default, enums use `i32`. You can specify a different integer type:

```axis
SmallStatus: enum u8:
    Off
    On

BigId: enum i64:
    First
    Second
```

Supported types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`.

## Match Statements

Match provides pattern matching on values:

```axis
x: i32 = 2
match x:
    1:
        writeln("one")
    2:
        writeln("two")
    3:
        writeln("three")
    _:
        writeln("other")
```

Output: `two`

The `_` is a wildcard that matches anything not covered by the other cases.

### Match with Enums

```axis
Color: enum:
    Red
    Green
    Blue

c: Color = Color.Green
match c:
    Color.Red:
        writeln("Red")
    Color.Green:
        writeln("Green")
    Color.Blue:
        writeln("Blue")
```

## Next

[Operators](08-operators.md)
