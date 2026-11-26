/*
 * The shell's commands: files, SET, SHOW, HELP, RUN, devices and the
 * session verbs. AUTHORIZE and SET PASSWORD are in shuaf.c. Each command
 * is a small state machine: start() and step() return SH_DONE, SH_MORE or
 * SH_WAIT, and shell.c drives them. Every error goes through
 * sh_err_msg(): no command formats its own error text.
 */

#include <robco/alloc.h>
#include <robco/console.h>
#include <robco/klibc.h>
#include <robco/rtc.h>

#include "devs.h"
#include "robco_config.h"
#include "sess.h"
#include "shint.h"
#include "uos.h"

static const char *param(const sh_job_t *j, unsigned i)
{
    return i < j->cmd.nparams ? j->cmd.params[i] : NULL;
}

static int on(const sh_job_t *j, const char *qual)
{
    return dcl_cmd_on(&j->cmd, qual);
}

static int has_wild(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s == '*' || *s == '%') {
            return 1;
        }
    }
    return 0;
}

/* ---- Printing a text buffer in steps: TYPE and HELP ---- */

/* Prints up to SH_LINES_PER_STEP rows of j->buf from j->bpos, wrapped at
 * the line width. A long line counts one row per wrapped segment, and
 * sh_next_row() reads at most a row's width ahead, so one step prints
 * and scans at most SH_LINES_PER_STEP * (UOS_LINE_MAX + 3) bytes, however
 * long the line. Returns 1 while text remains. */
static int print_lines(sh_job_t *j)
{
    char row[UOS_LINE_MAX + 2];
    unsigned n;
    size_t start;
    size_t len;

    for (n = 0; n < SH_LINES_PER_STEP; n++) {
        if (!sh_next_row(j->buf, j->blen, UOS_LINE_MAX + 1u, &j->bpos, &start, &len)) {
            break;
        }
        k_memcpy(row, j->buf + start, len);
        row[len] = '\0';
        sh_out(j, row);
    }
    return j->bpos < j->blen;
}

/* The count of matches the access table refused, as an information line. */
static void say_skipped(sh_job_t *j)
{
    if (j->tally.refused > 0) {
        sh_say(j, DCL_SYS_SKIPPED, NULL, j->tally.refused);
    }
}

/* ---- DIRECTORY ---- */

static void fat_date(const vfs_entry_t *e, char *buf, size_t size)
{
    buf[0] = '\0';
    if (e->date == 0) {
        return;
    }
    (void)sh_fmt_datetime(1980u + (e->date >> 9), (e->date >> 5) & 15u, e->date & 31u,
                          e->time >> 11, (e->time >> 5) & 63u, (e->time & 31u) * 2u, buf, size);
}

static int dir_start(sh_job_t *j)
{
    const char *spec = param(j, 0) != NULL ? param(j, 0) : "";
    char line[VP_DISPLAY_MAX + 16];
    int rc;

    if (on(j, "FULL")) {
        j->cols = SH_DIR_SIZE | SH_DIR_DATE;
    }
    if (on(j, "SIZE")) {
        j->cols |= SH_DIR_SIZE;
    }
    if (on(j, "DATE")) {
        j->cols |= SH_DIR_DATE;
    }
    j->out_spec = dcl_cmd_value(&j->cmd, "OUTPUT");
    j->ents = k_malloc(SH_DIR_MAX * sizeof(vfs_entry_t));
    if (j->ents == NULL) {
        return sh_end_err(j, SH_SRC_VFS, VFS_E_NOMEM, NULL);
    }
    rc = vfs_list(j->ctx, j->privs, spec, j->ents, SH_DIR_MAX, &j->ecount, &j->etotal);
    j->tally.refused = vfs_last_refused();
    if (rc != VFS_OK) {
        return sh_end_err(j, SH_SRC_VFS, rc, spec[0] != '\0' ? spec : NULL);
    }
    if (j->ecount == 0) {
        /* Every match refused is no privilege; no match is no files. */
        return sh_end_err(j, SH_SRC_VFS, VFS_E_NOFILES, NULL);
    }
    if (spec[0] == '\0') {
        (void)vp_show_default(j->ctx, j->tmp, sizeof(j->tmp));
        spec = j->tmp;
    }
    k_snprintf(line, sizeof(line), "Directory %s", spec);
    sh_out(j, line);
    sh_out(j, "");
    return SH_MORE;
}

static int dir_step(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 2];
    char date[VFS_DATE_LEN + 1];
    unsigned n;

    for (n = 0; n < SH_LINES_PER_STEP && j->eidx < j->ecount; n++, j->eidx++) {
        const vfs_entry_t *e = &j->ents[j->eidx];

        fat_date(e, date, sizeof(date));
        if (sh_dir_line(e->name, e->type, e->is_dir, e->size, date, j->cols, line,
                        sizeof(line)) >= 0) {
            sh_out(j, line);
        }
        if (!e->is_dir) {
            j->tally.done++;
            j->ebytes += e->size;
        }
    }
    if (j->eidx < j->ecount) {
        return SH_MORE;
    }
    sh_out(j, "");
    (void)sh_dir_total(j->tally.done, j->ebytes, j->cols, line, sizeof(line));
    sh_out(j, line);
    if (j->etotal > j->ecount) {
        k_snprintf(line, sizeof(line), "%u more entries not shown.",
                   (unsigned)(j->etotal - j->ecount));
        sh_out(j, line);
    }
    if (j->tally.refused > 0 &&
        dcl_format_message(line, sizeof(line), DCL_SYS_SKIPPED, NULL, j->tally.refused) >= 0) {
        sh_out(j, line);
    }
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* ---- File loops: TYPE, COPY, DELETE, RENAME ---- */

#define P_NEXT      0       /* take the next match */
#define P_ACT       1       /* act on j->cur */
#define P_WORK      2       /* a stepped action runs: the copy, the print */
#define P_ASK       3       /* /CONFIRM asks about j->cur */

typedef struct {
    const char *op;         /* the marker word */
    const char *ask;        /* the /CONFIRM question */
    int iter_op;            /* VP_OP_* */
    dcl_msg_t ok;           /* the count message */
    dcl_msg_t log;          /* the /LOG message */
} loop_def_t;

static const loop_def_t def_copy = { "copy", "copy", VP_OP_READ, DCL_SYS_COPIED, DCL_SYS_FILCOPY };
static const loop_def_t def_delete = { "delete", "delete", VP_OP_DELETE, DCL_SYS_DELETED,
                                       DCL_SYS_FILDEL };
static const loop_def_t def_rename = { "rename", "rename", VP_OP_DELETE, DCL_SYS_RENAMED,
                                       DCL_SYS_FILREN };
static const loop_def_t def_type = { "type", "type", VP_OP_READ, DCL_NORMAL, DCL_NORMAL };

static const loop_def_t *loop_def(const sh_job_t *j)
{
    const char *v = j->cmd.verb->name;

    if (k_strcmp(v, "COPY") == 0) {
        return &def_copy;
    }
    if (k_strcmp(v, "DELETE") == 0) {
        return &def_delete;
    }
    return &def_rename;
}

/* The copy in progress: about 1.7 KiB, static as vfs.h asks. */
static vfs_copy_t copy_state;
static int copy_running;

static int loop_start(sh_job_t *j)
{
    const loop_def_t *d = loop_def(j);
    int rc;

    j->confirm = on(j, "CONFIRM");
    j->log = on(j, "LOG");
    rc = vfs_iter_begin(&j->it, j->ctx, j->privs, param(j, 0), d->iter_op);
    /* Refusals count on failure too: every match refused is NOPRIV. */
    j->tally.refused = j->it.skipped;
    if (rc != VFS_OK) {
        return sh_end_err(j, SH_SRC_VFS, rc, param(j, 0));
    }
    j->it_open = 1;
    j->phase = P_NEXT;
    return SH_MORE;
}

static void loop_fail(sh_job_t *j, const loop_def_t *d, int src, int rc)
{
    dcl_msg_t m = sh_err_msg(src, rc, -1, 0);

    j->tally.failed++;
    j->tally.last_err = m;
    sh_say(j, m, j->cur, 0);
    sh_file_mark(j, d->op, j->cur, dcl_msg_ident(m));
}

static void loop_ok(sh_job_t *j, const loop_def_t *d)
{
    j->tally.done++;
    if (j->log) {
        sh_say(j, d->log, j->cur, 0);
    }
    sh_file_mark(j, d->op, j->cur, "ok");
}

static int loop_finish(sh_job_t *j)
{
    const loop_def_t *d = loop_def(j);
    uint32_t num;
    dcl_msg_t m = sh_tally_result(&j->tally, d->ok, &num);

    say_skipped(j);
    if (m == d->ok) {
        return sh_end(j, m, NULL, num, 1);
    }
    return sh_end(j, m, NULL, 0, 1);
}

static void on_confirm(int result, const char *text, unsigned len, void *ctx);

static int loop_step(sh_job_t *j)
{
    const loop_def_t *d = loop_def(j);
    int rc;

    char q[VP_DISPLAY_MAX + 24];

    if (j->phase == P_NEXT) {

        j->cur = vfs_iter_next(&j->it);
        if (j->cur == NULL) {
            return loop_finish(j);
        }
        j->phase = P_ASK;
    }
    if (j->phase == P_ASK) {
        if (sh_confirm_need(j->confirm, j->all) == SH_DO_ASK) {
            k_snprintf(q, sizeof(q), "%s, %s? [N]:", sh_spec_tail(j->cur), d->ask);
            if (sh_ask("confirm", q, 0, 8, on_confirm) != 0) {
                return sh_end(j, DCL_SYS_BUSY, NULL, 0, 1);
            }
            return SH_WAIT;
        }
        j->phase = P_ACT;
    }
    if (j->phase == P_ACT) {
        if (d == &def_delete) {
            rc = vfs_delete(j->ctx, j->privs, j->cur);
        } else if (d == &def_rename) {
            rc = vfs_rename(j->ctx, j->privs, j->cur, param(j, 1));
        } else {
            rc = vfs_copy_begin(&copy_state, j->ctx, j->privs, j->cur, param(j, 1), VFS_W_NEW);
            if (rc == VFS_OK) {
                copy_running = 1;
                j->phase = P_WORK;
                return SH_MORE;
            }
        }
        if (rc == VFS_OK) {
            loop_ok(j, d);
        } else {
            loop_fail(j, d, SH_SRC_VFS, rc);
        }
        j->phase = P_NEXT;
        return SH_MORE;
    }
    /* P_WORK: one step of the copy. */
    rc = vfs_copy_step(&copy_state);
    if (rc == 1) {
        return SH_MORE;
    }
    copy_running = 0;
    if (rc == 0) {
        loop_ok(j, d);
    } else {
        loop_fail(j, d, SH_SRC_VFS, rc);
    }
    j->phase = P_NEXT;
    return SH_MORE;
}

static void on_confirm(int result, const char *text, unsigned len, void *ctx)
{
    sh_job_t *j = &sh_job;
    int ans = result == CON_OK ? sh_confirm_answer(text) : SH_ANS_QUIT;
    int what = sh_confirm_step(ans, &j->all);

    (void)len;
    (void)ctx;
    if (!j->active) {
        return;
    }
    if (result != CON_OK) {
        /* Ctrl-C at the question cancels the command; Q only stops the
         * loop and reports the files done. */
        j->cancel = 1;
        sh_resume(j, SH_MORE);
        return;
    }
    switch (what) {
    case SH_DO_ACT:
        j->phase = P_ACT;
        break;
    case SH_DO_SKIP:
        j->tally.declined++;
        sh_file_mark(j, loop_def(j)->op, j->cur, "skip");
        j->phase = P_NEXT;
        break;
    case SH_DO_STOP:
        j->tally.cancelled = 1;
        sh_resume(j, loop_finish(j));
        return;
    default:
        /* Not an answer: the same question again. */
        j->phase = P_ASK;
        break;
    }
    sh_resume(j, SH_MORE);
}

static void loop_stop(sh_job_t *j)
{
    (void)j;
    if (copy_running) {
        vfs_copy_abort(&copy_state);
        copy_running = 0;
    }
}

/* ---- TYPE ---- */

static void type_paged(int result, void *ctx);

static int type_start(sh_job_t *j)
{
    const char *spec = param(j, 0);
    int rc;

    if (has_wild(spec)) {
        rc = vfs_iter_begin(&j->it, j->ctx, j->privs, spec, VP_OP_READ);
        j->tally.refused = j->it.skipped;
        if (rc != VFS_OK) {
            return sh_end_err(j, SH_SRC_VFS, rc, spec);
        }
        j->it_open = 1;
    }
    j->phase = P_NEXT;
    return SH_MORE;
}

static int type_step(sh_job_t *j)
{
    size_t len;
    int rc;

    if (j->phase == P_WORK) {
        if (print_lines(j)) {
            return SH_MORE;
        }
        vfs_free(j->buf);
        j->buf = NULL;
        j->tally.done++;
        j->phase = P_NEXT;
        return SH_MORE;
    }
    if (j->it_open) {
        j->cur = vfs_iter_next(&j->it);
    } else {
        j->cur = j->tally.done + j->tally.failed == 0 ? param(j, 0) : NULL;
    }
    if (j->cur == NULL) {
        say_skipped(j);
        if (j->tally.done == 0 && j->tally.failed > 0) {
            return sh_end(j, j->tally.last_err, NULL, 0, 0);
        }
        return sh_end(j, DCL_NORMAL, NULL, 0, 0);
    }
    rc = vfs_read_all(j->ctx, j->privs, j->cur, NULL, SH_TYPE_MAX, &j->buf, &len);
    if (rc != VFS_OK) {
        if (!j->it_open) {
            return sh_end_err(j, SH_SRC_VFS, rc, j->cur);
        }
        loop_fail(j, &def_type, SH_SRC_VFS, rc);
        return SH_MORE;
    }
    j->blen = len;
    j->bpos = 0;
    sh_file_mark(j, "type", j->cur, "ok");
    if (on(j, "PAGE")) {
        rc = con_page_text(sh_screen(), j->cur, j->buf, len, type_paged, sh_job_tag(j));
        vfs_free(j->buf);
        j->buf = NULL;
        if (rc != 0) {
            return sh_end_err(j, SH_SRC_VFS, VFS_E_NOMEM, NULL);
        }
        return SH_WAIT;
    }
    if (j->it_open) {
        sh_out(j, j->cur);
        sh_out(j, "");
    }
    j->phase = P_WORK;
    return SH_MORE;
}

static void type_paged(int result, void *ctx)
{
    if (!sh_job_live(ctx, "pager")) {
        return;
    }
    if (result != CON_OK) {
        /* Ctrl-C in the pager cancels the command, like at the prompt. */
        sh_job.cancel = 1;
        sh_resume(&sh_job, SH_MORE);
        return;
    }
    sh_job.tally.done++;
    sh_job.phase = P_NEXT;
    sh_resume(&sh_job, SH_MORE);
}

/* ---- HELP ---- */

static int help_start(sh_job_t *j)
{
    const char *spec;
    dcl_msg_t m = sh_help_spec(&dcl_uos_table, param(j, 0), &spec);
    size_t len;
    int rc;

    if (m != DCL_NORMAL) {
        return sh_end(j, m, param(j, 0), 0, 1);
    }
    rc = vfs_read_all(j->ctx, j->privs, spec, NULL, SH_OUT_MAX, &j->buf, &len);
    if (rc != VFS_OK) {
        return sh_end_err(j, SH_SRC_VFS, rc, spec);
    }
    j->blen = len;
    j->bpos = 0;
    return SH_MORE;
}

static int help_step(sh_job_t *j)
{
    if (print_lines(j)) {
        return SH_MORE;
    }
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* ---- CREATE ---- */

static void on_create_line(int result, const char *text, unsigned len, void *ctx);

static int create_ask(void)
{
    return sh_ask("line", ">", 0, 255, on_create_line) == 0 ? SH_WAIT
                                                           : sh_end(&sh_job, DCL_SYS_BUSY, NULL,
                                                                    0, 1);
}

static int create_start(sh_job_t *j)
{
    const char *spec = param(j, 0);
    int rc;

    j->log = on(j, "LOG");
    if (on(j, "DIRECTORY")) {
        rc = vfs_mkdir(j->ctx, j->privs, spec);
        if (rc != VFS_OK) {
            return sh_end_err(j, SH_SRC_VFS, rc, spec);
        }
        return sh_end(j, DCL_SYS_CREATED, spec, 0, j->log);
    }
    rc = vfs_exists(j->ctx, j->privs, spec);
    if (rc == 1) {
        return sh_end_err(j, SH_SRC_VFS, VFS_E_EXISTS, spec);
    }
    if (rc < 0 && rc != VFS_E_NOTFOUND) {
        return sh_end_err(j, SH_SRC_VFS, rc, spec);
    }
    j->buf = k_malloc(SH_CREATE_MAX);
    if (j->buf == NULL) {
        return sh_end_err(j, SH_SRC_VFS, VFS_E_NOMEM, NULL);
    }
    j->blen = 0;
    return create_ask();
}

static void on_create_line(int result, const char *text, unsigned len, void *ctx)
{
    sh_job_t *j = &sh_job;
    int rc;

    (void)ctx;
    if (!j->active) {
        return;
    }
    if (result != CON_OK) {
        sh_resume(j, sh_end(j, DCL_SYS_CANCEL, NULL, 0, 1));
        return;
    }
    if (!sh_create_end(text)) {
        if (j->blen + len + 1u > SH_CREATE_MAX) {
            sh_resume(j, sh_end_err(j, SH_SRC_VFS, VFS_E_TOOBIG, NULL));
            return;
        }
        k_memcpy(j->buf + j->blen, text, len);
        j->blen += len;
        j->buf[j->blen++] = '\n';
        sh_resume(j, create_ask());
        return;
    }
    rc = vfs_write_all(j->ctx, j->privs, param(j, 0), NULL, j->buf, j->blen, VFS_W_NEW);
    if (rc != VFS_OK) {
        sh_resume(j, sh_end_err(j, SH_SRC_VFS, rc, param(j, 0)));
        return;
    }
    j->tally.done = 1;
    sh_resume(j, sh_end(j, DCL_SYS_CREATED, param(j, 0), 0, j->log));
}

/* ---- SET ---- */

static int set_default(sh_job_t *j)
{
    vp_ctx_t tmp = *j->ctx;
    vfs_entry_t e;
    size_t count;
    size_t total;
    int rc = vp_set_default(&tmp, param(j, 0));

    if (rc != 0) {
        return sh_end_err(j, SH_SRC_VP, rc, param(j, 0));
    }
    rc = vfs_list(&tmp, j->privs, "", &e, 1, &count, &total);
    if (rc != VFS_OK && rc != VFS_E_NOFILES) {
        return sh_end_err(j, SH_SRC_VFS, rc, param(j, 0));
    }
    *j->ctx = tmp;
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static const char *const schemes[] = { "GREEN", "AMBER", "BLUE", "WHITE" };

static int set_terminal(sh_job_t *j)
{
    const char *v = dcl_cmd_value(&j->cmd, "SCHEME");
    unsigned i;

    if (v != NULL) {
        for (i = 0; i < 4u; i++) {
            if (k_strcmp(v, schemes[i]) == 0 && uos.fb_ok) {
                console_set_scheme(i);
                UOS_MARK("term scheme=%s", schemes[i]);
            }
        }
    }
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* Writes or removes /ROBCO/MAINT.REQ for SET HALT. 0, or 1 after sh_end():
 * a failed change stops the command before any halt or restart. */
static int halt_request(sh_job_t *j, int what)
{
    char note[UAF_NAME_MAX + 8];
    const char *res;
    int done = 0;
    int rc;

    if (what == SH_MREQ_NONE) {
        return 0;
    }
    if (what == SH_MREQ_WRITE) {
        (void)sh_maint_note(j->user, note, sizeof(note));
        rc = vfs_maint_request(note, &done);
        res = rc != VFS_OK ? "failed" : done ? "written" : "kept";
    } else {
        rc = vfs_maint_clear(&done);
        res = rc != VFS_OK ? "failed" : done ? "removed" : "none";
    }
    UOS_MARK("maint_req op=%s result=%s rc=%s", what == SH_MREQ_WRITE ? "write" : "clear",
             res, vfs_strerror(rc));
    if (rc != VFS_OK) {
        (void)sh_end(j, what == SH_MREQ_WRITE ? DCL_SYS_NOMAINT : DCL_SYS_MREQKEPT,
                     dcl_msg_text(vfs_msg(rc)), 0, 1);
        return 1;
    }
    return 0;
}

static int set_halt(sh_job_t *j)
{
    const dcl_qval_t *m = dcl_cmd_qual(&j->cmd, "MAINTENANCE");
    int restart = param(j, 0) != NULL;
    int what = sh_halt_mreq(m == NULL ? 0 : m->negated ? -1 : 1);

    if (halt_request(j, what) != 0) {
        return SH_DONE;
    }
    if (what == SH_MREQ_WRITE) {
        sh_out(j, "MAINTENANCE REQUESTED FOR THE NEXT START.");
    }
    sh_out(j, restart ? "SYSTEM RESTARTING." : "SYSTEM HALTED. RESTART THE TERMINAL BY POWER.");
    j->after = restart ? SH_AFTER_RESTART : SH_AFTER_HALT;
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* ---- SHOW ---- */

static void show_start(sh_job_t *j)
{
    j->out_spec = dcl_cmd_value(&j->cmd, "OUTPUT");
}

static int show_default(sh_job_t *j)
{
    show_start(j);
    (void)vp_show_default(j->ctx, j->tmp, sizeof(j->tmp));
    sh_out(j, j->tmp);
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static void uptime_text(char *buf, size_t size)
{
    (void)sh_fmt_uptime(uos_ticks() / UOS_TICK_HZ, buf, size);
}

static int show_users(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    char privs[UAF_PRIVS_TEXT_MAX];

    show_start(j);
    if (uaf_privs_format(sess_privs(), privs, sizeof(privs)) != UAF_OK || privs[0] == '\0') {
        k_snprintf(privs, sizeof(privs), "NONE");
    }
    sh_out(j, "      UOS user sessions: one terminal");
    sh_out(j, "Username          Terminal  Home      Privileges");
    k_snprintf(line, sizeof(line), "%-16s  %-8s  %-8s  %s", sess_user(), "CONSOLE", sess_home(),
               privs);
    sh_out(j, line);
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static int show_system(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    char up[24];
    const uaf_db_t *db = sess_db();

    show_start(j);
    uptime_text(up, sizeof(up));
    k_snprintf(line, sizeof(line), "UOS V%s on %s", UOS_VERSION_STR, uos_settings()->name);
    sh_out(j, line);
    k_snprintf(line, sizeof(line), "Uptime %s", up);
    sh_out(j, line);
    k_snprintf(line, sizeof(line), "Card %s, devices %u, users %u", uos_card_name(uos.card),
               devs_count(), db != NULL ? (unsigned)db->count : 0u);
    sh_out(j, line);
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static int show_time(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    char up[24];
    datetime_t dt;

    show_start(j);
    if (uos.rtc && clock_get(&dt) == 0 &&
        sh_fmt_datetime(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, line,
                        sizeof(line)) > 0) {
        sh_out(j, line);
        return sh_end(j, DCL_NORMAL, NULL, 0, 0);
    }
    uptime_text(up, sizeof(up));
    k_snprintf(line, sizeof(line), "Uptime %s", up);
    sh_out(j, line);
    return sh_end(j, DCL_SYS_NOCLOCK, NULL, 0, 1);
}

/* A device's status line through DEV:, printed. 0 or a VFS_E_* code. */
static int dev_line(sh_job_t *j, const char *name)
{
    char spec[24];
    char *buf = NULL;
    size_t len = 0;
    int rc;

    k_snprintf(spec, sizeof(spec), "DEV:%s", name);
    rc = vfs_read_all(j->ctx, j->privs, spec, NULL, 256, &buf, &len);
    if (rc != VFS_OK) {
        return rc;
    }
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    sh_out(j, buf);
    vfs_free(buf);
    return 0;
}

static int show_devices(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    size_t i;
    int rc;

    show_start(j);
    if (devs_status_header(line, sizeof(line)) >= 0) {
        sh_out(j, line);
    }
    if (param(j, 0) != NULL) {
        rc = dev_line(j, param(j, 0));
        return rc == 0 ? sh_end(j, DCL_NORMAL, NULL, 0, 0)
                       : sh_end_err(j, SH_SRC_DEV, rc, param(j, 0));
    }
    j->ents = k_malloc(32u * sizeof(vfs_entry_t));
    if (j->ents == NULL) {
        return sh_end_err(j, SH_SRC_VFS, VFS_E_NOMEM, NULL);
    }
    rc = vfs_list(j->ctx, j->privs, "DEV:", j->ents, 32u, &j->ecount, &j->etotal);
    if (rc != VFS_OK) {
        return sh_end_err(j, SH_SRC_DEV, rc, NULL);
    }
    for (i = 0; i < j->ecount; i++) {
        rc = dev_line(j, j->ents[i].name);
        if (rc != 0) {
            sh_say(j, sh_err_msg(SH_SRC_DEV, rc, -1, 0), j->ents[i].name, 0);
        }
    }
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static int show_terminal(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    unsigned id = console_scheme();

    show_start(j);
    k_snprintf(line, sizeof(line), "Terminal CONSOLE: %u columns, %u rows, %u text rows",
               console_cols(), console_rows(), con_text_rows());
    sh_out(j, line);
    k_snprintf(line, sizeof(line), "Scheme %s", id < 4u ? schemes[id] : "UNKNOWN");
    sh_out(j, line);
    sh_out(j, "Serial echo on the PL011, keyboard from the serial line and the buttons");
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static int show_memory(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    char a[16], b[16], c[16], d[16];
    k_heap_stats_t st;

    show_start(j);
    k_heap_stats(&st);
    (void)sh_fmt_size(st.total, a, sizeof(a));
    (void)sh_fmt_size(st.used, b, sizeof(b));
    (void)sh_fmt_size(st.free, c, sizeof(c));
    (void)sh_fmt_size(st.largest_free, d, sizeof(d));
    k_snprintf(line, sizeof(line), "Heap %s: %s used, %s free, largest free block %s", a, b, c,
               d);
    sh_out(j, line);
    k_snprintf(line, sizeof(line), "Blocks in use %u", (unsigned)st.allocs);
    sh_out(j, line);
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* ---- RUN and HOLOTAPE ---- */

/* A program's end. `tag` is the job identity its start received: a done()
 * from a run whose job ended, by Ctrl-C or a logout, is ignored even when
 * another RUN waits now. */
static void prog_done(void *tag, int status, const char *text)
{
    sh_job_t *j = &sh_job;

    if (!sh_job_live(tag, "prog") || !j->prog_wait) {
        return;
    }
    j->prog_wait = 0;
    if (status == SH_CANCELLED) {
        /* The program's screen took Ctrl-C. */
        j->cancel = 1;
        (void)sh_end(j, DCL_SYS_CANCEL, NULL, 0, 1);
        if (j->prog_in_start) {
            j->prog_sync = 1;
            return;
        }
        sh_resume(j, SH_DONE);
        return;
    }
    if (text != NULL) {
        k_snprintf(j->res_text, sizeof(j->res_text), "%s", text);
    }
    (void)sh_end(j, status == 0 ? DCL_NORMAL : DCL_SYS_PROGERR, NULL, 0, text != NULL);
    if (j->prog_in_start) {
        /* Ended inside start(): run_program() returns SH_DONE itself. */
        j->prog_sync = 1;
        return;
    }
    sh_resume(j, SH_DONE);
}

static int run_program(sh_job_t *j, const char *name, const char *args)
{
    sh_prog_start_fn start = sh_prog_find(&sh_progs, name);
    int rc;

    if (start == NULL) {
        return sh_end(j, DCL_SYS_NOTINST, name, 0, 1);
    }
    j->prog_wait = 1;
    j->prog_in_start = 1;
    rc = start(args, prog_done, sh_job_tag(j));
    j->prog_in_start = 0;
    if (rc != 0) {
        j->prog_wait = 0;
        return sh_end(j, DCL_SYS_PROGERR, name, 0, 1);
    }
    return j->prog_sync ? SH_DONE : SH_WAIT;
}

static int run_start(sh_job_t *j)
{
    return run_program(j, param(j, 0), "");
}

static int holo_start(sh_job_t *j)
{
    return run_program(j, "HOLOTAPE", param(j, 0) != NULL ? param(j, 0) : "");
}

static void run_stop(sh_job_t *j)
{
    /* The run's later done() finds another identity, or no job. */
    j->prog_wait = 0;
}

/* ---- Sessions ---- */

static int logout_start(sh_job_t *j)
{
    char line[UOS_LINE_MAX + 1];
    char up[24];

    uptime_text(up, sizeof(up));
    k_snprintf(line, sizeof(line), "%s logged out at uptime %s", j->user, up);
    sh_out(j, line);
    j->after = SH_AFTER_LOGOUT;
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static int login_start(sh_job_t *j)
{
    int rc = sh_login_name(param(j, 0), j->login_name, sizeof(j->login_name));

    if (rc != DCL_NORMAL) {
        return sh_end(j, rc, NULL, 0, 1);
    }
    if (j->login_name[0] != '\0') {
        sh_out(j, "This session ends. Give the password at the prompt.");
        j->after = SH_AFTER_LOGIN;
        return sh_end(j, DCL_NORMAL, NULL, 0, 0);
    }
    sh_out(j, "This session ends. Log in at the prompt.");
    j->after = SH_AFTER_LOGOUT;
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

static int robco_start(sh_job_t *j)
{
    static const char *const text[] = {
        "ROBCO INDUSTRIES UNIFIED OPERATING SYSTEM",
        "Terminal software for shelters, plants and offices.",
        "This unit answers to its operators and to no one else.",
        "Keep the card in the slot and the cover closed.",
    };
    unsigned i;

    for (i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
        sh_out(j, text[i]);
    }
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* ---- DEVICE ---- */

static int device_start(sh_job_t *j)
{
    const char *name = param(j, 0) != NULL ? param(j, 0) : "";
    const char *action = param(j, 1);
    char spec[24];
    int rc;

    k_snprintf(spec, sizeof(spec), "DEV:%s", name);
    if (action != NULL) {
        rc = vfs_write_all(j->ctx, j->privs, spec, NULL, action, k_strlen(action), 0);
        if (rc != VFS_OK) {
            return sh_end_err(j, SH_SRC_DEV, rc, name);
        }
        j->tally.done = 1;
    }
    rc = dev_line(j, name);
    if (rc != 0) {
        return sh_end_err(j, SH_SRC_DEV, rc, name);
    }
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* ---- The table ---- */

const sh_impl_t sh_cmds[] = {
    { "DIRECTORY", NULL, dir_start, dir_step, NULL },
    { "TYPE", NULL, type_start, type_step, NULL },
    { "COPY", NULL, loop_start, loop_step, loop_stop },
    { "DELETE", NULL, loop_start, loop_step, loop_stop },
    { "RENAME", NULL, loop_start, loop_step, loop_stop },
    { "CREATE", NULL, create_start, NULL, NULL },
    { "SET", "DEFAULT", set_default, NULL, NULL },
    { "SET", "TERMINAL", set_terminal, NULL, NULL },
    { "SET", "HALT", set_halt, NULL, NULL },
    { "SHOW", "DEFAULT", show_default, NULL, NULL },
    { "SHOW", "USERS", show_users, NULL, NULL },
    { "SHOW", "SYSTEM", show_system, NULL, NULL },
    { "SHOW", "TIME", show_time, NULL, NULL },
    { "SHOW", "DEVICES", show_devices, NULL, NULL },
    { "SHOW", "TERMINAL", show_terminal, NULL, NULL },
    { "SHOW", "MEMORY", show_memory, NULL, NULL },
    { "HELP", NULL, help_start, help_step, NULL },
    { "RUN", NULL, run_start, NULL, run_stop },
    { "HOLOTAPE", NULL, holo_start, NULL, run_stop },
    { "LOGIN", NULL, login_start, NULL, NULL },
    { "LOGOUT", NULL, logout_start, NULL, NULL },
    { "ROBCO", NULL, robco_start, NULL, NULL },
    { "DEVICE", NULL, device_start, NULL, NULL },
};

const unsigned sh_cmds_count = sizeof(sh_cmds) / sizeof(sh_cmds[0]);
