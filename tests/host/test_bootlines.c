/* Host tests of src/kernel/bootlines.c: the first screen's text. */

#include "rc_test.h"

#include "bootlines.h"

static uos_first_t base(void)
{
    uos_first_t f;

    memset(&f, 0, sizeof(f));
    f.name = "TEST TERMINAL";
    return f;
}

TEST(banner_lines)
{
    unsigned i;

    for (i = 0; i < UOS_BOOTLINES; i++) {
        CHECK(strlen(uos_bootline(i)) > 0);
        CHECK(strlen(uos_bootline(i)) <= UOS_LINE_MAX);
    }
    CHECK_STR(uos_bootline(0), "ROBCO UNIFIED OPERATING SYSTEM");
    CHECK_STR(uos_bootline(UOS_BOOTLINES), "");
}

TEST(version_line)
{
    char buf[32];

    CHECK_EQ(uos_version_line(buf, sizeof(buf), "1.2.3"), 17);
    CHECK_STR(buf, "UOS KERNEL V1.2.3");
    CHECK_EQ(uos_version_line(buf, 17, "1.2.3"), -1);
    CHECK_STR(buf, "");
    CHECK_EQ(uos_version_line(NULL, 8, "1"), -1);
}

TEST(placeholder_rows)
{
    uos_first_t f = base();
    char buf[UOS_LINE_MAX + 1];

    CHECK_EQ(uos_first_line(0, &f, buf, sizeof(buf)), 13);
    CHECK_STR(buf, "TEST TERMINAL");
    uos_first_line(UOS_FIRST_ROW_NOTICE, &f, buf, sizeof(buf));
    CHECK_STR(buf, "LOGIN IS NOT AVAILABLE.");
    uos_first_line(UOS_FIRST_ROW_NOTICE + 1, &f, buf, sizeof(buf));
    CHECK_STR(buf, "THIS TERMINAL RUNS THE KERNEL BASE ONLY.");
    uos_first_line(4, &f, buf, sizeof(buf));
    CHECK_STR(buf, "CARD: READY");
    uos_first_line(5, &f, buf, sizeof(buf));
    CHECK_STR(buf, "SETTINGS: OK");
    uos_first_line(UOS_FIRST_ROW_CLOCK, &f, buf, sizeof(buf));
    CHECK_STR(buf, "CLOCK: NO CLOCK FITTED");
    uos_first_line(UOS_FIRST_ROW_KEYS, &f, buf, sizeof(buf));
    CHECK_STR(buf, "KEYS: NONE YET");
    CHECK_EQ(uos_first_line(UOS_FIRST_ROWS, &f, buf, sizeof(buf)), -1);
    CHECK_STR(buf, "");
}

/* The session service's notice replaces the two default rows. */
TEST(notice_rows)
{
    uos_first_t f = base();
    char buf[UOS_LINE_MAX + 1];

    f.notice[0] = "USER DATABASE MISSING.";
    f.notice[1] = "PRESS F4.";
    uos_first_line(UOS_FIRST_ROW_NOTICE, &f, buf, sizeof(buf));
    CHECK_STR(buf, "USER DATABASE MISSING.");
    uos_first_line(UOS_FIRST_ROW_NOTICE + 1, &f, buf, sizeof(buf));
    CHECK_STR(buf, "PRESS F4.");
}

TEST(card_states)
{
    uos_first_t f = base();
    char buf[UOS_LINE_MAX + 1];

    f.card = UOS_CARD_NONE;
    uos_first_line(4, &f, buf, sizeof(buf));
    CHECK_STR(buf, "CARD: NO CARD. INSERT A CARD AND RESTART.");
    f.card = UOS_CARD_NOTREE;
    uos_first_line(4, &f, buf, sizeof(buf));
    CHECK_STR(buf, "CARD: NO /UOS FOLDER. SYSTEM FILES ARE MISSING.");
    f.card = 7;
    uos_first_line(4, &f, buf, sizeof(buf));
    CHECK_STR(buf, "CARD: UNKNOWN STATE");
    CHECK_STR(uos_card_name(UOS_CARD_OK), "ok");
    CHECK_STR(uos_card_name(UOS_CARD_NONE), "none");
    CHECK_STR(uos_card_name(UOS_CARD_NOTREE), "notree");
    CHECK_STR(uos_card_name(UOS_CARD_GONE), "gone");
    CHECK_EQ(uos_card_live(UOS_CARD_OK, 4, 4), UOS_CARD_OK);
    CHECK_EQ(uos_card_live(UOS_CARD_OK, 4, 5), UOS_CARD_GONE);
    CHECK_EQ(uos_card_live(UOS_CARD_NOTREE, 4, 5), UOS_CARD_GONE);
    CHECK_EQ(uos_card_live(UOS_CARD_NONE, 4, 5), UOS_CARD_NONE);
    CHECK_STR(uos_card_name(-1), "unknown");
}

TEST(clock_and_keys)
{
    uos_first_t f = base();
    char buf[UOS_LINE_MAX + 1];

    f.rtc = 1;
    f.year = 2077;
    f.month = 10;
    f.day = 23;
    f.hour = 9;
    f.minute = 47;
    f.second = 5;
    uos_first_line(UOS_FIRST_ROW_CLOCK, &f, buf, sizeof(buf));
    CHECK_STR(buf, "CLOCK: 2077-10-23 09:47:05");
    f.keys = 3;
    f.last_key = "up";
    uos_first_line(UOS_FIRST_ROW_KEYS, &f, buf, sizeof(buf));
    CHECK_STR(buf, "KEYS: 3, LAST up");
    f.cfg_bad = 1;
    uos_first_line(5, &f, buf, sizeof(buf));
    CHECK_STR(buf, "SETTINGS: SOME VALUES IGNORED");
}

TEST(too_small_and_bad_arguments)
{
    uos_first_t f = base();
    char buf[8];

    CHECK_EQ(uos_first_line(2, &f, buf, sizeof(buf)), -1);
    CHECK_STR(buf, "");
    CHECK_EQ(uos_first_line(0, NULL, buf, sizeof(buf)), -1);
    f.name = NULL;
    CHECK_EQ(uos_first_line(0, &f, buf, sizeof(buf)), -1);
    CHECK_EQ(uos_first_line(0, &f, NULL, 0), -1);
}
