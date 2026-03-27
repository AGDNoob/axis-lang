#!/bin/bash
# AXCC O3 vs GCC benchmark script
set -e

BMDIR="/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"
cd "$BMDIR"

echo "=== Building GCC binaries ==="
for opt in O0 O1 O2 O3; do
    for bench in bench1_fib_rec bench2_primes bench3_loops bench4_gcd; do
        gcc -"$opt" -o "${bench}_gcc_${opt}" "${bench}.c"
        echo "${bench} gcc -${opt} OK"
    done
done

echo ""
echo "================================================================"
echo "  AXCC -O3 vs GCC -O0 / -O1 / -O2 / -O3"
echo "  Each benchmark run 3 times, best time reported"
echo "================================================================"
echo ""

BENCHMARKS="bench1_fib_rec bench2_primes bench3_loops bench4_gcd"
LABELS=("Fibonacci(38)" "Primes(500k)" "NestedLoops(100M)" "GCD(2M)")

run_best_of_3() {
    local binary="$1"
    local best=999999
    for run in 1 2 3; do
        local t
        t=$( { time "$binary"; } 2>&1 | grep real | sed 's/real\t//' )
        # Parse minutes and seconds
        local min sec ms
        min=$(echo "$t" | sed 's/m.*//')
        sec=$(echo "$t" | sed 's/.*m//' | sed 's/s//')
        ms=$(echo "$min $sec" | awk '{printf "%.3f", $1*60 + $2}')
        if (( $(echo "$ms < $best" | bc -l) )); then
            best=$ms
        fi
    done
    echo "$best"
}

idx=0
for bench in $BENCHMARKS; do
    label="${LABELS[$idx]}"
    echo "--- $label ($bench) ---"
    
    # AXCC O3
    axcc_time=$(run_best_of_3 "./${bench}_axcc_O3")
    echo "  AXCC  -O3:  ${axcc_time}s"
    
    for opt in O0 O1 O2 O3; do
        gcc_time=$(run_best_of_3 "./${bench}_gcc_${opt}")
        ratio=$(echo "$gcc_time $axcc_time" | awk '{if ($2 > 0) printf "%.2f", $1/$2; else print "N/A"}')
        echo "  GCC  -${opt}:  ${gcc_time}s  (GCC/AXCC = ${ratio}x)"
    done
    echo ""
    idx=$((idx + 1))
done
