import sys

def is_prime(n):
    if n < 2:
        return 0
    i = 2
    while i * i <= n:
        if n % i == 0:
            return 0
        i = i + 1
    return 1

count = 0
num = 2
while num < 500000:
    count = count + is_prime(num)
    num = num + 1

sys.exit(count % 256)
