#!/bin/bash
cd "/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"

echo "=== Correctness check (exit codes) ==="
./bench1_axcc_O3; echo "bench1 AXCC O3: exit=$?"
./bench1_fib_rec_gcc_O0; echo "bench1 GCC O0: exit=$?"
./bench2_axcc_O3; echo "bench2 AXCC O3: exit=$?"
./bench2_primes_gcc_O0; echo "bench2 GCC O0: exit=$?"
./bench3_axcc_O3; echo "bench3 AXCC O3: exit=$?"
./bench3_loops_gcc_O0; echo "bench3 GCC O0: exit=$?"
./bench4_axcc_O3; echo "bench4 AXCC O3: exit=$?"
./bench4_gcd_gcc_O0; echo "bench4 GCC O0: exit=$?"

echo ""
echo "=== Timing check with higher precision ==="
echo "--- bench1 AXCC O3 ---"
time ./bench1_axcc_O3
echo "--- bench1 GCC O0 ---"
time ./bench1_fib_rec_gcc_O0
echo "--- bench4 AXCC O3 ---"
time ./bench4_axcc_O3
echo "--- bench4 GCC O0 ---"
time ./bench4_gcd_gcc_O0
