# AXIS Benchmarks

This document records the performance of AXCC (the AXIS compiler) on a small set
of CPU-bound microbenchmarks. The goal is to make the language's behaviour
measurable and reproducible, not to claim a position against any other compiler.
GCC `-O0` is used as a familiar reference point for native, unoptimised code,
and CPython is used as a reference for interpreted scripting.

- **CPU**: AMD Ryzen 5 3500 (consumer desktop)
- **OS**: WSL2 Ubuntu 24.04 on Windows 11
- **Method**: 7 interleaved runs per test, best wall-clock time reported
- **AXCC Version**: v1.3.0
- **GCC Version**: GCC 13.3.0 (`-O0`)
- **Python Version**: CPython 3.12.3

---

## 1. AXCC Compile Mode vs GCC `-O0`

Both compilers produce native x86-64 ELF binaries from equivalent programs.
GCC `-O0` is chosen as a baseline because it emits straightforward, unoptimised
code; it is not intended as a performance target. Comparisons against `-O2`/`-O3`
are out of scope for this release.

### v1.3.0 Results

AXCC v1.3.0 runs up to 32 optimisation passes at `-O3`; benchmarks here are
compiled at the default `-O2` level. All integer operations are emitted as
native 32-bit instructions where the source type is `i32`.

| Benchmark                      | AXCC `-O2` | GCC `-O0` |
|--------------------------------|-----------:|----------:|
| Recursive Fibonacci `fib(38)`  |   190 ms   |   270 ms  |
| Prime Count (0–500K)           |    60 ms   |    60 ms  |
| Nested Loops (100M iterations) |   100 ms   |   280 ms  |
| GCD Stress (2M calls)          |    30 ms   |    40 ms  |

On this hardware, AXCC `-O2` is in the same range as GCC `-O0` across these
four workloads.

### Progression across AXCC versions

These tables track how AXCC's own output has changed between releases on the
same hardware and the same benchmark sources. They are not comparisons against
other compilers.

**vs. v1.2.1**

| Benchmark                      | v1.2.1  | v1.3.0  |
|--------------------------------|--------:|--------:|
| Recursive Fibonacci `fib(38)`  | 295 ms  | 190 ms  |
| Prime Count (0–500K)           |  77 ms  |  60 ms  |
| Nested Loops (100M iterations) | 370 ms  | 100 ms  |
| GCD Stress (2M calls)          |  48 ms  |  30 ms  |

**vs. v1.2.0**

| Benchmark                      | v1.2.0  | v1.3.0  |
|--------------------------------|--------:|--------:|
| Recursive Fibonacci `fib(38)`  | 425 ms  | 190 ms  |
| Prime Count (0–500K)           |  96 ms  |  60 ms  |
| Nested Loops (100M iterations) | 491 ms  | 100 ms  |
| GCD Stress (2M calls)          |  68 ms  |  30 ms  |

**vs. v1.1.0**

| Benchmark                      | v1.1.0  | v1.3.0  |
|--------------------------------|--------:|--------:|
| Recursive Fibonacci `fib(38)`  | 554 ms  | 190 ms  |
| Prime Count (0–500K)           | 161 ms  |  60 ms  |
| Nested Loops (100M iterations) | 686 ms  | 100 ms  |
| GCD Stress (2M calls)          |  73 ms  |  30 ms  |

### Binary Size

| Benchmark           | AXCC   | GCC `-O0` |
|---------------------|-------:|----------:|
| Recursive Fibonacci | 4.3 KB |   15.4 KB |
| Prime Count         | 4.3 KB |   15.4 KB |
| Nested Loops        | 4.3 KB |   15.4 KB |
| GCD Stress          | 4.3 KB |   15.4 KB |

AXCC binaries contain only the emitted program code; there is no C runtime,
no standard library, and no dynamic linker stub. GCC links against glibc by
default, which accounts for most of the size difference.

### Source Code

<details>
<summary>Benchmark 1 — Recursive Fibonacci fib(38)</summary>

**AXIS** (`mode compile`):

```text
mode compile
func fib(n: i32) i32:
    when n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)
func main() i32:
    r: i32 = fib(38)
    return r & 255
```

**C** (equivalent):

```c
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}
int main(void) {
    int r = fib(38);
    return r & 255;
}
```

</details>

<details>
<summary>Benchmark 2 — Prime Count to 500K</summary>

**AXIS** (`mode compile`):

```text
mode compile
func is_prime(n: i32) i32:
    when n < 2:
        return 0
    when n == 2:
        return 1
    when n % 2 == 0:
        return 0
    d: i32 = 3
    while d * d <= n:
        when n % d == 0:
            return 0
        d = d + 2
    return 1
func main() i32:
    count: i32 = 0
    n: i32 = 2
    while n <= 500000:
        count = count + is_prime(n)
        n = n + 1
    return count & 255
```

**C** (equivalent):

```c
int is_prime(int n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (int d = 3; d * d <= n; d += 2)
        if (n % d == 0) return 0;
    return 1;
}
int main(void) {
    int count = 0;
    for (int n = 2; n <= 500000; n++)
        count += is_prime(n);
    return count & 255;
}
```

</details>

<details>
<summary>Benchmark 3 — Nested Loops (100M iterations)</summary>

**AXIS** (`mode compile`):

```text
mode compile
func main() i32:
    sum: i32 = 0
    i: i32 = 0
    j: i32 = 0
    t: i32 = 0
    while i < 10000:
        j = 0
        while j < 10000:
            t = sum * 7 + i * j
            sum = t ^ (i + j)
            j = j + 1
        i = i + 1
    return (sum >> 24) & 255
```

**C** (equivalent):

```c
int main(void) {
    int sum = 0;
    for (int i = 0; i < 10000; i++)
        for (int j = 0; j < 10000; j++) {
            int t = sum * 7 + i * j;
            sum = t ^ (i + j);
        }
    return (sum >> 24) & 255;
}
```

</details>

<details>
<summary>Benchmark 4 — GCD Stress (2M calls)</summary>

**AXIS** (`mode compile`):

```text
mode compile
func gcd(x: i32, y: i32) i32:
    a: i32 = x
    b: i32 = y
    t: i32 = 0
    while b != 0:
        t = b
        b = a % b
        a = t
    return a
func main() i32:
    sum: i32 = 0
    i: i32 = 1
    while i <= 2000000:
        sum = sum + gcd(i, i + 7)
        i = i + 1
    return sum & 255
```

**C** (equivalent):

```c
int gcd(int x, int y) {
    int a = x, b = y, t;
    while (b != 0) { t = b; b = a % b; a = t; }
    return a;
}
int main(void) {
    int sum = 0;
    for (int i = 1; i <= 2000000; i++)
        sum += gcd(i, i + 7);
    return sum & 255;
}
```

</details>

---

## 2. AXIS Script Mode vs Python (CPython 3.12.3)

AXIS script mode (`mode script`) compiles the source to a native binary on first
run, caches it on disk, and re-executes the cached binary on subsequent runs.
CPython interprets the source on every run. The two tools work very differently;
this section simply records the observed wall-clock time for the same algorithm
in both.

### v1.3.0 Results

| Benchmark                     | AXIS Script | Python 3.12 |
|-------------------------------|------------:|------------:|
| Recursive Fibonacci `fib(38)` |    250 ms   |    5 200 ms |
| Prime Count (0–500K)          |    130 ms   |    1 440 ms |

On these two CPU-bound workloads, the cached native binary finishes
approximately an order of magnitude faster than the interpreter. This is the
expected shape of the comparison—compiled native code versus a bytecode
interpreter—not a claim about Python as a language.

### Progression across AXCC versions

**vs. v1.2.1**

| Benchmark                     | v1.2.1  | v1.3.0  |
|-------------------------------|--------:|--------:|
| Recursive Fibonacci `fib(38)` | 424 ms  | 250 ms  |
| Prime Count (0–500K)          | 153 ms  | 130 ms  |

**vs. v1.2.0**

| Benchmark                     | v1.2.0  | v1.3.0  |
|-------------------------------|--------:|--------:|
| Recursive Fibonacci `fib(38)` | 436 ms  | 250 ms  |
| Prime Count (0–500K)          | 167 ms  | 130 ms  |

**vs. v1.1.0**

| Benchmark                     | v1.1.0  | v1.3.0  |
|-------------------------------|--------:|--------:|
| Recursive Fibonacci `fib(38)` | 795 ms  | 250 ms  |
| Prime Count (0–500K)          | 235 ms  | 130 ms  |

### Source Code

<details>
<summary>Benchmark 1 — Recursive Fibonacci fib(38)</summary>

**AXIS** (`mode script`):

```text
mode script

func fib(n: i32) i32:
    when n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

result: i32 = fib(38)
```

**Python** (equivalent):

```python
import sys

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

result = fib(38)
sys.exit(result % 256)
```

</details>

<details>
<summary>Benchmark 2 — Prime Count to 500K</summary>

**AXIS** (`mode script`):

```text
mode script

func is_prime(n: i32) i32:
    when n < 2:
        return 0
    i: i32 = 2
    while i * i <= n:
        when n % i == 0:
            return 0
        i = i + 1
    return 1

count: i32 = 0
num: i32 = 2
while num < 500000:
    count = count + is_prime(num)
    num = num + 1
```

**Python** (equivalent):

```python
import sys

def is_prime(n):
    if n < 2:
        return 0
    i = 2
    while i * i <= n:
        if n % i == 0:
            return 0
        i = i + 1
    return 1

count = 0
num = 2
while num < 500000:
    count = count + is_prime(num)
    num = num + 1

sys.exit(count % 256)
```

</details>

---

## 3. Compiler & Binary Sizes

| Component                     | v1.3.0     | Initial Release    |
|-------------------------------|------------|--------------------|
| AXCC compiler (ELF binary)    | 309 KB     | 203 KB             |
| Compiled AXIS binary (avg)    | ~4.3 KB    | ~2.8 KB            |
| GCC compiled binary (avg)     | ~15.4 KB   | ~60 KB             |

The entire AXCC toolchain — compiler, assembler, linker, PE/ELF generator — fits in a single **~309 KB** binary with **zero external dependencies**.

---

## Notes

- AXCC v1.3.0 runs up to 32 optimisation passes at `-O3` (dead-code elimination,
  constant folding and propagation, copy propagation, function inlining, LICM,
  loop unrolling, linear-scan register allocation, strength reduction,
  register-aware instruction selection, CMP+Branch fusion, IR load/store
  elimination, x64 spill/reload caching, peephole optimisation, redundant
  instruction elimination, and others). Benchmarks in this document were
  compiled at `-O2`, which is the default.
- Script-mode timings include the overhead of loading the cached binary from
  disk and executing it as a child process. First-run compilation time is
  excluded; the cache was pre-warmed before measurement.
- Python timings include interpreter startup, to keep the end-to-end shell
  measurement consistent between both tools.
- The Python benchmarks use `while` loops rather than `for i in range(…)` so
  that the control-flow structure matches the AXIS source line-for-line.
- Cross-version tables compare AXCC's own output across releases. The v1.1.0
  and v1.2.x numbers were measured on Windows PE binaries; v1.3.0 numbers are
  from Linux ELF binaries via WSL2. Hardware and CPU are identical across all
  versions.
