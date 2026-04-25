#!/bin/bash
# ============================================================
# AXIS Compiler — CI Test Runner
# Runs all code/tests/t*.axis at O0..Os and reports.
# ============================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AXIS="$ROOT/axcc/axis"
TEST_DIR="$ROOT/code/tests"

if [ ! -x "$AXIS" ]; then
    echo "ERROR: compiler not found at $AXIS"
    exit 2
fi
if [ ! -d "$TEST_DIR" ]; then
    echo "ERROR: tests not found at $TEST_DIR"
    exit 2
fi

VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

PASS=0
FAIL=0
ERROR=0
TOTAL=0
FAILS=""

OPT_LEVELS="O0 O1 O2 O3 Os"

for SRC in "$TEST_DIR"/t*.axis; do
    [ -f "$SRC" ] || continue
    NAME=$(basename "$SRC" .axis)

    EXPECT=$(head -1 "$SRC" | sed -n 's|^// EXPECT: \([0-9]*\).*|\1|p')
    if [ -z "$EXPECT" ]; then
        [ $VERBOSE -eq 1 ] && echo "  SKIP $NAME: no EXPECT line"
        continue
    fi

    for OPT in $OPT_LEVELS; do
        TOTAL=$((TOTAL + 1))
        OUT="/tmp/axis_test_${NAME}_${OPT}"

        if ! "$AXIS" "$SRC" -o "$OUT" --elf -$OPT 2>/tmp/axis_cerr ; then
            echo "  ERR  $NAME -$OPT: compile failed"
            head -5 /tmp/axis_cerr 2>/dev/null
            ERROR=$((ERROR + 1))
            FAILS="$FAILS\n  ERR  $NAME -$OPT"
            continue
        fi

        chmod +x "$OUT" 2>/dev/null || true
        timeout 10 "$OUT" >/dev/null 2>&1
        EC=$?

        if [ $EC -eq 124 ]; then
            echo "  FAIL $NAME -$OPT: TIMEOUT"
            FAIL=$((FAIL + 1))
            FAILS="$FAILS\n  FAIL $NAME -$OPT: TIMEOUT"
            continue
        fi

        if [ "$EC" -eq "$EXPECT" ]; then
            PASS=$((PASS + 1))
            [ $VERBOSE -eq 1 ] && echo "  OK   $NAME -$OPT (exit=$EC)"
        else
            echo "  FAIL $NAME -$OPT: got $EC, expected $EXPECT"
            FAIL=$((FAIL + 1))
            FAILS="$FAILS\n  FAIL $NAME -$OPT: got=$EC exp=$EXPECT"
        fi
    done
done

echo ""
echo "============================================================"
echo " AXIS CI Test Suite Results"
echo "============================================================"
echo " PASS:  $PASS"
echo " FAIL:  $FAIL"
echo " ERROR: $ERROR"
echo " TOTAL: $TOTAL"
echo "============================================================"

if [ $FAIL -gt 0 ] || [ $ERROR -gt 0 ]; then
    echo ""
    echo " Failures:"
    echo -e "$FAILS"
    exit 1
fi
echo " ALL TESTS PASSED"
exit 0
