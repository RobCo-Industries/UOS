/* Host tests of src/kernel/u64dec.c. */
#include <stdio.h>
#include <string.h>

#include "rc_test.h"

#include "uos_u64.h"

static void same(uint64_t v, const char *want)
{
    char buf[UOS_U64_DEC_SIZE];

    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(uos_u64_dec(v, buf, sizeof(buf)), strlen(want));
    CHECK_STR(buf, want);
}

TEST(u64_edges)
{
    same(0, "0");
    same(1, "1");
    same(9, "9");
    same(10, "10");
    same(4294967295ull, "4294967295");
    same(4294967296ull, "4294967296");
    same(1000000000000ull, "1000000000000");
    same(10000000000000000000ull, "10000000000000000000");
    same(18446744073709551615ull, "18446744073709551615");
}

TEST(u64_matches_libc)
{
    char want[32];
    uint64_t v = 1;
    int i;

    for (i = 0; i < 64; i++, v = v * 3u + 7u) {
        snprintf(want, sizeof(want), "%llu", (unsigned long long)v);
        same(v, want);
    }
}

TEST(u64_small_buffers)
{
    char buf[UOS_U64_DEC_SIZE];

    CHECK_EQ(uos_u64_dec(18446744073709551615ull, buf, UOS_U64_DEC_SIZE - 1u), 0);
    CHECK_STR(buf, "");
    CHECK_EQ(uos_u64_dec(123, buf, 4), 3);
    CHECK_STR(buf, "123");
    CHECK_EQ(uos_u64_dec(123, buf, 3), 0);
    CHECK_STR(buf, "");
    buf[0] = 'x';
    CHECK_EQ(uos_u64_dec(1, buf, 0), 0);
    CHECK_EQ(buf[0], 'x');
    CHECK_EQ(uos_u64_dec(1, NULL, 8), 0);
}
