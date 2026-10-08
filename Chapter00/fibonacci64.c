#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

extern uint64_t cal_fib(uint64_t n);

int main(void) {
    uint64_t n = 5;
    uint64_t result = cal_fib(n);

    printf("F(%llu) = %llu\n", (unsigned long long)n, (unsigned long long)result);
    return 0;
}
