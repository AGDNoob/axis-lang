# AXIS Benchmarks

All benchmarks were run on a consumer PC with debloated Windows 11.

- **CPU**: AMD Ryzen 5 3500 (consumer desktop)
- **OS**: Windows 11 (debloated)
- **Method**: 7 interleaved runs per test, best taken
- **AXCC Version**: v1.2.0
- **GCC Version**: MinGW-w64 GCC (no optimizations, `-O0`)
- **Python Version**: CPython 3.13.7

---

## 1. AXCC Compile Mode vs GCC `-O0`

Both compilers produce native x86-64 Windows PE executables from equivalent programs.
AXCC v1.2.0 performs dead code elimination, constant folding/propagation,
linear-scan register allocation, strength reduction, register-aware instruction
selection, CMP+Branch fusion, IR load-store elimination, x64 spill-reload
caching, and LEA-based multiply for small constants (×3, ×5, ×9).

| Benchmark                     | AXCC    | GCC `-O0` | Ratio       |
|-------------------------------|---------|-----------|-------------|
| Recursive Fibonacci `fib(38)` | 425 ms  | 320 ms    | 1.3× slower |
| Prime Count (0–500K)          | 96 ms   | 89 ms     | 1.1× slower |
| Nested Loops (100M iterations)| 491 ms  | 448 ms    | 1.1× slower |
| GCD Stress (2M calls)         | 68 ms   | 63 ms     | 1.1× slower |

**Takeaway**: AXCC v1.2.0 is 1.1–1.3× slower than GCC `-O0`, a major improvement
over v1.1.0 (1.4–7.1×). All four benchmarks are within 30% of GCC, with primes,
nested loops, and GCD stress at near-parity. The nested loop benchmark improved
dramatically from 4.7× to 1.1× thanks to IR load-store elimination,
LEA-multiply strength reduction, register allocation, and CMP+Branch fusion.

### Improvement over v1.1.0

| Benchmark                     | v1.1.0  | v1.2.0  | Speedup     |
|-------------------------------|---------|---------|-------------|
| Recursive Fibonacci `fib(38)` | 554 ms  | 425 ms  | **1.3× faster** |
| Prime Count (0–500K)          | 161 ms  | 96 ms   | **1.7× faster** |
| Nested Loops (100M iterations)| 686 ms  | 491 ms  | **1.4× faster** |
| GCD Stress (2M calls)         | 73 ms   | 68 ms   | **1.1× faster** |

### Binary Size

| Benchmark                     | AXCC      | GCC `-O0`  | Ratio        |
|-------------------------------|-----------|------------|--------------|
| Recursive Fibonacci           | 2.5 KB    | 59.6 KB    | **24× smaller** |
| Prime Count                   | 3.0 KB    | 59.6 KB    | **20× smaller** |
| Nested Loops                  | 2.5 KB    | 59.6 KB    | **24× smaller** |
| GCD Stress                    | 2.5 KB    | 59.6 KB    | **24× smaller** |

AXCC produces minimal PE binaries with no C runtime, no standard library, and no linker bloat. GCC links the MinGW CRT by default, which adds ~57 KB of overhead even at `-O0`.

### Source Code

<details>
<summary>Benchmark 1 — Recursive Fibonacci fib(38)</summary>

**AXIS** (`mode compile`):

``` text
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
```
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
```
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

## 2. AXIS Script Mode vs Python (CPython 3.13.7)

AXIS script mode (`mode script`) compiles to a native binary on first run, caches it, and re-executes the cached binary on subsequent runs. Python interprets the source every time.

Both run the **identical algorithm** — only the syntax differs.

| Benchmark                     | AXIS Script | Python 3.13 | Speedup     |
|-------------------------------|-------------|-------------|-------------|
| Recursive Fibonacci `fib(38)` | 436 ms      | 8 561 ms    | **19.6×** faster |
| Prime Count (0–500K)          | 167 ms      | 2 657 ms    | **15.9×** faster |

**Takeaway**: AXIS script mode is ~16–20× faster than CPython 3.13 on compute-heavy
workloads. v1.2.0's register allocation, constant folding, load-store elimination,
and backend optimizations improved this from 10–12× (v1.1.0) to 16–20× faster.

### Improvement over v1.1.0

| Benchmark                     | v1.1.0    | v1.2.0    | Speedup     |
|-------------------------------|-----------|-----------|-------------|
| Recursive Fibonacci `fib(38)` | 795 ms    | 436 ms    | **1.8× faster** |
| Prime Count (0–500K)          | 235 ms    | 167 ms    | **1.4× faster** |

### Source Code

<details>
<summary>Benchmark 1 — Recursive Fibonacci fib(38)</summary>

**AXIS** (`mode script`):
```
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
```
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

| Component                     | Size       |
|-------------------------------|------------|
| AXCC compiler (Windows PE)    | 203 KB     |
| AXCC compiler (Linux ELF)     | ~155 KB    |
| Compiled AXIS binary (avg)    | ~2.7 KB    |
| GCC compiled binary (avg)     | ~60 KB     |

The entire AXCC toolchain — compiler, assembler, linker, PE/ELF generator — fits in a single **~196 KB** binary with **zero external dependencies**.

---

## Notes

- AXCC v1.2.0 performs DCE, constant folding/propagation, strength reduction (power-of-2 multiply/divide/modulo), linear-scan register allocation, register-aware instruction selection, and CMP+Branch fusion. The remaining gap to GCC `-O0` is primarily in tight inner loops where GCC's instruction combining still wins.
- Script mode timings include the overhead of loading the cached binary from disk and executing it via a child process. First-run compilation time is excluded (cache was pre-warmed).
- Python timings include interpreter startup. Both AXIS script and Python were timed end-to-end from the shell.
- The Python benchmarks use `while` loops (not `for i in range(...)`) to match the AXIS code structure as closely as possible.
