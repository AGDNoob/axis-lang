#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS
PASS=0
FAIL=0

echo "=== examples/ ==="
for f in examples/*.axis; do
    mode=$(head -5 "$f" | grep -o "mode [a-z]*" | head -1)
    name=$(basename "$f")
    if echo "$mode" | grep -q "compile"; then
        out=$(./axcc/axis "$f" -o /tmp/test_ex --elf 2>&1)
        rc=$?
        if [ $rc -eq 0 ]; then
            echo "PASS (compile): $name"
            PASS=$((PASS+1))
        else
            echo "FAIL (compile): $name (exit=$rc)"
            echo "  $out" | head -2
            FAIL=$((FAIL+1))
        fi
    else
        # Skip interactive (19_guessing_game), run rest as script
        if echo "$name" | grep -qE "guessing|16_guessing"; then
            echo "SKIP (interactive): $name"
            continue
        fi
        out=$(./axcc/axis "$f" 2>&1)
        rc=$?
        if [ $rc -eq 0 ]; then
            echo "PASS: $name"
            PASS=$((PASS+1))
        else
            echo "FAIL: $name (exit=$rc)"
            echo "  $out" | head -2
            FAIL=$((FAIL+1))
        fi
    fi
done

echo ""
echo "=== tests/ ==="
for f in tests/*.axis; do
    name=$(basename "$f")
    mode=$(head -5 "$f" | grep -o "mode [a-z]*" | head -1)
    if echo "$mode" | grep -q "compile"; then
        out=$(./axcc/axis "$f" -o /tmp/test_t --elf 2>&1)
        rc=$?
        if [ $rc -eq 0 ]; then
            echo "PASS (compile): $name"
            PASS=$((PASS+1))
        else
            echo "FAIL (compile): $name (exit=$rc)"
            echo "  $out" | head -2
            FAIL=$((FAIL+1))
        fi
    else
        out=$(./axcc/axis "$f" 2>&1)
        rc=$?
        if [ $rc -eq 0 ]; then
            echo "PASS: $name"
            PASS=$((PASS+1))
        else
            echo "FAIL: $name (exit=$rc)"
            echo "  $out" | head -2
            FAIL=$((FAIL+1))
        fi
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
