/* Benchmark 2: Prime Counting (trial division)
   Tests: loops, modulo, comparison, branches
   Counts primes up to 500000 */

int is_prime(int n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (int d = 3; d * d <= n; d += 2) {
        if (n % d == 0) return 0;
    }
    return 1;
}

int main(void) {
    int count = 0;
    for (int n = 2; n <= 500000; n++) {
        count += is_prime(n);
    }
    return count & 255;
}
