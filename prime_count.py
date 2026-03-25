# prime_count.py
# Counts prime numbers in a range using trial division (benchmark comparison with Atomic)
# Run: python prime_count.py

import time

limit = 1000000
count = 0

start = time.time()

n = 2
while n < limit:
    is_prime = True
    d = 2
    while d * d <= n:
        if n % d == 0:
            is_prime = False
            break
        d += 1
    if is_prime:
        count += 1
    n += 1

elapsed = (time.time() - start) * 1000

print(f"Primes below {limit}: {count}")
print(f"Time: {elapsed:.0f}ms")
