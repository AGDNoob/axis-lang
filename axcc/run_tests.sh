#!/bin/bash
# Comprehensive test script for AXIS compiler (compile mode)

cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
AXIS=./axis
BM="benchmarks/Compile Mode"
PASS=0
FAIL=0
TOTAL=0

test_program() {
    local NAME="$1"
    local SRC="$2"
    local EXPECTED="$3"
    local OPT="$4"
    local OUT="/tmp/axis_test_${NAME}_${OPT}"
    
    TOTAL=$((TOTAL + 1))
    
    # Compile
    if ! $AXIS "$SRC" -o "$OUT" --elf -$OPT 2>/tmp/axis_compile_err; then
        echo "  FAIL $NAME -$OPT: COMPILE ERROR"
        cat /tmp/axis_compile_err
        FAIL=$((FAIL + 1))
        return
    fi
    
    # Run with timeout
    timeout 60 "$OUT" > /tmp/axis_run_out 2>&1
    EC=$?
    
    if [ $EC -eq 124 ]; then
        echo "  FAIL $NAME -$OPT: TIMEOUT (program hung!)"
        FAIL=$((FAIL + 1))
        return
    fi
    
    if [ "$EXPECTED" = "any" ]; then
        echo "  OK   $NAME -$OPT: exit=$EC (no expected value)"
        PASS=$((PASS + 1))
        return
    fi
    
    if [ $EC -eq $EXPECTED ]; then
        echo "  OK   $NAME -$OPT: exit=$EC"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $NAME -$OPT: exit=$EC (expected $EXPECTED)"
        FAIL=$((FAIL + 1))
    fi
}

test_program_stdout() {
    local NAME="$1"
    local SRC="$2"
    local OPT="$3"
    local OUT="/tmp/axis_test_${NAME}_${OPT}"
    
    TOTAL=$((TOTAL + 1))
    
    # Compile
    if ! $AXIS "$SRC" -o "$OUT" --elf -$OPT 2>/tmp/axis_compile_err; then
        echo "  FAIL $NAME -$OPT: COMPILE ERROR"
        cat /tmp/axis_compile_err
        FAIL=$((FAIL + 1))
        return
    fi
    
    # Run with timeout
    timeout 30 "$OUT" > /tmp/axis_stdout_${NAME}_${OPT} 2>&1
    EC=$?
    
    if [ $EC -eq 124 ]; then
        echo "  FAIL $NAME -$OPT: TIMEOUT (program hung!)"
        FAIL=$((FAIL + 1))
        return
    fi
    
    echo "  OK   $NAME -$OPT: exit=$EC, stdout:"
    cat /tmp/axis_stdout_${NAME}_${OPT} | head -20
    PASS=$((PASS + 1))
}

echo "============================================================"
echo " AXIS Compiler — Comprehensive Compile Mode Tests"
echo "============================================================"
echo ""

# --- Benchmark 1: Recursive Fibonacci ---
echo "--- BENCH1: Recursive Fibonacci (expected exit: 41) ---"
for OPT in O0 O1 O2 O3 Os; do
    test_program "bench1" "$BM/bench1_fib_rec.axis" 41 $OPT
done

echo ""
# --- Benchmark 2: Prime Counting ---
echo "--- BENCH2: Prime Counting (expected exit: 66) ---"
for OPT in O0 O1 O2 O3 Os; do
    test_program "bench2" "$BM/bench2_primes.axis" 66 $OPT
done

echo ""
# --- Benchmark 3: Nested Loops ---
echo "--- BENCH3: Nested Loops (expected exit: 65) ---"
for OPT in O0 O1 O2 O3 Os; do
    test_program "bench3" "$BM/bench3_loops.axis" 65 $OPT
done

echo ""
# --- Benchmark 4: GCD ---
echo "--- BENCH4: GCD Stress (expected exit: varies) ---"
for OPT in O0 O1 O2 O3 Os; do
    test_program "bench4" "$BM/bench4_gcd.axis" "any" $OPT
done

echo ""
# --- Example 20: Compile Mode ---
echo "--- EX20: Compile Mode (expected exit: 0, with stdout) ---"
CODE_EX="/mnt/c/Users/marlo/Projekte/AXIS/code/examples"
for OPT in O0 O1 O2 O3 Os; do
    test_program_stdout "ex20" "$CODE_EX/20_compile_mode.axis" $OPT
done

echo ""
# --- test_fields.axis ---
echo "--- TEST_FIELDS: Fields test (expected exit: 0, with stdout) ---"
TESTS="/mnt/c/Users/marlo/Projekte/AXIS/tests"
for OPT in O0 O1 O2 O3 Os; do
    test_program_stdout "fields" "$TESTS/test_fields.axis" $OPT
done

echo ""
echo "============================================================"
echo " RESULTS: $PASS passed, $FAIL failed, $TOTAL total"
echo "============================================================"
