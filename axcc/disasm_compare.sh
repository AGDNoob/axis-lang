#!/bin/bash
# Disassemble AXIS and GCC binaries for comparison
# Usage: bash disasm_compare.sh

cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
BM="benchmarks/Compile Mode"

echo "==========================================="
echo "  AXIS -O1 Fibonacci (fib)"
echo "==========================================="
./axis -O1 "$BM/bench1_fib_rec.axis" -o /tmp/b1_axis --elf --dump-ir 2>&1 | grep -A200 "^=== IR " | head -100
echo ""

echo "==========================================="
echo "  GCC -O1 Fibonacci (fib) assembly"
echo "==========================================="
gcc -O1 -S -masm=intel -o /tmp/b1_gcc.s "$BM/bench1_fib_rec.c"
cat /tmp/b1_gcc.s
echo ""

echo "==========================================="
echo "  AXIS -O1 Primes (is_prime)"
echo "==========================================="
./axis -O1 "$BM/bench2_primes.axis" -o /tmp/b2_axis --elf --dump-ir 2>&1 | grep -A300 "^=== IR " | head -200
echo ""

echo "==========================================="
echo "  GCC -O1 Primes assembly"
echo "==========================================="
gcc -O1 -S -masm=intel -o /tmp/b2_gcc.s "$BM/bench2_primes.c"
cat /tmp/b2_gcc.s
