#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
BM="benchmarks/Compile Mode"

# Build all 4
./axis build "$BM/bench1_fib_rec.axis"  -o /tmp/bench1_new -O3 --elf 2>/dev/null
./axis build "$BM/bench2_primes.axis"   -o /tmp/bench2_new -O3 --elf 2>/dev/null
./axis build "$BM/bench3_loops.axis"    -o /tmp/bench3_new -O3 --elf 2>/dev/null
./axis build "$BM/bench4_gcd.axis"      -o /tmp/bench4_new -O3 --elf 2>/dev/null

echo "=== BENCH1: Fibonacci ==="
echo "  NEW O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' /tmp/bench1_new 2>&1 | tail -1; done
echo "  PRECOMPILED O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench1_axcc_O3" 2>&1 | tail -1; done
echo "  GCC -O1:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench1_fib_rec_gcc_O1" 2>&1 | tail -1; done

echo ""
echo "=== BENCH2: Primes ==="
echo "  NEW O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' /tmp/bench2_new 2>&1 | tail -1; done
echo "  PRECOMPILED O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench2_axcc_O3" 2>&1 | tail -1; done
echo "  GCC -O1:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench2_primes_gcc_O1" 2>&1 | tail -1; done

echo ""
echo "=== BENCH3: NestedLoops ==="
echo "  NEW O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' /tmp/bench3_new 2>&1 | tail -1; done
echo "  PRECOMPILED O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench3_axcc_O3" 2>&1 | tail -1; done
echo "  GCC -O1:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench3_loops_gcc_O1" 2>&1 | tail -1; done

echo ""
echo "=== BENCH4: GCD ==="
echo "  NEW O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' /tmp/bench4_new 2>&1 | tail -1; done
echo "  PRECOMPILED O3:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench4_axcc_O3" 2>&1 | tail -1; done
echo "  GCC -O1:"
for i in 1 2 3; do /usr/bin/time -f '    %e s' "$BM/bench4_gcd_gcc_O1" 2>&1 | tail -1; done
