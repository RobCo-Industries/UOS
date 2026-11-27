/* Host tests of src/tui/tui.c: the screen model and its layout. */

#include "rc_test.h"
#include "tui.h"

static tui_t t;

/* ---- A fake caller: files by name ---- */

typedef struct {
    const char *name;
    const char *text;
} file_t;

static const file_t *files;

static const char *find(const char *name)
{
    const file_t *f;

    for (f = files; f && f->name; f++) {
        if (strcmp(f->name, name) == 0) {
            return f->text;
        }
    }
    return NULL;
}

/* Answers load actions the way the kernel would. Returns the next action. */
static int serve(int act)
{
    while (act == TUI_ACT_LOAD_MENU || act == TUI_ACT_LOAD_HEADER) {
        const char *s = find(t.name);

        if (s == NULL) {
            char line[64];

            snprintf(line, sizeof(line), "File not found: %s", t.name);
            return tui_give_error(&t, line);
        }
        act = act == TUI_ACT_LOAD_MENU ? tui_give_menu(&t, s, strlen(s))
                                       : tui_give_header(&t, s, strlen(s));
    }
    return act;
}

static int key(unsigned k)
{
    return serve(tui_key(&t, k));
}

static int start(const file_t *fs, uint32_t privs)
{
    files = fs;
    return serve(tui_init(&t, privs, "NORTH YARD", "TERMINAL.CFG"));
}

/* The layout as text: "RR CC A text" per line. */
static const char *dump(void)
{
    static char out[4096];
    tui_line_t lines[TUI_LINES_MAX];
    int n = tui_layout(&t, lines, TUI_LINES_MAX);
    size_t len = 0;
    int i;

    out[0] = 0;
    for (i = 0; i < n; i++) {
        len += (size_t)snprintf(out + len, sizeof(out) - len, "%02u %c %s\n", lines[i].row,
                                lines[i].attr == TUI_A_INVERSE ? 'I'
                                : lines[i].attr == TUI_A_DIM   ? 'D' : 'N',
                                lines[i].text);
        CHECK_EQ(lines[i].col, 1);
        CHECK(strlen(lines[i].text) <= TUI_TEXT_COLS);
    }
    return out;
}

#define RULE "------------------------------------------------------------------------------"

static const file_t basic[] = {
    { "TERMINAL.CFG",
      "[terminal]\ntitle = Main\nheader = HEAD.TXT\n"
      "[entry 1]\nlabel = Read the log\ntype = text\ntarget = LOG.TXT\n"
      "[entry 2]\nlabel = Tools\ntype = menu\ntarget = TOOLS.CFG\n"
      "[entry 3]\nlabel = Time\ntype = command\ntarget = SHOW TIME\n"
      "[entry 4]\nlabel = Lights\ntype = device\ntarget = LIGHT1 TOGGLE\n"
      "[entry 5]\nlabel = Tapes\ntype = holotape\n"
      "[entry 6]\nlabel = One tape\ntype = holotape\ntarget = A.HTP\n"
      "[entry 7]\nlabel = Missing menu\ntype = menu\ntarget = NONE.CFG\n"
      "[entry 8]\nlabel = Bye\ntype = logout\n" },
    { "HEAD.TXT", "Line one\r\n\tTabbed\n" },
    { "TOOLS.CFG", "[terminal]\ntitle = Tools\n[entry 1]\nlabel = Back to main\ntype = menu\ntarget = TERMINAL.CFG\n" },
    { NULL, NULL },
};

TEST(tui_start_layout)
{
    CHECK_EQ(start(basic, 0), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_MENU);
    CHECK_EQ(t.depth, 1);
    CHECK_STR(dump(),
              "00 N NORTH YARD\n"
              "01 N Main\n"
              "02 D " RULE "\n"
              "03 N Line one\n"
              "04 N         Tabbed\n"
              "06 I > Read the log\n"
              "07 N   Tools\n"
              "08 N   Time\n"
              "09 N   Lights\n"
              "10 N   Tapes\n"
              "11 N   One tape\n"
              "12 N   Missing menu\n"
              "13 N   Bye\n"
              "28 D Main\n"
              "29 D UP/DOWN: move  1-9: pick  ENTER: choose  BACK: log out\n");
}

TEST(tui_move_wrap_and_digits)
{
    start(basic, 0);
    CHECK_EQ(key(INPUT_KEY_UP), TUI_ACT_NONE);
    CHECK_EQ(t.level[0].sel, 7);
    CHECK_EQ(key(INPUT_KEY_DOWN), TUI_ACT_NONE);
    CHECK_EQ(t.level[0].sel, 0);
    key(INPUT_KEY_DOWN);
    CHECK_EQ(t.level[0].sel, 1);
    key(TUI_CHAR('5'));
    CHECK_EQ(t.level[0].sel, 4);
    key(TUI_CHAR('9'));         /* only 8 entries: ignored */
    CHECK_EQ(t.level[0].sel, 4);
    key(TUI_CHAR('0'));
    CHECK_EQ(t.level[0].sel, 4);
    key(TUI_CHAR('x'));
    key(INPUT_KEY_RIGHT);
    key(INPUT_KEY_F1);
    CHECK_EQ(t.level[0].sel, 4);
    CHECK_EQ(t.state, TUI_ST_MENU);
}

TEST(tui_every_action)
{
    start(basic, 0);
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_SHOW_TEXT);
    CHECK_STR(t.name, "LOG.TXT");
    CHECK_EQ(t.state, TUI_ST_WAIT);
    CHECK_EQ(key(INPUT_KEY_DOWN), TUI_ACT_NONE);   /* ignored while waiting */
    CHECK_EQ(t.level[0].sel, 0);
    CHECK(strstr(dump(), "29 D Please wait\n") != NULL);
    CHECK_EQ(tui_done(&t, NULL), TUI_ACT_NONE);
    CHECK_EQ(t.level[0].sel, 0);
    CHECK_STR(t.msg, "");

    key(TUI_CHAR('3'));
    CHECK_EQ(key(INPUT_KEY_DIAL_PUSH), TUI_ACT_COMMAND);
    CHECK_STR(t.text, "SHOW TIME");
    CHECK_EQ(tui_done(&t, "12:30:05"), TUI_ACT_NONE);
    CHECK_EQ(t.level[0].sel, 2);
    CHECK(strstr(dump(), "27 N 12:30:05\n") != NULL);
    key(INPUT_KEY_DOWN);                          /* clears and moves */
    CHECK_STR(t.msg, "");
    CHECK_EQ(t.level[0].sel, 3);

    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_DEVICE);
    CHECK_STR(t.name, "LIGHT1");
    CHECK_STR(t.word, "TOGGLE");
    CHECK_EQ(tui_done(&t, "LIGHT1: ON"), TUI_ACT_NONE);
    CHECK_STR(t.msg, "LIGHT1: ON");

    key(TUI_CHAR('5'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_HOLOTAPE);
    CHECK_STR(t.name, "");
    tui_done(&t, "");
    key(TUI_CHAR('6'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_HOLOTAPE);
    CHECK_STR(t.name, "A.HTP");
    tui_done(&t, NULL);

    key(TUI_CHAR('8'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_LOGOUT);
    CHECK_EQ(t.state, TUI_ST_DONE);
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_NONE);
    CHECK_EQ(key(INPUT_KEY_BACK), TUI_ACT_NONE);
    CHECK(strstr(dump(), "29 D Logged out\n") != NULL);
}

TEST(tui_submenu_and_back)
{
    start(basic, 0);
    key(TUI_CHAR('2'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_NONE);
    CHECK_EQ(t.depth, 2);
    CHECK_STR(dump(),
              "00 N NORTH YARD\n"
              "01 N Tools\n"
              "02 D " RULE "\n"
              "04 I > Back to main\n"
              "28 D Main / Tools\n"
              "29 D UP/DOWN: move  1-9: pick  ENTER: choose  BACK: up a level\n");
    /* UP and DOWN wrap on one entry. */
    key(INPUT_KEY_UP);
    CHECK_EQ(t.level[1].sel, 0);
    CHECK_EQ(key(INPUT_KEY_LEFT), TUI_ACT_NONE);
    CHECK_EQ(t.depth, 1);
    CHECK_EQ(t.level[0].sel, 1);                  /* selection kept */
    CHECK_EQ(t.state, TUI_ST_MENU);
}

TEST(tui_missing_file)
{
    start(basic, 0);
    key(TUI_CHAR('7'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_NONE);
    CHECK_EQ(t.depth, 1);
    CHECK_EQ(t.state, TUI_ST_MENU);
    CHECK_EQ(t.level[0].sel, 6);
    CHECK(strstr(dump(), "27 N File not found: NONE.CFG\n") != NULL);
    /* A text that cannot be loaded. */
    key(TUI_CHAR('1'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_SHOW_TEXT);
    CHECK_EQ(tui_give_error(&t, "File not found: LOG.TXT\x01 and a lot more text that runs over the width of the line"),
             TUI_ACT_NONE);
    CHECK_EQ(strlen(t.msg), TUI_MSG_MAX);
    CHECK(strncmp(t.msg, "File not found: LOG.TXT? and", 28) == 0);
}

TEST(tui_logout_question_both_ways)
{
    start(basic, 0);
    CHECK_EQ(key(INPUT_KEY_BACK), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_CONFIRM);
    CHECK(strstr(dump(), "27 N Log out? ENTER: yes  BACK: no\n") != NULL);
    CHECK(strstr(dump(), "29 D ENTER: log out  BACK: stay\n") != NULL);
    /* Other keys do nothing. */
    CHECK_EQ(key(INPUT_KEY_UP), TUI_ACT_NONE);
    CHECK_EQ(key(TUI_CHAR('1')), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_CONFIRM);
    CHECK_EQ(t.level[0].sel, 0);
    CHECK_EQ(key(INPUT_KEY_BACK), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_MENU);
    key(INPUT_KEY_LEFT);
    CHECK_EQ(t.state, TUI_ST_CONFIRM);
    CHECK_EQ(key(INPUT_KEY_LEFT), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_MENU);
    key(INPUT_KEY_BACK);
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_LOGOUT);
    CHECK_EQ(t.state, TUI_ST_DONE);
    key(INPUT_KEY_BACK);
    CHECK_EQ(key(INPUT_KEY_DIAL_PUSH), TUI_ACT_NONE);
}

TEST(tui_nesting_to_eight)
{
    static const file_t loop[] = {
        { "TERMINAL.CFG", "[terminal]\ntitle = Level\n[entry 1]\nlabel = Deeper\ntype = menu\ntarget = TERMINAL.CFG\n" },
        { NULL, NULL },
    };
    int i;

    start(loop, 0);
    for (i = 2; i <= TUI_DEPTH_MAX; i++) {
        CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_NONE);
        CHECK_EQ(t.depth, i);
    }
    CHECK_EQ(tui_key(&t, INPUT_KEY_SELECT), TUI_ACT_NONE);
    CHECK_EQ(t.depth, TUI_DEPTH_MAX);
    CHECK_EQ(t.state, TUI_ST_MENU);
    CHECK_STR(t.msg, "Menus nest 8 deep at most");
    CHECK_STR(dump(),
              "00 N NORTH YARD\n"
              "01 N Level\n"
              "02 D " RULE "\n"
              "04 I > Deeper\n"
              "27 N Menus nest 8 deep at most\n"
              "28 D Level / Level / Level / Level / Level / Level / Level / Level\n"
              "29 D UP/DOWN: move  1-9: pick  ENTER: choose  BACK: up a level\n");
    for (i = TUI_DEPTH_MAX; i > 1; i--) {
        key(INPUT_KEY_BACK);
        CHECK_EQ(t.depth, i - 1);
    }
    key(INPUT_KEY_BACK);
    CHECK_EQ(t.state, TUI_ST_CONFIRM);
}

TEST(tui_scroll_window)
{
    static char cfg[2048];
    static file_t many[] = { { "TERMINAL.CFG", cfg }, { "H.TXT", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" }, { NULL, NULL } };
    size_t n;
    int i;

    n = (size_t)snprintf(cfg, sizeof(cfg), "[terminal]\ntitle = Many\nheader = H.TXT\n");
    for (i = 1; i <= 16; i++) {
        n += (size_t)snprintf(cfg + n, sizeof(cfg) - n, "[entry %d]\nlabel = Item %d\ntype = logout\n", i, i);
    }
    start(many, 0);
    CHECK_EQ(t.level[0].nhead, 8);              /* 10 lines given, 8 kept */
    CHECK_EQ(tui_rows(&t), 15);
    key(INPUT_KEY_UP);                          /* wrap to the last */
    CHECK_EQ(t.level[0].sel, 15);
    CHECK_EQ(t.level[0].top, 1);
    CHECK_STR(dump(),
              "00 N NORTH YARD\n"
              "01 N Many\n"
              "02 D " RULE "\n"
              "03 N 1\n04 N 2\n05 N 3\n06 N 4\n07 N 5\n08 N 6\n09 N 7\n10 N 8\n"
              "12 N   Item 2\n13 N   Item 3\n14 N   Item 4\n15 N   Item 5\n16 N   Item 6\n"
              "17 N   Item 7\n18 N   Item 8\n19 N   Item 9\n20 N   Item 10\n21 N   Item 11\n"
              "22 N   Item 12\n23 N   Item 13\n24 N   Item 14\n25 N   Item 15\n26 I > Item 16\n"
              "28 D Many\n"
              "29 D UP/DOWN: move  1-9: pick  ENTER: choose  BACK: log out\n");
    key(INPUT_KEY_DOWN);                        /* wrap to the first */
    CHECK_EQ(t.level[0].sel, 0);
    CHECK_EQ(t.level[0].top, 0);
    for (i = 0; i < 15; i++) {
        key(INPUT_KEY_DOWN);
    }
    CHECK_EQ(t.level[0].sel, 15);
    CHECK_EQ(t.level[0].top, 1);
    key(TUI_CHAR('1'));
    CHECK_EQ(t.level[0].top, 0);
}

TEST(tui_empty_and_failed_root)
{
    static const file_t none[] = { { NULL, NULL } };
    static const file_t empty[] = { { "TERMINAL.CFG", "[terminal]\ntitle = Nothing\n[entry 1]\nlabel=X\ntype=logout\nprivs=2\n" },
                                    { NULL, NULL } };
    static const file_t bad[] = { { "TERMINAL.CFG", "[terminal]\ntitle = X\nbroken\n" }, { NULL, NULL } };

    CHECK_EQ(start(none, 0), TUI_ACT_NONE);
    CHECK_EQ(t.depth, 0);
    CHECK_STR(dump(),
              "00 N NORTH YARD\n"
              "02 D " RULE "\n"
              "27 N File not found: TERMINAL.CFG\n"
              "29 D BACK: log out\n");
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_NONE);
    CHECK_EQ(key(INPUT_KEY_DOWN), TUI_ACT_NONE);
    key(INPUT_KEY_BACK);
    CHECK_EQ(t.state, TUI_ST_CONFIRM);
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_LOGOUT);

    start(empty, 0);
    CHECK_EQ(t.level[0].menu.hidden, 1);
    CHECK_STR(t.msg, "No menu entry for this user in TERMINAL.CFG");
    CHECK_STR(dump(),
              "00 N NORTH YARD\n"
              "01 N Nothing\n"
              "02 D " RULE "\n"
              "04 D (no entries)\n"
              "27 N No menu entry for this user in TERMINAL.CFG\n"
              "28 D Nothing\n"
              "29 D BACK: log out\n");
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_NONE);
    CHECK_EQ(key(INPUT_KEY_UP), TUI_ACT_NONE);

    start(bad, 0);
    CHECK_EQ(t.depth, 0);
    CHECK_STR(t.msg, "Bad menu file TERMINAL.CFG (line 3)");
}

TEST(tui_empty_why_cases)
{
    static const file_t allbad[] = { { "TERMINAL.CFG", "[entry 1]\nlabel=A\ntype=nope\n" }, { NULL, NULL } };
    static const file_t noent[] = { { "TERMINAL.CFG", "[terminal]\ntitle = X\n" }, { NULL, NULL } };

    CHECK_EQ(tui_empty_why(1, 0, 0), TUI_EMPTY_NONE);
    CHECK_EQ(tui_empty_why(1, 2, 3), TUI_EMPTY_NONE);
    CHECK_EQ(tui_empty_why(0, 1, 0), TUI_EMPTY_USER);
    CHECK_EQ(tui_empty_why(0, 1, 2), TUI_EMPTY_USER);
    CHECK_EQ(tui_empty_why(0, 0, 1), TUI_EMPTY_ERRORS);
    CHECK_EQ(tui_empty_why(0, 0, 0), TUI_EMPTY_FILE);

    start(allbad, 0);
    CHECK_STR(t.msg, "TERMINAL.CFG: 1 errors, first: bad or missing type (line 3)");
    start(noent, 0);
    CHECK_STR(t.msg, "No menu entry in TERMINAL.CFG");
}

TEST(tui_file_errors_reported)
{
    static const file_t errs[] = {
        { "TERMINAL.CFG", "[entry 1]\nlabel=A\ntype=logout\n[entry 2]\nlabel=B\ntype=nope\n[entry 3]\nlabel=C\n" },
        { NULL, NULL },
    };

    start(errs, 0);
    CHECK_STR(t.msg, "TERMINAL.CFG: 2 errors, first: bad or missing type (line 6)");
    /* No title: the file name stands in. */
    CHECK(strstr(dump(), "01 N TERMINAL.CFG\n") != NULL);
}

TEST(tui_missing_header)
{
    static const file_t nohead[] = {
        { "TERMINAL.CFG", "[terminal]\ntitle=T\nheader=GONE.TXT\n[entry 1]\nlabel=A\ntype=logout\n" },
        { NULL, NULL },
    };

    start(nohead, 0);
    CHECK_EQ(t.depth, 1);
    CHECK_EQ(t.level[0].nhead, 0);
    CHECK_STR(t.msg, "File not found: GONE.TXT");
    CHECK_EQ(t.state, TUI_ST_MENU);
}

TEST(tui_header_cut)
{
    static char head[200];
    static const file_t fs[] = {
        { "TERMINAL.CFG", "[terminal]\ntitle=T\nheader=H.TXT\n" },
        { "H.TXT", head },
        { NULL, NULL },
    };

    memset(head, 'a', 100);
    strcpy(head + 100, "\nx\x7fy\rz");
    start(fs, 0);
    CHECK_EQ(t.level[0].nhead, 3);
    CHECK_EQ(strlen(t.level[0].head[0]), TUI_TEXT_COLS);
    CHECK_STR(t.level[0].head[1], "x?y");
    CHECK_STR(t.level[0].head[2], "z");
}

TEST(tui_state_errors)
{
    tui_line_t few[3];

    CHECK_EQ(tui_init(NULL, 0, "X", "TERMINAL.CFG"), TUI_E_ARG);
    CHECK_EQ(tui_init(&t, 0, "X", NULL), TUI_E_ARG);
    CHECK_EQ(tui_init(&t, 0, "X", "../A.CFG"), TUI_E_ARG);
    CHECK_EQ(tui_key(NULL, 1), TUI_E_ARG);
    CHECK_EQ(tui_event(&t, NULL), TUI_E_ARG);
    CHECK_EQ(tui_give_menu(NULL, "", 0), TUI_E_ARG);
    CHECK_EQ(tui_give_header(NULL, "", 0), TUI_E_ARG);
    CHECK_EQ(tui_give_error(NULL, ""), TUI_E_ARG);
    CHECK_EQ(tui_done(NULL, ""), TUI_E_ARG);
    CHECK_EQ(tui_layout(NULL, few, 3), TUI_E_ARG);

    CHECK_EQ(tui_init(&t, 0, NULL, "TERMINAL.CFG"), TUI_ACT_LOAD_MENU);
    CHECK_STR(t.sysname, "UOS");
    CHECK_EQ(tui_done(&t, NULL), TUI_E_STATE);           /* a load is not done with tui_done */
    CHECK_EQ(tui_give_header(&t, "", 0), TUI_E_STATE);
    CHECK_EQ(tui_give_menu(&t, "[entry 1]\nlabel=A\ntype=logout\n", 29), TUI_ACT_NONE);
    CHECK_EQ(tui_give_menu(&t, "", 0), TUI_E_STATE);
    CHECK_EQ(tui_give_error(&t, "x"), TUI_E_STATE);
    CHECK_EQ(tui_done(&t, "x"), TUI_E_STATE);
    CHECK_EQ(tui_layout(&t, few, 3), TUI_E_SPACE);
    CHECK(tui_layout(&t, few, 0) == TUI_E_SPACE);
    CHECK_EQ(tui_strerror(TUI_E_STATE)[0], 'n');
    /* A long system name is cut. */
    tui_init(&t, 0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", "TERMINAL.CFG");
    CHECK_EQ(strlen(t.sysname), TUI_SYSNAME_MAX);
}

TEST(tui_events)
{
    input_event_t ev;

    start(basic, 0);
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_KEY;
    ev.code = INPUT_KEY_DOWN;
    ev.value = INPUT_PRESS;
    CHECK_EQ(tui_event(&t, &ev), TUI_ACT_NONE);
    CHECK_EQ(t.level[0].sel, 1);
    ev.value = INPUT_REPEAT;
    tui_event(&t, &ev);
    CHECK_EQ(t.level[0].sel, 2);
    ev.value = INPUT_RELEASE;
    tui_event(&t, &ev);
    CHECK_EQ(t.level[0].sel, 2);
    ev.type = INPUT_EV_CHAR;
    ev.code = '6';
    tui_event(&t, &ev);
    CHECK_EQ(t.level[0].sel, 5);
    ev.code = 0x1234;
    tui_event(&t, &ev);
    ev.type = INPUT_EV_DIAL;
    ev.code = 0;
    ev.value = 1;
    CHECK_EQ(tui_event(&t, &ev), TUI_ACT_NONE);
    CHECK_EQ(t.level[0].sel, 5);
}

TEST(tui_hidden_by_privs)
{
    static const file_t fs[] = {
        { "TERMINAL.CFG", "[entry 1]\nlabel=All\ntype=logout\n[entry 2]\nlabel=Oper\ntype=logout\nprivs=2\n" },
        { NULL, NULL },
    };

    start(fs, 0);
    CHECK_EQ(t.level[0].menu.count, 1);
    start(fs, 2);
    CHECK_EQ(t.level[0].menu.count, 2);
}

TEST(tui_long_breadcrumb_fits)
{
    static char cfg[256];
    static const file_t fs[] = { { "TERMINAL.CFG", cfg }, { NULL, NULL } };
    tui_line_t lines[TUI_LINES_MAX];
    int n;
    int i;

    snprintf(cfg, sizeof(cfg),
             "[terminal]\ntitle = %s\n[entry 1]\nlabel=x\ntype=menu\ntarget=TERMINAL.CFG\n",
             "A title of exactly sixty characters for the trail test 0123");
    start(fs, 0);
    for (i = 0; i < 7; i++) {
        key(INPUT_KEY_SELECT);
    }
    n = tui_layout(&t, lines, TUI_LINES_MAX);
    CHECK(n > 0);
    for (i = 0; i < n; i++) {
        if (lines[i].row == TUI_ROW_CRUMB) {
            CHECK_EQ(strlen(lines[i].text), TUI_TEXT_COLS);
            CHECK(strncmp(lines[i].text, "...", 3) == 0);
            CHECK_STR(lines[i].text, "...ail test 0123 / A title of exactly sixty characters for the trail test 0123");
        }
        if (lines[i].row == TUI_ROW_FOOTER) {
            CHECK(strlen(lines[i].text) <= TUI_HINT_MAX);
        }
    }
}

TEST(tui_close_mode)
{
    start(basic, 0);
    CHECK_EQ(tui_set_close(NULL), TUI_E_ARG);
    CHECK_EQ(tui_set_close(&t), TUI_OK);
    CHECK(strstr(dump(), "29 D UP/DOWN: move  1-9: pick  ENTER: choose  BACK: close\n") != NULL);
    /* A sub-menu still goes up a level. */
    key(TUI_CHAR('2'));
    key(INPUT_KEY_SELECT);
    CHECK_EQ(t.depth, 2);
    CHECK(strstr(dump(), "BACK: up a level\n") != NULL);
    CHECK_EQ(key(INPUT_KEY_BACK), TUI_ACT_NONE);
    CHECK_EQ(t.depth, 1);
    CHECK_EQ(key(INPUT_KEY_BACK), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_CONFIRM);
    CHECK(strstr(dump(), "27 N Close the menu? ENTER: yes  BACK: no\n") != NULL);
    CHECK(strstr(dump(), "29 D ENTER: close  BACK: stay\n") != NULL);
    CHECK_EQ(key(INPUT_KEY_BACK), TUI_ACT_NONE);
    CHECK_EQ(t.state, TUI_ST_MENU);
    key(INPUT_KEY_BACK);
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_CLOSE);
    CHECK_EQ(t.state, TUI_ST_DONE);
    CHECK(strstr(dump(), "29 D Closed\n") != NULL);
    /* A logout entry still logs out in close mode. */
    start(basic, 0);
    tui_set_close(&t);
    key(TUI_CHAR('8'));
    CHECK_EQ(key(INPUT_KEY_SELECT), TUI_ACT_LOGOUT);
    /* An empty root in close mode says close. */
    {
        static const file_t empty[] = { { "TERMINAL.CFG", "[terminal]\ntitle = E\n" },
                                        { NULL, NULL } };

        start(empty, 0);
        tui_set_close(&t);
        CHECK(strstr(dump(), "29 D BACK: close\n") != NULL);
    }
}

TEST(tui_held_key_rule)
{
    unsigned k;

    /* Presses pass. */
    for (k = INPUT_KEY_UP; k < INPUT_KEY_COUNT; k++) {
        CHECK_EQ(tui_map_key(0, k, 0), k);
    }
    /* Only UP and DOWN repeat. */
    CHECK_EQ(tui_map_key(0, INPUT_KEY_UP, 1), INPUT_KEY_UP);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_DOWN, 1), INPUT_KEY_DOWN);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_SELECT, 1), 0);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_BACK, 1), 0);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_LEFT, 1), 0);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_DIAL_PUSH, 1), 0);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_NONE, 0), 0);
    CHECK_EQ(tui_map_key(0, INPUT_KEY_COUNT, 0), 0);
    /* Characters. */
    CHECK_EQ(tui_map_key(1, '1', 0), TUI_CHAR('1'));
    CHECK_EQ(tui_map_key(1, '9', 0), TUI_CHAR('9'));
    CHECK_EQ(tui_map_key(1, '0', 0), 0);
    CHECK_EQ(tui_map_key(1, 'a', 0), 0);
    CHECK_EQ(tui_map_key(1, 3, 0), 0);
    CHECK_EQ(tui_map_key(1, '\r', 0), INPUT_KEY_SELECT);
    CHECK_EQ(tui_map_key(1, '\n', 0), INPUT_KEY_SELECT);
}

TEST(tui_names)
{
    CHECK_STR(tui_act_name(TUI_ACT_NONE), "none");
    CHECK_STR(tui_act_name(TUI_ACT_LOAD_MENU), "menu");
    CHECK_STR(tui_act_name(TUI_ACT_LOAD_HEADER), "header");
    CHECK_STR(tui_act_name(TUI_ACT_SHOW_TEXT), "text");
    CHECK_STR(tui_act_name(TUI_ACT_COMMAND), "command");
    CHECK_STR(tui_act_name(TUI_ACT_DEVICE), "device");
    CHECK_STR(tui_act_name(TUI_ACT_HOLOTAPE), "holotape");
    CHECK_STR(tui_act_name(TUI_ACT_LOGOUT), "logout");
    CHECK_STR(tui_act_name(TUI_ACT_CLOSE), "close");
    CHECK_STR(tui_act_name(-1), "?");
    CHECK_STR(tui_act_name(TUI_ACT_CLOSE + 1), "?");
    CHECK_STR(tui_state_name(TUI_ST_WAIT), "wait");
    CHECK_STR(tui_state_name(TUI_ST_MENU), "menu");
    CHECK_STR(tui_state_name(TUI_ST_CONFIRM), "confirm");
    CHECK_STR(tui_state_name(TUI_ST_DONE), "done");
    CHECK_STR(tui_state_name(9), "?");
}

/* ---- The choice at login ---- */

/* Every row of the table for one privilege mask. `admin`: SYSTEM or
 * SECURITY, which opens the shell for a refused file. */
static void login_rows(uint32_t privs, int admin)
{
    int why = -1;

    /* No file: the shell, nothing refused. */
    CHECK_EQ(tui_login_choice(0, 0, TUI_OK, 0, privs, &why), TUI_OPEN_SHELL);
    CHECK_EQ(why, TUI_WHY_NONE);
    /* A good file: the menu. */
    CHECK_EQ(tui_login_choice(1, 1, TUI_OK, 3, privs, &why), TUI_OPEN_MENU);
    CHECK_EQ(why, TUI_WHY_NONE);
    /* A file that cannot be read. */
    CHECK_EQ(tui_login_choice(1, 0, TUI_OK, 0, privs, &why),
             admin ? TUI_OPEN_SHELL : TUI_OPEN_MENU);
    CHECK_EQ(why, TUI_WHY_READ);
    /* A file that does not parse. */
    CHECK_EQ(tui_login_choice(1, 1, TUI_E_SYNTAX, 0, privs, &why),
             admin ? TUI_OPEN_SHELL : TUI_OPEN_MENU);
    CHECK_EQ(why, TUI_WHY_SYNTAX);
    /* A file with no entry this user can see. */
    CHECK_EQ(tui_login_choice(1, 1, TUI_OK, 0, privs, &why),
             admin ? TUI_OPEN_SHELL : TUI_OPEN_MENU);
    CHECK_EQ(why, TUI_WHY_EMPTY);
}

TEST(login_choice_no_privilege)
{
    login_rows(0, 0);
}

TEST(login_choice_oper)
{
    login_rows(UOS_PRIV_OPER, 0);
    login_rows(UOS_PRIV_OPER | UOS_PRIV_READALL, 0);
}

TEST(login_choice_security)
{
    login_rows(UOS_PRIV_SECURITY, 1);
}

TEST(login_choice_system)
{
    login_rows(UOS_PRIV_SYSTEM, 1);
    login_rows(UOS_PRIV_ALL, 1);
}

TEST(login_choice_why_may_be_null)
{
    CHECK_EQ(tui_login_choice(1, 1, TUI_E_SYNTAX, 0, 0, NULL), TUI_OPEN_MENU);
    CHECK_STR(tui_why_name(TUI_WHY_NONE), "none");
    CHECK_STR(tui_why_name(TUI_WHY_READ), "read");
    CHECK_STR(tui_why_name(TUI_WHY_SYNTAX), "syntax");
    CHECK_STR(tui_why_name(TUI_WHY_EMPTY), "empty");
    CHECK_STR(tui_why_name(-1), "?");
    CHECK_STR(tui_why_name(TUI_WHY_EMPTY + 1), "?");
}
