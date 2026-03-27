#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc

B1="benchmarks/Compile Mode/bench1_fib_rec.axis"
B2="benchmarks/Compile Mode/bench2_primes.axis"
B3="benchmarks/Compile Mode/bench3_loops.axis"

run_test() {
    local file="$1" opt="$2"
    ./axis "$file" -o /tmp/b_test --elf -$opt 2>/dev/null
    /tmp/b_test 2>/dev/null
    echo "  $opt: exit=$?"
}

run_test_output() {
    local file="$1" opt="$2"
    ./axis "$file" -o /tmp/b_test --elf -$opt 2>/dev/null
    local out=$(/tmp/b_test 2>/dev/null)
    local ec=$?
    echo "  $opt: exit=$ec lines=$(echo "$out" | wc -l)"
    if [ "$3" = "show" ]; then
        echo "$out"
    fi
}

echo "=== bench1 (expected 41) ==="
for OPT in O0 O1 O2 O3 Os; do run_test "$B1" $OPT; done

echo ""
echo "=== bench2 (expected 66) ==="
for OPT in O0 O1 O2 O3 Os; do run_test "$B2" $OPT; done

echo ""
echo "=== bench3 (expected 65) ==="
for OPT in O0 O1 O2 O3 Os; do run_test "$B3" $OPT; done

echo ""
echo "=== example 20_compile_mode ==="
EX20="../code/examples/20_compile_mode.axis"
if [ -f "$EX20" ]; then
    echo "--- O0 (reference output) ---"
    run_test_output "$EX20" O0 show
    echo "--- all levels ---"
    for OPT in O0 O1 O2 O3 Os; do run_test_output "$EX20" $OPT; done
else
    echo "  Not found: $EX20"
fi

echo ""
echo "=== DONE ==="
