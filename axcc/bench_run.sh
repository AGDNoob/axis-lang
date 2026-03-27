#!/bin/bash
cd /mnt/c/Users/marlo/Projekte/AXIS/axcc
cat > /tmp/bench3.c << 'EOF'
#include <stdio.h>
int main(void) {
    int sum=0;
    for(int i=0;i<10000;i++)
        for(int j=0;j<10000;j++){
            int t=sum*7+i*j;
            sum=t^(i+j);
        }
    return (sum>>24)&255;
}
EOF
gcc -O1 -o /tmp/b3gcc /tmp/bench3.c
objdump -d /tmp/b3gcc | grep -A 40 '<main>'
