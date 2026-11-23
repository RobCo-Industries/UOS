/* Host tests of src/tui/pager.c. */

#include "rc_test.h"
#include "tui.h"

static pg_t p;
static uint32_t marks[PG_MARKS_FOR(65536)];

static int init(const char *s, unsigned width, unsigned height)
{
    return pg_init(&p, s, strlen(s), width, height, marks, PG_MARKS_FOR(strlen(s)));
}

/* All wrapped lines joined with '|'. */
static const char *lines(void)
{
    static char out[8192];
    char line[PG_MAX_WIDTH + 1];
    size_t n = 0;
    uint32_t i;

    out[0] = 0;
    for (i = 0; i < pg_lines(&p); i++) {
        int len = pg_line(&p, i, line, sizeof(line));

        CHECK(len >= 0 && len <= p.width);
        CHECK_EQ((size_t)len, strlen(line));
        n += (size_t)snprintf(out + n, sizeof(out) - n, "%s%s", i ? "|" : "", line);
    }
    return out;
}

static const char *status(void)
{
    static char s[64];

    CHECK(pg_status(&p, s, sizeof(s)) > 0);
    return s;
}

TEST(pg_empty_text)
{
    uint32_t first = 9;
    uint32_t count = 9;
    char line[81];

    CHECK_EQ(init("", 10, 5), PG_OK);
    CHECK_EQ(pg_lines(&p), 0);
    CHECK_EQ(pg_pages(&p), 0);
    CHECK_STR(status(), "line 0 to 0 of 0");
    pg_visible(&p, &first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, 0);
    pg_line_down(&p);
    pg_page_down(&p);
    pg_end(&p);
    pg_line_up(&p);
    pg_page_up(&p);
    CHECK_EQ(p.top, 0);
    CHECK_EQ(pg_line(&p, 0, line, sizeof(line)), PG_E_RANGE);
    CHECK_EQ(pg_init(&p, NULL, 0, 10, 5, marks, 1), PG_OK);
}

TEST(pg_one_line)
{
    CHECK_EQ(init("hello", 10, 5), PG_OK);
    CHECK_EQ(pg_lines(&p), 1);
    CHECK_EQ(pg_pages(&p), 1);
    CHECK_STR(lines(), "hello");
    CHECK_STR(status(), "line 1 to 1 of 1");
    CHECK_EQ(init("hello\n", 10, 5), PG_OK);
    CHECK_EQ(pg_lines(&p), 1);
    CHECK_EQ(init("\n", 10, 5), PG_OK);
    CHECK_EQ(pg_lines(&p), 1);
    CHECK_STR(lines(), "");
}

TEST(pg_line_ends)
{
    CHECK_EQ(init("a\r\nb\nc\rd\n\ne", 10, 5), PG_OK);
    CHECK_STR(lines(), "a|b|c|d||e");
    CHECK_EQ(init("a\r\n\r\n", 10, 5), PG_OK);
    CHECK_STR(lines(), "a|");
}

TEST(pg_wrap_exact)
{
    static const struct {
        const char *in;
        unsigned width;
        const char *out;
    } cases[] = {
        { "12345678", 8, "12345678" },                      /* exactly the width */
        { "12345678\nx", 8, "12345678|x" },                 /* width then a line end: no blank line */
        { "123456789", 8, "12345678|9" },                   /* long word split */
        { "abcdefghijklmnopqrstuvwx", 8, "abcdefgh|ijklmnop|qrstuvwx" },
        { "one two three four", 8, "one two|three|four" },
        { "one two  three", 8, "one two|three" },          /* blanks at a break are dropped */
        { "abc defghijklm", 8, "abc|defghijk|lm" },        /* break, then a long word */
        { "1234567 9", 8, "1234567|9" },                    /* the space at the width breaks */
        { "12345678 9", 8, "12345678|9" },
        { "abc      \nx", 8, "abc|x" },                     /* blanks at a break, then a line end */
        { "abc \nx", 8, "abc |x" },                      /* blanks before a hard line end stay */
        { "   abcdefghij", 8, "   abcde|fghij" },          /* leading blanks are no break */
        { "\tx", 8, "        |x" },                         /* a tab fills the line */
        { "ab\tc", 10, "ab      c" },                       /* tab to column 8 */
        { "ab\tcdefgh", 10, "ab|cdefgh" },                  /* the tab is a break */
        { "a\x01" "b\x7f", 8, "a?b?" },                     /* control bytes as '?' */
        { "the quick brown fox jumps over", 12, "the quick|brown fox|jumps over" },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK_EQ(init(cases[i].in, cases[i].width, 3), PG_OK);
        CHECK_STR(lines(), cases[i].out);
    }
}

TEST(pg_many_lines_moves)
{
    static char text[2000];
    size_t n = 0;
    uint32_t first;
    uint32_t count;
    int i;

    for (i = 1; i <= 100; i++) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "line %d\n", i);
    }
    CHECK_EQ(init(text, 20, 10), PG_OK);
    CHECK_EQ(pg_lines(&p), 100);
    CHECK_EQ(pg_pages(&p), 10);
    CHECK_EQ(p.nmarks, 2);
    CHECK_STR(status(), "line 1 to 10 of 100");
    /* Up at the top stays. */
    pg_line_up(&p);
    pg_page_up(&p);
    CHECK_EQ(p.top, 0);
    pg_line_down(&p);
    CHECK_STR(status(), "line 2 to 11 of 100");
    pg_page_down(&p);
    CHECK_EQ(p.top, 11);
    pg_page_up(&p);
    CHECK_EQ(p.top, 1);
    pg_page_up(&p);
    CHECK_EQ(p.top, 0);
    pg_end(&p);
    CHECK_EQ(p.top, 90);
    CHECK_STR(status(), "line 91 to 100 of 100");
    /* Down at the end stays. */
    pg_line_down(&p);
    pg_page_down(&p);
    CHECK_EQ(p.top, 90);
    pg_line_up(&p);
    CHECK_EQ(p.top, 89);
    pg_page_down(&p);
    CHECK_EQ(p.top, 90);
    pg_visible(&p, &first, &count);
    CHECK_EQ(first, 90);
    CHECK_EQ(count, 10);
    pg_home(&p);
    CHECK_EQ(p.top, 0);
    {
        char line[21];

        CHECK_EQ(pg_line(&p, 64, line, sizeof(line)), 7);
        CHECK_STR(line, "line 65");
        CHECK_EQ(pg_line(&p, 99, line, sizeof(line)), 8);
        CHECK_STR(line, "line 100");
        CHECK_EQ(pg_line(&p, 100, line, sizeof(line)), PG_E_RANGE);
        CHECK_EQ(pg_line(&p, 0, line, 20), PG_E_SPACE);
    }
    /* Fewer lines than the height. */
    CHECK_EQ(init("a\nb\nc", 8, 10), PG_OK);
    pg_end(&p);
    CHECK_EQ(p.top, 0);
    pg_page_down(&p);
    CHECK_EQ(p.top, 0);
    pg_visible(&p, &first, &count);
    CHECK_EQ(count, 3);
    CHECK_STR(status(), "line 1 to 3 of 3");
}

TEST(pg_keys)
{
    static char text[400];
    size_t n = 0;
    int i;

    for (i = 1; i <= 30; i++) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%d\n", i);
    }
    CHECK_EQ(init(text, 8, 5), PG_OK);
    CHECK_EQ(pg_key(&p, INPUT_KEY_DOWN), PG_OK);
    CHECK_EQ(p.top, 1);
    pg_key(&p, TUI_CHAR('j'));
    CHECK_EQ(p.top, 2);
    pg_key(&p, INPUT_KEY_UP);
    pg_key(&p, TUI_CHAR('k'));
    CHECK_EQ(p.top, 0);
    pg_key(&p, INPUT_KEY_RIGHT);
    CHECK_EQ(p.top, 5);
    pg_key(&p, INPUT_KEY_SELECT);
    pg_key(&p, INPUT_KEY_F2);
    pg_key(&p, TUI_CHAR(' '));
    CHECK_EQ(p.top, 20);
    pg_key(&p, INPUT_KEY_LEFT);
    pg_key(&p, INPUT_KEY_F1);
    pg_key(&p, TUI_CHAR('b'));
    CHECK_EQ(p.top, 5);
    pg_key(&p, INPUT_KEY_F4);
    CHECK_EQ(p.top, 25);
    pg_key(&p, INPUT_KEY_F3);
    CHECK_EQ(p.top, 0);
    pg_key(&p, TUI_CHAR('G'));
    CHECK_EQ(p.top, 25);
    pg_key(&p, TUI_CHAR('g'));
    CHECK_EQ(p.top, 0);
    CHECK_EQ(pg_key(&p, TUI_CHAR('x')), PG_OK);
    CHECK_EQ(pg_key(&p, INPUT_KEY_BACK), PG_EXIT);
    CHECK_EQ(pg_key(&p, TUI_CHAR('q')), PG_EXIT);
    CHECK_EQ(pg_key(NULL, INPUT_KEY_UP), PG_E_ARG);
}

TEST(pg_64k_text)
{
    static char text[65536];
    static char worst[65536];
    char line[81];
    uint32_t i;
    uint32_t n;

    /* Words of 1 to 12 letters. */
    for (i = 0; i < sizeof(text); i++) {
        text[i] = (i % 13 == 12) ? ' ' : (i % 997 == 0 ? '\n' : (char)('a' + i % 26));
    }
    CHECK_EQ(pg_init(&p, text, sizeof(text), 78, 25, marks, PG_MARKS_FOR(sizeof(text))), PG_OK);
    n = pg_lines(&p);
    CHECK(n > 800 && n < 1100);
    CHECK(p.nmarks <= PG_MARKS_FOR(sizeof(text)));
    pg_end(&p);
    CHECK_EQ(p.top, n - 25);
    for (i = 0; i < n; i++) {
        CHECK(pg_line(&p, i, line, sizeof(line)) >= 0);
    }
    /* The worst case: every byte a line. */
    memset(worst, '\n', sizeof(worst));
    CHECK_EQ(pg_init(&p, worst, sizeof(worst), 8, 30, marks, PG_MARKS_FOR(sizeof(worst))), PG_OK);
    CHECK_EQ(pg_lines(&p), 65536);
    CHECK_EQ(p.nmarks, 1024);
    CHECK_EQ(PG_MARKS_FOR(65536), 1026);
    CHECK_EQ(sizeof(marks), 4104);
    CHECK_EQ(pg_line(&p, 65535, line, sizeof(line)), 0);
    pg_end(&p);
    CHECK_STR(status(), "line 65507 to 65536 of 65536");
    /* Index too small. */
    CHECK_EQ(pg_init(&p, worst, sizeof(worst), 8, 30, marks, 1023), PG_E_SPACE);
    CHECK_EQ(pg_lines(&p), 0);
}

TEST(pg_args)
{
    char s[8];

    CHECK_EQ(pg_init(NULL, "a", 1, 8, 1, marks, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, NULL, 1, 8, 1, marks, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, "a", 1, 8, 1, NULL, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, "a", 1, 7, 1, marks, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, "a", 1, 81, 1, marks, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, "a", 1, 8, 0, marks, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, "a", 1, 8, 31, marks, 2), PG_E_ARG);
    CHECK_EQ(pg_init(&p, "a", 1, 80, 30, marks, 2), PG_OK);
    CHECK_EQ(pg_init(&p, "a", 1, 8, 1, marks, 0), PG_E_SPACE);
    CHECK_EQ(init("a", 8, 1), PG_OK);
    CHECK_EQ(pg_status(&p, s, sizeof(s)), PG_E_SPACE);
    CHECK_STR(s, "");
    CHECK_EQ(pg_status(NULL, s, sizeof(s)), PG_E_ARG);
    CHECK_EQ(pg_line(NULL, 0, s, sizeof(s)), PG_E_ARG);
    CHECK_EQ(pg_lines(NULL), 0);
    CHECK_EQ(pg_pages(NULL), 0);
}
