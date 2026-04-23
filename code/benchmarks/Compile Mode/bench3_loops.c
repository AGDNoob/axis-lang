/* Benchmark 3: Nested Loop with Data-Dependent Computation
   Tests: loop efficiency, multiply, XOR, data dependency chain
   100 million iterations (10000 x 10000) */

int main(void) {
    int sum = 0;
    for (int i = 0; i < 10000; i++) {
        for (int j = 0; j < 10000; j++) {
            int t = sum * 7 + i * j;
            sum = t ^ (i + j);
        }
    }
    return (sum >> 24) & 255;
}
