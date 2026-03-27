#!/bin/bash
set -e
BASEDIR=/mnt/c/Users/marlo/Projekte/AXIS
AXIS="$BASEDIR/axcc/axis"

# Use examples that are known to work (no spaces in path)
TRIVIAL="$BASEDIR/code/examples/01_hello_world.axis"
FIB="$BASEDIR/code/examples/16_fibonacci.axis"
PRIMES="$BASEDIR/code/examples/17_primes.axis"

echo "=== Trivial: 01_hello_world.axis (pure compile overhead) ==="
for i in 1 2 3; do
    { time "$AXIS" "$TRIVIAL" > /dev/null; } 2>&1
done

echo ""
echo "=== 16_fibonacci.axis (compile + execute) ==="
for i in 1 2 3; do
    { time "$AXIS" "$FIB" > /dev/null; } 2>&1
done

echo ""
echo "=== 17_primes.axis (compile + execute) ==="
for i in 1 2 3; do
    { time "$AXIS" "$PRIMES" > /dev/null; } 2>&1
done

# Now copy the heavy benchmarks to /tmp
cp "$BASEDIR/axcc/benchmarks/Script Mode/script_bench1_fib.axis" /tmp/bench_fib.axis
cp "$BASEDIR/axcc/benchmarks/Script Mode/script_bench2_primes.axis" /tmp/bench_primes.axis

echo ""
echo "=== bench: fib(38) (compile + execute) ==="
for i in 1 2 3; do
    { time "$AXIS" /tmp/bench_fib.axis > /dev/null; } 2>&1
done

echo ""
echo "=== bench: primes<500k (compile + execute) ==="
for i in 1 2 3; do
    { time "$AXIS" /tmp/bench_primes.axis > /dev/null; } 2>&1
done
