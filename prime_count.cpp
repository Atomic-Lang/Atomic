// prime_count.cpp
// Counts prime numbers in a range using trial division (benchmark comparison with Atomic)
// Compile: g++ -O0 -o prime_count.exe prime_count.cpp

#include <cstdio>
#include <chrono>

int main() {
    int limit = 1000000;
    int count = 0;

    auto start = std::chrono::high_resolution_clock::now();

    int n = 2;
    while (n < limit) {
        int is_prime = 1;
        int d = 2;
        while (d * d <= n) {
            if (n % d == 0) {
                is_prime = 0;
                break;
            }
            d += 1;
        }
        if (is_prime) {
            count += 1;
        }
        n += 1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    printf("Primes below %d: %d\n", limit, count);
    printf("Time: %lldms\n", (long long)ms);

    return 0;
}
