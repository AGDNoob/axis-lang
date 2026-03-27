#!/bin/bash
# Compare AXCC O1 vs O3 vs GCC-O1
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
BMDIR="benchmarks/Compile Mode"

cp "$BMDIR/bench1_fib_rec.axis" /tmp/bench1.axis
cp "$BMDIR/bench3_loops.axis" /tmp/bench3.axis
cp "$BMDIR/bench4_gcd.axis" /tmp/bench4.axis
cp "$BMDIR/bench2_primes.axis" /tmp/bench2.axis

./axis build /tmp/bench1.axis -o /tmp/b1_O1 -O1 --elf 2>/dev/null
./axis build /tmp/bench1.axis -o /tmp/b1_O3 -O3 --elf 2>/dev/null
./axis build /tmp/bench3.axis -o /tmp/b3_O1 -O1 --elf 2>/dev/null
./axis build /tmp/bench3.axis -o /tmp/b3_O3 -O3 --elf 2>/dev/null
./axis build /tmp/bench4.axis -o /tmp/b4_O1 -O1 --elf 2>/dev/null
./axis build /tmp/bench4.axis -o /tmp/b4_O3 -O3 --elf 2>/dev/null
./axis build /tmp/bench2.axis -o /tmp/b2_O1 -O1 --elf 2>/dev/null
./axis build /tmp/bench2.axis -o /tmp/b2_O3 -O3 --elf 2>/dev/null

chmod +x /tmp/b1_O1 /tmp/b1_O3 /tmp/b3_O1 /tmp/b3_O3 /tmp/b4_O1 /tmp/b4_O3 /tmp/b2_O1 /tmp/b2_O3

run3() {
    local best=""
    for i in 1 2 3; do
        local s
        s=$(date +%s%N)
        "$1" > /dev/null 2>&1
        local e
        e=$(date +%s%N)
        local ms=$(( (e - s) / 1000000 ))
        if [ -z "$best" ] || [ "$ms" -lt "$best" ]; then best=$ms; fi
    done
    echo "$best"
}

echo "================================================================"
echo "  AXCC -O1 vs -O3 vs GCC -O1"
echo "================================================================"
echo ""

echo "=== Benchmark 1: Fibonacci(38) ==="
t=$(run3 /tmp/b1_O1); printf "  AXCC -O1: %6d ms\n" "$t"
t=$(run3 /tmp/b1_O3); printf "  AXCC -O3: %6d ms\n" "$t"
t=$(run3 "$BMDIR/bench1_fib_rec_gcc_O1"); printf "  GCC  -O1: %6d ms\n" "$t"
echo ""

echo "=== Benchmark 2: Primes(500k) ==="
t=$(run3 /tmp/b2_O1); printf "  AXCC -O1: %6d ms\n" "$t"
t=$(run3 /tmp/b2_O3); printf "  AXCC -O3: %6d ms\n" "$t"
t=$(run3 "$BMDIR/bench2_primes_gcc_O1"); printf "  GCC  -O1: %6d ms\n" "$t"
echo ""

echo "=== Benchmark 3: NestedLoops(100M) ==="
t=$(run3 /tmp/b3_O1); printf "  AXCC -O1: %6d ms\n" "$t"
t=$(run3 /tmp/b3_O3); printf "  AXCC -O3: %6d ms\n" "$t"
t=$(run3 "$BMDIR/bench3_loops_gcc_O1"); printf "  GCC  -O1: %6d ms\n" "$t"
echo ""

echo "=== Benchmark 4: GCD(2M) ==="
t=$(run3 /tmp/b4_O1); printf "  AXCC -O1: %6d ms\n" "$t"
t=$(run3 /tmp/b4_O3); printf "  AXCC -O3: %6d ms\n" "$t"
t=$(run3 "$BMDIR/bench4_gcd_gcc_O1"); printf "  GCC  -O1: %6d ms\n" "$t"
