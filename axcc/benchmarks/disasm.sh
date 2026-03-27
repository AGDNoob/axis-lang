#!/bin/bash
BMDIR="/mnt/c/Users/marlo/Projekte/AXIS/axcc/benchmarks/Compile Mode"
AXCC="/mnt/c/Users/marlo/Projekte/AXIS/axcc"

cp "$BMDIR/bench3_axcc_O3" /tmp/b3_old
cp "$BMDIR/bench3_loops.axis" /tmp/bench3.axis

cd "$AXCC"
./axis build /tmp/bench3.axis -o /tmp/b3_new -O3 --elf 2>/dev/null
chmod +x /tmp/b3_new

echo "=== OLD (precompiled, fast ~177ms) ==="
objdump -d /tmp/b3_old
echo ""
echo "=== NEW (freshly compiled, slow ~382ms) ==="
objdump -d /tmp/b3_new
