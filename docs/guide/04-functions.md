# Functions

## Defining Functions

```axis
func greet():
    writeln("Hello!")

func add(a: i32, b: i32) -> i32:
    give a + b
```

- `func` starts a function definition
- Parameters have explicit types
- `-> type` declares the return type
- `give` returns a value (same as `return` in other languages)

## Calling Functions

```axis
greet()
result: i32 = add(10, 20)
writeln(result)    # 30
```

## The `update` Modifier

By default, parameters are passed by value — the function gets a copy. To modify the caller's variable, use `update`:

```axis
func double(update x: i32):
    x = x * 2

val: i32 = 5
double(val)
writeln(val)    # 10
```

Without `update`, the original variable wouldn't change:

```axis
func try_double(x: i32):
    x = x * 2    # modifies local copy only

val: i32 = 5
try_double(val)
writeln(val)    # still 5
```

## Multiple `update` Parameters

You can have multiple `update` parameters. This makes things like swap straightforward:

```axis
func swap(update a: i32, update b: i32):
    temp: i32 = a
    a = b
    b = temp

x: i32 = 1
y: i32 = 2
swap(x, y)
writeln(x)    # 2
writeln(y)    # 1
```

## Compile Mode: `main()`

In compile mode, your program needs a `main()` function that returns `i32`. The return value becomes the process exit code:

```axis
mode compile

func main() -> i32:
    writeln("Running")
    give 0
```

Other functions work the same in both modes.

## Next

[Arrays](05-arrays.md)
