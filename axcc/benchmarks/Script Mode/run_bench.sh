#!/usr/bin/env bash
set -e
cd /mnt/c/Users/marlo/Projekte/AXIS
AXIS=./axcc/axis
BDIR="axcc/benchmarks/Script Mode"
PYDIR="axcc/benchmarks/Script Mode"

cp "$BDIR/script_bench1_fib.axis" /tmp/fib.axis
cp "$BDIR/script_bench2_primes.axis" /tmp/primes.axis
cp "$PYDIR/bench1_fib.py" /tmp/bench1_fib.py
cp "$PYDIR/bench2_primes.py" /tmp/bench2_primes.py

echo "============================================"
echo " AXIS Script Mode  vs  $(python3 --version 2>&1)"
echo "============================================"
echo ""

RUNS=3

# --- Fibonacci(38) ---
echo "--- Benchmark 1: Fibonacci(38) - recursive ---"
echo ""

for r in $(seq 1 $RUNS); do
    t=$( { time $AXIS /tmp/fib.axis >/dev/null 2>&1; } 2>&1 )
    echo "  AXIS run $r: $t"
done

echo ""

for r in $(seq 1 $RUNS); do
    t=$( { time python3 /tmp/bench1_fib.py >/dev/null 2>&1; } 2>&1 )
    echo "  Python run $r: $t"
done

echo ""
echo "--- Benchmark 2: Primes < 500000 ---"
echo ""

for r in $(seq 1 $RUNS); do
    t=$( { time $AXIS /tmp/primes.axis >/dev/null 2>&1; } 2>&1 )
    echo "  AXIS run $r: $t"
done

echo ""

for r in $(seq 1 $RUNS); do
    t=$( { time python3 /tmp/bench2_primes.py >/dev/null 2>&1; } 2>&1 )
    echo "  Python run $r: $t"
done

echo ""
echo "============================================"
echo " Done"
echo "============================================"
