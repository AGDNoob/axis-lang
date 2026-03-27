#!/bin/bash
BMDIR="/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"
cd "$BMDIR"

run3() {
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

echo "=== bench3 -O2 vs -O3 ==="
t=$(run3 ./bench3_axcc_O2); printf "  AXCC -O2: %6d ms\n" "$t"
t=$(run3 ./bench3_axcc_O3); printf "  AXCC -O3: %6d ms\n" "$t"
t=$(run3 ./bench3_loops_gcc_O1); printf "  GCC  -O1: %6d ms\n" "$t"
