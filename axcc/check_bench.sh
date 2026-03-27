#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
echo "Compiling bench2 with sink..."
./axis -O1 /tmp/bench2.axis -o /tmp/bench2_sink 2>/dev/null
echo "Running bench2..."
/tmp/bench2_sink
RET=$?
echo "Exit code: $RET"
