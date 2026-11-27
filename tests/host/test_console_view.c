#include "rc_test.h"

#include "conview.h"
#include "lineedit.h"

static input_event_t mk(unsigned type, unsigned source, unsigned code, int value)
{
    input_event_t e;

    memset(&e, 0, sizeof(e));
    e.type = (uint8_t)type;
    e.source = (uint8_t)source;
    e.code = (uint16_t)code;
    e.value = value;
    return e;
}

TEST(cv_input_row_moves_above_keyboard)
{
    CHECK_EQ(cv_input_row(10, 25, 5), 10u);
    CHECK_EQ(cv_input_row(24, 25, 5), 24u);
    CHECK_EQ(cv_input_row(25, 25, 5), 24u);
    CHECK_EQ(cv_input_row(29, 25, 5), 24u);
    CHECK_EQ(cv_input_row(29, 0, 0), 29u);
}

TEST(cv_alt_key_rules)
{
    input_event_t e;

    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, CV_ALT_KEY, INPUT_PRESS);
    CHECK(cv_alt_key(&e, 1, 0, 0));
    CHECK(!cv_alt_key(&e, 0, 0, 0));        /* the read did not ask for it */
    CHECK(!cv_alt_key(&e, 1, 1, 0));        /* text on the line: F2 is END */
    CHECK(!cv_alt_key(&e, 1, 0, 1));        /* the keyboard is open */
    e = mk(INPUT_EV_KEY, INPUT_SRC_SCRIPT, CV_ALT_KEY, INPUT_PRESS);
    CHECK(cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, CV_ALT_KEY, INPUT_PRESS);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, CV_ALT_KEY, INPUT_RELEASE);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, CV_ALT_KEY, INPUT_REPEAT);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, INPUT_KEY_F1, INPUT_PRESS);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_CHAR, INPUT_SRC_SERIAL, CV_ALT_KEY, INPUT_PRESS);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    CHECK(!cv_alt_key(NULL, 1, 0, 0));
}

TEST(cv_alt_key_button_back)
{
    input_event_t e;

    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, CV_ALT_BUTTON, INPUT_PRESS);
    CHECK(cv_alt_key(&e, 1, 0, 0));
    CHECK(!cv_alt_key(&e, 0, 0, 0));        /* no alt flag */
    CHECK(!cv_alt_key(&e, 1, 1, 0));        /* text on the line */
    CHECK(!cv_alt_key(&e, 1, 0, 1));        /* the keyboard is open */
    CHECK(!cv_opens_osk(&e, 1));
    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, CV_ALT_BUTTON, INPUT_RELEASE);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, CV_ALT_BUTTON, INPUT_PRESS);
    CHECK(!cv_alt_key(&e, 1, 0, 0));        /* a serial BACK */
    e = mk(INPUT_EV_KEY, INPUT_SRC_SCRIPT, CV_ALT_BUTTON, INPUT_PRESS);
    CHECK(!cv_alt_key(&e, 1, 0, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, INPUT_KEY_SELECT, INPUT_PRESS);
    CHECK(!cv_alt_key(&e, 1, 0, 0));        /* another button */
    CHECK(cv_opens_osk(&e, 0));
}

TEST(cv_opens_osk_rules)
{
    input_event_t e;

    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, INPUT_KEY_UP, INPUT_PRESS);
    CHECK(cv_opens_osk(&e, 0));
    CHECK(!cv_opens_osk(&e, 1));
    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, INPUT_KEY_SELECT, INPUT_REPEAT);
    CHECK(cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_GPIO, INPUT_KEY_UP, INPUT_RELEASE);
    CHECK(!cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, INPUT_KEY_UP, INPUT_PRESS);
    CHECK(!cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SCRIPT, INPUT_KEY_UP, INPUT_PRESS);
    CHECK(!cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, CV_OSK_KEY, INPUT_PRESS);
    CHECK(cv_opens_osk(&e, 0));
    CHECK(!cv_opens_osk(&e, 1));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SCRIPT, CV_OSK_KEY, INPUT_PRESS);
    CHECK(cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_KEY, INPUT_SRC_SERIAL, CV_OSK_KEY, INPUT_REPEAT);
    CHECK(!cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_CHAR, INPUT_SRC_SERIAL, 'a', 0);
    CHECK(!cv_opens_osk(&e, 0));
    e = mk(INPUT_EV_CHAR, INPUT_SRC_GPIO, 'a', 0);
    CHECK(!cv_opens_osk(&e, 0));
    CHECK(!cv_opens_osk(NULL, 0));
    CHECK_EQ(CV_OSK_KEY, INPUT_KEY_F4);
}

TEST(cv_line_width_limits)
{
    CHECK_EQ(cv_line_width(0, 80), 79u);
    CHECK_EQ(cv_line_width(2, 80), 77u);
    CHECK_EQ(cv_line_width(40, 80), 39u);
    CHECK_EQ(cv_line_width(60, 80), 39u);
    CHECK_EQ(cv_line_width(0, 100), LE_MAX_WIDTH);
    CHECK_EQ(cv_line_width(10, 12), LE_MIN_WIDTH);
    CHECK_EQ(cv_line_width(10, 0), LE_MIN_WIDTH);
}

TEST(cv_pager_status_text)
{
    static uint32_t marks[PG_MARKS_FOR(70000)];
    static char text[70000];
    char out[CV_STATUS_BUF];
    pg_t p;
    unsigned i;
    int n;

    CHECK_EQ(pg_init(&p, "", 0, 78, 28, marks, PG_MARKS_FOR(0)), PG_OK);
    n = cv_pager_status(&p, out, sizeof(out));
    CHECK_STR(out, "line 0 to 0 of 0  UP/DN line  LT/RT page  F3/F4 ends  BACK quit");
    CHECK_EQ(n, (int)strlen(out));
    CHECK_EQ(cv_pager_status(&p, out, sizeof(out) - 1), CV_ERR_ARG);
    CHECK_EQ(cv_pager_status(NULL, out, sizeof(out)), CV_ERR_ARG);
    /* The widest numbers stay within 78. */
    for (i = 0; i < sizeof(text); i++) {
        text[i] = '\n';
    }
    CHECK_EQ(pg_init(&p, text, sizeof(text), 78, 28, marks, PG_MARKS_FOR(70000)), PG_OK);
    pg_end(&p);
    n = cv_pager_status(&p, out, sizeof(out));
    CHECK_STR(out, "line 69973 to 70000 of 70000  UP/DN line  LT/RT page  F3/F4 ends  BACK quit");
    CHECK(n <= 78);
}

TEST(cv_printable_dots)
{
    const char in[] = "a\tb\nc\r\x01\x1f\x7f\x80\xff~ ";
    char out[sizeof(in)];

    cv_printable(out, in, sizeof(in) - 1);
    out[sizeof(in) - 1] = 0;
    CHECK_STR(out, "a\tb\nc\r.....~ ");
}

TEST(cv_quote_text)
{
    char out[16];

    CHECK_EQ(cv_quote(out, sizeof(out), "abc", 3, 100), 0);
    CHECK_STR(out, "\"abc\"");
    CHECK_EQ(cv_quote(out, sizeof(out), "a\"b c%", 6, 100), 0);
    CHECK_STR(out, "\"a%22b%20c%25\"");
    CHECK_EQ(cv_quote(out, sizeof(out), "\x01\x7f", 2, 100), 0);
    CHECK_STR(out, "\"%01%7F\"");
    CHECK_EQ(cv_quote(out, sizeof(out), "", 0, 100), 0);
    CHECK_STR(out, "\"\"");
    CHECK_EQ(cv_quote(out, sizeof(out), "abcdef", 6, 3), 1);
    CHECK_STR(out, "\"abc\"");
    /* 16 bytes hold 13 characters, the quotes and the NUL. */
    CHECK_EQ(cv_quote(out, sizeof(out), "abcdefghijklmnopq", 17, 100), 1);
    CHECK_STR(out, "\"abcdefghijklm\"");
    /* An escape that does not fit whole is left out. */
    CHECK_EQ(cv_quote(out, sizeof(out), "abcdefghijk ", 12, 100), 1);
    CHECK_STR(out, "\"abcdefghijk\"");
    CHECK_EQ(cv_quote(out, 2, "a", 1, 100), CV_ERR_ARG);
}

TEST(cv_parse_words)
{
    unsigned f = 99, n = 0;

    CHECK_EQ(cv_parse_flags("-", &f), 0);
    CHECK_EQ(f, 0u);
    CHECK_EQ(cv_parse_flags("m", &f), 0);
    CHECK_EQ(f, LE_F_MASK);
    CHECK_EQ(cv_parse_flags("hm", &f), 0);
    CHECK_EQ(f, LE_F_MASK | LE_F_NOHIST);
    CHECK_EQ(cv_parse_flags("mm", &f), CV_ERR_ARG);
    CHECK_EQ(cv_parse_flags("x", &f), CV_ERR_ARG);
    CHECK_EQ(cv_parse_flags("", &f), CV_ERR_ARG);
    CHECK_EQ(cv_parse_flags("--", &f), CV_ERR_ARG);
    CHECK_EQ(cv_parse_count("1", 255, &n), 0);
    CHECK_EQ(n, 1u);
    CHECK_EQ(cv_parse_count("255", 255, &n), 0);
    CHECK_EQ(n, 255u);
    CHECK_EQ(cv_parse_count("256", 255, &n), CV_ERR_ARG);
    CHECK_EQ(cv_parse_count("0", 255, &n), CV_ERR_ARG);
    CHECK_EQ(cv_parse_count("1a", 255, &n), CV_ERR_ARG);
    CHECK_EQ(cv_parse_count("-1", 255, &n), CV_ERR_ARG);
    CHECK_EQ(cv_parse_count("9999999", 255, &n), CV_ERR_ARG);
    CHECK_EQ(cv_parse_count("", 255, &n), CV_ERR_ARG);
}
