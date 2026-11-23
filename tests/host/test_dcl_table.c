/* The UOS command table: soundness, abbreviations of every verb and keyword, and parsed lines. */

#include <stdio.h>
#include <string.h>

#include "dcl.h"
#include "rc_test.h"

static const dcl_table_t *U = &dcl_uos_table;
static dcl_cmd_t cmd;

/* Renders a parse as "VERB KEY p1|p2 /Q /NOQ /Q=V /Q=(A|B)" or "!IDENT token" or "?prompt". */
static const char *render(const char *line)
{
    static char out[1024];
    dcl_msg_t m = dcl_parse(line, U, &cmd);
    size_t i;
    size_t j;

    out[0] = '\0';
    if (m == DCL_INSFPRM) {
        snprintf(out, sizeof(out), "?%s", cmd.prompt);
        return out;
    }
    if (m != DCL_NORMAL) {
        snprintf(out, sizeof(out), "!%s %s", dcl_msg_ident(m), cmd.token ? cmd.token : "-");
        return out;
    }
    if (cmd.verb == NULL) {
        return "";
    }
    strcat(out, cmd.verb->name);
    if (cmd.keyword != NULL) {
        strcat(out, " ");
        strcat(out, cmd.keyword->name);
    }
    for (i = 0; i < cmd.nparams; i++) {
        strcat(out, i == 0 ? " " : "|");
        strcat(out, cmd.params[i]);
    }
    for (i = 0; i < cmd.nquals; i++) {
        const dcl_qval_t *q = &cmd.quals[i];

        strcat(out, q->negated ? " /NO" : " /");
        strcat(out, q->def->name);
        if (q->nvalues == 1 && !(q->def->flags & DCL_QF_LIST)) {
            strcat(out, "=");
            strcat(out, q->values[0]);
        } else if (q->nvalues > 0) {
            strcat(out, "=(");
            for (j = 0; j < q->nvalues; j++) {
                if (j > 0) {
                    strcat(out, "|");
                }
                strcat(out, q->values[j]);
            }
            strcat(out, ")");
        }
    }
    return out;
}

TEST(table_is_sound)
{
    const char *bad = "x";

    CHECK_EQ(dcl_table_check(U, &bad), 0);
    CHECK(bad == NULL);
    CHECK_EQ(U->count, 17);
}

TEST(every_design_verb_is_present)
{
    static const char *const verbs[] = {
        "DIRECTORY", "TYPE", "COPY", "DELETE", "RENAME", "CREATE", "SET", "SHOW", "HELP", "RUN",
        "LOGIN", "LOGOUT", "ROBCO", "DEVICE", "HOLOTAPE", "MAIL", "AUTHORIZE",
    };
    size_t i;
    size_t k;

    for (i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        CHECK_EQ(dcl_lookup(verbs[i], U->verbs, U->count, sizeof(U->verbs[0]), &k), DCL_LK_FOUND);
        CHECK_STR(U->verbs[k].name, verbs[i]);
    }
}

TEST(help_keys)
{
    size_t i;

    for (i = 0; i < U->count; i++) {
        const char *h = U->verbs[i].help;
        size_t n = strlen(h);
        size_t base = n - 4 - 9; /* "SYS$HELP:" ... ".HLP" */
        size_t cut = strlen(U->verbs[i].name) < 8 ? strlen(U->verbs[i].name) : 8;

        CHECK_EQ(strncmp(h, "SYS$HELP:", 9), 0);
        CHECK_STR(h + n - 4, ".HLP");
        CHECK_EQ(base, cut);
        CHECK_EQ(strncmp(h + 9, U->verbs[i].name, cut), 0);
    }
}

/* Shortest prefix of names[k] that resolves to it, found by trying lengths. */
static size_t shortest(const void *items, size_t count, size_t stride, size_t k)
{
    const char *name = *(const char *const *)(const void *)((const char *)items + k * stride);
    size_t len;

    for (len = 1; len <= strlen(name); len++) {
        char w[64];
        size_t at = 99;

        memcpy(w, name, len);
        w[len] = '\0';
        if (dcl_lookup(w, items, count, stride, &at) == DCL_LK_FOUND && at == k) {
            return len;
        }
    }
    return 0;
}

static void check_abbrevs(const void *items, size_t count, size_t stride)
{
    size_t k;

    for (k = 0; k < count; k++) {
        const char *name = *(const char *const *)(const void *)((const char *)items + k * stride);
        size_t s = shortest(items, count, stride, k);
        size_t len;

        CHECK(s >= 1);
        CHECK(s <= 4); /* four characters suffice in this table */
        for (len = 1; len <= strlen(name); len++) {
            char w[64];
            size_t at = 99;
            dcl_lookup_t lk;

            memcpy(w, name, len);
            w[len] = '\0';
            lk = dcl_lookup(w, items, count, stride, &at);
            if (len < s) {
                CHECK(lk != DCL_LK_FOUND || at != k);
            } else {
                CHECK_EQ(lk, DCL_LK_FOUND);
                CHECK_EQ(at, k);
            }
        }
    }
}

TEST(every_verb_abbreviates)
{
    size_t k;

    check_abbrevs(U->verbs, U->count, sizeof(U->verbs[0]));
    /* And through the parser, with the shortest prefix in lower case. */
    for (k = 0; k < U->count; k++) {
        char w[64];
        size_t s = shortest(U->verbs, U->count, sizeof(U->verbs[0]), k);
        size_t i;
        dcl_msg_t m;

        for (i = 0; i < s; i++) {
            w[i] = (char)(U->verbs[k].name[i] - 'A' + 'a');
        }
        w[s] = '\0';
        m = dcl_parse(w, U, &cmd);
        CHECK(m == DCL_NORMAL || m == DCL_INSFPRM);
        CHECK(cmd.verb == &U->verbs[k]);
        if (s > 1) {
            w[s - 1] = '\0';
            m = dcl_parse(w, U, &cmd);
            CHECK(m == DCL_ABVERB);
        }
    }
}

TEST(every_keyword_and_qualifier_abbreviates)
{
    size_t k;
    size_t j;

    for (k = 0; k < U->count; k++) {
        const dcl_verb_t *v = &U->verbs[k];

        check_abbrevs(v->quals, v->nquals, sizeof(v->quals[0]));
        check_abbrevs(v->keys, v->nkeys, sizeof(v->keys[0]));
        for (j = 0; j < v->nkeys; j++) {
            check_abbrevs(v->keys[j].quals, v->keys[j].nquals, sizeof(v->keys[j].quals[0]));
        }
    }
}

TEST(known_ambiguities)
{
    CHECK_STR(render("d"), "!ABVERB D");
    CHECK_STR(render("de"), "!ABVERB DE");
    CHECK_STR(render("r"), "!ABVERB R");
    CHECK_STR(render("lo"), "!ABVERB LO");
    CHECK_STR(render("logi"), "LOGIN");
    CHECK_STR(render("logo"), "LOGOUT");
    CHECK_STR(render("s"), "!ABVERB S");
    CHECK_STR(render("show t"), "!ABKEYW T");
    CHECK_STR(render("show de"), "!ABKEYW DE");
    CHECK_STR(render("h"), "!ABVERB H");
}

static const struct {
    const char *line;
    const char *want;
} lines[] = {
    /* DIRECTORY */
    { "directory", "DIRECTORY" },
    { "dir", "DIRECTORY" },
    { "DIR/FULL [.MAIL]*.TXT", "DIRECTORY [.MAIL]*.TXT /FULL" },
    { "dir *.txt/size/date", "DIRECTORY *.TXT /SIZE /DATE" },
    { "dir/nodate/nosize", "DIRECTORY /NODATE /NOSIZE" },
    { "dir/out=list.txt", "DIRECTORY /OUTPUT=LIST.TXT" },
    { "dir/output", "!VALREQ OUTPUT" },
    { "dir/nooutput", "!NONEG NOOUTPUT" },
    { "dir/full=yes", "!NOVALU FULL" },
    { "dir a b", "!MAXPARM B" },
    { "dir/f/s/d", "DIRECTORY /FULL /SIZE /DATE" },
    { "dir/page", "!IVQUAL PAGE" },
    { "dir sys$system:*.*;*", "DIRECTORY SYS$SYSTEM:*.*;*" },
    /* TYPE */
    { "type notes.txt", "TYPE NOTES.TXT" },
    { "ty notes.txt/page", "TYPE NOTES.TXT /PAGE" },
    { "type/nopage a.txt", "TYPE A.TXT /NOPAGE" },
    { "type", "?File" },
    { "t a", "TYPE A" },
    /* COPY */
    { "COPY A.TXT B.TXT/LOG", "COPY A.TXT|B.TXT /LOG" },
    { "copy/confirm a b", "COPY A|B /CONFIRM" },
    { "cop a", "?To" },
    { "copy", "?From" },
    { "copy a b c", "!MAXPARM C" },
    { "copy a b/noconf/nolog", "COPY A|B /NOCONFIRM /NOLOG" },
    { "copy a b/c/l", "COPY A|B /CONFIRM /LOG" },
    /* DELETE */
    { "delete x.txt;1", "DELETE X.TXT;1" },
    { "del *.tmp/log/confirm", "DELETE *.TMP /LOG /CONFIRM" },
    { "dele", "?File" },
    { "del a/full", "!IVQUAL FULL" },
    /* RENAME */
    { "rename a.txt b.txt", "RENAME A.TXT|B.TXT" },
    { "ren a b/log", "RENAME A|B /LOG" },
    { "rename a", "?To" },
    /* CREATE */
    { "create notes.txt", "CREATE NOTES.TXT" },
    { "create/directory [.mail]", "CREATE [.MAIL] /DIRECTORY" },
    { "cre/dir [.x]/log", "CREATE [.X] /DIRECTORY /LOG" },
    { "create/nodirectory x", "!NONEG NODIRECTORY" },
    { "create", "?File" },
    /* SET */
    { "SET PASSWORD", "SET PASSWORD" },
    { "set default [-]", "SET DEFAULT [-]" },
    { "set def sys$login:", "SET DEFAULT SYS$LOGIN:" },
    { "set default", "?Directory" },
    { "set terminal/scheme=amber", "SET TERMINAL /SCHEME=AMBER" },
    { "set term/sch=gr", "SET TERMINAL /SCHEME=GREEN" },
    { "set ter/scheme=pink", "!IVKEYW PINK" },
    { "set terminal/scheme", "!VALREQ SCHEME" },
    { "set terminal/scheme=(green,blue)", "!NOLIST SCHEME" },
    { "set halt", "SET HALT" },
    { "set halt restart", "SET HALT RESTART" },
    { "SET HALT RESTART/MAINT", "SET HALT RESTART /MAINTENANCE" },
    { "set halt res/nomaint", "SET HALT RESTART /NOMAINTENANCE" },
    { "set halt reboot", "!IVKEYW REBOOT" },
    { "set", "?What" },
    { "set nothing", "!IVKEYW NOTHING" },
    { "set password now", "!MAXPARM NOW" },
    { "set/scheme=green terminal", "SET TERMINAL /SCHEME=GREEN" },
    /* SHOW */
    { "show default", "SHOW DEFAULT" },
    { "sho users", "SHOW USERS" },
    { "show sys", "SHOW SYSTEM" },
    { "show ti", "SHOW TIME" },
    { "SHOW DEVICE DOOR1", "SHOW DEVICES DOOR1" },
    { "show dev", "SHOW DEVICES" },
    { "show term", "SHOW TERMINAL" },
    { "show mem/output=mem.txt", "SHOW MEMORY /OUTPUT=MEM.TXT" },
    { "show", "?What" },
    { "show time now", "!MAXPARM NOW" },
    { "show disk", "!IVKEYW DISK" },
    /* HELP */
    { "help", "HELP" },
    { "help copy", "HELP COPY" },
    { "hel \"set terminal\"", "HELP set terminal" },
    { "help a b", "!MAXPARM B" },
    /* RUN */
    { "run hack", "RUN HACK" },
    { "run ter", "RUN TERMINAL" },
    { "run h", "!ABKEYW H" },
    { "run holo", "RUN HOLOTAPE" },
    { "run", "?Program" },
    { "run game", "!IVKEYW GAME" },
    /* LOGIN, LOGOUT, ROBCO */
    { "login", "LOGIN" },
    { "login guest", "LOGIN GUEST" },
    { "logout", "LOGOUT" },
    { "logout now", "!MAXPARM NOW" },
    { "robco", "ROBCO" },
    { "rob ! the lore screen", "ROBCO" },
    /* DEVICE */
    { "device door1 toggle", "DEVICE DOOR1|TOGGLE" },
    { "dev light2 on", "DEVICE LIGHT2|ON" },
    { "device door1", "DEVICE DOOR1" },
    { "device", "?Device" },
    { "device a b c", "!MAXPARM C" },
    /* HOLOTAPE */
    { "holotape", "HOLOTAPE" },
    { "holo diary", "HOLOTAPE DIARY" },
    { "holo diary \"Day one\"", "HOLOTAPE DIARY|Day one" },
    /* MAIL */
    { "mail dir", "MAIL DIRECTORY" },
    { "mail read", "MAIL READ" },
    { "mail read 3", "MAIL READ 3" },
    { "mail read %x10", "MAIL READ %X10" },
    { "mail read 0", "!NUMRNG 0" },
    { "mail read three", "!IVNUMB THREE" },
    { "mail send oper/subject=\"Shift notes\"", "MAIL SEND OPER /SUBJECT=Shift notes" },
    { "mail send", "?To" },
    { "mail delete 2", "MAIL DELETE 2" },
    { "mail d", "!ABKEYW D" },
    { "mail", "?Action" },
    /* AUTHORIZE */
    { "authorize list", "AUTHORIZE LIST" },
    { "auth list bob", "AUTHORIZE LIST BOB" },
    { "auth add bob/password=\"Pa55word\"/privs=(oper,sec)/home=bob",
      "AUTHORIZE ADD BOB /PASSWORD=Pa55word /PRIVS=(OPER|SECURITY) /HOME=BOB" },
    { "auth mod bob/disabled", "AUTHORIZE MODIFY BOB /DISABLED" },
    { "auth mod bob/nohackable/noprivs", "AUTHORIZE MODIFY BOB /NOHACKABLE /NOPRIVS" },
    { "auth modify bob/privs=readall", "AUTHORIZE MODIFY BOB /PRIVS=(READALL)" },
    { "auth add bob/privs=(oper,boss)", "!IVKEYW BOSS" },
    { "auth add bob/privs=(s)", "!ABKEYW S" },
    { "auth add bob/privs=(a,b,c,d,e,f,g,h,i)", "!MAXVAL I" },
    { "auth remove bob", "AUTHORIZE REMOVE BOB" },
    { "auth rem", "?Username" },
    { "auth add", "?Username" },
    { "auth list/disabled", "!IVQUAL DISABLED" },
    { "auth", "?Action" },
    /* General syntax through the real table */
    { "", "" },
    { "   ! comment only", "" },
    { "frobnicate", "!IVVERB FROBNICATE" },
    { "copy \"a b\" c", "COPY a b|C" },
    { "type \"unterminated", "!UNTERM -" },
    { "copy a,b c", "!IVCHAR ,"  },
    { "dir/", "!IVQUAL /" },
};

TEST(parsed_lines)
{
    size_t i;
    size_t n = sizeof(lines) / sizeof(lines[0]);

    CHECK(n >= 80);
    for (i = 0; i < n; i++) {
        const char *got = render(lines[i].line);

        if (strcmp(got, lines[i].want) != 0) {
            RC_FAIL("line %zu \"%s\": got \"%s\", want \"%s\"", i, lines[i].line, got, lines[i].want);
        }
    }
}

TEST(privileges_of_the_table)
{
    CHECK_STR(render("auth list"), "AUTHORIZE LIST");
    CHECK_EQ(dcl_check_privs(&cmd, 0), DCL_SYS_NOPRIV);
    CHECK_EQ(dcl_check_privs(&cmd, DCL_PRIV_OPER), DCL_SYS_NOPRIV);
    CHECK_EQ(dcl_check_privs(&cmd, DCL_PRIV_SECURITY), DCL_NORMAL);
    CHECK_EQ(dcl_check_privs(&cmd, DCL_PRIV_SYSTEM), DCL_NORMAL);
    CHECK_STR(render("set halt"), "SET HALT");
    CHECK_EQ(dcl_cmd_privs(&cmd), DCL_PRIV_OPER);
    CHECK_EQ(dcl_check_privs(&cmd, 0), DCL_SYS_NOPRIV);
    CHECK_STR(render("set password"), "SET PASSWORD");
    CHECK_EQ(dcl_check_privs(&cmd, 0), DCL_NORMAL);
    CHECK_STR(render("dir"), "DIRECTORY");
    CHECK_EQ(dcl_check_privs(&cmd, 0), DCL_NORMAL);
}

TEST(qualifier_helpers_on_the_table)
{
    CHECK_STR(render("dir/full/out=x.lis"), "DIRECTORY /FULL /OUTPUT=X.LIS");
    CHECK(dcl_cmd_on(&cmd, "FULL"));
    CHECK(!dcl_cmd_on(&cmd, "SIZE"));
    CHECK_STR(dcl_cmd_value(&cmd, "OUTPUT"), "X.LIS");
    CHECK(dcl_cmd_value(&cmd, "FULL") == NULL);
    CHECK_STR(render("show time/out=t.txt"), "SHOW TIME /OUTPUT=T.TXT");
    CHECK_STR(dcl_cmd_value(&cmd, "OUTPUT"), "T.TXT");
}
