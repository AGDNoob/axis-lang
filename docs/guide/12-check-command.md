# Check Command

## What It Does

`axis check` validates an AXIS source file **without** generating code or running it. The compiler runs the lexer, parser, and semantic analysis — but stops before IR/code generation.

```bash
axis check program.axis
```

If errors are found, the compiler reports all of them (instead of stopping at the first) and exits with code 1.

## Flags

| Flag        | Description                          |
|-------------|--------------------------------------|
| `--dead`    | Warn about unreachable code          |
| `--unused`  | Warn about unused variables          |
| `--all`     | Enable all warnings                  |

`--all` enables both `--dead` and `--unused`.

## Examples

### Errors Only

```bash
axis check program.axis
```

Checks syntax and semantics — reports all errors found.

### Unreachable Code (`--dead`)

```axis
mode compile

func main() -> i32:
    give 0
    writeln("This will never run")    # warning!
```

```bash
axis check program.axis --dead
```

```
program.axis:5:5: warning: unreachable code after return
```

The compiler detects code after `give` (return), `break`, and `continue` as unreachable.

### Unused Variables (`--unused`)

```axis
mode compile

func main() -> i32:
    x: i32 = 42          # warning — never used
    y: i32 = 10
    writeln(y)
    give 0
```

```bash
axis check program.axis --unused
```

```
program.axis:4:5: warning: unused variable 'x'
```

Function parameters do **not** trigger a warning — only local variables.

### All Warnings (`--all`)

```bash
axis check program.axis --all
```

Enables all warnings at once. Equivalent to `--dead --unused`.

## Error Collection

In normal mode, the compiler stops at the first error. In check mode, it collects **all** errors and reports them together:

```bash
axis check broken.axis
```

```
broken.axis:3:5: semantic error: undeclared variable 'x'
broken.axis:7:12: semantic error: type mismatch in assignment
2 errors found
```

This makes `axis check` particularly useful in CI pipelines and editors that want to see all problems at once.
