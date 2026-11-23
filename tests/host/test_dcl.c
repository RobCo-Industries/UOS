/* DCL syntax: tokeniser, continuation, numbers, lookup, parser errors and limits. */

#include <stdio.h>
#include <string.h>

#include "dcl.h"
#include "rc_test.h"

/* A small table that uses every feature. */
static const char *const colours[] = { "RED", "REDDISH", "GREEN", "GREY", NULL };
static const dcl_param_t t_params[] = {
    { "First", DCL_VT_TEXT, NULL, 0, 0 },
    { "Second", DCL_VT_TEXT, NULL, 0, 0 },
    { "P3", DCL_VT_TEXT, NULL, 0, 0 },
    { "P4", DCL_VT_TEXT, NULL, 0, 0 },
    { "P5", DCL_VT_TEXT, NULL, 0, 0 },
    { "P6", DCL_VT_TEXT, NULL, 0, 0 },
    { "P7", DCL_VT_TEXT, NULL, 0, 0 },
    { "P8", DCL_VT_TEXT, NULL, 0, 0 },
};
static const dcl_param_t t_typed[] = {
    { "Colour", DCL_VT_KEYWORD, colours, 0, 0 },
    { "Count", DCL_VT_NUMBER, NULL, -5, 300 },
    { "Where", DCL_VT_FILE, NULL, 0, 0 },
};
static const dcl_qual_t t_quals[] = {
    { "LOG", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "LIST", DCL_QF_LIST, DCL_QV_REQUIRED, DCL_VT_TEXT, NULL, 0, 0, NULL },
    { "LEVEL", 0, DCL_QV_OPTIONAL, DCL_VT_NUMBER, NULL, 0, 9, "5" },
    { "COLOUR", DCL_QF_LIST | DCL_QF_NEG, DCL_QV_REQUIRED, DCL_VT_KEYWORD, colours, 0, 0, NULL },
    { "NOTE", 0, DCL_QV_REQUIRED, DCL_VT_TEXT, NULL, 0, 0, NULL },
    { "HEADER", DCL_QF_NEG | DCL_QF_DEFON, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "FIXED", 0, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "OUT", 0, DCL_QV_REQUIRED, DCL_VT_FILE, NULL, 0, 0, NULL },
};
static const dcl_qual_t t_kq[] = {
    { "SPEED", 0, DCL_QV_REQUIRED, DCL_VT_NUMBER, NULL, 1, 10, NULL },
    { "LOUD", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
};
static const dcl_verb_t t_keys[] = {
    { "ALPHA", 0, 1, t_params, 2, t_kq, 0, NULL, NULL, 0, NULL },
    { "ALPINE", 1, 1, t_params, 0, NULL, 0, NULL, NULL, 4, NULL },
    { "BETA", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
};
static const dcl_verb_t t_verbs[] = {
    { "ECHO", 0, 8, t_params, 8, t_quals, 0, NULL, NULL, 0, "H" },
    { "EXIT", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "H" },
    { "TYPED", 1, 3, t_typed, 0, NULL, 0, NULL, NULL, 0, "H" },
    { "TWO", 2, 2, t_params, 0, NULL, 0, NULL, NULL, 0, "H" },
    { "SUB", 0, 0, NULL, 1, t_quals, 3, t_keys, "Which", 2, "H" },
    { "SUBMIT", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "H" },
};
static const dcl_table_t T = { sizeof(t_verbs) / sizeof(t_verbs[0]), t_verbs };

static dcl_cmd_t cmd;

static dcl_msg_t P(const char *line)
{
    return dcl_parse(line, &T, &cmd);
}

/* Parses a line that must fail with m and the given token (NULL: none). */
#define CHECK_ERR(line, m, tok)                     \
    do {                                            \
        CHECK_EQ(P(line), m);                       \
        if ((tok) == NULL) {                        \
            CHECK(cmd.token == NULL);               \
        } else {                                    \
            CHECK_STR(cmd.token, (const char *)(tok)); \
        }                                           \
    } while (0)

/* ---- tokeniser ---- */

static int lex_all(const char *line, dcl_token_t *toks, int max, char *out, size_t out_size,
                   dcl_msg_t *err)
{
    dcl_lexer_t lx;
    int n = 0;

    dcl_lex_init(&lx, line, out, out_size);
    for (;;) {
        dcl_token_t t;
        dcl_msg_t m = dcl_lex_next(&lx, &t);

        if (m != DCL_NORMAL) {
            *err = m;
            return n;
        }
        if (t.type == DCL_TOK_END || n >= max) {
            *err = DCL_NORMAL;
            return n;
        }
        toks[n++] = t;
    }
}

TEST(lex_every_token)
{
    dcl_token_t t[16];
    char out[256];
    dcl_msg_t e;
    int n = lex_all("dir/q=(a,b) \"Mixed \"\"case\"\"\" x\"y z\"w ! gone", t, 16, out, sizeof(out), &e);

    CHECK_EQ(e, DCL_NORMAL);
    CHECK_EQ(n, 11);
    CHECK_EQ(t[0].type, DCL_TOK_WORD);
    CHECK_STR(t[0].text, "DIR");
    CHECK_EQ(t[0].quoted, 0);
    CHECK_EQ(t[1].type, DCL_TOK_SLASH);
    CHECK_STR(t[2].text, "Q");
    CHECK_EQ(t[3].type, DCL_TOK_EQUALS);
    CHECK_EQ(t[4].type, DCL_TOK_LPAREN);
    CHECK_STR(t[5].text, "A");
    CHECK_EQ(t[6].type, DCL_TOK_COMMA);
    CHECK_STR(t[7].text, "B");
    CHECK_EQ(t[8].type, DCL_TOK_RPAREN);
    CHECK_STR(t[9].text, "Mixed \"case\"");
    CHECK_EQ(t[9].quoted, 1);
    CHECK_STR(t[10].text, "Xy zW");
    CHECK_EQ(t[10].quoted, 1);
}

TEST(lex_blank_and_comment)
{
    dcl_token_t t[4];
    char out[64];
    dcl_msg_t e;

    CHECK_EQ(lex_all("", t, 4, out, sizeof(out), &e), 0);
    CHECK_EQ(lex_all(" \t ", t, 4, out, sizeof(out), &e), 0);
    CHECK_EQ(lex_all("! only a comment", t, 4, out, sizeof(out), &e), 0);
    CHECK_EQ(lex_all("a!b", t, 4, out, sizeof(out), &e), 1);
    CHECK_STR(t[0].text, "A");
    CHECK_EQ(lex_all("\"a!b\"", t, 4, out, sizeof(out), &e), 1);
    CHECK_STR(t[0].text, "a!b");
    CHECK_EQ(lex_all("\"\"", t, 4, out, sizeof(out), &e), 1);
    CHECK_STR(t[0].text, "");
    CHECK_EQ(t[0].quoted, 1);
    CHECK_EQ(lex_all("[.mail]*.txt;1 sd0:[-]x-y.t$_", t, 4, out, sizeof(out), &e), 2);
    CHECK_STR(t[0].text, "[.MAIL]*.TXT;1");
    CHECK_STR(t[1].text, "SD0:[-]X-Y.T$_");
}

TEST(lex_errors)
{
    dcl_token_t t[4];
    char out[512];
    char line[300];
    dcl_msg_t e;

    lex_all("a \"open", t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_UNTERM);
    lex_all("a \"x\"\"", t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_UNTERM);
    lex_all("a\x01", t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_IVCHAR);
    lex_all("\x7f", t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_IVCHAR);
    lex_all("\"a\x02\"", t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_IVCHAR);
    /* Token length: 127 is fine, 128 is not, quoted or not. */
    memset(line, 'a', 127);
    line[127] = '\0';
    CHECK_EQ(lex_all(line, t, 4, out, sizeof(out), &e), 1);
    CHECK_EQ(e, DCL_NORMAL);
    CHECK_EQ(strlen(t[0].text), 127);
    memset(line, 'a', 128);
    line[128] = '\0';
    lex_all(line, t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_TKNOVF);
    line[0] = '"';
    line[128] = 'a';
    line[129] = '"';
    line[130] = '\0';
    lex_all(line, t, 4, out, sizeof(out), &e);
    CHECK_EQ(e, DCL_TKNOVF);
    /* A full output buffer. */
    lex_all("abcdef", t, 4, out, 4, &e);
    CHECK_EQ(e, DCL_LINOVF);
}

/* ---- continuation ---- */

TEST(continuation)
{
    dcl_line_t ln;

    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, "two a.txt -"), DCL_CONTIN);
    CHECK_EQ(dcl_line_add(&ln, "b.txt  -  "), DCL_CONTIN);
    CHECK_EQ(dcl_line_add(&ln, ""), DCL_NORMAL);
    CHECK_STR(ln.text, "two a.txt b.txt  ");
    CHECK_EQ(P(ln.text), DCL_NORMAL);
    CHECK_EQ(cmd.nparams, 2);

    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, "dir ! not -"), DCL_NORMAL);
    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, "echo \"a -"), DCL_NORMAL);
    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, "echo \"a\" -"), DCL_CONTIN);
    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, "-"), DCL_CONTIN);
    CHECK_EQ(dcl_line_add(&ln, ""), DCL_NORMAL);
    CHECK_STR(ln.text, "");
    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, "set default [-]"), DCL_NORMAL);
}

TEST(continuation_limit)
{
    dcl_line_t ln;
    char a[200];
    char b[100];

    memset(a, 'a', 199);
    a[198] = '-';
    a[199] = '\0';
    memset(b, 'b', 99);
    b[99] = '\0';
    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, a), DCL_CONTIN);
    CHECK_EQ(ln.len, 198);
    b[57] = '\0'; /* 198 + 57 = 255: fits */
    CHECK_EQ(dcl_line_add(&ln, b), DCL_NORMAL);
    CHECK_EQ(ln.len, 255);
    dcl_line_reset(&ln);
    CHECK_EQ(dcl_line_add(&ln, a), DCL_CONTIN);
    b[57] = 'b';
    b[58] = '\0'; /* 256: too long */
    CHECK_EQ(dcl_line_add(&ln, b), DCL_LINOVF);
    CHECK_EQ(ln.len, 0);
    CHECK_STR(ln.text, "");
}

/* ---- numbers ---- */

TEST(numbers)
{
    int32_t v = 0;

    CHECK_EQ(dcl_parse_number("0", 0, 10, &v), DCL_NORMAL);
    CHECK_EQ(v, 0);
    CHECK_EQ(dcl_parse_number("42", 0, 100, &v), DCL_NORMAL);
    CHECK_EQ(v, 42);
    CHECK_EQ(dcl_parse_number("-7", -10, 10, &v), DCL_NORMAL);
    CHECK_EQ((long)v, -7);
    CHECK_EQ(dcl_parse_number("+7", -10, 10, &v), DCL_NORMAL);
    CHECK_EQ(v, 7);
    CHECK_EQ(dcl_parse_number("%X1F", 0, 100, &v), DCL_NORMAL);
    CHECK_EQ(v, 31);
    CHECK_EQ(dcl_parse_number("%xff", 0, 1000, &v), DCL_NORMAL);
    CHECK_EQ(v, 255);
    CHECK_EQ(dcl_parse_number("%O17", 0, 100, &v), DCL_NORMAL);
    CHECK_EQ(v, 15);
    CHECK_EQ(dcl_parse_number("%D19", 0, 100, &v), DCL_NORMAL);
    CHECK_EQ(v, 19);
    CHECK_EQ(dcl_parse_number("-%X10", -100, 100, &v), DCL_NORMAL);
    CHECK_EQ((long)v, -16);
    CHECK_EQ(dcl_parse_number("2147483647", INT32_MIN, INT32_MAX, &v), DCL_NORMAL);
    CHECK_EQ(v, 2147483647);
    CHECK_EQ(dcl_parse_number("-2147483648", INT32_MIN, INT32_MAX, &v), DCL_NORMAL);
    CHECK(v == INT32_MIN);
    CHECK_EQ(dcl_parse_number("%X7FFFFFFF", INT32_MIN, INT32_MAX, &v), DCL_NORMAL);
    CHECK_EQ(v, 0x7FFFFFFF);
}

TEST(number_errors)
{
    int32_t v = 99;

    CHECK_EQ(dcl_parse_number("", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("-", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("%X", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("%Q1", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("%", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("1A", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("%O8", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("%XG", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("1 2", 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number(NULL, 0, 10, &v), DCL_IVNUMB);
    CHECK_EQ(dcl_parse_number("11", 0, 10, &v), DCL_NUMRNG);
    CHECK_EQ(dcl_parse_number("-1", 0, 10, &v), DCL_NUMRNG);
    CHECK_EQ(dcl_parse_number("2147483648", INT32_MIN, INT32_MAX, &v), DCL_NUMRNG);
    CHECK_EQ(dcl_parse_number("-2147483649", INT32_MIN, INT32_MAX, &v), DCL_NUMRNG);
    CHECK_EQ(dcl_parse_number("%XFFFFFFFF", INT32_MIN, INT32_MAX, &v), DCL_NUMRNG);
    CHECK_EQ(dcl_parse_number("99999999999999999999", INT32_MIN, INT32_MAX, &v), DCL_NUMRNG);
    CHECK_EQ(v, 99);
    /* At the edges of a range. */
    CHECK_EQ(dcl_parse_number("10", 0, 10, &v), DCL_NORMAL);
    CHECK_EQ(dcl_parse_number("0", 0, 10, &v), DCL_NORMAL);
}

/* ---- lookup ---- */

TEST(lookup_rules)
{
    static const char *const names[] = { "DELETE", "DEVICE", "DIRECTORY", "SET", "SETUP", "SHOW", NULL };
    size_t i = 99;

    CHECK_EQ(dcl_lookup_key("D", names, &i), DCL_LK_AMBIG);
    CHECK_EQ(dcl_lookup_key("DE", names, &i), DCL_LK_AMBIG);
    CHECK_EQ(dcl_lookup_key("DEL", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 0);
    CHECK_EQ(dcl_lookup_key("DEV", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 1);
    CHECK_EQ(dcl_lookup_key("DI", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 2);
    CHECK_EQ(dcl_lookup_key("DIRECTORY", names, &i), DCL_LK_FOUND);
    CHECK_EQ(dcl_lookup_key("DIRECTORYX", names, &i), DCL_LK_NONE);
    CHECK_EQ(dcl_lookup_key("DIRX", names, &i), DCL_LK_NONE);
    /* An exact name wins over a longer one it prefixes. */
    CHECK_EQ(dcl_lookup_key("SET", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 3);
    CHECK_EQ(dcl_lookup_key("SETU", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 4);
    CHECK_EQ(dcl_lookup_key("SE", names, &i), DCL_LK_AMBIG);
    CHECK_EQ(dcl_lookup_key("SH", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 5);
    CHECK_EQ(dcl_lookup_key("", names, &i), DCL_LK_NONE);
    CHECK_EQ(dcl_lookup_key("X", names, &i), DCL_LK_NONE);
    CHECK_EQ(dcl_lookup_key("x", NULL, &i), DCL_LK_NONE);
    CHECK_EQ(dcl_lookup_key("del", names, &i), DCL_LK_NONE); /* callers upper-case */
}

TEST(four_characters_suffice)
{
    /* Every name unique in its first four resolves from four characters on. */
    static const char *const names[] = { "DIRECTORY", "DIRT", "DISK", "DISMOUNT", NULL };
    size_t i;
    size_t k;

    CHECK_EQ(dcl_lookup_key("DIR", names, &i), DCL_LK_AMBIG);
    CHECK_EQ(dcl_lookup_key("DIRE", names, &i), DCL_LK_FOUND);
    CHECK_EQ(i, 0);
    CHECK_EQ(dcl_lookup_key("DIS", names, &i), DCL_LK_AMBIG);
    for (k = 0; names[k] != NULL; k++) {
        size_t len;

        for (len = 4; len <= strlen(names[k]); len++) {
            char w[16];

            memcpy(w, names[k], len);
            w[len] = '\0';
            CHECK_EQ(dcl_lookup_key(w, names, &i), DCL_LK_FOUND);
            CHECK_EQ(i, k);
        }
    }
}

/* ---- parser: success ---- */

TEST(empty_lines)
{
    CHECK_EQ(P(""), DCL_NORMAL);
    CHECK(cmd.verb == NULL);
    CHECK_EQ(P("   "), DCL_NORMAL);
    CHECK(cmd.verb == NULL);
    CHECK_EQ(P("! just a note"), DCL_NORMAL);
    CHECK(cmd.verb == NULL);
}

TEST(verb_qualifier_parameter_order)
{
    CHECK_EQ(P("echo/log one \"Two words\" /list=x three"), DCL_NORMAL);
    CHECK_STR(cmd.verb->name, "ECHO");
    CHECK(cmd.keyword == NULL);
    CHECK_EQ(cmd.nparams, 3);
    CHECK_STR(cmd.params[0], "ONE");
    CHECK_STR(cmd.params[1], "Two words");
    CHECK_STR(cmd.params[2], "THREE");
    CHECK_EQ(cmd.nquals, 2);
    CHECK_STR(cmd.quals[0].def->name, "LOG");
    CHECK_STR(cmd.quals[1].def->name, "LIST");
    CHECK_STR(cmd.quals[1].values[0], "X");
    CHECK(dcl_cmd_on(&cmd, "LOG"));
}

TEST(qualifier_values_and_lists)
{
    CHECK_EQ(P("echo /list=(a,\"b c\", d ) /note=\"Keep Case\""), DCL_NORMAL);
    CHECK_EQ(cmd.nquals, 2);
    CHECK_EQ(cmd.quals[0].nvalues, 3);
    CHECK_STR(cmd.quals[0].values[0], "A");
    CHECK_STR(cmd.quals[0].values[1], "b c");
    CHECK_STR(cmd.quals[0].values[2], "D");
    CHECK_STR(dcl_cmd_value(&cmd, "NOTE"), "Keep Case");
    CHECK_EQ(P("echo/list = x"), DCL_NORMAL);
    CHECK_STR(dcl_cmd_value(&cmd, "LIST"), "X");
    CHECK_EQ(P("echo/list=(x)"), DCL_NORMAL);
    CHECK_EQ(cmd.quals[0].nvalues, 1);
}

TEST(keyword_values)
{
    CHECK_EQ(P("echo/col=(gree,grey,red)"), DCL_NORMAL);
    CHECK_STR(cmd.quals[0].values[0], "GREEN");
    CHECK_STR(cmd.quals[0].values[1], "GREY");
    CHECK_STR(cmd.quals[0].values[2], "RED");
    CHECK_ERR("echo/col=gre", DCL_ABKEYW, "GRE");
    CHECK_ERR("echo/col=blue", DCL_IVKEYW, "BLUE");
    CHECK_ERR("echo/col=(red,pink)", DCL_IVKEYW, "PINK");
}

TEST(optional_value_and_default)
{
    CHECK_EQ(P("echo/level"), DCL_NORMAL);
    CHECK_EQ(cmd.quals[0].nvalues, 0);
    CHECK_STR(dcl_cmd_value(&cmd, "LEVEL"), "5");
    CHECK_EQ(P("echo/level=7"), DCL_NORMAL);
    CHECK_STR(dcl_cmd_value(&cmd, "LEVEL"), "7");
    CHECK_EQ(cmd.quals[0].numbers[0], 7);
    CHECK_EQ(P("echo/level=%x9"), DCL_NORMAL);
    CHECK_EQ(cmd.quals[0].numbers[0], 9);
    CHECK_EQ(P("echo"), DCL_NORMAL);
    CHECK(dcl_cmd_value(&cmd, "LEVEL") == NULL);
    CHECK(!dcl_cmd_on(&cmd, "LEVEL"));
    CHECK(dcl_cmd_qual(&cmd, "LEVEL") == NULL);
    CHECK_ERR("echo/level=10", DCL_NUMRNG, "10");
    CHECK_ERR("echo/level=x", DCL_IVNUMB, "X");
}

TEST(default_on_and_negation)
{
    CHECK_EQ(P("echo"), DCL_NORMAL);
    CHECK(dcl_cmd_on(&cmd, "HEADER"));
    CHECK(!dcl_cmd_on(&cmd, "LOG"));
    CHECK(!dcl_cmd_on(&cmd, "NOSUCH"));
    CHECK_EQ(P("echo/nohead"), DCL_NORMAL);
    CHECK(!dcl_cmd_on(&cmd, "HEADER"));
    CHECK_EQ(cmd.quals[0].negated, 1);
    CHECK_STR(cmd.quals[0].typed, "NOHEAD");
    CHECK_EQ(P("echo/nolog"), DCL_NORMAL);
    CHECK(!dcl_cmd_on(&cmd, "LOG"));
    CHECK_EQ(P("echo/nocol"), DCL_NORMAL); /* a negated required-value qualifier needs no value */
    CHECK(dcl_cmd_value(&cmd, "COLOUR") == NULL);
    /* NOTE is a qualifier itself: the full name wins over NO+TE. */
    CHECK_EQ(P("echo/note=x"), DCL_NORMAL);
    CHECK_EQ(cmd.quals[0].negated, 0);
    CHECK_STR(cmd.quals[0].def->name, "NOTE");
    /* NOL could be NO+LOG or NO+LIST: LO is ambiguous but L alone is too. */
    CHECK_ERR("echo/nol", DCL_IVQUAL, "NOL");
    CHECK_EQ(P("echo/nolo"), DCL_NORMAL);
    CHECK(!dcl_cmd_on(&cmd, "LOG"));
}

TEST(last_qualifier_wins)
{
    CHECK_EQ(P("echo/log/list=a/nolog/list=b"), DCL_NORMAL);
    CHECK_EQ(cmd.nquals, 2);
    CHECK(!dcl_cmd_on(&cmd, "LOG"));
    CHECK_STR(dcl_cmd_value(&cmd, "LIST"), "B");
}

TEST(typed_parameters)
{
    CHECK_EQ(P("typed reddi %x10 [.a]b.c"), DCL_NORMAL);
    CHECK_STR(cmd.params[0], "REDDISH");
    CHECK_EQ(cmd.numbers[1], 16);
    CHECK_STR(cmd.params[2], "[.A]B.C");
    CHECK_EQ(P("typed red"), DCL_NORMAL); /* exact match beats REDDISH */
    CHECK_STR(cmd.params[0], "RED");
    CHECK_EQ(P("typed green -5"), DCL_NORMAL);
    CHECK_EQ((long)cmd.numbers[1], -5);
    CHECK_ERR("typed gr", DCL_ABKEYW, "GR");
    CHECK_ERR("typed pink", DCL_IVKEYW, "PINK");
    CHECK_ERR("typed red 301", DCL_NUMRNG, "301");
    CHECK_ERR("typed red -6", DCL_NUMRNG, "-6");
    CHECK_ERR("typed red ten", DCL_IVNUMB, "TEN");
}

TEST(second_level_keywords)
{
    CHECK_EQ(P("sub alph"), DCL_NORMAL);
    CHECK_STR(cmd.keyword->name, "ALPHA");
    CHECK_EQ(P("sub alpi x"), DCL_NORMAL);
    CHECK_STR(cmd.keyword->name, "ALPINE");
    CHECK_STR(cmd.params[0], "X");
    CHECK_EQ(P("sub/log beta"), DCL_NORMAL); /* verb qualifier before the keyword */
    CHECK_STR(cmd.keyword->name, "BETA");
    CHECK(dcl_cmd_on(&cmd, "LOG"));
    CHECK_EQ(P("sub alpha/speed=3/noloud"), DCL_NORMAL);
    CHECK_EQ(cmd.quals[0].numbers[0], 3);
    CHECK(!dcl_cmd_on(&cmd, "LOUD"));
    CHECK_ERR("sub al", DCL_ABKEYW, "AL");
    CHECK_ERR("sub gamma", DCL_IVKEYW, "GAMMA");
    CHECK_ERR("sub beta/speed=1", DCL_IVQUAL, "SPEED");
    CHECK_EQ(P("sub"), DCL_INSFPRM);
    CHECK_STR(cmd.prompt, "Which");
    CHECK_ERR("sub alpine", DCL_INSFPRM, NULL);
    CHECK_STR(cmd.prompt, "First");
    CHECK_STR(cmd.keyword->name, "ALPINE");
}

TEST(privileges)
{
    CHECK_EQ(P("sub alpine x"), DCL_NORMAL);
    CHECK_EQ(dcl_cmd_privs(&cmd), 6);
    CHECK_EQ(dcl_check_privs(&cmd, 6), DCL_NORMAL);
    CHECK_EQ(dcl_check_privs(&cmd, 2), DCL_SYS_NOPRIV);
    CHECK_EQ(dcl_check_privs(&cmd, 4), DCL_SYS_NOPRIV);
    CHECK_EQ(dcl_check_privs(&cmd, DCL_PRIV_SYSTEM), DCL_NORMAL);
    CHECK_EQ(P("sub beta"), DCL_NORMAL);
    CHECK_EQ(dcl_cmd_privs(&cmd), 2);
    CHECK_EQ(P("echo"), DCL_NORMAL);
    CHECK_EQ(dcl_check_privs(&cmd, 0), DCL_NORMAL);
}

TEST(missing_parameters)
{
    CHECK_ERR("two", DCL_INSFPRM, NULL);
    CHECK_STR(cmd.prompt, "First");
    CHECK_EQ(cmd.nparams, 0);
    CHECK_ERR("two a", DCL_INSFPRM, NULL);
    CHECK_STR(cmd.prompt, "Second");
    CHECK_EQ(cmd.nparams, 1);
    CHECK_STR(cmd.verb->name, "TWO");
    CHECK_EQ(P("two a b"), DCL_NORMAL);
}

/* ---- parser: errors ---- */

TEST(verb_errors)
{
    CHECK_ERR("bogus", DCL_IVVERB, "BOGUS");
    CHECK_ERR("e", DCL_ABVERB, "E");
    CHECK_ERR("s", DCL_ABVERB, "S");
    CHECK_ERR("su", DCL_ABVERB, "SU");
    CHECK_EQ(P("sub"), DCL_INSFPRM); /* exact */
    CHECK_EQ(P("subm"), DCL_NORMAL);
    CHECK_ERR("\"echo\"", DCL_IVVERB, "echo");
    CHECK_ERR("/log", DCL_IVVERB, "/");
    CHECK_ERR("=x", DCL_IVVERB, "=");
    CHECK_ERR("echoo", DCL_IVVERB, "ECHOO");
}

TEST(qualifier_errors)
{
    CHECK_ERR("echo/zz", DCL_IVQUAL, "ZZ");
    CHECK_ERR("echo/l", DCL_ABQUAL, "L");
    CHECK_ERR("echo/", DCL_IVQUAL, "/");
    CHECK_ERR("echo/ /log", DCL_IVQUAL, "/");
    CHECK_ERR("echo/\"log\"", DCL_IVQUAL, "log");
    CHECK_ERR("echo/nofixed", DCL_NONEG, "NOFIXED");
    CHECK_ERR("echo/nonote", DCL_NONEG, "NONOTE");
    CHECK_ERR("echo/note", DCL_VALREQ, "NOTE");
    CHECK_ERR("echo/note=", DCL_VALREQ, "NOTE");
    CHECK_ERR("echo/note=/log", DCL_VALREQ, "NOTE");
    CHECK_ERR("echo/log=yes", DCL_NOVALU, "LOG");
    CHECK_ERR("echo/nolog=yes", DCL_NOVALU, "NOLOG");
    CHECK_ERR("echo/note=(a,b)", DCL_NOLIST, "NOTE");
    CHECK_ERR("echo/list=(a,b", DCL_PARENS, "LIST");
    CHECK_ERR("echo/list=(a,", DCL_PARENS, "LIST");
    CHECK_ERR("echo/list=(", DCL_PARENS, "LIST");
    CHECK_ERR("echo/list=()", DCL_VALREQ, "LIST");
    CHECK_ERR("echo/list=(a,,b)", DCL_VALREQ, "LIST");
    CHECK_ERR("echo/list=(a b)", DCL_IVCHAR, "B");
    CHECK_ERR("echo/list=(a=b)", DCL_IVCHAR, "=");
}

TEST(stray_punctuation)
{
    CHECK_ERR("echo a=b", DCL_IVCHAR, "=");
    CHECK_ERR("echo (a)", DCL_IVCHAR, "(");
    CHECK_ERR("echo a)", DCL_IVCHAR, ")");
    CHECK_ERR("echo a,b", DCL_IVCHAR, ",");
    CHECK_ERR("echo a\x01", DCL_IVCHAR, NULL);
    CHECK_ERR("echo \"abc", DCL_UNTERM, NULL);
}

TEST(limit_parameters)
{
    CHECK_EQ(P("echo 1 2 3 4 5 6 7 8"), DCL_NORMAL);
    CHECK_EQ(cmd.nparams, 8);
    CHECK_STR(cmd.params[7], "8");
    CHECK_ERR("echo 1 2 3 4 5 6 7 8 9", DCL_MAXPARM, "9");
    CHECK_ERR("two a b c", DCL_MAXPARM, "C");
    CHECK_ERR("exit now", DCL_MAXPARM, "NOW");
    CHECK_ERR("sub beta x", DCL_MAXPARM, "X");
    CHECK_ERR("sub alpha 1 2 3 4 5 6 7 8 9", DCL_MAXPARM, "9");
}

TEST(limit_qualifiers)
{
    char line[300];
    int i;

    strcpy(line, "echo");
    for (i = 0; i < DCL_QUAL_MAX; i++) {
        strcat(line, "/log");
    }
    CHECK_EQ(P(line), DCL_NORMAL);
    CHECK_EQ(cmd.nquals, 1);
    strcat(line, "/fixed");
    CHECK_ERR(line, DCL_MAXQUAL, "FIXED");
}

TEST(limit_list)
{
    CHECK_EQ(P("echo/list=(1,2,3,4,5,6,7,8)"), DCL_NORMAL);
    CHECK_EQ(cmd.quals[0].nvalues, 8);
    CHECK_STR(cmd.quals[0].values[7], "8");
    CHECK_ERR("echo/list=(1,2,3,4,5,6,7,8,9)", DCL_MAXVAL, "9");
}

TEST(limit_line_and_token)
{
    char line[300];

    memset(line, ' ', sizeof(line));
    memcpy(line, "echo", 4);
    line[DCL_LINE_MAX] = '\0';
    CHECK_EQ(P(line), DCL_NORMAL);
    line[DCL_LINE_MAX] = ' ';
    line[DCL_LINE_MAX + 1] = '\0';
    CHECK_ERR(line, DCL_LINOVF, NULL);

    memcpy(line, "echo ", 5);
    memset(line + 5, 'x', DCL_TOKEN_MAX);
    line[5 + DCL_TOKEN_MAX] = '\0';
    CHECK_EQ(P(line), DCL_NORMAL);
    CHECK_EQ(strlen(cmd.params[0]), DCL_TOKEN_MAX);
    line[5 + DCL_TOKEN_MAX] = 'x';
    line[6 + DCL_TOKEN_MAX] = '\0';
    CHECK_ERR(line, DCL_TKNOVF, NULL);
}

TEST(worst_case_text_storage)
{
    /* The densest line: one-character words split by commas, 255 characters. */
    char line[DCL_LINE_MAX + 1];
    size_t n;

    strcpy(line, "echo/list=(");
    n = strlen(line);
    while (n + 3 < DCL_LINE_MAX) {
        line[n++] = 'a';
        line[n++] = ',';
    }
    line[n++] = 'a';
    line[n++] = ')';
    line[n] = '\0';
    CHECK_EQ(P(line), DCL_MAXVAL);
    CHECK_STR(cmd.token, "A");
}

TEST(table_check)
{
    const char *bad = "x";

    CHECK_EQ(dcl_table_check(&T, &bad), 0);
    CHECK(bad == NULL);
}

TEST(table_check_catches)
{
    static const dcl_verb_t dup[] = {
        { "DIRECTORY", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "H" },
        { "DIRT", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "H" },
        { "DIRECT", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "H" },
    };
    static const dcl_verb_t lower[] = { { "dir", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "H" } };
    static const dcl_verb_t counts[] = { { "X", 2, 1, t_params, 0, NULL, 0, NULL, NULL, 0, "H" } };
    static const dcl_verb_t nohelp[] = { { "X", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL } };
    static const dcl_qual_t badq[] = {
        { "LOG", 0, DCL_QV_REQUIRED, DCL_VT_NONE, NULL, 0, 0, NULL },
    };
    static const dcl_verb_t qv[] = { { "X", 0, 0, NULL, 1, badq, 0, NULL, NULL, 0, "H" } };
    static const dcl_table_t t1 = { 3, dup };
    static const dcl_table_t t2 = { 1, lower };
    static const dcl_table_t t3 = { 1, counts };
    static const dcl_table_t t4 = { 1, nohelp };
    static const dcl_table_t t5 = { 1, qv };
    const char *bad = NULL;

    CHECK_EQ(dcl_table_check(&t1, &bad), -1);
    CHECK_STR(bad, "DIRECT");
    CHECK_EQ(dcl_table_check(&t2, &bad), -1);
    CHECK_STR(bad, "dir");
    CHECK_EQ(dcl_table_check(&t3, &bad), -1);
    CHECK_STR(bad, "X");
    CHECK_EQ(dcl_table_check(&t4, &bad), -1);
    CHECK_EQ(dcl_table_check(&t5, &bad), -1);
    CHECK_STR(bad, "LOG");
}
