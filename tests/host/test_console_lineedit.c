#include "rc_test.h"

#include "lineedit.h"

static char buf[LE_MAX + 1];
static le_history_t hist;

static int ev(le_t *e, unsigned type, unsigned code, int value, le_redraw_t *rd)
{
    input_event_t in;

    memset(&in, 0, sizeof(in));
    in.type = (uint8_t)type;
    in.source = INPUT_SRC_SERIAL;
    in.code = (uint16_t)code;
    in.value = value;
    return le_event(e, &in, rd);
}

static int key(le_t *e, unsigned code)
{
    return ev(e, INPUT_EV_KEY, code, INPUT_PRESS, NULL);
}

static int rep(le_t *e, unsigned code)
{
    return ev(e, INPUT_EV_KEY, code, INPUT_REPEAT, NULL);
}

static void type(le_t *e, const char *s)
{
    while (*s) {
        CHECK_EQ(le_char(e, (unsigned char)*s++, NULL), LE_EDITING);
    }
}

static void start(le_t *e, unsigned max, unsigned flags, unsigned width)
{
    CHECK_EQ(le_init(e, buf, sizeof(buf), max, flags, &hist, width), 0);
}

static void disp(const le_t *e, const char *want, unsigned want_col)
{
    char out[LE_MAX_WIDTH + 1];
    unsigned col = 99;

    CHECK_EQ(le_display(e, out, sizeof(out), &col), (int)e->width);
    CHECK_STR(out, want);
    CHECK_EQ(col, want_col);
}

/* Reads a line through the editor: types it and accepts it. */
static void enter_line(const char *s)
{
    le_t e;

    start(&e, LE_MAX, 0, 40);
    type(&e, s);
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
}

TEST(le_init_args)
{
    le_t e;

    CHECK_EQ(le_init(NULL, buf, sizeof(buf), 10, 0, NULL, 20), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, NULL, sizeof(buf), 10, 0, NULL, 20), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, sizeof(buf), 0, 0, NULL, 20), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, sizeof(buf), 256, 0, NULL, 20), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, 10, 10, 0, NULL, 20), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, 11, 10, 0, NULL, 20), 0);
    CHECK_EQ(le_init(&e, buf, sizeof(buf), 10, 0, NULL, 3), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, sizeof(buf), 10, 0, NULL, 81), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, sizeof(buf), 10, 0x4, NULL, 20), LE_ERR_ARG);
    CHECK_EQ(le_init(&e, buf, sizeof(buf), 255, LE_F_MASK | LE_F_NOHIST, NULL, 4), 0);
    CHECK_EQ(le_len(&e), 0u);
    CHECK_STR(buf, "");
    CHECK_EQ(le_event(&e, NULL, NULL), LE_ERR_ARG);
    CHECK_EQ(le_display(&e, buf, 4, NULL), LE_ERR_ARG);
}

TEST(le_insert_and_redraw)
{
    le_t e;
    le_redraw_t rd;

    le_hist_init(&hist);
    start(&e, 20, 0, 20);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, 'a', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_FROM);
    CHECK_EQ(rd.from, 0);
    type(&e, "bd");
    CHECK_EQ(key(&e, INPUT_KEY_LEFT), LE_EDITING);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, 'c', 0, &rd), LE_EDITING);
    CHECK_STR(buf, "abcd");
    CHECK_EQ(rd.what, LE_RD_FROM);
    CHECK_EQ(rd.from, 2);
    CHECK_EQ(le_cursor_col(&e), 3u);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_LEFT, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_CURSOR);
    disp(&e, "abcd                ", 2);
    /* Release, dial, unknown keys and control characters do nothing. */
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_LEFT, INPUT_RELEASE, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    CHECK_EQ(ev(&e, INPUT_EV_DIAL, 0, 1, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_F4, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, '\t', 0, &rd), LE_EDITING);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, 0x7f, 0, &rd), LE_EDITING);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, 0x80, 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    CHECK_STR(buf, "abcd");
}

TEST(le_every_edit_key)
{
    le_t e;
    le_redraw_t rd;

    le_hist_init(&hist);
    start(&e, 30, 0, 40);
    type(&e, "hello world");
    /* Home and end, by F keys and control characters. */
    key(&e, INPUT_KEY_F1);
    CHECK_EQ(le_cursor_col(&e), 0u);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_F1, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    key(&e, INPUT_KEY_F2);
    CHECK_EQ(le_cursor_col(&e), 11u);
    le_char(&e, LE_CTRL('A'), NULL);
    CHECK_EQ(le_cursor_col(&e), 0u);
    le_char(&e, LE_CTRL('E'), NULL);
    CHECK_EQ(le_cursor_col(&e), 11u);
    le_char(&e, LE_CTRL('B'), NULL);
    CHECK_EQ(le_cursor_col(&e), 10u);
    le_char(&e, LE_CTRL('F'), NULL);
    CHECK_EQ(le_cursor_col(&e), 11u);
    /* Right at the end and left at the start are refused. */
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_RIGHT, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    CHECK_EQ(rd.refused, 1);
    /* Backspace at the end. */
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_BACK, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_FROM);
    CHECK_EQ(rd.from, 10);
    CHECK_STR(buf, "hello worl");
    /* Delete under the cursor, F3 and Ctrl-D. */
    key(&e, INPUT_KEY_F1);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_BACK, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_LEFT, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    key(&e, INPUT_KEY_F3);
    CHECK_STR(buf, "ello worl");
    le_char(&e, LE_CTRL('D'), NULL);
    CHECK_STR(buf, "llo worl");
    key(&e, INPUT_KEY_F2);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_F3, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    /* Kill to the end of the line. */
    key(&e, INPUT_KEY_F1);
    key(&e, INPUT_KEY_RIGHT);
    key(&e, INPUT_KEY_RIGHT);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, LE_CTRL('K'), 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_FROM);
    CHECK_EQ(rd.from, 2);
    CHECK_STR(buf, "ll");
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, LE_CTRL('K'), 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    /* Overwrite mode replaces, then appends at the end. */
    key(&e, INPUT_KEY_F1);
    le_char(&e, LE_CTRL('O'), NULL);
    type(&e, "ABC");
    CHECK_STR(buf, "ABC");
    le_char(&e, LE_CTRL('O'), NULL);
    key(&e, INPUT_KEY_F1);
    type(&e, "x");
    CHECK_STR(buf, "xABC");
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
    CHECK_STR(buf, "xABC");
    /* Done: later events change nothing. */
    CHECK_EQ(le_char(&e, 'z', NULL), LE_ACCEPT);
    CHECK_EQ(key(&e, INPUT_KEY_BACK), LE_ACCEPT);
    CHECK_STR(buf, "xABC");
    CHECK_EQ(le_state(&e), LE_ACCEPT);
}

TEST(le_cancel)
{
    le_t e;

    le_hist_init(&hist);
    start(&e, 30, 0, 40);
    type(&e, "abc");
    CHECK_EQ(le_char(&e, LE_CTRL('C'), NULL), LE_CANCEL);
    CHECK_EQ(le_state(&e), LE_CANCEL);
    CHECK_EQ(le_hist_count(&hist), 0u);
    /* BACK is backspace, never cancel. */
    start(&e, 30, 0, 40);
    CHECK_EQ(key(&e, INPUT_KEY_BACK), LE_EDITING);
}

TEST(le_limits)
{
    le_t e;
    le_redraw_t rd;
    unsigned i;

    le_hist_init(&hist);
    start(&e, 5, 0, 20);
    type(&e, "12345");
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, '6', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_NONE);
    CHECK_EQ(rd.refused, 1);
    CHECK_STR(buf, "12345");
    key(&e, INPUT_KEY_F1);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, '0', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    /* Overwrite works on a full line. */
    le_char(&e, LE_CTRL('O'), NULL);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, '0', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 0);
    CHECK_STR(buf, "02345");
    key(&e, INPUT_KEY_F2);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, '6', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);

    /* 255 characters, the most. */
    start(&e, LE_MAX, 0, 79);
    for (i = 0; i < LE_MAX; i++) {
        CHECK_EQ(ev(&e, INPUT_EV_CHAR, 'a' + i % 26, 0, &rd), LE_EDITING);
        CHECK_EQ(rd.refused, 0);
    }
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, 'x', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    CHECK_EQ(le_len(&e), 255u);
    CHECK_EQ(strlen(buf), 255u);
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
    CHECK_EQ(le_hist_count(&hist), 1u);
    CHECK_EQ(strlen(le_hist_get(&hist, 0)), 255u);
    /* A 256-byte line is never stored. */
    {
        char big[300];

        memset(big, 'q', sizeof(big));
        CHECK_EQ(le_hist_add(&hist, big, 256), 0);
        CHECK_EQ(le_hist_add(&hist, big, 0), 0);
        CHECK_EQ(le_hist_add(NULL, big, 3), 0);
    }
}

TEST(le_repeats)
{
    le_t e;

    le_hist_init(&hist);
    start(&e, 30, 0, 40);
    type(&e, "abcdef");
    CHECK_EQ(rep(&e, INPUT_KEY_LEFT), LE_EDITING);
    CHECK_EQ(rep(&e, INPUT_KEY_LEFT), LE_EDITING);
    CHECK_EQ(le_cursor_col(&e), 4u);
    rep(&e, INPUT_KEY_RIGHT);
    CHECK_EQ(le_cursor_col(&e), 5u);
    rep(&e, INPUT_KEY_BACK);
    CHECK_STR(buf, "abcdf");
    rep(&e, INPUT_KEY_F3);
    CHECK_STR(buf, "abcd");
    /* SELECT, F1 and F2 do not repeat. */
    CHECK_EQ(rep(&e, INPUT_KEY_SELECT), LE_EDITING);
    rep(&e, INPUT_KEY_F1);
    CHECK_EQ(le_cursor_col(&e), 4u);
    key(&e, INPUT_KEY_F1);
    rep(&e, INPUT_KEY_F2);
    CHECK_EQ(le_cursor_col(&e), 0u);
    /* UP and DOWN repeat through the history. */
    enter_line("one");
    enter_line("two");
    start(&e, 30, 0, 40);
    rep(&e, INPUT_KEY_UP);
    rep(&e, INPUT_KEY_UP);
    CHECK_STR(buf, "one");
    rep(&e, INPUT_KEY_DOWN);
    CHECK_STR(buf, "two");
}

TEST(le_history_empty_and_one)
{
    le_t e;
    le_redraw_t rd;

    le_hist_init(&hist);
    CHECK_EQ(le_hist_count(&hist), 0u);
    CHECK(le_hist_get(&hist, 0) == NULL);
    start(&e, 30, 0, 40);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_UP, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_DOWN, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    /* An empty line is not stored. */
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
    CHECK_EQ(le_hist_count(&hist), 0u);

    enter_line("show time");
    CHECK_EQ(le_hist_count(&hist), 1u);
    start(&e, 30, 0, 40);
    type(&e, "dir");
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_UP, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_ALL);
    CHECK_STR(buf, "show time");
    CHECK_EQ(le_cursor_col(&e), 9u);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_UP, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    CHECK_STR(buf, "show time");
    /* Down past the newest brings back the line being typed. */
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_DOWN, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_ALL);
    CHECK_STR(buf, "dir");
    le_char(&e, LE_CTRL('P'), NULL);
    CHECK_STR(buf, "show time");
    le_char(&e, LE_CTRL('N'), NULL);
    CHECK_STR(buf, "dir");
}

TEST(le_history_sixteen_and_more)
{
    char line[16];
    le_t e;
    unsigned i;

    le_hist_init(&hist);
    for (i = 1; i <= 16; i++) {
        snprintf(line, sizeof(line), "line %u", i);
        enter_line(line);
    }
    CHECK_EQ(le_hist_count(&hist), 16u);
    CHECK_STR(le_hist_get(&hist, 0), "line 16");
    CHECK_STR(le_hist_get(&hist, 15), "line 1");
    CHECK(le_hist_get(&hist, 16) == NULL);
    start(&e, 30, 0, 40);
    for (i = 0; i < 20; i++) {
        key(&e, INPUT_KEY_UP);
    }
    CHECK_STR(buf, "line 1");
    /* The 17th drops the oldest. */
    enter_line("line 17");
    CHECK_EQ(le_hist_count(&hist), 16u);
    CHECK_STR(le_hist_get(&hist, 0), "line 17");
    CHECK_STR(le_hist_get(&hist, 15), "line 2");
    /* A duplicate of the newest is not stored; of an older one it is. */
    enter_line("line 17");
    CHECK_STR(le_hist_get(&hist, 1), "line 16");
    enter_line("line 16");
    CHECK_STR(le_hist_get(&hist, 0), "line 16");
    CHECK_STR(le_hist_get(&hist, 1), "line 17");
}

TEST(le_history_recalled_line_edit)
{
    le_t e;

    le_hist_init(&hist);
    enter_line("type notes.txt");
    start(&e, 30, 0, 40);
    key(&e, INPUT_KEY_UP);
    key(&e, INPUT_KEY_BACK);
    key(&e, INPUT_KEY_BACK);
    key(&e, INPUT_KEY_BACK);
    type(&e, "log");
    CHECK_STR(buf, "type notes.log");
    /* The stored line did not change. */
    CHECK_STR(le_hist_get(&hist, 0), "type notes.txt");
    /* Going down and up again shows the stored line, not the edit. */
    key(&e, INPUT_KEY_DOWN);
    CHECK_STR(buf, "");
    key(&e, INPUT_KEY_UP);
    CHECK_STR(buf, "type notes.txt");
    key(&e, INPUT_KEY_BACK);
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
    CHECK_EQ(le_hist_count(&hist), 2u);
    CHECK_STR(le_hist_get(&hist, 0), "type notes.tx");
    CHECK_STR(le_hist_get(&hist, 1), "type notes.txt");
    /* A recalled line longer than the call's max is cut to it. */
    start(&e, 4, 0, 40);
    key(&e, INPUT_KEY_UP);
    CHECK_STR(buf, "type");
    CHECK_EQ(le_len(&e), 4u);
}

TEST(le_mask_and_nohist)
{
    le_t e;
    le_redraw_t rd;

    le_hist_init(&hist);
    enter_line("visible");
    start(&e, 32, LE_F_MASK, 20);
    type(&e, "s3cret");
    disp(&e, "******              ", 6);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_UP, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.refused, 1);
    CHECK_STR(buf, "s3cret");
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
    CHECK_STR(buf, "s3cret");
    CHECK_EQ(le_hist_count(&hist), 1u);
    CHECK_STR(le_hist_get(&hist, 0), "visible");

    start(&e, 32, LE_F_NOHIST, 20);
    type(&e, "plain");
    key(&e, INPUT_KEY_UP);
    CHECK_STR(buf, "plain");
    CHECK_EQ(key(&e, INPUT_KEY_SELECT), LE_ACCEPT);
    CHECK_EQ(le_hist_count(&hist), 1u);

    /* A long masked line scrolls with stars and markers only. */
    start(&e, 64, LE_F_MASK, 10);
    type(&e, "abcdefghijklmnopqrstuvwxyz");
    disp(&e, "<*******  ", 8);
    key(&e, INPUT_KEY_F1);
    disp(&e, "*********>", 0);
}

TEST(le_scroll_window)
{
    char text[61];
    le_t e;
    le_redraw_t rd;
    unsigned i;

    le_hist_init(&hist);
    /* A line three times the 20-column window. */
    for (i = 0; i < 60; i++) {
        text[i] = (char)('A' + i % 26);
    }
    text[60] = 0;
    start(&e, 100, 0, 20);
    /* 19 characters and the cursor cell fit: no scroll. */
    for (i = 0; i < 19; i++) {
        CHECK_EQ(ev(&e, INPUT_EV_CHAR, (unsigned char)text[i], 0, &rd), LE_EDITING);
        CHECK_EQ(rd.what, LE_RD_FROM);
    }
    disp(&e, "ABCDEFGHIJKLMNOPQRS ", 19);
    CHECK_EQ(e.scroll, 0);
    /* The 20th scrolls: the cursor stays in column 18. */
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, 'T', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_ALL);
    disp(&e, "<DEFGHIJKLMNOPQRST  ", 18);
    for (i = 20; i < 60; i++) {
        le_char(&e, (unsigned char)text[i], NULL);
    }
    CHECK_EQ(le_len(&e), 60u);
    disp(&e, "<RSTUVWXYZABCDEFGH  ", 18);
    /* Home: the start, with the right marker. */
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_F1, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_ALL);
    disp(&e, "ABCDEFGHIJKLMNOPQRS>", 0);
    /* Right up to column 18 does not scroll; one more does. */
    for (i = 0; i < 18; i++) {
        CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_RIGHT, INPUT_PRESS, &rd), LE_EDITING);
        CHECK_EQ(rd.what, LE_RD_CURSOR);
    }
    CHECK_EQ(le_cursor_col(&e), 18u);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_RIGHT, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_ALL);
    disp(&e, "<CDEFGHIJKLMNOPQRST>", 18);
    /* Left to column 1 keeps the window; one more moves it back to the start. */
    for (i = 0; i < 17; i++) {
        key(&e, INPUT_KEY_LEFT);
    }
    CHECK_EQ(le_cursor_col(&e), 1u);
    CHECK_EQ(ev(&e, INPUT_EV_KEY, INPUT_KEY_LEFT, INPUT_PRESS, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_ALL);
    disp(&e, "ABCDEFGHIJKLMNOPQRS>", 1);
    /* Middle of the line: both markers. Insert keeps the window. */
    key(&e, INPUT_KEY_F2);
    for (i = 0; i < 25; i++) {
        key(&e, INPUT_KEY_LEFT);
    }
    CHECK_EQ(le_cursor_col(&e), 1u);
    CHECK_EQ(ev(&e, INPUT_EV_CHAR, '*', 0, &rd), LE_EDITING);
    CHECK_EQ(rd.what, LE_RD_FROM);
    CHECK_EQ(rd.from, 1);
    disp(&e, "<*JKLMNOPQRSTUVWXYZ>", 2);
    /* Deleting from the end pulls the window back: no blank run. */
    key(&e, INPUT_KEY_F2);
    for (i = 0; i < 50; i++) {
        key(&e, INPUT_KEY_BACK);
    }
    CHECK_EQ(le_len(&e), 11u);
    CHECK_EQ(e.scroll, 0);
    disp(&e, "ABCDEFGHIJK         ", 11);
}

TEST(le_kill_with_scroll)
{
    le_t e;
    unsigned i;

    le_hist_init(&hist);
    start(&e, 100, 0, 10);
    for (i = 0; i < 30; i++) {
        le_char(&e, 'a' + i % 26, NULL);
    }
    for (i = 0; i < 15; i++) {
        key(&e, INPUT_KEY_LEFT);
    }
    le_char(&e, LE_CTRL('K'), NULL);
    CHECK_EQ(le_len(&e), 15u);
    /* The window pulls back so no more than the cursor cell is blank. */
    disp(&e, "<ijklmno  ", 8);
}
