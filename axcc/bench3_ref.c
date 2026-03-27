#include <stdio.h>
int main(void) {
    int sum = 0, i = 0, j = 0, t = 0;
    while (i < 10000) {
        j = 0;
        while (j < 10000) {
            t = sum * 7 + i * j;
            sum = t ^ (i + j);
            j = j + 1;
        }
        i = i + 1;
    }
    printf("%d\n", (sum >> 24) & 255);
    return 0;
}
