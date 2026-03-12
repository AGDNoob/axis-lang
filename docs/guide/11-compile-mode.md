# Compile Mode

## What It Is

Compile mode produces a standalone executable — a Windows PE or Linux ELF64 binary with zero runtime dependencies.

```axis
mode compile

func main() -> i32:
    writeln("Hello from a native binary")
    give 0
```

## Building

```bash
# Windows PE executable
./axis program.axis -o program

# Linux ELF64 executable
./axis program.axis -o program --elf

# Explicit PE (e.g. cross-compile on Linux)
./axis program.axis -o program.exe --pe
```

The output binary can be distributed and run on any machine with the same OS and architecture. No AXIS installation needed on the target machine.

## Entry Point

Compile mode requires a `main()` function that returns `i32`:

```axis
mode compile

func main() -> i32:
    // your program here
    give 0    // exit code
```

The return value becomes the process exit code.

## Helper Functions

Define other functions alongside `main()`:

```axis
mode compile

func factorial(n: i32) -> i32:
    result: i32 = 1
    i: i32 = 2
    while i <= n:
        result = result * i
        i = i + 1
    give result

func main() -> i32:
    writeln(factorial(10))
    give 0
```

## Binary Size

AXCC-compiled binaries are small — typically 2.5 to 3 KB. They contain only your code, a minimal startup stub, and the I/O routines you actually use. No C runtime, no standard library.

See [Benchmarks](../Benchmarks.md) for detailed measurements.

## Cross-Compilation

AXCC can produce both PE (Windows) and ELF (Linux) binaries from the same machine:

- `--pe` — force Windows PE output
- `--elf` — force Linux ELF64 output
- No flag — use the current OS default (PE on Windows, ELF on Linux)
