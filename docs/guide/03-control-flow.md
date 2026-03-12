# Control Flow

## Conditionals: `when`

AXIS uses `when` instead of `if`:

```axis
x: i32 = 10

when x > 0:
    writeln("positive")

when x == 0:
    writeln("zero")

when x < 0:
    writeln("negative")
```

## Infinite Loops: `repeat`

`repeat` starts a loop that runs until you `stop` it:

```axis
i: i32 = 0
repeat:
    writeln(i)
    i = i + 1
    when i >= 5:
        stop
```

Output:

```
0
1
2
3
4
```

## While Loops

```axis
i: i32 = 0
while i < 5:
    writeln(i)
    i = i + 1
```

## For Loops

Iterate over a range:

```axis
for i in range(0, 5):
    writeln(i)           # 0, 1, 2, 3, 4

for i in range(0, 10, 2):
    writeln(i)           # 0, 2, 4, 6, 8
```

Iterate over an array:

```axis
arr: (i32; 3) = [10, 20, 30]
for x in arr:
    writeln(x)
```

## Loop Control

- `stop` — break out of the loop
- `skip` — jump to the next iteration

```axis
i: i32 = 0
repeat:
    i = i + 1
    when i == 3:
        skip         # skip printing 3
    when i > 5:
        stop         # exit after 5
    writeln(i)
```

Output:

```
1
2
4
5
```

## Nested Loops

Loops can be nested. `stop` and `skip` apply to the innermost loop:

```axis
row: i32 = 1
while row <= 3:
    col: i32 = 1
    while col <= 3:
        write(row * col)
        write(" ")
        col = col + 1
    writeln("")
    row = row + 1
```

## Next

[Functions](04-functions.md)
