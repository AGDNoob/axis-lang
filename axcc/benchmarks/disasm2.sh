#!/bin/bash
BMDIR="/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"
AXCC="/mnt/c/Users/marlo/Projekte/AXIS/axcc"

cp "$BMDIR/bench3_axcc_O3" /tmp/b3_old
cp "$BMDIR/bench3_loops.axis" /tmp/bench3.axis
cd "$AXCC"
./axis build /tmp/bench3.axis -o /tmp/b3_new -O3 --elf 2>/dev/null
chmod +x /tmp/b3_new

echo "=== OLD (fast, precompiled) ==="
objdump -D -b binary -m i386:x86-64 --start-address=0xaf /tmp/b3_old | head -120

echo ""
echo "=== NEW (slow, freshly compiled) ==="
objdump -D -b binary -m i386:x86-64 --start-address=0xaf /tmp/b3_new | head -120
