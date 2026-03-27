#!/bin/bash
# AXIS -O1 vs GCC -O1 benchmark suite
# Uses: interleaved runs, warmup, median-of-N, taskset pinning
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
BM="benchmarks/Compile Mode"

RUNS=11          # odd number for clean median
WARMUP=3         # warmup runs (discarded)
PIN=""           # CPU pinning prefix (set below if taskset available)

# Use taskset to pin to a single core if available (reduces scheduling jitter)
if command -v taskset &>/dev/null; then
    PIN="taskset -c 1"
fi

echo "=== Compiling AXIS -O1 ==="
./axis "$BM/bench1_fib_rec.axis" -o /tmp/b1_axis --elf -O1 2>/dev/null && echo "b1_axis OK"
./axis "$BM/bench2_primes.axis" -o /tmp/b2_axis --elf -O1 2>/dev/null && echo "b2_axis OK"
./axis "$BM/bench3_loops.axis" -o /tmp/b3_axis --elf -O1 2>/dev/null && echo "b3_axis OK"
./axis "$BM/bench4_gcd.axis"   -o /tmp/b4_axis --elf -O1 2>/dev/null && echo "b4_axis OK"

echo ""
echo "=== Compiling GCC -O1 ==="
gcc -O1 -o /tmp/b1_gcc "$BM/bench1_fib_rec.c" && echo "b1_gcc OK"
gcc -O1 -o /tmp/b2_gcc "$BM/bench2_primes.c"  && echo "b2_gcc OK"
gcc -O1 -o /tmp/b3_gcc "$BM/bench3_loops.c"   && echo "b3_gcc OK"
gcc -O1 -o /tmp/b4_gcc "$BM/bench4_gcd.c"     && echo "b4_gcc OK"

echo ""
echo "=== GCC version ==="
gcc --version | head -1

echo ""
echo "=== Verifying correctness (exit codes should match) ==="
for b in 1 2 3 4; do
    /tmp/b${b}_axis >/dev/null 2>&1; a=$?
    /tmp/b${b}_gcc  >/dev/null 2>&1; g=$?
    if [ "$a" = "$g" ]; then
        echo "bench${b}: AXIS=$a GCC=$g  OK"
    else
        echo "bench${b}: AXIS=$a GCC=$g  MISMATCH!"
    fi
done

# Precise timing via date +%s%N (nanosecond-resolution)
time_one() {
    local BIN=$1
    local T0 T1
    T0=$(date +%s%N)
    $PIN "$BIN" >/dev/null 2>&1
    T1=$(date +%s%N)
    echo "scale=4; ($T1 - $T0) / 1000000000" | bc
}

# Interleaved benchmark: runs AXIS and GCC alternately to share thermal/scheduling conditions
interleaved_bench() {
    local AXIS_BIN=$1
    local GCC_BIN=$2
    local N=$RUNS
    local WARM=$WARMUP
    local AXIS_TIMES=()
    local GCC_TIMES=()

    # Warmup both binaries (primes caches, triggers CPU turbo boost)
    for i in $(seq 1 $WARM); do
        $PIN "$AXIS_BIN" >/dev/null 2>&1
        $PIN "$GCC_BIN"  >/dev/null 2>&1
    done

    # Interleaved timed runs: AXIS, GCC, AXIS, GCC, ...
    for i in $(seq 1 $N); do
        AT=$(time_one "$AXIS_BIN")
        GT=$(time_one "$GCC_BIN")
        AXIS_TIMES+=("$AT")
        GCC_TIMES+=("$GT")
    done

    # Median of each
    AXIS_MED=$(printf '%s\n' "${AXIS_TIMES[@]}" | sort -n | awk -v n="$N" 'NR==int(n/2)+1{printf "%.4f",$1}')
    GCC_MED=$(printf '%s\n'  "${GCC_TIMES[@]}" | sort -n | awk -v n="$N" 'NR==int(n/2)+1{printf "%.4f",$1}')

    # Min of each (best-case, least noise)
    AXIS_MIN=$(printf '%s\n' "${AXIS_TIMES[@]}" | sort -n | head -1 | awk '{printf "%.4f",$1}')
    GCC_MIN=$(printf '%s\n'  "${GCC_TIMES[@]}" | sort -n | head -1 | awk '{printf "%.4f",$1}')

    echo "$AXIS_MED $GCC_MED $AXIS_MIN $GCC_MIN"
}

echo ""
echo "================================================================="
echo "  AXIS -O1 vs GCC -O1  (median of $RUNS interleaved runs, $WARMUP warmup)"
[ -n "$PIN" ] && echo "  CPU pinning: enabled (core 1)"
echo "================================================================="

for bench in "1:Fibonacci recursive" "2:Prime counting" "3:Nested loops" "4:GCD stress"; do
    NUM=${bench%%:*}
    DESC=${bench#*:}
    echo ""
    echo "--- bench${NUM} ($DESC) ---"

    RESULT=$(interleaved_bench /tmp/b${NUM}_axis /tmp/b${NUM}_gcc)
    AXIS_MED=$(echo "$RESULT" | awk '{print $1}')
    GCC_MED=$(echo "$RESULT"  | awk '{print $2}')
    AXIS_MIN=$(echo "$RESULT" | awk '{print $3}')
    GCC_MIN=$(echo "$RESULT"  | awk '{print $4}')

    RATIO_MED=$(echo "scale=2; $AXIS_MED / $GCC_MED" | bc)
    RATIO_MIN=$(echo "scale=2; $AXIS_MIN / $GCC_MIN" | bc)

    AXIS_MS=$(echo "scale=1; $AXIS_MED * 1000" | bc)
    GCC_MS=$(echo "scale=1; $GCC_MED * 1000" | bc)
    AXIS_MIN_MS=$(echo "scale=1; $AXIS_MIN * 1000" | bc)
    GCC_MIN_MS=$(echo "scale=1; $GCC_MIN * 1000" | bc)

    printf "  %-10s  median: %7.1f ms   best: %7.1f ms\n" "AXIS -O1" "$AXIS_MS" "$AXIS_MIN_MS"
    printf "  %-10s  median: %7.1f ms   best: %7.1f ms\n" "GCC  -O1" "$GCC_MS" "$GCC_MIN_MS"
    printf "  Ratio:     median=%sx  best=%sx  (AXIS/GCC)\n" "$RATIO_MED" "$RATIO_MIN"
done

echo ""
echo "================================================================="
echo "  < 1.0x = AXIS faster, > 1.0x = GCC faster"
echo "================================================================="
