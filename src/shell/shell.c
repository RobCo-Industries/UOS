/*
 * The shell: the DCL prompt screen, the one executor every caller shares,
 * the service and the `sh` maintenance command. The rules are pure, in
 * shrules.c. The commands are shcmds.c and shuaf.c.
 *
 *   sh state       sh state=<idle|prompt|running> kind= ask= verb= pending=
 *                  prompts= runs= id=
 *   sh run <line>  runs one command line as the logged-in user. The line
 *                  is quoted as the console's markers quote text: a blank
 *                  is %20. Ends with ev=sh run=done status= result=
 * Test builds only (UOS_TEST_HOOKS=1):
 *   sh prog reg <NAME>        registers a test program for RUN <NAME>
 *   sh prog done <n> <status> ends the n-th run of a test program, even
 *                             one whose job has ended
 *   sh api <-|q> [null|long|<line>]
 *                             calls sh_run_line() with flags 0 or
 *                             SH_RUN_QUIET on a quoted line, an empty one,
 *                             NULL, or one byte past DCL_LINE_MAX. Marks
 *                             "ev=sh api=call rc= cbs=" and, per callback,
 *                             "ev=sh api=done n= status= result="
 *   sh hold <1|0>             1: the next step of a command waits ("ev=sh
 *                             held=1"); 0: it runs. Ctrl-C still cancels.
 *   sh park <1|0>             1: a program's posted call waits; 0: it is
 *                             posted, so a test orders it after a logout.
 *
 * Markers:
 *   sh state=prompt kind=<cmd|contin|param|confirm|line|password> n=
 *   cmd state=start verb= src=<shell|port|api|host screen name>
 *   cmd file= op= result=         one per file of a file loop
 *   cmd state=end verb= rc=<IDENT> sev= files= skipped= failed= steps= maxticks=
 *   cmd output=<written|kept|toobig|failed> verb=   /OUTPUT, before the end
 *   sh stale=<step|read|pager|prog> id= cur=         a late callback ignored
 *   sh stale=post id= cur=                           a program call dropped: its session ended
 *   sh op=wait why=covered id= owe=<step|end>        a job waits under a screen, once
 *   sh op=resume id= owe=<step|end>                  the shell is on top again
 *   sh prog=<reg|start|done> n=                      test programs only
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
static uint32_t job_seq;            /* the last job identity given */
static int test_hold, test_held;    /* sh hold: test builds only */
static int owed;                    /* SH_OWE_*: the job's work a covered shell owes */
/* 1: a session reset took the shell away under a running job. The job
 * takes no step and prints nothing; the next tick ends it quietly. */
static int orphan;

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

/* ---- Job identity: every callback the shell hands out carries it ---- */

void *sh_job_tag(const sh_job_t *j)
{
    return (void *)(uintptr_t)j->id;
}

int sh_job_live(void *tag, const char *kind)
{
    uint32_t t = (uint32_t)(uintptr_t)tag;

    if (sh_tag_live(sh_job.active, sh_job.id, t)) {
        return 1;
    }
    UOS_MARK("sh stale=%s id=%u cur=%u", kind, (unsigned)t,
             sh_job.active ? (unsigned)sh_job.id : 0u);
    return 0;
}

/* ---- Program calls posted past their run: they keep the session ---- */

#define SH_PCALL_MAX 4

static struct {
    sh_prog_call_fn fn;     /* NULL: the slot is free */
    uint32_t serial;        /* sess_id() when posted */
    int parked;             /* sh park: test builds only, not posted yet */
} pcalls[SH_PCALL_MAX];
static int test_park;       /* sh park 1: program calls wait for sh park 0 */

static void prog_call(void *arg)
{
    uintptr_t i = (uintptr_t)arg;
    sh_prog_call_fn fn = pcalls[i].fn;
    uint32_t t = pcalls[i].serial;
    int live = sh_tag_live(sess_logged_in(), sess_id(), t);

    pcalls[i].fn = NULL;
    if (!live) {
        UOS_MARK("sh stale=post id=%u cur=%u", (unsigned)t, (unsigned)sess_id());
    }
    fn(live);
}

int sh_program_post(sh_prog_call_fn fn)
{
    uintptr_t i;

    if (fn == NULL || sess_id() == 0) {
        return SH_ERR_ARG;
    }
    for (i = 0; i < SH_PCALL_MAX; i++) {
        if (pcalls[i].fn == NULL) {
            break;
        }
    }
    if (i == SH_PCALL_MAX || (!test_park && uos_post(prog_call, (void *)i) != 0)) {
        return SH_ERR_FULL;
    }
    pcalls[i].parked = test_park;
    pcalls[i].fn = fn;
    pcalls[i].serial = sess_id();
    return 0;
}

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
static sess_next_fn first_fn;       /* asked first for the session's screen */
static char *cap_view;              /* SH_RUN_CAPTURE text during the callback */
static size_t cap_view_len;

/* 1 when the job's screen is on and another one covers it. The shell
 * is on while it is the session's screen; a host of sh_run_line_on() is
 * on until sh_host_gone(), which makes the job an orphan. The screen's
 * own children, its reads and its pager, are part of it: a command the
 * API starts while the `$` read waits runs under it. */
static int covered(void)
{
    if (sh_job.host != NULL) {
        return sh_job_waits(1, uos_screen_unit_on_top(sh_job.host));
    }
    return sh_job_waits(shp.on, uos_screen_unit_on_top(&shell_screen));
}

uos_screen_t *sh_screen(void)
{
    return sh_job.active && sh_job.host != NULL ? sh_job.host : &shell_screen;
}

static const char *const owe_name[] = { "none", "step", "end" };

/* Records work the job owes while the shell is covered; marks once. */
static void owe(int what)
{
    if (sh_owe(&owed, what)) {
        UOS_MARK("sh op=wait why=covered id=%u owe=%s", (unsigned)sh_job.id, owe_name[what]);
    }
}

static void on_line(int result, const char *text, unsigned len, void *ctx);
static int launch(uos_screen_t *host, const char *line, unsigned flags, const char *src,
                  sh_done_fn cb, void *ctx, char *need, size_t need_size);

static void prompt_start(int kind)
{
    int rc;

    shp.kind = kind;
    if (kind == K_CMD) {
        k_snprintf(shp.prompt, sizeof(shp.prompt), "$ ");
    } else if (kind == K_CONTIN) {
        k_snprintf(shp.prompt, sizeof(shp.prompt), "_$ ");
    }
    rc = con_read_line(&shell_screen, shp.prompt, kind == K_PARAM ? CON_RL_NOHIST : 0u, DCL_LINE_MAX, on_line,
                       NULL);
    if (rc == 0) {
        shp.reading = 1;
    }
    shp.prompts++;
    UOS_MARK("sh state=prompt kind=%s n=%u rc=%d", kind_name[kind], shp.prompts, rc);
}

/* The next loop pass after a command: the prompt again, when the shell is
 * the screen on top and nothing else reads or runs. A covered shell owes
 * the prompt: shell_uncover() calls this again once it is the top. The
 * guards make a second call do nothing while a read or a command runs. */
static void shell_idle(void *arg)
{
    (void)arg;
    if (!shp.on || shp.reading || sh_job.active || !sess_logged_in() ||
        !uos_screen_on_top(&shell_screen) || con_line_active()) {
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
    int rc = launch(NULL, shp.line, 0, "shell", shell_done, NULL, need, sizeof(need));

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
    if (sh_job.active) {
        orphan = 1;
    }
}

static int shell_event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)s;
    if (ev->type == UOS_EV_CHAR && ev->code == 3u && sh_job.active) {
        sh_cancel();
        return 1;
    }
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

static void finish(void);

void sh_cancel(void)
{
    if (!sh_job.active) {
        return;
    }
    sh_job.cancel = 1;
    /* A job that waits on a program with no screen of its own: no step is
     * posted to see the flag, so post one. A read or a pager on top takes
     * Ctrl-C itself. */
    if (!sh_job.posted && (sh_job.prog_wait || test_held) && !con_line_active() &&
        !con_page_active()) {
        sh_resume(&sh_job, SH_MORE);
    }
}

/* Runs the work a covered job owes: its end, or its next step. 1 when
 * there was some. */
static int pay_owed(void)
{
    int what = sh_owe_take(&owed);

    if (what == SH_OWE_NONE || !sh_job.active) {
        return 0;
    }
    UOS_MARK("sh op=resume id=%u owe=%s", (unsigned)sh_job.id, owe_name[what]);
    if (what == SH_OWE_END) {
        finish();
    } else {
        sh_resume(&sh_job, SH_MORE);
    }
    return 1;
}

int sh_host_uncover(uos_screen_t *host)
{
    if (host == NULL || !sh_job.active || sh_job.host != host) {
        return 0;
    }
    return pay_owed();
}

void sh_host_gone(uos_screen_t *host)
{
    if (host != NULL && sh_job.active && sh_job.host == host) {
        orphan = 1;
    }
}

/* On top again. A job that waited runs on: its owed end, or its next step.
 * sh_owe_take() clears the debt before the work, and sh_resume() posts
 * at most one step, so the work runs once. With no job, the `$` read a
 * covered shell_idle() could not start. */
static void shell_uncover(uos_screen_t *s)
{
    (void)s;
    /* A job of another host pays its debt in that host's uncover(). */
    if (sh_job.active && sh_job.host == NULL && pay_owed()) {
        return;
    }
    shell_idle(NULL);
}

static uos_screen_t shell_screen = {
    .name = "shell",
    .uncover = shell_uncover,
    .enter = shell_enter,
    .leave = shell_leave,
    .event = shell_event,
    .draw = shell_draw,
    .full = 1,
};

static uos_screen_t *shell_next(void)
{
    uos_screen_t *s = first_fn != NULL ? first_fn() : NULL;

    return s != NULL ? s : &shell_screen;
}

int sh_session_screen_register(sess_next_fn fn)
{
    if (!sh_up) {
        return SH_ERR_DOWN;
    }
    if (fn == NULL) {
        return SH_ERR_ARG;
    }
    if (first_fn != NULL) {
        return SH_ERR_DUP;
    }
    first_fn = fn;
    return 0;
}

/* ---- Output for the commands ---- */

/* SH_RUN_CAPTURE: appends one line. Past SH_CAPTURE_MAX the rest is cut. */
static void cap_add(sh_job_t *j, const char *text)
{
    size_t n = k_strlen(text);

    if (j->capfull) {
        return;
    }
    if (j->caplen + n + 1u > SH_CAPTURE_MAX) {
        j->capfull = 1;
        return;
    }
    if (j->cap == NULL || j->caplen + n + 1u > j->capcap) {
        size_t cap = j->capcap == 0 ? 1024u : j->capcap * 2u;
        char *nb;

        while (cap < j->caplen + n + 1u) {
            cap *= 2u;
        }
        if (cap > SH_CAPTURE_MAX) {
            cap = SH_CAPTURE_MAX;
        }
        nb = k_realloc(j->cap, cap);
        if (nb == NULL) {
            j->capfull = 1;
            return;
        }
        j->cap = nb;
        j->capcap = cap;
    }
    k_memcpy(j->cap + j->caplen, text, n);
    j->caplen += n;
    j->cap[j->caplen++] = '\n';
}

const char *sh_output(size_t *len)
{
    if (len != NULL) {
        *len = cap_view != NULL ? cap_view_len : 0;
    }
    return cap_view;
}

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
    if (j->flags & SH_RUN_CAPTURE) {
        cap_add(j, text);
    } else if (!(j->flags & SH_RUN_QUIET)) {
        put_line(text, CONSOLE_ATTR_NORMAL);
    }
}

void sh_say(sh_job_t *j, dcl_msg_t id, const char *arg, uint32_t num)
{
    if (j->flags & SH_RUN_CAPTURE) {
        char buf[DCL_MSG_SIZE];

        if (dcl_format_message(buf, sizeof(buf), id, arg, num) >= 0) {
            cap_add(j, buf);
        }
    } else if (!(j->flags & SH_RUN_QUIET)) {
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

    if (!sh_job_live(ctx, "read")) {
        return;
    }
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
    rc = con_read_line(sh_screen(), prompt, flags | CON_RL_NOHIST, max, ask_done, sh_job_tag(&sh_job));
    if (rc != 0) {
        ask_kind = NULL;
        ask_cb = NULL;
    }
    shp.prompts++;
    UOS_MARK("sh state=prompt kind=%s n=%u rc=%d", kind, shp.prompts, rc);
    return rc;
}

/* ---- The executor ---- */

static void drive(void *arg)
{
    sh_job_t *j = &sh_job;
    uint32_t t0;
    uint32_t dt;
    int rc;

    if (!sh_job_live(arg, "step")) {
        return;
    }
    j->posted = 0;
    if (orphan) {
        return;
    }
    /* A covered shell: the step could ask, page, start a program or print
     * over the covering screen. It waits; shell_uncover() posts it. */
    if (covered()) {
        owe(SH_OWE_STEP);
        return;
    }
    if (j->cancel || j->impl == NULL || j->impl->step == NULL) {
        j->tally.cancelled = 1;
        (void)sh_end(j, DCL_SYS_CANCEL, NULL, 0, 1);
        finish();
        return;
    }
    if (test_hold) {
        test_held = 1;
        UOS_MARK("sh held=1 id=%u", (unsigned)j->id);
        return;
    }
    test_held = 0;
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
    if (!j->active || orphan) {
        return;
    }
    if (rc == SH_MORE) {
        if (j->posted) {
            return;
        }
        j->posted = 1;
        if (uos_post(drive, sh_job_tag(j)) != 0) {
            uos_timer_id_t id;

            if (uos_timer_start(1, 0, drive, sh_job_tag(j), &id) != 0) {
                (void)sh_end(j, DCL_SYS_INSFMEM, NULL, 0, 1);
                finish();
            }
        }
    } else if (rc == SH_DONE) {
        if (covered()) {
            /* The end prints its message: it waits for the shell. */
            owe(SH_OWE_END);
            return;
        }
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

/* /OUTPUT: the collected text replaces the file in one atomic write, only
 * when the command succeeded. A failure, a warning, a cancel or a full
 * buffer leave the file exactly as it was: no partial text ever reaches
 * it. The write is one call in this loop pass, so Ctrl-C cannot stop it
 * halfway; a power cut leaves the whole old or the whole new file. */
static void write_output(sh_job_t *j)
{
    const char *what = "written";
    int rc;

    if (j->out_spec == NULL) {
        return;
    }
    if (j->ofull && sh_output_write(j->msg, j->cancel)) {
        (void)sh_end(j, DCL_SYS_TOOBIG, j->out_spec, 0, 1);
        what = "toobig";
    } else if (!sh_output_write(j->msg, j->cancel)) {
        what = "kept";
    } else {
        rc = vfs_write_all(j->ctx, j->privs, j->out_spec, "LIS", j->obuf != NULL ? j->obuf : "",
                           j->olen, 0);
        if (rc != VFS_OK) {
            (void)sh_end_err(j, SH_SRC_VFS, rc, j->out_spec);
            what = "failed";
        }
    }
    UOS_MARK("cmd output=%s verb=%s", what, j->verb);
}

static void finish(void)
{
    sh_job_t *j = &sh_job;
    char res[DCL_MSG_SIZE];
    char *nl;
    sh_done_fn cb = j->cb;
    void *cbctx = j->cbctx;
    int after = j->after;
    char login_name[UAF_NAME_MAX + 1];
    int status;
    char *cap;
    size_t caplen;

    owed = SH_OWE_NONE;
    orphan = 0;
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
        if (j->msg_print && (j->flags & SH_RUN_CAPTURE)) {
            cap_add(j, res);
        } else if (j->msg_print && !(j->flags & SH_RUN_QUIET)) {
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
    cap = j->cap;
    caplen = j->caplen;
    k_memcpy(login_name, j->login_name, sizeof(login_name));
    k_memset(j, 0, sizeof(*j));
    if (cb != NULL) {
        cap_view = cap;
        cap_view_len = caplen;
        cb(status, res, cbctx);
        cap_view = NULL;
        cap_view_len = 0;
    }
    if (cap != NULL) {
        k_free(cap);
    }
    switch (after) {
    case SH_AFTER_LOGOUT:
        (void)sess_logout(SESS_END_USER);
        return;
    case SH_AFTER_LOGIN:
        (void)sess_logout_as(SESS_END_USER, login_name);
        k_memset(login_name, 0, sizeof(login_name));
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
static int launch(uos_screen_t *host, const char *line, unsigned flags, const char *src,
                  sh_done_fn cb, void *ctx, char *need, size_t need_size)
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
    if (host != NULL ? !sh_takes_job(1, uos_screen_unit_on_top(host))
                     : !sh_takes_job(shp.on, uos_screen_unit_on_top(&shell_screen))) {
        /* start() may ask, page or start a program's screen. A shell
         * covered by another screen, or not on the stack yet, such as
         * during the welcome, takes no job. */
        return SH_ERR_COVERED;
    }
    k_memset(j, 0, sizeof(*j));
    owed = SH_OWE_NONE;
    orphan = 0;
    if (++job_seq == 0) {
        job_seq = 1;
    }
    j->id = job_seq;
    j->host = host;
    k_snprintf(j->line, sizeof(j->line), "%s", line);
    rc = dcl_parse(j->line, &dcl_uos_table, &j->cmd);
    if (rc == DCL_NORMAL && j->cmd.verb == NULL) {
        /* An empty or comment-only line: the prompt shows a new prompt,
         * an API caller gets an error and no callback. */
        return need != NULL ? 2 : SH_ERR_ARG;
    }
    if (rc == DCL_INSFPRM && need != NULL && j->cmd.prompt != NULL) {
        k_snprintf(need, need_size, "%s", j->cmd.prompt);
        return 1;
    }
    j->active = 1;
    j->host = host;
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
    return launch(NULL, line, flags, "api", cb, ctx, NULL, 0);
}

int sh_run_line_on(uos_screen_t *host, const char *line, unsigned flags, sh_done_fn cb,
                   void *ctx)
{
    if (host == NULL) {
        return SH_ERR_ARG;
    }
    return launch(host, line, flags, host->name != NULL ? host->name : "api", cb, ctx, NULL, 0);
}

int sh_busy(void)
{
    return sh_job.active;
}

int sh_program_register(const char *name, sh_prog_start_fn start)
{
    if (!sh_up) {
        return SH_ERR_DOWN;
    }
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
     * the prompt comes back after it. A shell under another screen, a
     * holotape reader for one, takes no command. */
    if (shp.on && !uos_screen_unit_on_top(&shell_screen) && (shp.reading || !sh_job.active)) {
        UOS_MARK("sh run=refused why=covered");
        return;
    }
    if (shp.reading && ask_kind == NULL) {
        (void)con_line_abort();
        shp.reading = 0;
        dcl_line_reset(&shp.ln);
    }
    rc = launch(NULL, port_line, 0, "port", port_done, NULL, NULL, 0);
    if (rc == SH_ERR_COVERED) {
        UOS_MARK("sh run=refused why=covered");
    } else if (rc != 0) {
        UOS_MARK("sh run=refused rc=%d", rc);
    }
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* A test program: it pushes no screen and ends only when the port says
 * so, with `sh prog done <n> <status>`, even after its job ended. */
#define TP_MAX 8u

static struct {
    sh_prog_done_fn done;
    void *tag;
} tp[TP_MAX];
static unsigned tp_n;
static char tp_name[SH_PROG_NAME_MAX + 1];
static unsigned tp_end_n;
static int tp_end_status;
static int tp_busy;

static int test_prog_start(const char *args, sh_prog_done_fn done, void *tag)
{
    (void)args;
    if (tp_n >= TP_MAX) {
        return 1;
    }
    tp[tp_n].done = done;
    tp[tp_n].tag = tag;
    tp_n++;
    UOS_MARK("sh prog=start n=%u", tp_n);
    return 0;
}

static void test_prog_reg(void *arg)
{
    (void)arg;
    tp_busy = 0;
    /* The test program stands in for an installed one of the same name. */
    UOS_MARK("sh prog=reg name=%s rc=%d", tp_name, sh_prog_put(&sh_progs, tp_name, test_prog_start));
}

static void test_prog_end(void *arg)
{
    char text[40];
    unsigned n = tp_end_n;

    (void)arg;
    tp_busy = 0;
    UOS_MARK("sh prog=done n=%u", n);
    k_snprintf(text, sizeof(text), "TEST PROGRAM RUN %u ENDED", n);
    tp[n - 1u].done(tp[n - 1u].tag, tp_end_status, text);
}

/* A small decimal number, or -1. */
static int tp_num(const char *s)
{
    int v = 0;

    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || v > 1000) {
            return -1;
        }
        v = v * 10 + (*s - '0');
    }
    return v;
}

/* sh api: one sh_run_line() call as another service makes it. */
static char api_line[DCL_LINE_MAX + 2];
static int api_kind;            /* 0 the line, 1 NULL, 2 one byte too long */
static unsigned api_flags;
static unsigned api_cbs;        /* callbacks so far, for exactly-once checks */

static void api_done(int status, const char *result, void *ctx)
{
    char q[DCL_MSG_SIZE + 8];

    (void)ctx;
    api_cbs++;
    (void)cv_quote(q, sizeof(q), result, k_strlen(result), 120);
    UOS_MARK("sh api=done n=%u status=%d result=%s", api_cbs, status, q);
}

static void api_run(void *arg)
{
    const char *line = api_line;
    int rc;

    (void)arg;
    port_pending = 0;
    if (api_kind == 1) {
        line = NULL;
    } else if (api_kind == 2) {
        k_memset(api_line, 'A', DCL_LINE_MAX + 1u);
        api_line[DCL_LINE_MAX + 1u] = '\0';
    }
    rc = sh_run_line(line, api_flags, api_done, NULL);
    UOS_MARK("sh api=call rc=%d cbs=%u", rc, api_cbs);
}

static int test_api_cmd(int argc, char **argv)
{
    const char *f = argv[0];

    if (port_pending) {
        return uos_maint_fail("busy");
    }
    if (k_strcmp(f, "q") == 0) {
        api_flags = SH_RUN_QUIET;
    } else if (k_strcmp(f, "-") == 0) {
        api_flags = 0;
    } else {
        return uos_maint_fail("usage sh api <-|q> null|long|<line>");
    }
    api_kind = 0;
    if (argc < 2) {
        api_line[0] = '\0';
    } else if (argc == 2 && k_strcmp(argv[1], "null") == 0) {
        api_kind = 1;
    } else if (argc == 2 && k_strcmp(argv[1], "long") == 0) {
        api_kind = 2;
    } else if (sh_unquote(uos_mt_rejoin(argv[1], argv[argc - 1]), api_line,
                          sizeof(api_line)) < 0) {
        return uos_maint_fail("bad line");
    }
    if (uos_post(api_run, NULL) != 0) {
        return uos_maint_fail("full");
    }
    port_pending = 1;
    return 0;
}

static void test_release(void *arg)
{
    (void)arg;
    test_hold = 0;
    if (test_held) {
        test_held = 0;
        sh_resume(&sh_job, SH_MORE);
    }
}

static int test_prog_cmd(int argc, char **argv)
{
    if (tp_busy) {
        return uos_maint_fail("busy");
    }
    if (argc == 2 && k_strcmp(argv[0], "reg") == 0) {
        k_snprintf(tp_name, sizeof(tp_name), "%s", argv[1]);
        if (uos_post(test_prog_reg, NULL) != 0) {
            return uos_maint_fail("full");
        }
        tp_busy = 1;
        return 0;
    }
    if (argc == 3 && k_strcmp(argv[0], "done") == 0) {
        int n = tp_num(argv[1]);
        int st = tp_num(argv[2]);

        if (n <= 0 || (unsigned)n > tp_n || st < 0) {
            return uos_maint_fail("no such run");
        }
        tp_end_n = (unsigned)n;
        tp_end_status = st;
        if (uos_post(test_prog_end, NULL) != 0) {
            return uos_maint_fail("full");
        }
        tp_busy = 1;
        return 0;
    }
    return uos_maint_fail("usage sh prog reg <NAME>|done <n> <status>");
}
#endif

static int cmd_sh(int argc, char **argv)
{
    if (argc == 1 && k_strcmp(argv[0], "state") == 0) {
        const char *st = sh_job.active ? "running" : (shp.reading ? "prompt" : "idle");

        uos_maint_reply("sh state=%s kind=%s ask=%s verb=%s pending=%u prompts=%u runs=%u "
                        "id=%u", st,
                        shp.reading ? kind_name[shp.kind] : "none",
                        ask_kind != NULL ? ask_kind : "none",
                        sh_job.active ? sh_job.verb : "none", input_pending(), shp.prompts,
                        runs, sh_job.active ? (unsigned)sh_job.id : 0u);
        return 0;
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (argc >= 3 && k_strcmp(argv[0], "prog") == 0) {
        return test_prog_cmd(argc - 1, argv + 1);
    }
    if (argc >= 2 && k_strcmp(argv[0], "api") == 0) {
        return test_api_cmd(argc - 1, argv + 1);
    }
    if (argc == 2 && k_strcmp(argv[0], "hold") == 0) {
        if (k_strcmp(argv[1], "1") == 0) {
            test_hold = 1;
            return 0;
        }
        return uos_post(test_release, NULL) == 0 ? 0 : uos_maint_fail("full");
    }
    if (argc == 2 && k_strcmp(argv[0], "park") == 0) {
        /* 1 keeps program calls back; 0 posts them, in slot order. */
        uintptr_t i;

        test_park = k_strcmp(argv[1], "1") == 0;
        for (i = 0; !test_park && i < SH_PCALL_MAX; i++) {
            if (pcalls[i].fn != NULL && pcalls[i].parked) {
                if (uos_post(prog_call, (void *)i) != 0) {
                    return uos_maint_fail("full");
                }
                pcalls[i].parked = 0;
            }
        }
        return 0;
    }
#endif
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
     * screen, so a read or a program it waited on never calls back. A
     * reset that took the shell away leaves the job an orphan: it ends
     * here, before any step paints over the screen that replaced it. */
    if (sh_job.active && (!sess_logged_in() || orphan)) {
        sh_job.cancel = 1;
        (void)sh_end(&sh_job, DCL_SYS_CANCEL, NULL, 0, 0);
        finish();
    }
}

const uos_service_t uos_svc_shell = { "shell", sh_svc_init, sh_svc_tick, NULL };
