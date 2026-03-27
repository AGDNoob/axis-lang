#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS
PASS=0
FAIL=0
SKIP=0

for f in code/examples/01_hello_world.axis \
         code/examples/02_variables.axis \
         code/examples/03_arithmetic.axis \
         code/examples/04_conditionals.axis \
         code/examples/05_loops.axis \
         code/examples/06_loop_control.axis \
         code/examples/07_boolean_logic.axis \
         code/examples/08_bitwise.axis \
         code/examples/09_functions.axis \
         code/examples/10_update_modifier.axis \
         code/examples/11_arrays.axis \
         code/examples/12_fields.axis \
         code/examples/13_enums_match.axis \
         code/examples/14_copy.axis \
         code/examples/15_fizzbuzz.axis \
         code/examples/16_fibonacci.axis \
         code/examples/17_primes.axis \
         code/examples/18_gcd_lcm.axis \
         code/examples/21_constants.axis; do
    out=$(./axcc/axis "$f" 2>&1)
    rc=$?
    name=$(basename "$f")
    if [ $rc -eq 0 ]; then
        echo "PASS: $name"
        PASS=$((PASS+1))
    else
        echo "FAIL: $name (exit=$rc)"
        echo "  $out" | head -3
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
