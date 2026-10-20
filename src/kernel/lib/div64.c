#include "stdint.h"
uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem) {
    if (den == 0) {
        if (rem)
            *rem = 0;
        return 0;
    }
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((num >> bit) & 1ULL);
        if (remainder >= den) {
            remainder -= den;
            quotient |= (1ULL << bit);
        }
    }
    if (rem)
        *rem = remainder;
    return quotient;
}
uint64_t __udivdi3(uint64_t num, uint64_t den) {
    return __udivmoddi4(num, den, 0);
}
