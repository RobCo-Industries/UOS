/* Host tests of src/shell/shrules.c: the shell's pure rules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_test.h"

#include "path.h"
#include "shrules.h"
#include "uaf.h"
#include "vfs.h"

/* ---- The error map ---- */

TEST(err_vfs_codes)
{
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, 0, -1, 0), DCL_NORMAL);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_NOTFOUND, -1, 0), DCL_SYS_FNF);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_DIRNF, -1, 0), DCL_SYS_DNF);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_READONLY, -1, 0), DCL_SYS_WRITLCK);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_PROTECTED, -1, 0), DCL_SYS_PROTECT);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_EXISTS, -1, 0), DCL_SYS_EXISTS);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_FULL, -1, 0), DCL_SYS_DISKFULL);
    /* No match at all: NOFILES. Matches all refused: the refusal. */
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_NOFILES, -1, 0), DCL_SYS_NOFILES);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_NOFILES, VP_E_NOPRIV, 2), DCL_SYS_NOPRIV);
    CHECK_EQ(sh_err_msg(SH_SRC_VFS, VFS_E_NOFILES, VP_E_PROTECTED, 1), DCL_SYS_PROTECT);
}

TEST(err_dev_codes)
{
    /* DEV: codes mean the device service's errors, not a card's. */
    CHECK_EQ(sh_err_msg(SH_SRC_DEV, VFS_E_NODEV, -1, 0), DCL_SYS_NODEV);
    CHECK_EQ(sh_err_msg(SH_SRC_DEV, VFS_E_NOPRIV, -1, 0), DCL_SYS_NOPRIV);
    CHECK_EQ(sh_err_msg(SH_SRC_DEV, VFS_E_READONLY, -1, 0), DCL_SYS_SENSOR);
    CHECK_EQ(sh_err_msg(SH_SRC_DEV, VFS_E_ARG, -1, 0), DCL_SYS_IVACT);
    CHECK_EQ(sh_err_msg(SH_SRC_DEV, VFS_E_WRITE, -1, 0), DCL_SYS_DEVERR);
    CHECK_EQ(sh_err_msg(SH_SRC_DEV, VFS_E_NOMEM, -1, 0), DCL_SYS_INSFMEM);
}

TEST(err_uaf_and_path_codes)
{
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_NO_USER, -1, 0), DCL_SYS_NOUSER);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_EXISTS, -1, 0), DCL_SYS_UEXISTS);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_FULL, -1, 0), DCL_SYS_UAFFULL);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_NAME, -1, 0), DCL_SYS_IVUSER);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_HOME, -1, 0), DCL_SYS_IVHOME);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_PASSWORD_LEN, -1, 0), DCL_SYS_IVPWD);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_PASSWORD_CHAR, -1, 0), DCL_SYS_IVPWD);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_BAD_LOGIN, -1, 0), DCL_SYS_BADPWD);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_DISABLED, -1, 0), DCL_SYS_BADPWD);
    CHECK_EQ(sh_err_msg(SH_SRC_UAF, UAF_E_CRC, -1, 0), DCL_SYS_UAFERR);
    CHECK_EQ(sh_err_msg(SH_SRC_VP, VP_E_NODEV, -1, 0), DCL_SYS_NODEV);
    CHECK_EQ(sh_err_msg(SH_SRC_VP, VP_E_ROOT, -1, 0), DCL_SYS_IVSPEC);
    CHECK_EQ(sh_err_msg(SH_SRC_VP, VP_E_PROTECTED, -1, 0), DCL_SYS_PROTECT);
}

/* ---- Wildcard loops ---- */

TEST(confirm_answers)
{
    CHECK_EQ(sh_confirm_answer("y"), SH_ANS_YES);
    CHECK_EQ(sh_confirm_answer(" Yes "), SH_ANS_YES);
    CHECK_EQ(sh_confirm_answer("true"), SH_ANS_YES);
    CHECK_EQ(sh_confirm_answer("1"), SH_ANS_YES);
    CHECK_EQ(sh_confirm_answer(""), SH_ANS_NO);
    CHECK_EQ(sh_confirm_answer("   "), SH_ANS_NO);
    CHECK_EQ(sh_confirm_answer("n"), SH_ANS_NO);
    CHECK_EQ(sh_confirm_answer("FALSE"), SH_ANS_NO);
    CHECK_EQ(sh_confirm_answer(NULL), SH_ANS_NO);
    CHECK_EQ(sh_confirm_answer("q"), SH_ANS_QUIT);
    CHECK_EQ(sh_confirm_answer("quit"), SH_ANS_QUIT);
    CHECK_EQ(sh_confirm_answer("a"), SH_ANS_ALL);
    CHECK_EQ(sh_confirm_answer("All"), SH_ANS_ALL);
    CHECK_EQ(sh_confirm_answer("ye"), SH_ANS_AGAIN);
    CHECK_EQ(sh_confirm_answer("yess"), SH_ANS_AGAIN);
    CHECK_EQ(sh_confirm_answer("maybe"), SH_ANS_AGAIN);
}

TEST(confirm_steps)
{
    uint8_t all = 0;

    CHECK_EQ(sh_confirm_need(0, 0), SH_DO_ACT);
    CHECK_EQ(sh_confirm_need(1, 0), SH_DO_ASK);
    CHECK_EQ(sh_confirm_step(SH_ANS_YES, &all), SH_DO_ACT);
    CHECK_EQ(all, 0);
    CHECK_EQ(sh_confirm_step(SH_ANS_NO, &all), SH_DO_SKIP);
    CHECK_EQ(sh_confirm_step(SH_ANS_QUIT, &all), SH_DO_STOP);
    CHECK_EQ(sh_confirm_step(SH_ANS_AGAIN, &all), SH_DO_ASK);
    CHECK_EQ(all, 0);
    CHECK_EQ(sh_confirm_step(SH_ANS_ALL, &all), SH_DO_ACT);
    CHECK_EQ(all, 1);
    /* After ALL, no more questions. */
    CHECK_EQ(sh_confirm_need(1, all), SH_DO_ACT);
}

TEST(tally_results)
{
    sh_tally_t t;
    uint32_t n = 99;

    sh_tally_init(&t);
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_DELETED, &n), DCL_SYS_DELETED);
    CHECK_EQ(n, 0);
    t.done = 3;
    t.failed = 1;
    t.last_err = DCL_SYS_WRITERR;
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_DELETED, &n), DCL_SYS_DELETED);
    CHECK_EQ(n, 3);
    t.done = 0;
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_DELETED, &n), DCL_SYS_WRITERR);
    t.failed = 0;
    t.refused = 2;
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_DELETED, &n), DCL_SYS_NOPRIV);
    /* Every file declined: zero files, a success. */
    sh_tally_init(&t);
    t.declined = 2;
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_COPIED, &n), DCL_SYS_COPIED);
    CHECK_EQ(n, 0);
    /* A cancel with nothing done is a cancel; with work done, the count. */
    t.cancelled = 1;
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_COPIED, &n), DCL_SYS_CANCEL);
    t.done = 1;
    CHECK_EQ(sh_tally_result(&t, DCL_SYS_COPIED, &n), DCL_SYS_COPIED);
    CHECK_EQ(n, 1);
}

/* ---- Text ---- */

TEST(unquote)
{
    char out[32];

    CHECK_EQ(sh_unquote("\"show%20time\"", out, sizeof(out)), 9);
    CHECK_STR(out, "show time");
    CHECK_EQ(sh_unquote("dir%2F%73ize", out, sizeof(out)), 8);
    CHECK_STR(out, "dir/size");
    CHECK_EQ(sh_unquote("plain", out, sizeof(out)), 5);
    CHECK_EQ(sh_unquote("%22q%22", out, sizeof(out)), 3);
    CHECK_STR(out, "\"q\"");
    CHECK_EQ(sh_unquote("bad%2", out, sizeof(out)), -1);
    CHECK_STR(out, "");
    CHECK_EQ(sh_unquote("bad%zz", out, sizeof(out)), -1);
    CHECK_EQ(sh_unquote("abcdef", out, 6), -1);
    CHECK_EQ(sh_unquote("abcde", out, 6), 5);
    CHECK_EQ(sh_unquote(NULL, out, sizeof(out)), -1);
}

TEST(spec_tail)
{
    CHECK_STR(sh_spec_tail("SD0:[HOME.ADMIN]A.TXT;1"), "A.TXT;1");
    CHECK_STR(sh_spec_tail("DEV:DOOR1"), "DOOR1");
    CHECK_STR(sh_spec_tail("A.TXT"), "A.TXT");
    CHECK_STR(sh_spec_tail(NULL), "");
}

TEST(next_line)
{
    const char *t = "one\ntwo\r\nthree\rfour";
    size_t pos = 0;
    size_t s;
    size_t n;

    CHECK_EQ(sh_next_line(t, strlen(t), &pos, &s, &n), 1);
    CHECK_EQ(s, 0);
    CHECK_EQ(n, 3);
    CHECK_EQ(sh_next_line(t, strlen(t), &pos, &s, &n), 1);
    CHECK_EQ(n, 3);
    CHECK(strncmp(t + s, "two", 3) == 0);
    CHECK_EQ(sh_next_line(t, strlen(t), &pos, &s, &n), 1);
    CHECK(strncmp(t + s, "three", n) == 0 && n == 5);
    CHECK_EQ(sh_next_line(t, strlen(t), &pos, &s, &n), 1);
    CHECK(strncmp(t + s, "four", n) == 0 && n == 4);
    CHECK_EQ(sh_next_line(t, strlen(t), &pos, &s, &n), 0);
    pos = 0;
    CHECK_EQ(sh_next_line("\n\n", 2, &pos, &s, &n), 1);
    CHECK_EQ(n, 0);
    CHECK_EQ(sh_next_line("\n\n", 2, &pos, &s, &n), 1);
    CHECK_EQ(sh_next_line("\n\n", 2, &pos, &s, &n), 0);
}

/* Rows of `t` at `width`, joined with '|'. */
static void rows_of(const char *t, size_t width, char *out, size_t size)
{
    size_t pos = 0, st, n, o = 0;

    out[0] = '\0';
    while (sh_next_row(t, strlen(t), width, &pos, &st, &n)) {
        o += (size_t)snprintf(out + o, size - o, "%s%.*s", o ? "|" : "", (int)n, t + st);
    }
}

TEST(next_row_wraps_in_bounded_rows)
{
    char b[128];
    size_t pos = 0, st, n, rows = 0;
    const size_t big = 1u << 20;
    char *line;

    rows_of("one\ntwo\r\nthree\rfour", 80, b, sizeof(b));
    CHECK_STR(b, "one|two|three|four");
    rows_of("abcdefgh\nxy", 3, b, sizeof(b));
    CHECK_STR(b, "abc|def|gh|xy");
    /* A line of exactly the width is one row, its line end consumed. */
    rows_of("abc\r\nde", 3, b, sizeof(b));
    CHECK_STR(b, "abc|de");
    rows_of("abc\n\nde", 3, b, sizeof(b));
    CHECK_STR(b, "abc||de");
    rows_of("abcdef", 3, b, sizeof(b));
    CHECK_STR(b, "abc|def");
    CHECK_EQ(sh_next_row(NULL, 4, 3, &pos, &st, &n), 0);
    CHECK_EQ(sh_next_row("ab", 2, 0, &pos, &st, &n), 1);
    CHECK_EQ(n, 1);
    /* A 1 MiB line: one row per call, each at most the width. */
    line = malloc(big);
    CHECK(line != NULL);
    memset(line, 'x', big);
    pos = 0;
    while (sh_next_row(line, big, 81, &pos, &st, &n)) {
        CHECK(n <= 81 && n > 0);
        rows++;
    }
    CHECK_EQ(rows, (big + 80u) / 81u);
    free(line);
    /* A call reads at most width + 1 bytes ahead: the sanitizer catches
     * a read past this exact-size buffer although `size` claims 1 MiB. */
    line = malloc(82);
    CHECK(line != NULL);
    memset(line, 'x', 82);
    pos = 0;
    CHECK_EQ(sh_next_row(line, big, 81, &pos, &st, &n), 1);
    CHECK_EQ(n, 81);
    CHECK_EQ(pos, 81);
    free(line);
}

TEST(output_written_only_on_success)
{
    CHECK_EQ(sh_output_write(DCL_NORMAL, 0), 1);
    CHECK_EQ(sh_output_write(DCL_SYS_DELETED, 0), 1);
    CHECK_EQ(sh_output_write(DCL_SYS_NOCLOCK, 0), 0);    /* W */
    CHECK_EQ(sh_output_write(DCL_SYS_NOFILES, 0), 0);    /* W */
    CHECK_EQ(sh_output_write(DCL_SYS_DNF, 0), 0);        /* E */
    CHECK_EQ(sh_output_write(DCL_SYS_TOOBIG, 0), 0);
    CHECK_EQ(sh_output_write(DCL_SYS_CANCEL, 0), 0);
    CHECK_EQ(sh_output_write(DCL_NORMAL, 1), 0);
}

TEST(job_tags)
{
    CHECK_EQ(sh_tag_live(1, 7, 7), 1);
    CHECK_EQ(sh_tag_live(1, 8, 7), 0);       /* a later job runs */
    CHECK_EQ(sh_tag_live(0, 7, 7), 0);       /* no job */
    CHECK_EQ(sh_tag_live(1, 0, 0), 0);       /* tag 0 is never a job */
}

TEST(covered_shell_owes)
{
    int owed = SH_OWE_NONE;

    CHECK_EQ(sh_job_waits(1, 0), 1);         /* a screen covers the shell */
    CHECK_EQ(sh_job_waits(1, 1), 0);
    CHECK_EQ(sh_job_waits(0, 0), 0);         /* no shell: nothing to wait for */
    CHECK_EQ(sh_takes_job(1, 1), 1);
    CHECK_EQ(sh_takes_job(1, 0), 0);         /* covered */
    CHECK_EQ(sh_takes_job(0, 0), 0);         /* not on the stack: the welcome */
    CHECK_EQ(sh_takes_job(0, 1), 0);
    CHECK_EQ(sh_owe(&owed, SH_OWE_STEP), 1); /* the first debt marks */
    CHECK_EQ(sh_owe(&owed, SH_OWE_STEP), 0);
    CHECK_EQ(owed, SH_OWE_STEP);
    CHECK_EQ(sh_owe(&owed, SH_OWE_END), 0);  /* an end outranks a step */
    CHECK_EQ(sh_owe(&owed, SH_OWE_STEP), 0);
    CHECK_EQ(owed, SH_OWE_END);
    CHECK_EQ(sh_owe_take(&owed), SH_OWE_END);
    CHECK_EQ(sh_owe_take(&owed), SH_OWE_NONE); /* taken once */
}

TEST(numbers_and_dates)
{
    char b[32];

    CHECK_EQ(sh_fmt_uptime(0, b, sizeof(b)), 10);
    CHECK_STR(b, "0 00:00:00");
    CHECK(sh_fmt_uptime(86400u * 3u + 3661u, b, sizeof(b)) > 0);
    CHECK_STR(b, "3 01:01:01");
    CHECK(sh_fmt_uptime(4294967295u, b, sizeof(b)) > 0);
    CHECK_STR(b, "49710 06:28:15");
    CHECK_EQ(sh_fmt_uptime(5, b, 5), -1);
    CHECK_STR(b, "");
    CHECK_EQ(sh_fmt_datetime(2077, 10, 23, 9, 47, 5, b, sizeof(b)), 20);
    CHECK_STR(b, "23-OCT-2077 09:47:05");
    CHECK_EQ(sh_fmt_datetime(2077, 13, 1, 0, 0, 0, b, sizeof(b)), -1);
    CHECK_EQ(sh_fmt_datetime(2077, 1, 0, 0, 0, 0, b, sizeof(b)), -1);
    CHECK_EQ(sh_fmt_datetime(2077, 1, 1, 24, 0, 0, b, sizeof(b)), -1);
    CHECK_EQ(sh_fmt_size(0, b, sizeof(b)), 1);
    CHECK_STR(b, "0");
    (void)sh_fmt_size(10239, b, sizeof(b));
    CHECK_STR(b, "10239");
    (void)sh_fmt_size(10240, b, sizeof(b));
    CHECK_STR(b, "10 KiB");
    (void)sh_fmt_size(64u * 1024u * 1024u, b, sizeof(b));
    CHECK_STR(b, "64 MiB");
    (void)sh_fmt_size(6ull * 1024u * 1024u * 1024u * 1024u, b, sizeof(b));
    CHECK_STR(b, "6144 GiB");
    CHECK_EQ(sh_fmt_size(123456, b, 3), -1);
}

TEST(dir_lines)
{
    char b[96];

    CHECK(sh_dir_line("NOTICE", "TXT", 0, 1234, "01-JAN-2077 00:00:00", 0, b, sizeof(b)) > 0);
    CHECK_STR(b, "NOTICE.TXT");
    CHECK(sh_dir_line("NOTICE", "TXT", 0, 1234, NULL, SH_DIR_SIZE, b, sizeof(b)) > 0);
    CHECK_STR(b, "NOTICE.TXT         1234");
    CHECK(sh_dir_line("LOGS", "", 1, 0, NULL, SH_DIR_SIZE, b, sizeof(b)) > 0);
    CHECK_STR(b, "LOGS.DIR");
    CHECK(sh_dir_line("A", "B", 0, 7, "01-JAN-2077 00:00:00", SH_DIR_SIZE | SH_DIR_DATE, b,
                      sizeof(b)) > 0);
    CHECK_STR(b, "A.B                   7  01-JAN-2077 00:00:00");
    CHECK(sh_dir_line("A", "B", 0, 7, "01-JAN-2077 00:00:00", SH_DIR_DATE, b, sizeof(b)) > 0);
    CHECK_STR(b, "A.B                      01-JAN-2077 00:00:00");
    CHECK_EQ(sh_dir_line("A", "B", 0, 7, NULL, SH_DIR_SIZE, b, 8), -1);
    CHECK(sh_dir_total(1, 7, SH_DIR_SIZE, b, sizeof(b)) > 0);
    CHECK_STR(b, "Total of 1 file, 7 bytes.");
    CHECK(sh_dir_total(3, 20480, SH_DIR_SIZE, b, sizeof(b)) > 0);
    CHECK_STR(b, "Total of 3 files, 20 KiB.");
    CHECK(sh_dir_total(0, 0, 0, b, sizeof(b)) > 0);
    CHECK_STR(b, "Total of 0 files.");
}

TEST(create_end)
{
    CHECK(sh_create_end("."));
    CHECK(!sh_create_end(".."));
    CHECK(!sh_create_end(" ."));
    CHECK(!sh_create_end(""));
    CHECK(!sh_create_end(NULL));
}

/* ---- HELP ---- */

TEST(help_lookup)
{
    const char *spec;

    CHECK_EQ(sh_help_spec(&dcl_uos_table, NULL, &spec), DCL_NORMAL);
    CHECK_STR(spec, "SYS$HELP:HELP.HLP");
    CHECK_EQ(sh_help_spec(&dcl_uos_table, "", &spec), DCL_NORMAL);
    CHECK_STR(spec, "SYS$HELP:HELP.HLP");
    CHECK_EQ(sh_help_spec(&dcl_uos_table, "dir", &spec), DCL_NORMAL);
    CHECK_STR(spec, "SYS$HELP:DIRECTOR.HLP");
    CHECK_EQ(sh_help_spec(&dcl_uos_table, "Authorize", &spec), DCL_NORMAL);
    CHECK_STR(spec, "SYS$HELP:AUTHORIZ.HLP");
    CHECK_EQ(sh_help_spec(&dcl_uos_table, "LOG", &spec), DCL_ABVERB);
    CHECK(spec == NULL);
    CHECK_EQ(sh_help_spec(&dcl_uos_table, "BOGUS", &spec), DCL_SYS_NOHELP);
    CHECK_EQ(sh_help_spec(NULL, "DIR", &spec), DCL_SYS_NOHELP);
}

/* Every verb's help file is on the default card, with an 8.3 name. */
TEST(help_files_shipped)
{
    size_t i;

    for (i = 0; i < dcl_uos_table.count; i++) {
        const char *spec = dcl_uos_table.verbs[i].help;
        char path[96];
        const char *name;
        FILE *f;

        CHECK(spec != NULL);
        if (spec == NULL) {
            continue;
        }
        CHECK(strncmp(spec, "SYS$HELP:", 9) == 0);
        name = spec + 9;
        CHECK(strlen(name) <= 12 && strchr(name, '.') - name <= 8);
        snprintf(path, sizeof(path), "card/UOS/SYS/HELP/%s", name);
        f = fopen(path, "rb");
        CHECK(f != NULL);
        if (f != NULL) {
            fclose(f);
        }
    }
}

/* ---- Programs ---- */

static int prog_a(const char *args, sh_prog_done_fn done, void *tag)
{
    (void)args;
    (void)done;
    (void)tag;
    return 0;
}

static int prog_b(const char *args, sh_prog_done_fn done, void *tag)
{
    (void)args;
    (void)done;
    (void)tag;
    return 1;
}

TEST(programs)
{
    sh_progtab_t t;
    char name[8];
    unsigned i;

    sh_prog_init(&t);
    CHECK(sh_prog_find(&t, "HACK") == NULL);
    CHECK_EQ(sh_prog_add(&t, "hack", prog_a), 0);
    CHECK(sh_prog_find(&t, "HACK") == prog_a);
    CHECK(sh_prog_find(&t, "Hack") == prog_a);
    CHECK_EQ(sh_prog_add(&t, "HACK", prog_b), SH_PROG_ERR_DUP);
    CHECK_EQ(sh_prog_add(&t, "", prog_b), SH_PROG_ERR_ARG);
    CHECK_EQ(sh_prog_add(&t, "TOOLONGNAME13", prog_b), SH_PROG_ERR_ARG);
    CHECK_EQ(sh_prog_add(&t, "BAD-NAME", prog_b), SH_PROG_ERR_ARG);
    CHECK_EQ(sh_prog_add(&t, "OK", NULL), SH_PROG_ERR_ARG);
    CHECK_EQ(sh_prog_add(&t, "TERMINAL", prog_b), 0);
    CHECK(sh_prog_find(&t, "TERMINAL") == prog_b);
    for (i = 2; i < SH_PROG_MAX; i++) {
        snprintf(name, sizeof(name), "P%u", i);
        CHECK_EQ(sh_prog_add(&t, name, prog_a), 0);
    }
    CHECK_EQ(sh_prog_add(&t, "ONEMORE", prog_a), SH_PROG_ERR_FULL);
}

/* ---- Markers ---- */

TEST(verb_words)
{
    dcl_cmd_t cmd;
    char w[32];

    sh_verb_word(NULL, w, sizeof(w));
    CHECK_STR(w, "-");
    CHECK_EQ(dcl_parse("set halt restart", &dcl_uos_table, &cmd), DCL_NORMAL);
    sh_verb_word(&cmd, w, sizeof(w));
    CHECK_STR(w, "SET_HALT");
    CHECK_EQ(dcl_parse("del a.txt", &dcl_uos_table, &cmd), DCL_NORMAL);
    sh_verb_word(&cmd, w, sizeof(w));
    CHECK_STR(w, "DELETE");
    CHECK_EQ(dcl_parse("! a comment", &dcl_uos_table, &cmd), DCL_NORMAL);
    sh_verb_word(&cmd, w, sizeof(w));
    CHECK_STR(w, "-");
}

TEST(halt_request_per_qualifier)
{
    CHECK_EQ(sh_halt_mreq(0), SH_MREQ_NONE);
    CHECK_EQ(sh_halt_mreq(1), SH_MREQ_WRITE);
    CHECK_EQ(sh_halt_mreq(-1), SH_MREQ_CLEAR);
}

TEST(maint_note_is_one_printable_line)
{
    char buf[80];
    char small[7];
    char longname[100];

    CHECK_EQ(sh_maint_note("OPER", buf, sizeof(buf)), 10);
    CHECK_STR(buf, "UOS OPER\r\n");
    CHECK_EQ(sh_maint_note("A\tB\x7f", buf, sizeof(buf)), 10);
    CHECK_STR(buf, "UOS A?B?\r\n");
    CHECK_EQ(sh_maint_note(NULL, buf, sizeof(buf)), 6);
    CHECK_STR(buf, "UOS \r\n");
    memset(longname, 'X', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    CHECK_EQ(sh_maint_note(longname, buf, sizeof(buf)), 65);
    CHECK_EQ(strlen(buf), 65u);
    CHECK_EQ(sh_maint_note("OPER", small, sizeof(small)), 6);
    CHECK_STR(small, "UOS \r\n");
    CHECK_EQ(sh_maint_note("OPER", small, 6), -1);
}
