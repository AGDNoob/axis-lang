#!/usr/bin/env bash
# Bisect which new pass causes the NestedLoops regression
set -e
cd "$(dirname "$0")/.."

BENCH="benchmarks/Compile Mode/bench3_loops.axis"
PRECOMP="benchmarks/Compile Mode/bench3_axcc_O3"
OUT="/tmp/bench3_test"
RUNS=3

echo "=== Baseline: precompiled O3 binary ==="
best=999999
for i in $(seq 1 $RUNS); do
    t=$( { time "$PRECOMP"; } 2>&1 | grep real | sed 's/real\t//' )
    ms=$(echo "$t" | awk -F'[ms]' '{print int($1*60000 + $2*1000)}')
    [[ $ms -lt $best ]] && best=$ms
done
echo "  Precompiled: ${best}ms"
echo ""

# Test pass combinations by commenting/uncommenting in ssa_opt.c
SSA_OPT="src/ssa_opt.c"

# Save original
cp "$SSA_OPT" "${SSA_OPT}.backup"

run_bench() {
    local label="$1"
    make -j4 -s 2>/dev/null
    ./axis build "$BENCH" -o "$OUT" -O3 --elf 2>/dev/null
    best=999999
    for i in $(seq 1 $RUNS); do
        t=$( { time "$OUT"; } 2>&1 | grep real | sed 's/real\t//' )
        ms=$(echo "$t" | awk -F'[ms]' '{print int($1*60000 + $2*1000)}')
        [[ $ms -lt $best ]] && best=$ms
    done
    echo "  $label: ${best}ms"
}

echo "=== With ALL new passes (current state) ==="
run_bench "All new passes"

echo ""
echo "=== Disable opt_reassociate ==="
sed -i 's|^    opt_reassociate(ir);|    //opt_reassociate(ir);|' "$SSA_OPT"
run_bench "No reassociate"
cp "${SSA_OPT}.backup" "$SSA_OPT"

echo ""
echo "=== Disable opt_jump_thread ==="
sed -i 's|^    opt_jump_thread(ir);|    //opt_jump_thread(ir);|' "$SSA_OPT"
run_bench "No jump_thread"
cp "${SSA_OPT}.backup" "$SSA_OPT"

echo ""
echo "=== Disable opt_simplify_cfg ==="
sed -i 's|^    opt_simplify_cfg(ir);|    //opt_simplify_cfg(ir);|' "$SSA_OPT"
run_bench "No simplify_cfg"
cp "${SSA_OPT}.backup" "$SSA_OPT"

echo ""
echo "=== Disable opt_if_convert ==="
sed -i 's|^    opt_if_convert(ir);|    //opt_if_convert(ir);|' "$SSA_OPT"
run_bench "No if_convert"
cp "${SSA_OPT}.backup" "$SSA_OPT"

echo ""
echo "=== Disable ALL 4 post-SSA new passes ==="
sed -i 's|^    opt_reassociate(ir);|    //opt_reassociate(ir);|' "$SSA_OPT"
sed -i 's|^    opt_jump_thread(ir);|    //opt_jump_thread(ir);|' "$SSA_OPT"
sed -i 's|^    opt_simplify_cfg(ir);|    //opt_simplify_cfg(ir);|' "$SSA_OPT"
sed -i 's|^    opt_if_convert(ir);|    //opt_if_convert(ir);|' "$SSA_OPT"
run_bench "No post-SSA new passes"
cp "${SSA_OPT}.backup" "$SSA_OPT"

echo ""
echo "=== Disable opt_tail_call (pre-SSA) ==="
sed -i 's|^        opt_tail_call(ir);|        //opt_tail_call(ir);|' "$SSA_OPT"
run_bench "No tail_call"
cp "${SSA_OPT}.backup" "$SSA_OPT"

# Cleanup
rm -f "${SSA_OPT}.backup" "$OUT"
echo ""
echo "=== Done ==="
