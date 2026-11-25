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
