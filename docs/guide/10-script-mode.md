# Script Mode

## What It Is

Script mode compiles your AXIS code to a native binary, caches it, and runs it. On subsequent runs with unchanged source, the cached binary is reused — skipping compilation entirely.

```axis
mode script

writeln("This compiles to native code and runs")
```

## Running

```bash
./axis program.axis
```

No `-o` flag, no build step. Just run.

## How the Cache Works

On first run:
1. AXCC compiles the `.axis` file to a native binary
2. The binary is stored in an `__axcache__/` directory next to the source file
3. The binary is executed

On subsequent runs:
1. AXCC checks if the source file has changed (timestamp comparison)
2. If unchanged, it runs the cached binary directly
3. If changed, it recompiles and updates the cache

## Structure

Script mode programs have top-level statements. No `main()` function needed:

```axis
mode script

x: i32 = 10
y: i32 = 20
writeln(x + y)

func helper(a: i32) -> i32:
    give a * 2

result: i32 = helper(x)
writeln(result)
```

Functions can be defined anywhere and called from top-level code.

## Performance

Script mode produces the same native code as compile mode. The only overhead is the initial compilation on first run (typically under 50ms). After that, it's the same native binary.

See [Benchmarks](../Benchmarks.md) for measurements.

## Next

[Compile Mode](11-compile-mode.md)
