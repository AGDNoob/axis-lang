#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
./axis "benchmarks/Compile Mode/bench3_loops.axis" -O1 -o /tmp/b3o1
python3 disasm_elf.py /tmp/b3o1 2>&1 | tail -n +57 | head -100
