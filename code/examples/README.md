# AXIS Examples

A progressive collection of 20 examples showcasing the AXIS programming language — from basic output to compiled native binaries.

## Running Examples

**Script mode** (examples 01–19):
```bash
axis run examples/01_hello_world.axis
```

**Compile mode** (example 20):
```bash
axis build examples/20_compile_mode.axis -o demo --elf
./demo
echo $?
```

## Examples Overview

| # | File | Topic | Features Covered |
|---|------|-------|------------------|
| 01 | `hello_world.axis` | Hello World | `mode script`, `writeln` |
| 02 | `variables.axis` | Variables & Types | `i32`, `i64`, `u8`, `bool`, `str`, zero-init |
| 03 | `arithmetic.axis` | Arithmetic | `+`, `-`, `*`, `/`, `%`, compound assignment |
| 04 | `conditionals.axis` | Conditionals | `when`, `else when`, `else`, nested |
| 05 | `loops.axis` | Loops | `for..in range()`, `while`, `repeat`/`stop` |
| 06 | `loop_control.axis` | Loop Control | `skip` (continue), `stop` (break) |
| 07 | `boolean_logic.axis` | Boolean Logic | `and`, `or`, `not`, `True`/`False` |
| 08 | `bitwise.axis` | Bitwise Ops | `&`, `\|`, `^`, `<<`, `>>`, hex literals |
| 09 | `functions.axis` | Functions | `func`, `give`, return types, recursion |
| 10 | `update_modifier.axis` | Pass by Reference | `update` modifier for in-place mutation |
| 11 | `arrays.axis` | Arrays | `(type; size)` syntax, for-each, indexing |
| 12 | `fields.axis` | Fields (Structs) | `field:`, nested access, `update` params |
| 13 | `enums_match.axis` | Enums & Match | `enum:`, `match:`, `_` wildcard |
| 14 | `copy.axis` | Copy Semantics | `copy` keyword for arrays and fields |
| 15 | `fizzbuzz.axis` | FizzBuzz | Classic challenge using modulo and conditionals |
| 16 | `fibonacci.axis` | Fibonacci | Iterative approach with variable swapping |
| 17 | `primes.axis` | Prime Numbers | Trial division, boolean returns |
| 18 | `gcd_lcm.axis` | GCD & LCM | Euclidean algorithm, helper functions |
| 19 | `guessing_game.axis` | Guessing Game | `read()`, `read_failed`, interactive I/O |
| 20 | `compile_mode.axis` | Compile Mode | `mode compile`, `main() -> i32`, native binary |

## Language Features Demonstrated

- **I/O**: `write()`, `writeln()`, `read()`, `readln()`, `readchar()`, `read_failed`
- **Variables**: `name: type = value` with types `i8`–`i64`, `u8`–`u64`, `bool`, `str`
- **Conditionals**: `when`/`else when`/`else`
- **Loops**: `for x in range(a, b)`, `for x in array`, `while cond:`, `repeat:`
- **Control flow**: `stop` (break), `skip` (continue)
- **Functions**: `func name(params) -> type:` with `give` (return)
- **Mutation**: `update` modifier for pass-by-reference
- **Data**: arrays `(type; size)`, fields `field:`, enums `enum:`
- **Pattern matching**: `match value:` with `_` wildcard
- **Copy**: explicit `copy` for value-type deep copies
- **Operators**: arithmetic, comparison, logical (`and`/`or`/`not`), bitwise
- **Modes**: `mode script` (top-level execution) / `mode compile` (requires `main() -> i32`)
