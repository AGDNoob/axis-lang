# AXIS Examples

These examples demonstrate the features of the AXIS programming language.

## Running Examples

**Script mode** (examples 01-19):
```bash
python compilation_pipeline.py run examples/01_hello_world.axis
```

**Compile mode** (example 20):
```bash
python compilation_pipeline.py examples/20_compile_mode.axis -o demo --elf
# On Linux:
./demo
echo $?  # Shows exit code (30)
```

## Examples Overview

| # | File | Description |
|---|------|-------------|
| 01 | `hello_world.axis` | Basic output with `writeln` |
| 02 | `variables.axis` | Variable declarations and types |
| 03 | `arithmetic.axis` | Math operators: `+`, `-`, `*`, `/`, `%` |
| 04 | `conditionals.axis` | `when` branching |
| 05 | `loops.axis` | `repeat` loops with `stop` |
| 06 | `while_loops.axis` | `while` condition loops |
| 07 | `break_continue.axis` | `stop` and `skip` keywords |
| 08 | `nested_loops.axis` | Multiplication table |
| 09 | `boolean_logic.axis` | Bitwise `&`, `\|`, `^` as logic |
| 10 | `comparison.axis` | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| 11 | `bitwise.axis` | `&`, `\|`, `^`, `<<`, `>>` |
| 12 | `functions.axis` | Function definitions with `func` |
| 13 | `fibonacci.axis` | Fibonacci sequence |
| 14 | `prime_numbers.axis` | Prime number checker |
| 15 | `factorial.axis` | Factorial calculation |
| 16 | `guessing_game.axis` | Binary search simulation |
| 17 | `ascii_art.axis` | Drawing with loops |
| 18 | `gcd.axis` | Euclidean algorithm |
| 19 | `fizzbuzz.axis` | Classic challenge |
| 20 | `compile_mode.axis` | Native binary compilation |

## Language Features

- **Output**: `write()`, `writeln()`
- **Variables**: `name: type = value`
- **Types**: `i8`, `i16`, `i32`, `i64`
- **Conditionals**: `when condition:`
- **Loops**: `repeat:`, `while condition:`
- **Control**: `stop` (break), `skip` (continue)
- **Functions**: `func name():`
- **Operators**: arithmetic, comparison, bitwise
- **Modes**: `mode script` (interpreted) / `mode compile` (native x86-64)
