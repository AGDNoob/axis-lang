# AXIS Benchmarks

All benchmarks were run on a consumer PC with debloated Windows 11.

- **CPU**: AMD Ryzen 5 3500 (consumer desktop)
- **OS**: Windows 11 (debloated)
- **Method**: 5 runs per test, median taken
- **AXCC Version**: v1.1.0
- **GCC Version**: MinGW-w64 GCC (no optimizations, `-O0`)
- **Python Version**: CPython 3.13.7

---

## 1. AXCC Compile Mode vs GCC `-O0`

Both compilers produce native x86-64 Windows PE executables from equivalent programs.
AXCC currently performs no optimizations — this is a direct comparison against GCC at `-O0`.

| Benchmark                     | AXCC    | GCC `-O0` | Ratio       |
|-------------------------------|---------|-----------|-------------|
| Recursive Fibonacci `fib(38)` | 554 ms  | 78 ms     | 7.1× slower |
| Prime Count (0–500K)          | 161 ms  | 77 ms     | 2.1× slower |
| Nested Loops (100M iterations)| 686 ms  | 146 ms    | 4.7× slower |
| GCD Stress (2M calls)         | 73 ms   | 51 ms     | 1.4× slower |

**Takeaway**: AXCC is 1.4–7.1× slower than GCC `-O0`. The biggest gap is in recursive Fibonacci, where function call overhead dominates. For integer-heavy workloads like GCD, AXCC is already within striking distance of GCC.

### Binary Size

| Benchmark                     | AXCC      | GCC `-O0`  | Ratio        |
|-------------------------------|-----------|------------|--------------||
| Recursive Fibonacci           | 2.5 KB    | 59.6 KB    | **24× smaller** |
| Prime Count                   | 3.0 KB    | 59.6 KB    | **20× smaller** |
| Nested Loops                  | 3.0 KB    | 59.6 KB    | **20× smaller** |
| GCD Stress                    | 3.0 KB    | 59.6 KB    | **20× smaller** |

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
| Recursive Fibonacci `fib(38)` | 795 ms      | 8 411 ms    | **10.6×** faster |
| Prime Count (0–500K)          | 235 ms      | 2 779 ms    | **11.8×** faster |

**Takeaway**: AXIS script mode is ~10–12× faster than CPython 3.13 on compute-heavy workloads. This is expected — AXIS compiles to native x86-64 machine code while Python interprets bytecode.

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
| AXCC compiler (Windows PE)    | 196 KB     |
| AXCC compiler (Linux ELF)     | 147 KB     |
| Compiled AXIS binary (avg)    | ~3 KB      |
| GCC compiled binary (avg)     | ~60 KB     |

The entire AXCC toolchain — compiler, assembler, linker, PE/ELF generator — fits in a single **~196 KB** binary with **zero external dependencies**.

---

## Notes

- AXCC performs **no optimizations** — no register allocation, no constant folding, no dead code elimination. All values go through the stack. Future optimization passes will close the gap with GCC.
- Script mode timings include the overhead of loading the cached binary from disk and executing it via a child process. First-run compilation time is excluded (cache was pre-warmed).
- Python timings include interpreter startup. Both AXIS script and Python were timed end-to-end from the shell.
- The Python benchmarks use `while` loops (not `for i in range(...)`) to match the AXIS code structure as closely as possible.
