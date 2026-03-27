#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS

for opt in O0 O1 O2 O3; do
    echo "=== -$opt ==="
    ./axcc/axis tests/test_fields.axis -o /tmp/test_fields --elf -$opt 2>/dev/null
    /tmp/test_fields 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "OK"
    else
        echo "FAIL (exit $?)"
    fi
done
