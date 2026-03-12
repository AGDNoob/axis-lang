/* Benchmark 1: Recursive Fibonacci
   Tests: function call overhead, recursion, stack management
   ~63 million recursive calls for fib(38) */

int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    int r = fib(38);
    return r & 255;
}
