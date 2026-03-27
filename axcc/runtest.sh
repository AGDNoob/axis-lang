#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
echo "--- bench2 -O1 ---"
./axis -O1 /tmp/bench2.axis -o /tmp/bench2
/tmp/bench2
echo "bench2_O1=$?"
echo "--- fields -O1 ---"
./axis -O1 /mnt/c/Users/marlo/Projekte/AXIS/tests/test_fields.axis -o /tmp/fields
/tmp/fields
echo "fields_O1=$?"
