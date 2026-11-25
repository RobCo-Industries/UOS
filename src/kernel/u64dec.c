/*
 * Decimal text of an unsigned 64-bit value by repeated subtraction of
 * powers of ten from a table: at most 9 subtractions per digit and no
 * division, so the target needs no 64-bit division helper. See uos_u64.h.
 * Pure.
 */
#include "uos_u64.h"

/* 10^19 down to 10^0. 10^19 is the largest power of ten below 2^64. */
static const uint64_t pow10[] = {
    10000000000000000000ull, 1000000000000000000ull, 100000000000000000ull,
    10000000000000000ull, 1000000000000000ull, 100000000000000ull, 10000000000000ull,
    1000000000000ull, 100000000000ull, 10000000000ull, 1000000000ull, 100000000ull,
    10000000ull, 1000000ull, 100000ull, 10000ull, 1000ull, 100ull, 10ull, 1ull,
};

#define POW10_COUNT (sizeof(pow10) / sizeof(pow10[0]))

size_t uos_u64_dec(uint64_t v, char *buf, size_t size)
{
    char tmp[UOS_U64_DEC_SIZE];
    size_t n = 0, i;

    if (buf == NULL) {
        return 0;
    }
    for (i = 0; i < POW10_COUNT; i++) {
        unsigned d = 0;

        while (v >= pow10[i]) {
            v -= pow10[i];
            d++;
        }
        if (d != 0 || n != 0 || i + 1u == POW10_COUNT) {
            tmp[n++] = (char)('0' + d);
        }
    }
    if (n + 1u > size) {
        if (size != 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    for (i = 0; i < n; i++) {
        buf[i] = tmp[i];
    }
    buf[n] = '\0';
    return n;
}
