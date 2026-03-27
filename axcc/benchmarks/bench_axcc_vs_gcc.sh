#!/bin/bash
# Clean AXCC O3 vs GCC benchmark - precise timing
cd "/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"

measure() {
    local label="$1"
    local binary="$2"
    local best=999999999
    for run in 1 2 3; do
        local start end elapsed
        start=$(date +%s%N)
        "$binary" > /dev/null 2>&1
        end=$(date +%s%N)
        elapsed=$(( (end - start) / 1000000 ))  # milliseconds
        if [ "$elapsed" -lt "$best" ]; then
            best=$elapsed
        fi
    done
    echo "$best"
}

echo "================================================================"
echo "  AXCC -O3 vs GCC  (best of 3 runs, milliseconds)"
echo "  GCC version: $(gcc --version | head -1)"
echo "================================================================"
echo ""

printf "%-20s | %10s | %10s | %10s | %10s | %10s\n" "Benchmark" "AXCC -O3" "GCC -O0" "GCC -O1" "GCC -O2" "GCC -O3"
printf "%-20s-+-%10s-+-%10s-+-%10s-+-%10s-+-%10s\n" "--------------------" "----------" "----------" "----------" "----------" "----------"

benches=("bench1_fib_rec" "bench2_primes" "bench3_loops" "bench4_gcd")
labels=("Fibonacci(38)" "Primes(500k)" "NestedLoops(100M)" "GCD(2M)")

for i in "${!benches[@]}"; do
    bench="${benches[$i]}"
    label="${labels[$i]}"
    
    axcc=$(measure "AXCC-O3" "./${bench}_axcc_O3")
    gcc0=$(measure "GCC-O0" "./${bench}_gcc_O0")
    gcc1=$(measure "GCC-O1" "./${bench}_gcc_O1")
    gcc2=$(measure "GCC-O2" "./${bench}_gcc_O2")
    gcc3=$(measure "GCC-O3" "./${bench}_gcc_O3")
    
    printf "%-20s | %7d ms | %7d ms | %7d ms | %7d ms | %7d ms\n" "$label" "$axcc" "$gcc0" "$gcc1" "$gcc2" "$gcc3"
done

echo ""
echo "--- Ratios (GCC time / AXCC time, >1 = AXCC faster) ---"
echo ""
printf "%-20s | %10s | %10s | %10s | %10s\n" "Benchmark" "vs GCC-O0" "vs GCC-O1" "vs GCC-O2" "vs GCC-O3"
printf "%-20s-+-%10s-+-%10s-+-%10s-+-%10s\n" "--------------------" "----------" "----------" "----------" "----------"

for i in "${!benches[@]}"; do
    bench="${benches[$i]}"
    label="${labels[$i]}"
    
    axcc=$(measure "AXCC-O3" "./${bench}_axcc_O3")
    gcc0=$(measure "GCC-O0" "./${bench}_gcc_O0")
    gcc1=$(measure "GCC-O1" "./${bench}_gcc_O1")
    gcc2=$(measure "GCC-O2" "./${bench}_gcc_O2")
    gcc3=$(measure "GCC-O3" "./${bench}_gcc_O3")
    
    r0=$(awk "BEGIN{if($axcc>0) printf \"%.2fx\", $gcc0/$axcc; else print \"N/A\"}")
    r1=$(awk "BEGIN{if($axcc>0) printf \"%.2fx\", $gcc1/$axcc; else print \"N/A\"}")
    r2=$(awk "BEGIN{if($axcc>0) printf \"%.2fx\", $gcc2/$axcc; else print \"N/A\"}")
    r3=$(awk "BEGIN{if($axcc>0) printf \"%.2fx\", $gcc3/$axcc; else print \"N/A\"}")
    
    printf "%-20s | %10s | %10s | %10s | %10s\n" "$label" "$r0" "$r1" "$r2" "$r3"
done
