# AXIS Examples

A progressive collection of 30 examples showcasing the AXIS programming language — from basic output to compiled native binaries.

## Running Examples

**Script mode** (most examples):

```bash
axis run examples/01_hello_world.axis
```

**Compile mode** (examples 14, 15, 30):

```bash
axis build examples/30_compile_mode.axis -o demo --elf
./demo
echo $?
```

## Examples Overview

### Basics (01–10)

| # | File | Topic | Features Covered |
| - | ---- | ----- | ---------------- |
| 01 | `01_hello_world.axis` | Hello World | `mode script`, `writeln` |
| 02 | `02_variables.axis` | Variables & Types | `i8`–`i64`, `u8`–`u64`, `bool`, `str`, zero-init |
| 03 | `03_arithmetic.axis` | Arithmetic | `+`, `-`, `*`, `/`, `%`, compound assignment |
| 04 | `04_comparison_logic.axis` | Comparison & Logic | `==`, `!=`, `>`, `<=`, `and`, `or`, `not` |
| 05 | `05_conditionals.axis` | Conditionals | `when`, `else when`, `else`, grading system |
| 06 | `06_for_loops.axis` | For Loops | `for..in range()`, array iteration, nested loops |
| 07 | `07_while_repeat.axis` | While & Repeat | `while`, `repeat`/`stop` |
| 08 | `08_loop_control.axis` | Loop Control | `skip` (continue), `stop` (break) |
| 09 | `09_bitwise.axis` | Bitwise Ops | `&`, `\|`, `^`, `<<`, `>>`, `0b`/`0x` literals |
| 10 | `10_functions.axis` | Functions | `func`, `return`, return types, parameters |

### Intermediate (11–22)

| # | File | Topic | Features Covered |
| - | ---- | ----- | ---------------- |
| 11 | `11_recursion.axis` | Recursion | Recursive factorial, power function |
| 12 | `12_update_modifier.axis` | Pass by Reference | `update` modifier, swap, clamp |
| 13 | `13_arrays.axis` | Arrays | `(type; size)` syntax, indexing, for-each |
| 14 | `14_fields.axis` | Fields (Structs) | `mode compile`, `field:`, dot access, `update` on fields |
| 15 | `15_enums.axis` | Enums | `mode compile`, `enum:` definition, ordinal values |
| 16 | `16_match.axis` | Pattern Matching | `match:`, integer patterns, `_` wildcard |
| 17 | `17_copy.axis` | Copy Semantics | `copy` for arrays, independence |
| 18 | `18_constants.axis` | Constants | `const`, const expressions, const arrays |
| 19 | `19_labeled_loops.axis` | Labeled Loops | `@label`, `stop @label`, `skip @label` |
| 20 | `20_type_casting.axis` | Type Casting | `update..as`, `copy..as`, widening/narrowing |
| 21 | `21_aliases.axis` | Aliases | Alias semantics, mutation, `copy` for independence |
| 22 | `22_input.axis` | User Input | `input()`, interactive I/O |

### Applied Projects (23–30)

| # | File | Topic | Features Covered |
| - | ---- | ----- | ---------------- |
| 23 | `23_fizzbuzz.axis` | FizzBuzz | Classic challenge, modulo, conditionals |
| 24 | `24_fibonacci.axis` | Fibonacci | Iterative sequence generation |
| 25 | `25_primes.axis` | Prime Numbers | Trial division, boolean returns |
| 26 | `26_bubble_sort.axis` | Bubble Sort | Array sorting, nested loops, swap via copy |
| 27 | `27_gcd_lcm.axis` | GCD & LCM | Euclidean algorithm, helper functions |
| 28 | `28_fast_power.axis` | Fast Exponentiation | Repeated squaring, bitwise operations |
| 29 | `29_patterns.axis` | Number Patterns | Nested loops, text-based geometric shapes |
| 30 | `30_compile_mode.axis` | Compile Mode | `mode compile`, `main() -> i32`, native binary |

## Language Features Demonstrated

- **I/O**: `write()`, `writeln()`, `input()`
- **Variables**: `name: type = value` with types `i8`–`i64`, `u8`–`u64`, `bool`, `str`
- **Conditionals**: `when`/`else when`/`else`
- **Loops**: `for x in range(a, b)`, `for x in array`, `while cond:`, `repeat:`
- **Control flow**: `stop` (break), `skip` (continue)
- **Functions**: `func name(params) -> type:` with `return`
- **Recursion**: Functions calling themselves (factorial, power)
- **Mutation**: `update` modifier for pass-by-reference
- **Data**: arrays `(type; size)`, fields `field:`, enums `enum:`
- **Pattern matching**: `match value:` with `_` wildcard
- **Copy**: explicit `copy` for value-type deep copies
- **Type casting**: `update x as type` (in-place), `copy x as type` (converting)
- **Const**: `const name: type = value` for immutable bindings
- **Labels**: `@flag` on loops for multi-level `stop`/`skip`
- **Alias**: implicit aliasing semantics, `copy` for independence
- **Operators**: arithmetic, comparison, logical (`and`/`or`/`not`), bitwise
- **Modes**: `mode script` (top-level execution) / `mode compile` (requires `main() -> i32`)
