#!/bin/bash
# Precise benchmark with /usr/bin/time
cd "/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"

echo "================================================================"
echo "  AXCC -O3 vs GCC  (user CPU time, best of 3 runs)"
echo "  GCC version: $(gcc --version | head -1)"
echo "================================================================"
echo ""

measure_cpu() {
    local binary="$1"
    local best="999.999"
    for run in 1 2 3; do
        local t
        t=$(/usr/bin/time -f "%e" "$binary" 2>&1 >/dev/null)
        if [ "$(echo "$t < $best" | bc -l)" -eq 1 ]; then
            best="$t"
        fi
    done
    echo "$best"
}

echo "=== Individual timing (wall clock seconds, best of 3) ==="
echo ""

for bench in bench1_fib_rec bench2_primes bench3_loops bench4_gcd; do
    echo "--- $bench ---"
    axcc=$(measure_cpu "./${bench}_axcc_O3")
    echo "  AXCC -O3: ${axcc}s"
    for opt in O0 O1 O2 O3; do
        gcc_t=$(measure_cpu "./${bench}_gcc_${opt}")
        echo "  GCC  -${opt}: ${gcc_t}s"
    done
    echo ""
done
