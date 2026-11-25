#include <string.h>

#include "uos_text.h"

#include "rc_test.h"

TEST(dec_u32_rules)
{
    uint32_t v = 7;

    CHECK_EQ(uos_dec_u32("0", 10, &v), 0);
    CHECK_EQ(v, 0u);
    CHECK_EQ(uos_dec_u32("10", 10, &v), 0);
    CHECK_EQ(v, 10u);
    CHECK_EQ(uos_dec_u32("11", 10, &v), -1);
    CHECK_EQ(v, 10u);
    CHECK_EQ(uos_dec_u32("4294967295", 0xffffffffu, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    CHECK_EQ(uos_dec_u32("4294967296", 0xffffffffu, &v), -1);
    CHECK_EQ(uos_dec_u32("", 10, &v), -1);
    CHECK_EQ(uos_dec_u32("1a", 10, &v), -1);
    CHECK_EQ(uos_dec_u32("-1", 10, &v), -1);
    CHECK_EQ(uos_dec_u32(NULL, 10, &v), -1);
    CHECK_EQ(v, 0xffffffffu);
}

/* One case: text, limit, expected return, expected value when it works. */
typedef struct {
    const char *s;
    uint32_t max;
    int rc;
    uint32_t v;
} dec_case_t;

TEST(dec_u32_limits)
{
    static const dec_case_t cases[] = {
        /* max 0: only zero, in any number of digits. */
        { "0", 0, 0, 0 }, { "00", 0, 0, 0 }, { "1", 0, -1, 0 }, { "9", 0, -1, 0 },
        /* max 1. */
        { "1", 1, 0, 1 }, { "01", 1, 0, 1 }, { "2", 1, -1, 0 }, { "9", 1, -1, 0 },
        { "10", 1, -1, 0 },
        /* max 5: a digit above the limit used to wrap max - digit. */
        { "5", 5, 0, 5 }, { "6", 5, -1, 0 }, { "9", 5, -1, 0 }, { "10", 5, -1, 0 },
        { "99", 5, -1, 0 }, { "0005", 5, 0, 5 }, { "0009", 5, -1, 0 },
        /* max 9. */
        { "9", 9, 0, 9 }, { "10", 9, -1, 0 }, { "19", 9, -1, 0 }, { "09", 9, 0, 9 },
        /* max 10. */
        { "10", 10, 0, 10 }, { "11", 10, -1, 0 }, { "19", 10, -1, 0 }, { "100", 10, -1, 0 },
        { "010", 10, 0, 10 },
        /* UINT32_MAX. */
        { "4294967295", 0xffffffffu, 0, 0xffffffffu },
        { "4294967296", 0xffffffffu, -1, 0 },
        { "4294967299", 0xffffffffu, -1, 0 },
        { "9999999999", 0xffffffffu, -1, 0 },
        { "04294967295", 0xffffffffu, 0, 0xffffffffu },
        /* 20 digits. */
        { "00000000000000000007", 10, 0, 7 },
        { "00000000004294967295", 0xffffffffu, 0, 0xffffffffu },
        { "18446744073709551615", 0xffffffffu, -1, 0 },
        { "99999999999999999999", 5, -1, 0 },
        /* Empty. */
        { "", 0, -1, 0 }, { "", 0xffffffffu, -1, 0 },
    };
    size_t i;
    uint32_t v;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        v = 0xdeadbeefu;
        CHECK_EQ(uos_dec_u32(cases[i].s, cases[i].max, &v), cases[i].rc);
        CHECK_EQ(v, cases[i].rc == 0 ? cases[i].v : 0xdeadbeefu);
    }
}

TEST(quote_word_rules)
{
    char out[16];

    uos_quote_word("A B%\"c", out, sizeof(out));
    CHECK_STR(out, "A%20B%25%22c");
    uos_quote_word("\x01~", out, sizeof(out));
    CHECK_STR(out, "%01~");
    uos_quote_word(NULL, out, sizeof(out));
    CHECK_STR(out, "");
    /* Cut before a character that does not fit, never in the middle of %XX. */
    uos_quote_word("ABCD EF", out, 7);
    CHECK_STR(out, "ABCD");
    uos_quote_word("ABCDEFGHIJ", out, 5);
    CHECK_STR(out, "ABCD");
}
