/* Benchmark 4: GCD Stress (Euclidean Algorithm)
   Tests: function calls, while loops, modulo
   2 million GCD computations */

int gcd(int x, int y) {
    int a = x, b = y, t;
    while (b != 0) {
        t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main(void) {
    int sum = 0;
    for (int i = 1; i <= 2000000; i++) {
        sum += gcd(i, i + 7);
    }
    return sum & 255;
}
