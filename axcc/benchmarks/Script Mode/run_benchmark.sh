#!/usr/bin/env bash
# AXIS Script Mode vs Python 3 — Benchmark Runner
# Usage: bash run_benchmark.sh [RUNS]
#   RUNS = number of iterations per benchmark (default: 3)

set -e

BASEDIR="$(cd "$(dirname "$0")" && pwd)"
AXIS="${BASEDIR}/../../axis"
RUNS="${1:-3}"

# Verify tools
if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 not found"; exit 1
fi
if [ ! -x "$AXIS" ]; then
    echo "ERROR: AXIS binary not found at $AXIS"; exit 1
fi

PYVER=$(python3 --version 2>&1)
echo "============================================"
echo " AXIS Script Mode  vs  $PYVER"
echo " Runs per benchmark: $RUNS"
echo "============================================"
echo ""

# ---------- helper: median of RUNS timings ----------
time_cmd() {
    # $1 = label, rest = command
    local label="$1"; shift
    local times=()
    for ((r=1; r<=RUNS; r++)); do
        local t
        t=$( { time "$@" >/dev/null 2>&1; } 2>&1 | grep real | sed 's/real\s*//' )
        # convert to seconds
        local min sec
        min=$(echo "$t" | sed 's/m.*//')
        sec=$(echo "$t" | sed 's/.*m//;s/s//')
        local total
        total=$(echo "$min * 60 + $sec" | bc)
        times+=("$total")
    done
    # Sort and pick median
    IFS=$'\n' sorted=($(sort -g <<<"${times[*]}")); unset IFS
    local mid=$(( RUNS / 2 ))
    echo "${sorted[$mid]}"
}

# ---------- Benchmark 1: Fibonacci(38) ----------
echo "--- Benchmark 1: Fibonacci(38) — recursive ---"
echo ""

axis_fib=$(time_cmd "AXIS" "$AXIS" "${BASEDIR}/script_bench1_fib.axis")
echo "  AXIS Script Mode:  ${axis_fib}s"

py_fib=$(time_cmd "Python" python3 "${BASEDIR}/bench1_fib.py")
echo "  Python 3:          ${py_fib}s"

if (( $(echo "$py_fib > 0" | bc -l) )); then
    ratio_fib=$(echo "scale=2; $py_fib / $axis_fib" | bc)
    echo "  Ratio (Py/AXIS):   ${ratio_fib}x"
else
    echo "  Ratio:             (python too fast to measure)"
fi
echo ""

# ---------- Benchmark 2: Primes < 500 000 ----------
echo "--- Benchmark 2: Count primes < 500 000 ---"
echo ""

axis_pr=$(time_cmd "AXIS" "$AXIS" "${BASEDIR}/script_bench2_primes.axis")
echo "  AXIS Script Mode:  ${axis_pr}s"

py_pr=$(time_cmd "Python" python3 "${BASEDIR}/bench2_primes.py")
echo "  Python 3:          ${py_pr}s"

if (( $(echo "$py_pr > 0" | bc -l) )); then
    ratio_pr=$(echo "scale=2; $py_pr / $axis_pr" | bc)
    echo "  Ratio (Py/AXIS):   ${ratio_pr}x"
else
    echo "  Ratio:             (python too fast to measure)"
fi

echo ""
echo "============================================"
echo " Summary"
echo "============================================"
printf "  %-30s %10s %10s %8s\n" "Benchmark" "AXIS (s)" "Python (s)" "Py/AXIS"
printf "  %-30s %10s %10s %8s\n" "------------------------------" "--------" "--------" "------"
printf "  %-30s %10s %10s %8s\n" "Fibonacci(38)" "$axis_fib" "$py_fib" "${ratio_fib:-n/a}x"
printf "  %-30s %10s %10s %8s\n" "Primes < 500k" "$axis_pr" "$py_pr" "${ratio_pr:-n/a}x"
echo ""
