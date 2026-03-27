#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
BM="benchmarks/Compile Mode"

echo "=== Compiling ==="
./axis "$BM/bench3_loops.axis" -o /tmp/b3_O0 --elf -O0 2>/dev/null
./axis "$BM/bench3_loops.axis" -o /tmp/b3_O1 --elf -O1 2>/dev/null
./axis "$BM/bench1_fib_rec.axis" -o /tmp/b1_O0 --elf -O0 2>/dev/null
./axis "$BM/bench1_fib_rec.axis" -o /tmp/b1_O1 --elf -O1 2>/dev/null
./axis "$BM/bench2_primes.axis" -o /tmp/b2_O0 --elf -O0 2>/dev/null
./axis "$BM/bench2_primes.axis" -o /tmp/b2_O1 --elf -O1 2>/dev/null

run_bench() {
    local NAME=$1
    local BIN=$2
    local RUNS=$3
    local TOTAL=0
    for i in $(seq 1 $RUNS); do
        T=$( { time "$BIN" >/dev/null 2>&1; } 2>&1 | grep real | sed 's/real\t//;s/m/ /;' | awk '{print $1*60+$2}' )
        TOTAL=$(echo "$TOTAL + $T" | bc)
    done
    AVG=$(echo "scale=4; $TOTAL / $RUNS" | bc)
    echo "  $NAME: avg=${AVG}s ($RUNS runs)"
}

echo ""
echo "=== bench1 (Fibonacci recursive) ==="
run_bench "O0" /tmp/b1_O0 5
run_bench "O1" /tmp/b1_O1 5

echo ""
echo "=== bench2 (Prime counting) ==="
run_bench "O0" /tmp/b2_O0 5
run_bench "O1" /tmp/b2_O1 5

echo ""
echo "=== bench3 (Nested loops) ==="
run_bench "O0" /tmp/b3_O0 5
run_bench "O1" /tmp/b3_O1 5
