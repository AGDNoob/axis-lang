#!/bin/bash
AXIS=/mnt/c/Users/marlo/Projekte/AXIS/axcc/axis
SRC=/mnt/c/Users/marlo/Projekte/AXIS/code/examples/20_compile_mode.axis

for opt in O0 O1 O2 O3; do
    echo "=== $opt ==="
    $AXIS $SRC -o /tmp/ex20test --elf -$opt 2>&1
    /tmp/ex20test 2>&1 | head -8
    echo "---"
done
