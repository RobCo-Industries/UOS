/* Host tests of src/kernel/maintreg.c: the maintenance command table and
 * the word splitting. */

#include "rc_test.h"

#include "uos_maint.h"

static int f_a(int argc, char **argv) { (void)argc; (void)argv; return 1; }
static int f_b(int argc, char **argv) { (void)argc; (void)argv; return 2; }

TEST(add_and_find)
{
    uos_maint_table_t t;

    uos_mt_init(&t);
    CHECK_EQ(uos_mt_add(&t, "state", f_a, "the state"), 0);
    CHECK_EQ(uos_mt_add(&t, "dump", f_b, NULL), 0);
    CHECK_EQ(t.count, 2u);
    CHECK(uos_mt_find(&t, "state")->fn == f_a);
    CHECK_STR(uos_mt_find(&t, "state")->help, "the state");
    CHECK(uos_mt_find(&t, "dump")->fn == f_b);
    CHECK_STR(uos_mt_find(&t, "dump")->help, "");
    CHECK(uos_mt_find(&t, "dum") == NULL);
    CHECK(uos_mt_find(&t, "dumps") == NULL);
    CHECK(uos_mt_find(&t, NULL) == NULL);
}

TEST(duplicates_are_refused)
{
    uos_maint_table_t t;

    uos_mt_init(&t);
    CHECK_EQ(uos_mt_add(&t, "state", f_a, "one"), 0);
    CHECK_EQ(uos_mt_add(&t, "state", f_b, "two"), UOS_MAINT_ERR_DUP);
    CHECK_EQ(t.count, 1u);
    CHECK(uos_mt_find(&t, "state")->fn == f_a);
}

TEST(library_builtins_are_refused)
{
    static const char *const names[] = { "key", "char", "text", "dial", "wait", "stats" };
    uos_maint_table_t t;
    unsigned i;

    uos_mt_init(&t);
    for (i = 0; i < 6; i++) {
        CHECK_EQ(uos_mt_add(&t, names[i], f_a, NULL), UOS_MAINT_ERR_DUP);
    }
    CHECK_EQ(t.count, 0u);
    CHECK_EQ(uos_mt_add(&t, "keys", f_a, NULL), 0);
}

TEST(name_rules)
{
    uos_maint_table_t t;

    uos_mt_init(&t);
    CHECK_EQ(uos_mt_add(&t, "", f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, NULL, f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, "abcdefghijklm", f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, "State", f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, "dump2", f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, "a_b", f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, "a b", f_a, NULL), UOS_MAINT_ERR_NAME);
    CHECK_EQ(uos_mt_add(&t, "ok", NULL, NULL), UOS_MAINT_ERR_ARG);
    CHECK_EQ(t.count, 0u);
    CHECK_EQ(uos_mt_add(&t, "abcdefghijkl", f_a, NULL), 0);
    CHECK_EQ(uos_mt_add(&t, "a", f_a, NULL), 0);
    CHECK(uos_mt_find(&t, "abcdefghijkl") != NULL);
}

TEST(sorted_by_name)
{
    static const char *const in[] = { "timers", "dump", "help", "uptime", "state", "bench" };
    static const char *const out[] = { "bench", "dump", "help", "state", "timers", "uptime" };
    uos_maint_table_t t;
    unsigned i;

    uos_mt_init(&t);
    for (i = 0; i < 6; i++) {
        CHECK_EQ(uos_mt_add(&t, in[i], f_a, in[i]), 0);
    }
    for (i = 0; i < 6; i++) {
        CHECK_STR(t.cmd[i].name, out[i]);
        CHECK_STR(t.cmd[i].help, out[i]);
    }
}

TEST(full_at_the_limit)
{
    uos_maint_table_t t;
    char name[4] = "aaa";
    unsigned i;

    uos_mt_init(&t);
    for (i = 0; i < UOS_MAINT_MAX; i++) {
        name[1] = (char)('a' + i / 26);
        name[2] = (char)('a' + i % 26);
        CHECK_EQ(uos_mt_add(&t, name, f_a, NULL), 0);
    }
    CHECK_EQ(t.count, (unsigned)UOS_MAINT_MAX);
    CHECK_EQ(uos_mt_add(&t, "zzz", f_a, NULL), UOS_MAINT_ERR_FULL);
    /* A duplicate in a full table is still a duplicate. */
    CHECK_EQ(uos_mt_add(&t, "aaa", f_a, NULL), UOS_MAINT_ERR_DUP);
    for (i = 1; i < t.count; i++) {
        CHECK(strcmp(t.cmd[i - 1].name, t.cmd[i].name) < 0);
    }
}

static int split(const char *in, char **argv, unsigned max, char *buf)
{
    strcpy(buf, in);
    return uos_mt_split(buf, argv, max);
}

TEST(split_words)
{
    char buf[200];
    char *argv[UOS_MAINT_ARGS_MAX + 1];

    CHECK_EQ(split("", argv, 8, buf), 0);
    CHECK(argv[0] == NULL);
    CHECK_EQ(split("   \t ", argv, 8, buf), 0);
    CHECK_EQ(split("uaf 2000", argv, 8, buf), 2);
    CHECK_STR(argv[0], "uaf");
    CHECK_STR(argv[1], "2000");
    CHECK(argv[2] == NULL);
    CHECK_EQ(split("  a \t  bb   c  ", argv, 8, buf), 3);
    CHECK_STR(argv[0], "a");
    CHECK_STR(argv[1], "bb");
    CHECK_STR(argv[2], "c");
    CHECK_EQ(split("a\"b c", argv, 8, buf), 2);
    CHECK_STR(argv[0], "a\"b");
}

TEST(split_quotes)
{
    char buf[200];
    char *argv[UOS_MAINT_ARGS_MAX + 1];

    CHECK_EQ(split("\"hello  world\" x", argv, 8, buf), 2);
    CHECK_STR(argv[0], "hello  world");
    CHECK_STR(argv[1], "x");
    CHECK_EQ(split("say \"a \"\"quoted\"\" word\"", argv, 8, buf), 2);
    CHECK_STR(argv[1], "a \"quoted\" word");
    CHECK_EQ(split("\"\" b", argv, 8, buf), 2);
    CHECK_STR(argv[0], "");
    CHECK_STR(argv[1], "b");
    CHECK_EQ(split("\"\"\"\"", argv, 8, buf), 1);
    CHECK_STR(argv[0], "\"");
    CHECK_EQ(split("\"open", argv, 8, buf), UOS_MAINT_SPLIT_QUOTE);
    CHECK_EQ(split("\"a\"b", argv, 8, buf), UOS_MAINT_SPLIT_QUOTE);
    CHECK_EQ(split("x \"a\"\"", argv, 8, buf), UOS_MAINT_SPLIT_QUOTE);
}

TEST(split_limits)
{
    char buf[200];
    char line[200];
    char *argv[UOS_MAINT_ARGS_MAX + 1];

    CHECK_EQ(split("1 2 3 4 5 6 7 8", argv, 8, buf), 8);
    CHECK(argv[8] == NULL);
    CHECK_EQ(split("1 2 3 4 5 6 7 8 9", argv, 8, buf), UOS_MAINT_SPLIT_WORDS);
    CHECK_EQ(split("1 2 3", argv, 2, buf), UOS_MAINT_SPLIT_WORDS);
    memset(line, 'a', UOS_MAINT_LINE_MAX);
    line[UOS_MAINT_LINE_MAX] = '\0';
    CHECK_EQ(split(line, argv, 8, buf), 1);
    CHECK_EQ(strlen(argv[0]), (size_t)UOS_MAINT_LINE_MAX);
    line[UOS_MAINT_LINE_MAX] = 'a';
    line[UOS_MAINT_LINE_MAX + 1] = '\0';
    CHECK_EQ(split(line, argv, 8, buf), UOS_MAINT_SPLIT_LONG);
}

TEST(rejoin_undoes_the_channel_split)
{
    /* The channel writes a NUL over every blank: "say  \"a b\"\tc". */
    char line[] = "say\0\0\"a\0b\"\0c";
    char *argv[UOS_MAINT_ARGS_MAX + 1];
    char *rest = uos_mt_rejoin(line + 5, line + 12);

    CHECK_STR(rest, "\"a b\" c");
    CHECK_EQ(uos_mt_split(rest, argv, 8), 2);
    CHECK_STR(argv[0], "a b");
    CHECK_STR(argv[1], "c");
}
