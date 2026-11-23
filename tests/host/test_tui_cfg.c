/* Host tests of src/tui/tuicfg.c: the TERMINAL.CFG parser. */

#include "rc_test.h"
#include "tui.h"

#ifndef TUI_CARD_HOME
#define TUI_CARD_HOME "card/UOS/HOME"
#endif

static tui_menu_t m;

static int parse(const char *s, uint32_t privs)
{
    return tui_cfg_parse(s, strlen(s), privs, &m, NULL);
}

/* A menu with one entry of the given fields, any of them NULL to leave it out. */
static int one(const char *type, const char *target)
{
    char buf[512];

    snprintf(buf, sizeof(buf), "[terminal]\ntitle = T\n[entry 1]\nlabel = L\n%s%s%s%s%s%s",
             type ? "type = " : "", type ? type : "", type ? "\n" : "",
             target ? "target = " : "", target ? target : "", target ? "\n" : "");
    return parse(buf, 0);
}

TEST(cfg_every_type_good)
{
    static const struct {
        const char *type;
        const char *target;
        int t;
        const char *stored;
    } good[] = {
        { "text", "duty.txt", TUI_T_TEXT, "DUTY.TXT" },
        { "text", "README", TUI_T_TEXT, "README" },
        { "text", "A$_-1234.X", TUI_T_TEXT, "A$_-1234.X" },
        { "menu", "maint.cfg", TUI_T_MENU, "MAINT.CFG" },
        { "command", "SHOW TIME", TUI_T_COMMAND, "SHOW TIME" },
        { "command", "type \"a b\"", TUI_T_COMMAND, "type \"a b\"" },
        { "device", "door1 toggle", TUI_T_DEVICE, "DOOR1 TOGGLE" },
        { "device", "LIGHT_2   ON", TUI_T_DEVICE, "LIGHT_2 ON" },
        { "holotape", NULL, TUI_T_HOLOTAPE, "" },
        { "holotape", "safety.htp", TUI_T_HOLOTAPE, "SAFETY.HTP" },
        { "logout", NULL, TUI_T_LOGOUT, "" },
    };
    unsigned i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        CHECK_EQ(one(good[i].type, good[i].target), TUI_OK);
        CHECK_EQ(m.errors, 0);
        CHECK_EQ(m.count, 1);
        CHECK_EQ(m.entry[0].type, good[i].t);
        CHECK_STR(m.entry[0].target, good[i].stored);
        CHECK_STR(m.entry[0].label, "L");
        CHECK_STR(tui_type_name(good[i].t), good[i].type);
    }
}

TEST(cfg_every_bad_target)
{
    static const struct {
        const char *type;
        const char *target;
        int code;
    } bad[] = {
        { "text", NULL, TUI_E_TARGET },
        { "text", "[.MAIL]A.TXT", TUI_E_TARGET },
        { "text", "../X.TXT", TUI_E_TARGET },
        { "text", "SUB/X.TXT", TUI_E_TARGET },
        { "text", "NINECHARS.TXT", TUI_E_TARGET },
        { "text", "A.TEXT", TUI_E_TARGET },
        { "text", ".TXT", TUI_E_TARGET },
        { "text", "A.B.C", TUI_E_TARGET },
        { "text", "SYS$SYSTEM:A.TXT", TUI_E_TARGET },
        { "menu", "MAINT.TXT", TUI_E_TARGET },
        { "menu", "MAINT", TUI_E_TARGET },
        { "menu", "/UOS/X.CFG", TUI_E_TARGET },
        { "command", NULL, TUI_E_TARGET },
        { "device", "DOOR1", TUI_E_TARGET },
        { "device", "DOOR1 OPEN NOW", TUI_E_TARGET },
        { "device", "DOOR1 OPEN2", TUI_E_TARGET },
        { "device", "DOOR-1 OPEN", TUI_E_TARGET },
        { "device", "ABCDEFGHIJKLMNOP OPEN", TUI_E_TARGET },
        { "device", "DOOR1 ABCDEFGHIJKLMNOP", TUI_E_TARGET },
        { "holotape", "A/B.HTP", TUI_E_TARGET },
        { "logout", "NOW", TUI_E_TARGET },
        { NULL, "X.TXT", TUI_E_TYPE },
        { "file", "X.TXT", TUI_E_TYPE },
        { "TEXT", "X.TXT", TUI_E_TYPE },
    };
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(one(bad[i].type, bad[i].target), TUI_OK);
        CHECK_EQ(m.count, 0);
        CHECK_EQ(m.errors, 1);
        CHECK_EQ(m.err[0].code, bad[i].code);
        CHECK_EQ(m.err[0].entry, 1);
        if (m.err[0].code != bad[i].code) {
            printf("  case %u\n", i);
        }
    }
}

TEST(cfg_device_limits)
{
    CHECK_EQ(one("device", "ABCDEFGHIJKLMNO ABCDEFGHIJKLMNO"), TUI_OK);
    CHECK_EQ(m.count, 1);
    CHECK_STR(m.entry[0].target, "ABCDEFGHIJKLMNO ABCDEFGHIJKLMNO");
}

TEST(cfg_command_limit)
{
    char cmd[128];

    memset(cmd, 'A', 127);
    cmd[127] = 0;
    CHECK_EQ(one("command", cmd), TUI_OK);
    CHECK_EQ(m.count, 1);
    CHECK_EQ(strlen(m.entry[0].target), 127);
    /* 128 is over the INI value limit: the whole file fails. */
    {
        char buf[300];

        snprintf(buf, sizeof(buf), "[entry 1]\nlabel=L\ntype=command\ntarget=%sB\n", cmd);
        CHECK_EQ(parse(buf, 0), TUI_E_SYNTAX);
        CHECK_EQ(m.count, 0);
    }
}

TEST(cfg_label_and_title)
{
    char buf[256];
    char s61[62];

    memset(s61, 'x', 61);
    s61[61] = 0;
    /* 60 is fine, 61 is not. */
    snprintf(buf, sizeof(buf), "[terminal]\ntitle=%.60s\n[entry 1]\nlabel=%.60s\ntype=logout\n", s61, s61);
    CHECK_EQ(parse(buf, 0), TUI_OK);
    CHECK_EQ(m.errors, 0);
    CHECK_EQ(strlen(m.title), 60);
    CHECK_EQ(strlen(m.entry[0].label), 60);

    snprintf(buf, sizeof(buf), "[terminal]\ntitle=%s\n[entry 1]\nlabel=%s\ntype=logout\n", s61, s61);
    CHECK_EQ(parse(buf, 0), TUI_OK);
    CHECK_EQ(m.errors, 2);
    CHECK_EQ(m.err[0].code, TUI_E_TITLE);
    CHECK_EQ(m.err[0].line, 2);
    CHECK_EQ(m.err[1].code, TUI_E_LABEL);
    CHECK_EQ(m.err[1].line, 4);
    CHECK_STR(m.title, "");
    CHECK_EQ(m.count, 0);

    CHECK_EQ(parse("[entry 1]\nlabel=\"\"\ntype=logout\n", 0), TUI_OK);
    CHECK_EQ(m.err[0].code, TUI_E_LABEL);
    CHECK_EQ(parse("[entry 1]\nlabel=\"a\\nb\"\ntype=logout\n", 0), TUI_OK);
    CHECK_EQ(m.err[0].code, TUI_E_LABEL);
    CHECK_EQ(parse("[entry 1]\ntype=logout\n", 0), TUI_OK);
    CHECK_EQ(m.err[0].code, TUI_E_LABEL);
    CHECK_EQ(m.err[0].line, 2);
    CHECK_EQ(m.count, 0);
}

TEST(cfg_header)
{
    CHECK_EQ(parse("[terminal]\nheader = welcome.txt\n", 0), TUI_OK);
    CHECK_STR(m.header, "WELCOME.TXT");
    CHECK_EQ(parse("[terminal]\nheader =\n", 0), TUI_OK);
    CHECK_STR(m.header, "");
    CHECK_EQ(m.errors, 0);
    CHECK_EQ(parse("[terminal]\nheader = [-]X.TXT\n", 0), TUI_OK);
    CHECK_STR(m.header, "");
    CHECK_EQ(m.err[0].code, TUI_E_HEADER);
}

TEST(cfg_privs)
{
    CHECK_EQ(parse("[entry 1]\nlabel=A\ntype=logout\nprivs=0x6\n", 0x6), TUI_OK);
    CHECK_EQ(m.count, 1);
    CHECK_EQ(m.entry[0].privs, 6);
    CHECK_EQ(parse("[entry 1]\nlabel=A\ntype=logout\nprivs=4294967295\n", 1), TUI_OK);
    CHECK_EQ(m.count, 1);
    CHECK_EQ(m.entry[0].privs, 0xffffffffu);
    static const char *const bad[] = { "4294967296", "-1", "0x", "0x123456789", "12a", "" };
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char buf[128];

        snprintf(buf, sizeof(buf), "[entry 1]\nlabel=A\ntype=logout\nprivs=%s\n", bad[i]);
        CHECK_EQ(parse(buf, 0xffffffffu), TUI_OK);
        CHECK_EQ(m.count, 0);
        CHECK_EQ(m.err[0].code, TUI_E_PRIVS);
        CHECK_EQ(m.err[0].line, 4);
    }
}

TEST(cfg_hidden_entries)
{
    const char *s =
        "[entry 1]\nlabel=Open\ntype=logout\n"
        "[entry 2]\nlabel=Oper\ntype=logout\nprivs=2\n"
        "[entry 3]\nlabel=Oper+Sec\ntype=logout\nprivs=6\n";

    CHECK_EQ(parse(s, 0), TUI_OK);
    CHECK_EQ(m.count, 1);
    CHECK_EQ(m.hidden, 2);
    CHECK_EQ(m.errors, 0);
    CHECK_EQ(parse(s, 2), TUI_OK);
    CHECK_EQ(m.count, 2);
    CHECK_EQ(m.hidden, 1);
    CHECK_STR(m.entry[1].label, "Oper");
    CHECK_EQ(parse(s, 6), TUI_OK);
    CHECK_EQ(m.count, 3);
    CHECK_EQ(parse(s, TUI_PRIV_SYSTEM), TUI_OK);
    CHECK_EQ(m.count, 3);
    CHECK_EQ(m.hidden, 0);
}

TEST(cfg_order_gaps_duplicates)
{
    const char *s =
        "[entry 30]\nlabel=Thirty\ntype=logout\n"
        "[entry 2]\nlabel=Two\ntype=logout\n"
        "[entry 999]\nlabel=Last\ntype=logout\n"
        "[entry 2]\nlabel=Again\ntype=logout\n"
        "[entry 7]\nlabel=Seven\ntype=logout\n";

    CHECK_EQ(parse(s, 0), TUI_OK);
    CHECK_EQ(m.count, 4);
    CHECK_STR(m.entry[0].label, "Two");
    CHECK_STR(m.entry[1].label, "Seven");
    CHECK_STR(m.entry[2].label, "Thirty");
    CHECK_STR(m.entry[3].label, "Last");
    CHECK_EQ(m.errors, 1);
    CHECK_EQ(m.err[0].code, TUI_E_DUP_ENTRY);
    CHECK_EQ(m.err[0].entry, 2);
    CHECK_EQ(m.err[0].line, 11);
}

TEST(cfg_entry_numbers)
{
    static const char *const bad[] = { "entry 0", "entry 1000", "entry 01", "entry x", "entry 1x", "entry -1" };
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char buf[128];

        snprintf(buf, sizeof(buf), "[%s]\nlabel=A\ntype=logout\n", bad[i]);
        CHECK_EQ(parse(buf, 0), TUI_OK);
        CHECK_EQ(m.count, 0);
        CHECK_EQ(m.errors, 1);
        CHECK_EQ(m.err[0].code, TUI_E_ENTRY_NUM);
    }
    CHECK_EQ(parse("[entry\t5]\nlabel=A\ntype=logout\n", 0), TUI_OK);
    CHECK_EQ(m.count, 1);
    CHECK_EQ(m.entry[0].num, 5);
}

TEST(cfg_seventeen_entries)
{
    char buf[2048];
    size_t n = 0;
    int i;

    for (i = 1; i <= 17; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "[entry %d]\nlabel=E%d\ntype=logout\n", i, i);
    }
    CHECK_EQ(parse(buf, 0), TUI_OK);
    CHECK_EQ(m.count, 16);
    CHECK_EQ(m.errors, 1);
    CHECK_EQ(m.err[0].code, TUI_E_TOO_MANY);
    CHECK_EQ(m.err[0].entry, 17);
    CHECK_STR(m.entry[15].label, "E16");
    /* Hidden entries count toward the 16. */
    CHECK_EQ(tui_cfg_parse(buf, n, 0, &m, NULL), TUI_OK);
    n = 0;
    for (i = 1; i <= 17; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "[entry %d]\nlabel=E%d\ntype=logout\nprivs=%d\n",
                              i, i, i == 1 ? 2 : 0);
    }
    CHECK_EQ(parse(buf, 0), TUI_OK);
    CHECK_EQ(m.count, 15);
    CHECK_EQ(m.hidden, 1);
    CHECK_EQ(m.err[0].code, TUI_E_TOO_MANY);
}

TEST(cfg_sections_and_keys)
{
    const char *s =
        "stray = 1\n"                       /* 1: before any section */
        "[terminal]\n"
        "title = A\n"
        "title = B\n"                       /* 4: dup key, first wins */
        "colour = green\n"                  /* 5: unknown key */
        "[other]\n"
        "x = 1\n"                           /* 7: unknown section */
        "[entry 1]\n"
        "label = One\n"
        "label = Uno\n"                     /* 10: dup key, kept */
        "type = logout\n"
        "[entry 2]\n"
        "label = Two\n"
        "type = logout\n"
        "speed = 3\n";                      /* 15: unknown key, entry skipped */

    CHECK_EQ(parse(s, 0), TUI_OK);
    CHECK_STR(m.title, "A");
    CHECK_EQ(m.count, 1);
    CHECK_STR(m.entry[0].label, "One");
    CHECK_EQ(m.errors, 6);
    CHECK_EQ(m.err[0].code, TUI_E_SECTION);
    CHECK_EQ(m.err[0].line, 1);
    CHECK_EQ(m.err[1].code, TUI_E_DUP_KEY);
    CHECK_EQ(m.err[1].line, 4);
    CHECK_EQ(m.err[2].code, TUI_E_KEY);
    CHECK_EQ(m.err[3].code, TUI_E_SECTION);
    CHECK_EQ(m.err[3].line, 7);
    CHECK_EQ(m.err[4].code, TUI_E_DUP_KEY);
    CHECK_EQ(m.err[4].line, 10);
    CHECK_EQ(m.err[4].entry, 1);
    CHECK_EQ(m.err[5].code, TUI_E_KEY);
    CHECK_EQ(m.err[5].line, 15);
    CHECK_EQ(m.err[5].entry, 2);
}

TEST(cfg_error_log_overflow)
{
    char buf[1024];
    size_t n = 0;
    int i;

    for (i = 1; i <= 12; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "[entry %d]\nlabel=E\ntype=bad\n", i);
    }
    CHECK_EQ(parse(buf, 0), TUI_OK);
    CHECK_EQ(m.errors, 12);
    CHECK_EQ(m.err[TUI_CFG_ERR_LOG - 1].entry, TUI_CFG_ERR_LOG);
}

TEST(cfg_syntax_and_args)
{
    unsigned line = 9;

    CHECK_EQ(tui_cfg_parse("[terminal]\ntitle=A\nnot a key line\n", 31, 0, &m, &line), TUI_E_SYNTAX);
    CHECK_EQ(line, 3);
    CHECK_EQ(m.count, 0);
    CHECK_STR(m.title, "");
    CHECK_EQ(tui_cfg_parse(NULL, 1, 0, &m, NULL), TUI_E_ARG);
    CHECK_EQ(tui_cfg_parse("", 0, 0, NULL, NULL), TUI_E_ARG);
    CHECK_EQ(tui_cfg_parse(NULL, 0, 0, &m, &line), TUI_OK);
    CHECK_EQ(line, 0);
    CHECK_EQ(m.count, 0);
    /* CR LF line ends and comments. */
    CHECK_EQ(parse("[terminal]\r\ntitle = A b ; note\r\n[entry 1]\r\nlabel=X\r\ntype=logout\r\n", 0), TUI_OK);
    CHECK_STR(m.title, "A b");
    CHECK_EQ(m.count, 1);
}

TEST(cfg_is_83)
{
    CHECK(tui_is_83("A"));
    CHECK(tui_is_83("ABCDEFGH.TXT"));
    CHECK(tui_is_83("abc."));
    CHECK(!tui_is_83(""));
    CHECK(!tui_is_83(NULL));
    CHECK(!tui_is_83("ABCDEFGHI"));
    CHECK(!tui_is_83("A.TXTX"));
    CHECK(!tui_is_83("A B"));
    CHECK(!tui_is_83("A:B"));
}

TEST(cfg_strerror)
{
    int c;

    CHECK_STR(tui_strerror(TUI_OK), "ok");
    CHECK_STR(tui_strerror(TUI_E_TOO_MANY), "more than 16 entries");
    CHECK_STR(tui_strerror(TUI_E_SPACE), "buffer too small");
    CHECK_STR(tui_strerror(-TUI_E_COUNT - 1), "unknown error");
    CHECK_STR(tui_strerror(1), "unknown error");
    for (c = 0; c >= -TUI_E_COUNT; c--) {
        CHECK(strcmp(tui_strerror(c), "unknown error") != 0);
    }
    CHECK_STR(tui_type_name(0), "?");
    CHECK_STR(tui_type_name(7), "?");
}

/* ---- The shipped card content ---- */

static size_t read_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        printf("  cannot open %s\n", path);
        return (size_t)-1;
    }
    n = fread(buf, 1, size, f);
    fclose(f);
    return n;
}

static void check_menu(const char *home, const char *file, uint32_t privs, int depth)
{
    static char buf[8192];
    char path[256];
    tui_menu_t mm;
    size_t n;
    unsigned i;

    snprintf(path, sizeof(path), "%s/%s/%s", TUI_CARD_HOME, home, file);
    n = read_file(path, buf, sizeof(buf));
    CHECK(n != (size_t)-1 && n < sizeof(buf));
    if (n == (size_t)-1) {
        return;
    }
    CHECK_EQ(tui_cfg_parse(buf, n, TUI_PRIV_SYSTEM, &mm, NULL), TUI_OK);
    CHECK_EQ(mm.errors, 0);
    if (mm.errors) {
        printf("  %s: %s line %u\n", path, tui_strerror(mm.err[0].code), mm.err[0].line);
    }
    CHECK(mm.count > 0);
    CHECK(mm.title[0] != 0);
    if (mm.header[0]) {
        char hp[256];
        static char hb[2048];

        snprintf(hp, sizeof(hp), "%s/%s/%s", TUI_CARD_HOME, home, mm.header);
        CHECK(read_file(hp, hb, sizeof(hb)) != (size_t)-1);
    }
    (void)privs;
    for (i = 0; i < mm.count; i++) {
        const tui_entry_t *e = &mm.entry[i];
        char tp[256];
        static char tb[8192];

        if (e->type == TUI_T_TEXT) {
            snprintf(tp, sizeof(tp), "%s/%s/%s", TUI_CARD_HOME, home, e->target);
            CHECK(read_file(tp, tb, sizeof(tb)) != (size_t)-1);
        } else if (e->type == TUI_T_MENU) {
            CHECK(depth < 3);
            if (depth < 3) {
                check_menu(home, e->target, privs, depth + 1);
            }
        }
    }
}

TEST(card_content_parses)
{
    check_menu("ADMIN", "TERMINAL.CFG", TUI_PRIV_SYSTEM, 0);
    check_menu("OPER", "TERMINAL.CFG", 2, 0);
    check_menu("GUEST", "TERMINAL.CFG", 0, 0);
}

TEST(card_guest_sees_no_priv_entries)
{
    static char buf[4096];
    char path[256];
    size_t n;

    snprintf(path, sizeof(path), "%s/ADMIN/MAINT.CFG", TUI_CARD_HOME);
    n = read_file(path, buf, sizeof(buf));
    CHECK(n != (size_t)-1);
    if (n == (size_t)-1) {
        return;
    }
    CHECK_EQ(tui_cfg_parse(buf, n, 0, &m, NULL), TUI_OK);
    CHECK_EQ(m.count, 2);
    CHECK_EQ(m.hidden, 3);
}
