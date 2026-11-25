/*
 * The shell: the DCL prompt screen, the one executor every caller shares,
 * the service and the `sh` maintenance command. The rules are pure, in
 * shrules.c. The commands are shcmds.c and shuaf.c.
 *
 *   sh state       sh state=<idle|prompt|running> kind= ask= verb= pending=
 *                  prompts= runs=
 *   sh run <line>  runs one command line as the logged-in user. The line
 *                  is quoted as the console's markers quote text: a blank
 *                  is %20. Ends with ev=sh run=done status= result=
 *
 * Markers:
 *   sh state=prompt kind=<cmd|contin|param|confirm|line|password> n=
 *   cmd state=start verb= src=<shell|port|api>
 *   cmd file= op= result=         one per file of a file loop
 *   cmd state=end verb= rc=<IDENT> sev= files= skipped= failed= steps= maxticks=
 * No marker holds a password: SET PASSWORD and AUTHORIZE read passwords
 * masked, and a command line never holds one.
 */

#include <robco/alloc.h>
#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>

#include "conview.h"
#include "robco_config.h"
#include "sess.h"
#include "shint.h"
#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"

sh_job_t sh_job;
sh_progtab_t sh_progs;

static int sh_up;
static unsigned runs;

/* ---- The session's default folder ---- */

static vp_ctx_t sh_ctx;
static char sh_ctx_user[UAF_NAME_MAX + 1];
static int sh_ctx_ok;

static vp_ctx_t *ctx_get(void)
{
    if (!sh_ctx_ok || k_strcmp(sh_ctx_user, sess_user()) != 0) {
        if (sess_vp_ctx(&sh_ctx) != 0) {
            vp_ctx_init(&sh_ctx, "");
        }
        k_snprintf(sh_ctx_user, sizeof(sh_ctx_user), "%s", sess_user());
        sh_ctx_ok = 1;
    }
    return &sh_ctx;
}

/* ---- Runs a call on the next loop pass ---- */

static void later(uos_call_fn fn)
{
    uos_timer_id_t id;

    if (uos_post(fn, NULL) != 0 && uos_timer_start(1, 0, fn, NULL, &id) != 0) {
        uos_set_error("shell");
    }
}

/* ---- The screen's rows: the shell redraws them after a pager or a keyboard ---- */

#define RING_ROWS 40u

static char ring[RING_ROWS][UOS_LINE_MAX + 1];
static uint8_t ring_attr[RING_ROWS];
static unsigned ring_head, ring_count;

static void ring_add(const char *text, unsigned attr)
{
    unsigned slot = (ring_head + ring_count) % RING_ROWS;

    if (ring_count == RING_ROWS) {
        slot = ring_head;
        ring_head = (ring_head + 1u) % RING_ROWS;
    } else {
        ring_count++;
    }
    k_snprintf(ring[slot], sizeof(ring[slot]), "%s", text != NULL ? text : "");
    ring_attr[slot] = (uint8_t)attr;
}

static void put_line(const char *text, unsigned attr)
{
    ring_add(text, attr);
    con_print(text, attr);
}

/* Prints a message: one put_line() per line of dcl_format_message(). */
static void put_msg(dcl_msg_t id, const char *arg, uint32_t num)
{
    char buf[DCL_MSG_SIZE];
    char *p;
    char *nl;
    char sev = dcl_msg_severity(id);
    unsigned attr = (sev == 'E' || sev == 'F') ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL;

    if (dcl_format_message(buf, sizeof(buf), id, arg, num) < 0) {
        return;
    }
    p = buf;
    for (;;) {
        for (nl = p; *nl != '\0' && *nl != '\n'; nl++) {
        }
        if (*nl == '\0') {
            put_line(p, attr);
            return;
        }
        *nl = '\0';
        put_line(p, attr);
        p = nl + 1;
    }
}

/* ---- The prompt ---- */

#define K_CMD     0
#define K_CONTIN  1
#define K_PARAM   2

static const char *const kind_name[] = { "cmd", "contin", "param" };

static struct {
    int on;                         /* the shell screen is the session's screen */
    int reading;
    int kind;
    char prompt[48];
    dcl_line_t ln;
    char line[DCL_LINE_MAX + 1];
    unsigned prompts;
} shp;

static const char *ask_kind;        /* a command's read: its kind, else NULL */
static con_line_fn ask_cb;

static uos_screen_t shell_screen;
static void on_line(int result, const char *text, unsigned len, void *ctx);
static int launch(const char *line, unsigned flags, const char *src, sh_done_fn cb, void *ctx,
                  char *need, size_t need_size);

static void prompt_start(int kind)
{
    int rc;

    shp.kind = kind;
    if (kind == K_CMD) {
        k_snprintf(shp.prompt, sizeof(shp.prompt), "$ ");
    } else if (kind == K_CONTIN) {
        k_snprintf(shp.prompt, sizeof(shp.prompt), "_$ ");
    }
    rc = con_read_line(shp.prompt, kind == K_PARAM ? CON_RL_NOHIST : 0u, DCL_LINE_MAX, on_line,
                       NULL);
    if (rc == 0) {
        shp.reading = 1;
    }
    shp.prompts++;
    UOS_MARK("sh state=prompt kind=%s n=%u rc=%d", kind_name[kind], shp.prompts, rc);
}

/* The next loop pass after a command: the prompt again, when the shell is
 * the screen on top and nothing else reads or runs. */
static void shell_idle(void *arg)
{
    (void)arg;
    if (!shp.on || shp.reading || sh_job.active || !sess_logged_in() ||
        uos_screen_top() != &shell_screen || con_line_active()) {
        return;
    }
    dcl_line_reset(&shp.ln);
    prompt_start(K_CMD);
}

static void shell_done(int status, const char *result, void *ctx)
{
    (void)status;
    (void)result;
    (void)ctx;
}

static void begin_line(void)
{
    char need[48];
    int rc = launch(shp.line, 0, "shell", shell_done, NULL, need, sizeof(need));

    if (rc == 1) {
        /* A missing parameter: ask for it and parse again. */
        k_snprintf(shp.prompt, sizeof(shp.prompt), "_%s: ", need);
        prompt_start(K_PARAM);
        return;
    }
    if (rc == 2) {
        /* An empty or comment-only line. */
        later(shell_idle);
        return;
    }
    if (rc < 0) {
        put_msg(rc == SH_ERR_BUSY ? DCL_SYS_BUSY : DCL_SYS_NOSESS, NULL, 0);
        later(shell_idle);
    }
}

static void on_line(int result, const char *text, unsigned len, void *ctx)
{
    char row[UOS_LINE_MAX + 1];
    dcl_msg_t m;

    (void)ctx;
    shp.reading = 0;
    k_snprintf(row, sizeof(row), "%s%s", shp.prompt, result == CON_OK ? text : "");
    ring_add(row, CONSOLE_ATTR_NORMAL);
    if (result != CON_OK) {
        dcl_line_reset(&shp.ln);
        later(shell_idle);
        return;
    }
    if (shp.kind == K_PARAM) {
        size_t have = k_strlen(shp.line);

        if (len == 0) {
            prompt_start(K_PARAM);
            return;
        }
        if (have + 1u + len > DCL_LINE_MAX) {
            put_msg(DCL_LINOVF, NULL, 0);
            later(shell_idle);
            return;
        }
        shp.line[have] = ' ';
        k_memcpy(shp.line + have + 1, text, len);
        shp.line[have + 1 + len] = '\0';
        begin_line();
        return;
    }
    m = dcl_line_add(&shp.ln, text);
    if (m == DCL_CONTIN) {
        prompt_start(K_CONTIN);
        return;
    }
    if (m != DCL_NORMAL) {
        put_msg(m, NULL, 0);
        dcl_line_reset(&shp.ln);
        later(shell_idle);
        return;
    }
    k_snprintf(shp.line, sizeof(shp.line), "%s", shp.ln.text);
    dcl_line_reset(&shp.ln);
    begin_line();
}

/* ---- The screen ---- */

static void shell_draw(uos_screen_t *s)
{
    unsigned rows;
    unsigned n;
    unsigned first;
    unsigned i;

    (void)s;
    if (!uos.fb_ok) {
        return;
    }
    console_clear();
    rows = con_text_rows();
    n = ring_count < rows ? ring_count : (rows > 0 ? rows - 1u : 0u);
    first = ring_count - n;
    for (i = 0; i < n; i++) {
        unsigned slot = (ring_head + first + i) % RING_ROWS;

        console_set_cursor(0, i);
        console_set_attr(ring_attr[slot]);
        console_puts(ring[slot]);
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_set_cursor(0, n);
    con_status_redraw();
}

static void shell_enter(uos_screen_t *s)
{
    char row[UOS_LINE_MAX + 1];

    (void)s;
    k_memset(&shp, 0, sizeof(shp));
    shp.on = 1;
    ring_head = 0;
    ring_count = 0;
    sh_ctx_ok = 0;
    (void)ctx_get();
    k_snprintf(row, sizeof(row), "UOS V%s command shell. Type HELP for the commands.",
               UOS_VERSION_STR);
    put_line(row, CONSOLE_ATTR_DIM);
    later(shell_idle);
}

static void shell_leave(uos_screen_t *s)
{
    (void)s;
    shp.on = 0;
    shp.reading = 0;
}

static int shell_event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)s;
    if (ev->type == UOS_EV_CHAR && ev->code == 3u && sh_job.active) {
        sh_job.cancel = 1;
        return 1;
    }
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

static uos_screen_t shell_screen = {
    .name = "shell",
    .enter = shell_enter,
    .leave = shell_leave,
    .event = shell_event,
    .draw = shell_draw,
};

static uos_screen_t *shell_next(void)
{
    return &shell_screen;
}

/* ---- Output for the commands ---- */

void sh_out(sh_job_t *j, const char *text)
{
    if (j->out_spec != NULL) {
        size_t n = k_strlen(text);

        if (j->ofull || j->olen + n + 1u > SH_OUT_MAX) {
            j->ofull = 1;
            return;
        }
        if (j->obuf == NULL || j->olen + n + 1u > j->ocap) {
            size_t cap = j->ocap == 0 ? 4096u : j->ocap * 2u;
            char *nb;

            while (cap < j->olen + n + 1u) {
                cap *= 2u;
            }
            if (cap > SH_OUT_MAX) {
                cap = SH_OUT_MAX;
            }
            nb = k_realloc(j->obuf, cap);
            if (nb == NULL) {
                j->ofull = 1;
                return;
            }
            j->obuf = nb;
            j->ocap = cap;
        }
        k_memcpy(j->obuf + j->olen, text, n);
        j->olen += n;
        j->obuf[j->olen++] = '\n';
        return;
    }
    if (!(j->flags & SH_RUN_QUIET)) {
        put_line(text, CONSOLE_ATTR_NORMAL);
    }
}

void sh_say(sh_job_t *j, dcl_msg_t id, const char *arg, uint32_t num)
{
    if (!(j->flags & SH_RUN_QUIET)) {
        put_msg(id, arg, num);
    }
}

int sh_end(sh_job_t *j, dcl_msg_t id, const char *arg, uint32_t num, int print)
{
    j->msg = id;
    k_snprintf(j->msg_arg, sizeof(j->msg_arg), "%s", arg != NULL ? arg : "");
    j->msg_num = num;
    j->msg_print = print;
    return SH_DONE;
}

int sh_end_err(sh_job_t *j, int src, int code, const char *arg)
{
    uint32_t refused = j->it_open ? j->it.skipped : j->tally.refused;

    return sh_end(j, sh_err_msg(src, code, vfs_last_vp(), refused), arg, 0, 1);
}

void sh_file_mark(sh_job_t *j, const char *op, const char *spec, const char *result)
{
    char q[VP_DISPLAY_MAX + 8];

    (void)j;
    (void)cv_quote(q, sizeof(q), spec, k_strlen(spec), 100);
    UOS_MARK("cmd file=%s op=%s result=%s", q, op, result);
}

static void ask_done(int result, const char *text, unsigned len, void *ctx)
{
    con_line_fn cb = ask_cb;

    ask_kind = NULL;
    ask_cb = NULL;
    if (cb != NULL) {
        cb(result, text, len, ctx);
    }
}

int sh_ask(const char *kind, const char *prompt, unsigned flags, unsigned max, con_line_fn cb)
{
    int rc;

    ask_kind = kind;
    ask_cb = cb;
    rc = con_read_line(prompt, flags | CON_RL_NOHIST, max, ask_done, NULL);
    if (rc != 0) {
        ask_kind = NULL;
        ask_cb = NULL;
    }
    shp.prompts++;
    UOS_MARK("sh state=prompt kind=%s n=%u rc=%d", kind, shp.prompts, rc);
    return rc;
}

/* ---- The executor ---- */

static void finish(void);

static void drive(void *arg)
{
    sh_job_t *j = &sh_job;
    uint32_t t0;
    uint32_t dt;
    int rc;

    (void)arg;
    if (!j->active) {
        return;
    }
    if (j->cancel || j->impl == NULL || j->impl->step == NULL) {
        j->tally.cancelled = 1;
        (void)sh_end(j, DCL_SYS_CANCEL, NULL, 0, 1);
        finish();
        return;
    }
    t0 = uos_ticks();
    rc = j->impl->step(j);
    dt = uos_ticks() - t0;
    j->steps++;
    if (dt > j->maxticks) {
        j->maxticks = dt;
    }
    sh_resume(j, rc);
}

void sh_resume(sh_job_t *j, int rc)
{
    if (!j->active) {
        return;
    }
    if (rc == SH_MORE) {
        if (uos_post(drive, NULL) != 0) {
            uos_timer_id_t id;

            if (uos_timer_start(1, 0, drive, NULL, &id) != 0) {
                (void)sh_end(j, DCL_SYS_INSFMEM, NULL, 0, 1);
                finish();
            }
        }
    } else if (rc == SH_DONE) {
        finish();
    }
}

static int status_of(dcl_msg_t m)
{
    switch (dcl_msg_severity(m)) {
    case 'S':
    case 'I': return m == DCL_SYS_CANCEL ? SH_CANCELLED : SH_OK;
    case 'W': return SH_WARN;
    default:  return SH_FAIL;
    }
}

static void write_output(sh_job_t *j)
{
    int rc;

    if (j->out_spec == NULL || j->msg == DCL_SYS_CANCEL) {
        return;
    }
    if (j->ofull) {
        (void)sh_end(j, DCL_SYS_TOOBIG, j->out_spec, 0, 1);
        return;
    }
    rc = vfs_write_all(j->ctx, j->privs, j->out_spec, "LIS", j->obuf != NULL ? j->obuf : "",
                       j->olen, 0);
    if (rc != VFS_OK) {
        (void)sh_end_err(j, SH_SRC_VFS, rc, j->out_spec);
    }
}

static void finish(void)
{
    sh_job_t *j = &sh_job;
    char res[DCL_MSG_SIZE];
    char *nl;
    sh_done_fn cb = j->cb;
    void *cbctx = j->cbctx;
    int after = j->after;
    int status;

    if (j->impl != NULL && j->impl->stop != NULL) {
        j->impl->stop(j);
    }
    if (j->it_open) {
        vfs_iter_end(&j->it);
        j->it_open = 0;
    }
    if (j->buf != NULL) {
        vfs_free(j->buf);
        j->buf = NULL;
    }
    if (j->ents != NULL) {
        k_free(j->ents);
        j->ents = NULL;
    }
    write_output(j);
    if (j->obuf != NULL) {
        k_free(j->obuf);
        j->obuf = NULL;
    }
    if (j->res_text[0] != '\0') {
        /* A program's own result line. */
        k_snprintf(res, sizeof(res), "%s", j->res_text);
        if (j->msg_print && !(j->flags & SH_RUN_QUIET)) {
            put_line(res, CONSOLE_ATTR_NORMAL);
        }
    } else {
        if (j->msg_print) {
            sh_say(j, j->msg, j->msg_arg[0] != '\0' ? j->msg_arg : NULL, j->msg_num);
        }
        if (dcl_format_message(res, sizeof(res), j->msg,
                               j->msg_arg[0] != '\0' ? j->msg_arg : NULL, j->msg_num) < 0) {
            res[0] = '\0';
        }
    }
    nl = res;
    while (*nl != '\0' && *nl != '\n') {
        nl++;
    }
    *nl = '\0';
    status = j->cancel || j->msg == DCL_SYS_CANCEL ? SH_CANCELLED : status_of(j->msg);
    UOS_MARK("cmd state=end verb=%s rc=%s sev=%c files=%u skipped=%u failed=%u steps=%u "
             "maxticks=%u", j->verb, dcl_msg_ident(j->msg), dcl_msg_severity(j->msg),
             (unsigned)j->tally.done, (unsigned)(j->tally.refused + j->tally.declined),
             (unsigned)j->tally.failed, (unsigned)j->steps, (unsigned)j->maxticks);
    k_memset(j, 0, sizeof(*j));
    if (cb != NULL) {
        cb(status, res, cbctx);
    }
    switch (after) {
    case SH_AFTER_LOGOUT:
        (void)sess_logout(SESS_END_USER);
        return;
    case SH_AFTER_HALT:
        uos_halt();
    case SH_AFTER_RESTART:
        uos_reboot("shell");
    default:
        break;
    }
    later(shell_idle);
}

const sh_impl_t *sh_impl_find(const dcl_cmd_t *cmd)
{
    const sh_impl_t *tabs[2] = { sh_cmds, sh_uaf_cmds };
    unsigned counts[2] = { sh_cmds_count, sh_uaf_cmds_count };
    unsigned t;
    unsigned i;

    if (cmd == NULL || cmd->verb == NULL) {
        return NULL;
    }
    for (t = 0; t < 2; t++) {
        for (i = 0; i < counts[t]; i++) {
            const sh_impl_t *c = &tabs[t][i];

            if (k_strcmp(c->verb, cmd->verb->name) != 0) {
                continue;
            }
            if (c->key == NULL || (cmd->keyword != NULL && k_strcmp(c->key, cmd->keyword->name) == 0)) {
                return c;
            }
        }
    }
    return NULL;
}

/*
 * Starts a command. Returns 0 when it started (its callback follows), 1
 * when a parameter is missing and `need` names it (interactive callers
 * only), 2 for an empty line from the prompt, or SH_ERR_*.
 */
static int launch(const char *line, unsigned flags, const char *src, sh_done_fn cb, void *ctx,
                  char *need, size_t need_size)
{
    sh_job_t *j = &sh_job;
    dcl_msg_t rc;
    int r;

    if (!sh_up) {
        return SH_ERR_DOWN;
    }
    if (line == NULL || k_strlen(line) > DCL_LINE_MAX) {
        return SH_ERR_ARG;
    }
    if (j->active) {
        return SH_ERR_BUSY;
    }
    if (!sess_logged_in()) {
        return SH_ERR_NOUSER;
    }
    k_memset(j, 0, sizeof(*j));
    k_snprintf(j->line, sizeof(j->line), "%s", line);
    rc = dcl_parse(j->line, &dcl_uos_table, &j->cmd);
    if (rc == DCL_NORMAL && j->cmd.verb == NULL && need != NULL) {
        return 2;
    }
    if (rc == DCL_INSFPRM && need != NULL && j->cmd.prompt != NULL) {
        k_snprintf(need, need_size, "%s", j->cmd.prompt);
        return 1;
    }
    j->active = 1;
    j->flags = flags;
    j->src = src;
    j->cb = cb;
    j->cbctx = ctx;
    j->ctx = ctx_get();
    j->privs = sess_privs();
    k_snprintf(j->user, sizeof(j->user), "%s", sess_user());
    sh_tally_init(&j->tally);
    j->msg = DCL_NORMAL;
    sh_verb_word(&j->cmd, j->verb, sizeof(j->verb));
    runs++;
    UOS_MARK("cmd state=start verb=%s src=%s", j->verb, src);
    if (rc != DCL_NORMAL) {
        (void)sh_end(j, rc, j->cmd.token, 0, 1);
        finish();
        return 0;
    }
    if (j->cmd.verb == NULL) {
        (void)sh_end(j, DCL_NORMAL, NULL, 0, 0);
        finish();
        return 0;
    }
    if (dcl_check_privs(&j->cmd, j->privs) != DCL_NORMAL) {
        (void)sh_end(j, DCL_SYS_NOPRIV, NULL, 0, 1);
        finish();
        return 0;
    }
    j->impl = sh_impl_find(&j->cmd);
    if (j->impl == NULL) {
        (void)sh_end(j, DCL_SYS_NOTINST, j->cmd.verb->name, 0, 1);
        finish();
        return 0;
    }
    r = j->impl->start(j);
    sh_resume(j, r);
    return 0;
}

int sh_run_line(const char *line, unsigned flags, sh_done_fn cb, void *ctx)
{
    return launch(line, flags, "api", cb, ctx, NULL, 0);
}

int sh_busy(void)
{
    return sh_job.active;
}

int sh_program_register(const char *name, sh_prog_start_fn start)
{
    switch (sh_prog_add(&sh_progs, name, start)) {
    case 0:                 return 0;
    case SH_PROG_ERR_DUP:   return SH_ERR_DUP;
    case SH_PROG_ERR_FULL:  return SH_ERR_FULL;
    default:                return SH_ERR_ARG;
    }
}

/* ---- The maintenance command ---- */

static char port_line[DCL_LINE_MAX + 1];
static int port_pending;

static void port_done(int status, const char *result, void *ctx)
{
    char q[DCL_MSG_SIZE + 8];

    (void)ctx;
    (void)cv_quote(q, sizeof(q), result, k_strlen(result), 120);
    UOS_MARK("sh run=done status=%d result=%s", status, q);
}

static void port_run(void *arg)
{
    int rc;

    (void)arg;
    port_pending = 0;
    /* The prompt gives way: its read ends quietly, the command runs, and
     * the prompt comes back after it. */
    if (shp.reading && uos_screen_top() != &shell_screen && con_line_active() &&
        ask_kind == NULL) {
        (void)uos_screen_pop();
        shp.reading = 0;
        dcl_line_reset(&shp.ln);
    }
    rc = launch(port_line, 0, "port", port_done, NULL, NULL, 0);
    if (rc != 0) {
        UOS_MARK("sh run=refused rc=%d", rc);
    }
}

static int cmd_sh(int argc, char **argv)
{
    if (argc == 1 && k_strcmp(argv[0], "state") == 0) {
        const char *st = sh_job.active ? "running" : (shp.reading ? "prompt" : "idle");

        uos_maint_reply("sh state=%s kind=%s ask=%s verb=%s pending=%u prompts=%u runs=%u", st,
                        shp.reading ? kind_name[shp.kind] : "none",
                        ask_kind != NULL ? ask_kind : "none",
                        sh_job.active ? sh_job.verb : "none", input_pending(), shp.prompts,
                        runs);
        return 0;
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (argc >= 2 && k_strcmp(argv[0], "run") == 0) {
        char *text = uos_mt_rejoin(argv[1], argv[argc - 1]);

        if (port_pending) {
            return uos_maint_fail("busy");
        }
        if (sh_unquote(text, port_line, sizeof(port_line)) < 0) {
            return uos_maint_fail("bad line");
        }
        if (uos_post(port_run, NULL) != 0) {
            return uos_maint_fail("full");
        }
        port_pending = 1;
        return 0;
    }
    return uos_maint_fail("usage sh state|run <line>");
}

/* ---- The service ---- */

static int sh_svc_init(void)
{
    int rc;

    sh_prog_init(&sh_progs);
    rc = sess_set_next(shell_next);
    if (rc != 0) {
        return rc;
    }
    sh_up = 1;
    return uos_maint_register("sh", cmd_sh, "the shell: sh state|run <line>");
}

static void sh_svc_tick(uint32_t now)
{
    (void)now;
    /* The session ended under a running command: a logout pops every
     * screen, so a read or a program it waited on never calls back. */
    if (sh_job.active && !sess_logged_in()) {
        sh_job.cancel = 1;
        (void)sh_end(&sh_job, DCL_SYS_CANCEL, NULL, 0, 0);
        finish();
    }
}

const uos_service_t uos_svc_shell = { "shell", sh_svc_init, sh_svc_tick, NULL };
