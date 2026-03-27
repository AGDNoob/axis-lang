#!/bin/bash
AXCC="/mnt/c/Users/marlo/Projekte/AXIS/axcc"
BMDIR="$AXCC/benchmarks/Compile Mode"
cd "$AXCC"

# Compile fresh
cp "$BMDIR/bench3_loops.axis" /tmp/bench3.axis
./axis build /tmp/bench3.axis -o /tmp/b3_O1 -O1 --elf 2>/dev/null
./axis build /tmp/bench3.axis -o /tmp/b3_O3 -O3 --elf 2>/dev/null
chmod +x /tmp/b3_O1 /tmp/b3_O3

cp "$BMDIR/bench1_fib_rec.axis" /tmp/bench1.axis
./axis build /tmp/bench1.axis -o /tmp/b1_O1 -O1 --elf 2>/dev/null
./axis build /tmp/bench1.axis -o /tmp/b1_O3 -O3 --elf 2>/dev/null
chmod +x /tmp/b1_O1 /tmp/b1_O3

cd "$BMDIR"

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

echo "=== NestedLoops: precompiled vs freshly compiled ==="
t=$(run3 ./bench3_axcc_O3); printf "  Precompiled O3: %6d ms\n" "$t"
t=$(run3 /tmp/b3_O3);       printf "  Fresh O3:       %6d ms\n" "$t"
t=$(run3 /tmp/b3_O1);       printf "  Fresh O1:       %6d ms\n" "$t"
t=$(run3 ./bench3_loops_gcc_O1); printf "  GCC O1:         %6d ms\n" "$t"

echo ""
echo "=== Checking correctness ==="
echo -n "Precompiled exit: "; ./bench3_axcc_O3 > /dev/null; echo $?
echo -n "Fresh O3 exit: "; /tmp/b3_O3 > /dev/null; echo $?  
echo -n "Fresh O1 exit: "; /tmp/b3_O1 > /dev/null; echo $?
echo -n "GCC O1 exit: "; ./bench3_loops_gcc_O1 > /dev/null; echo $?

echo ""
echo "=== Code sizes ==="
wc -c ./bench3_axcc_O3 /tmp/b3_O3 /tmp/b3_O1

echo ""
echo "=== Fibonacci ==="
t=$(run3 /tmp/b1_O1); printf "  Fresh O1: %6d ms\n" "$t"
t=$(run3 /tmp/b1_O3); printf "  Fresh O3: %6d ms\n" "$t"
t=$(run3 ./bench1_axcc_O3); printf "  Precomp:  %6d ms\n" "$t"
t=$(run3 ./bench1_fib_rec_gcc_O1); printf "  GCC O1:   %6d ms\n" "$t"
