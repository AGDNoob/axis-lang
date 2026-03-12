# Getting Started

AXIS is a programming language with Python-like syntax. It has two modes:

- **Script mode** — compiles to a native binary on first run, caches it, and executes it. Subsequent runs use the cached binary.
- **Compile mode** — compiles to a standalone Windows PE or Linux ELF64 executable.

Both modes produce native x86-64 machine code via the AXCC compiler.

## Building AXCC

You need a C compiler (GCC or compatible).

```bash
# Windows (MinGW)
cd axcc
mingw32-make CC=gcc

# Linux
cd axcc
make CC=gcc
```

This produces a single binary: `axis.exe` (Windows) or `axis` (Linux). No other dependencies.

## Your First Program

Create a file called `hello.axis`:

```axis
mode script

writeln("Hello, World!")
```

Run it:

```bash
./axis hello.axis
```

Output:

```
Hello, World!
```

The first run compiles the program and caches the binary in `__axcache__/`. Future runs skip compilation and execute the cached binary directly.

## Compile Mode

For a standalone executable, use compile mode. The program needs a `main()` function:

```axis
mode compile

func main() -> i32:
    writeln("Hello from a native binary!")
    give 0
```

Build it:

```bash
# Windows PE
./axis hello.axis -o hello

# Linux ELF64
./axis hello.axis -o hello --elf

# Explicit PE
./axis hello.axis -o hello.exe --pe
```

The output is a standalone binary with zero runtime dependencies.

## VS Code Extension

The `axis-vscode/` folder contains a VS Code extension for syntax highlighting. Install it by copying the folder to your VS Code extensions directory or via the Extensions panel.

## Next

[Variables and Types](02-variables-and-types.md)
