#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
SRC="benchmarks/Compile Mode/bench4_gcd"

gcc -O0 -o /tmp/b4_gcc_O0 "$SRC.c"
/tmp/b4_gcc_O0; echo "GCC_O0=$?"

gcc -O1 -o /tmp/b4_gcc_O1 "$SRC.c"
/tmp/b4_gcc_O1; echo "GCC_O1=$?"

./axis -O0 "$SRC.axis" -o /tmp/b4_axis_O0
/tmp/b4_axis_O0; echo "AXIS_O0=$?"

./axis -O1 "$SRC.axis" -o /tmp/b4_axis_O1
/tmp/b4_axis_O1; echo "AXIS_O1=$?"
