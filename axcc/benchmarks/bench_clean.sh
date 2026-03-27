#!/bin/bash
# Simple, clean AXCC O3 vs GCC benchmark
BMDIR="/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"
cd "$BMDIR"

echo "================================================================"
echo "  AXCC -O3 vs GCC (best of 3 runs)"
echo "  GCC: $(gcc --version | head -1)"
echo "================================================================"
echo ""

run3() {
    # Run 3 times, print best wall time
    local bin="$1"
    local best=""
    for i in 1 2 3; do
        local s=$(date +%s%N)
        "$bin" > /dev/null 2>&1
        local e=$(date +%s%N)
        local ms=$(( (e - s) / 1000000 ))
        if [ -z "$best" ] || [ "$ms" -lt "$best" ]; then
            best=$ms
        fi
    done
    echo "$best"
}

# bench1: Fibonacci(38)
echo "=== Benchmark 1: Fibonacci(38) - 63M recursive calls ==="
t=$(run3 ./bench1_axcc_O3);       printf "  AXCC -O3: %6d ms\n" "$t"; axcc1=$t
t=$(run3 ./bench1_fib_rec_gcc_O0); printf "  GCC  -O0: %6d ms\n" "$t"; gcc1_0=$t
t=$(run3 ./bench1_fib_rec_gcc_O1); printf "  GCC  -O1: %6d ms\n" "$t"; gcc1_1=$t
t=$(run3 ./bench1_fib_rec_gcc_O2); printf "  GCC  -O2: %6d ms\n" "$t"; gcc1_2=$t
t=$(run3 ./bench1_fib_rec_gcc_O3); printf "  GCC  -O3: %6d ms\n" "$t"; gcc1_3=$t
echo ""

# bench2: Primes(500k)
echo "=== Benchmark 2: Primes(500k) - trial division ==="
t=$(run3 ./bench2_axcc_O3);       printf "  AXCC -O3: %6d ms\n" "$t"; axcc2=$t
t=$(run3 ./bench2_primes_gcc_O0); printf "  GCC  -O0: %6d ms\n" "$t"; gcc2_0=$t
t=$(run3 ./bench2_primes_gcc_O1); printf "  GCC  -O1: %6d ms\n" "$t"; gcc2_1=$t
t=$(run3 ./bench2_primes_gcc_O2); printf "  GCC  -O2: %6d ms\n" "$t"; gcc2_2=$t
t=$(run3 ./bench2_primes_gcc_O3); printf "  GCC  -O3: %6d ms\n" "$t"; gcc2_3=$t
echo ""

# bench3: NestedLoops(100M)
echo "=== Benchmark 3: NestedLoops(100M) - mul/xor chain ==="
t=$(run3 ./bench3_axcc_O3);       printf "  AXCC -O3: %6d ms\n" "$t"; axcc3=$t
t=$(run3 ./bench3_loops_gcc_O0);  printf "  GCC  -O0: %6d ms\n" "$t"; gcc3_0=$t
t=$(run3 ./bench3_loops_gcc_O1);  printf "  GCC  -O1: %6d ms\n" "$t"; gcc3_1=$t
t=$(run3 ./bench3_loops_gcc_O2);  printf "  GCC  -O2: %6d ms\n" "$t"; gcc3_2=$t
t=$(run3 ./bench3_loops_gcc_O3);  printf "  GCC  -O3: %6d ms\n" "$t"; gcc3_3=$t
echo ""

# bench4: GCD(2M)
echo "=== Benchmark 4: GCD(2M) - Euclidean algorithm ==="
t=$(run3 ./bench4_axcc_O3);       printf "  AXCC -O3: %6d ms\n" "$t"; axcc4=$t
t=$(run3 ./bench4_gcd_gcc_O0);    printf "  GCC  -O0: %6d ms\n" "$t"; gcc4_0=$t
t=$(run3 ./bench4_gcd_gcc_O1);    printf "  GCC  -O1: %6d ms\n" "$t"; gcc4_1=$t
t=$(run3 ./bench4_gcd_gcc_O2);    printf "  GCC  -O2: %6d ms\n" "$t"; gcc4_2=$t
t=$(run3 ./bench4_gcd_gcc_O3);    printf "  GCC  -O3: %6d ms\n" "$t"; gcc4_3=$t
echo ""

echo "================================================================"
echo "  SUMMARY: Ratio GCC/AXCC (>1.0 = AXCC faster)"
echo "================================================================"
echo ""
printf "%-20s | %10s | %10s | %10s | %10s\n" "Benchmark" "vs GCC-O0" "vs GCC-O1" "vs GCC-O2" "vs GCC-O3"
printf "%-20s-+-%10s-+-%10s-+-%10s-+-%10s\n" "--------------------" "----------" "----------" "----------" "----------"

ratio() { awk "BEGIN{if($1>0) printf \"%.1fx\", $2/$1; else print \"N/A\"}"; }

r=$(ratio $axcc1 $gcc1_0); printf "%-20s | %10s" "Fibonacci(38)" "$r"
r=$(ratio $axcc1 $gcc1_1); printf " | %10s" "$r"
r=$(ratio $axcc1 $gcc1_2); printf " | %10s" "$r"
r=$(ratio $axcc1 $gcc1_3); printf " | %10s\n" "$r"

r=$(ratio $axcc2 $gcc2_0); printf "%-20s | %10s" "Primes(500k)" "$r"
r=$(ratio $axcc2 $gcc2_1); printf " | %10s" "$r"
r=$(ratio $axcc2 $gcc2_2); printf " | %10s" "$r"
r=$(ratio $axcc2 $gcc2_3); printf " | %10s\n" "$r"

r=$(ratio $axcc3 $gcc3_0); printf "%-20s | %10s" "NestedLoops(100M)" "$r"
r=$(ratio $axcc3 $gcc3_1); printf " | %10s" "$r"
r=$(ratio $axcc3 $gcc3_2); printf " | %10s" "$r"
r=$(ratio $axcc3 $gcc3_3); printf " | %10s\n" "$r"

r=$(ratio $axcc4 $gcc4_0); printf "%-20s | %10s" "GCD(2M)" "$r"
r=$(ratio $axcc4 $gcc4_1); printf " | %10s" "$r"
r=$(ratio $axcc4 $gcc4_2); printf " | %10s" "$r"
r=$(ratio $axcc4 $gcc4_3); printf " | %10s\n" "$r"
echo ""
