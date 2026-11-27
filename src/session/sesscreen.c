/*
 * The session's screens: login, lockout and welcome. After the welcome the
 * screen of sess_set_next() takes over. Thin: every decision
 * comes from login.c. The state machine is lg_next(); sess_go() applies an
 * event and shows the screen of the new state.
 *
 * Markers:
 *   login state=<user|pass|check>          a prompt or the check starts
 *   login user=<NAME> result=<ok|bad> left=<tries> steps= ticks= us= maxstep_us=
 *   lockout state=start s=<seconds>, lockout s=<seconds> once a second,
 *   lockout state=end
 *   session screen=<name>                  the screen after the welcome
 * The user name is the typed name as one word (lg_name_word). Unknown user
 * and wrong password give the same marker, the same message and the same
 * work. No marker holds the password, a salt or a hash.
 */

#include <robco/console.h>
#include <robco/klibc.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "con.h"
#include "devs.h"
#include "sessint.h"

/* Rows of the login screen. */
#define ROW_MSG         LG_HEADER_ROWS
#define ROW_USER        (LG_HEADER_ROWS + 2)
#define ROW_PASS        (LG_HEADER_ROWS + 3)
#define ROW_CHECK       (LG_HEADER_ROWS + 5)

#define WELCOME_TICKS   150u            /* 1.5 s, or until a key */
#define LOCK_TICKS      UOS_TICK_HZ     /* the countdown, once a second */

static lg_check_t chk;
static uint32_t chk_t0;                 /* tick the check started */
static uint64_t chk_us0;
static uint32_t chk_maxstep_us;

/* ---- Helpers ---- */

void sess_serial(const char *line)
{
    uos_serial_break();
    uart_puts(line);
    uart_putc('\n');
}

static void put_row(unsigned row, const char *text, unsigned attr)
{
    unsigned n = (unsigned)k_strlen(text);
    unsigned cols;

    if (!uos.fb_ok) {
        return;
    }
    cols = console_cols();
    if (n > cols - 1) {
        n = cols - 1;
    }
    console_set_cursor(0, row);
    console_set_attr(attr);
    console_write(text, n);
    console_set_attr(CONSOLE_ATTR_NORMAL);
    for (; n < cols - 1; n++) {
        console_putc(' ');
    }
}

static void header(int serial)
{
    char line[UOS_LINE_MAX + 1];
    unsigned i;
    int bright;

    if (uos.fb_ok) {
        console_set_attr(CONSOLE_ATTR_NORMAL);
        console_clear();
    }
    for (i = 0; i < LG_HEADER_ROWS; i++) {
        if (i == LG_HEADER_ROWS - 1) {
            (void)lg_notice_line(uos.cfg_bad, devs_cfg_errors() != 0, line, sizeof(line));
        } else {
            (void)lg_header_line(uos_settings()->name, i, line, sizeof(line));
        }
        bright = i == 0 || i == LG_HEADER_ROWS - 1;
        put_row(i, line, bright ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL);
        if (serial) {
            sess_serial(line);
        }
    }
}

static void set_cursor(unsigned row)
{
    if (uos.fb_ok) {
        console_set_cursor(0, row);
    }
}

/* Runs fn on the next loop pass; a full queue falls back to a 1-tick timer. */
static void later(uos_call_fn fn)
{
    uos_timer_id_t id;

    if (uos_post(fn, NULL) != 0 && uos_timer_start(1, 0, fn, NULL, &id) != 0) {
        uos_set_error("session");
    }
}

/* ---- The login screen ---- */

static void on_user(int result, const char *text, unsigned len, void *ctx);
static void on_pass(int result, const char *text, unsigned len, void *ctx);
static void start_pass(void);
static void step(lg_event_t e);

static void login_draw(uos_screen_t *s)
{
    char line[UOS_LINE_MAX + 1];

    (void)s;
    header(0);
    put_row(ROW_MSG, sess.msg_text, CONSOLE_ATTR_BRIGHT);
    if (sess.st == LG_S_PASS || sess.st == LG_S_CHECK) {
        k_snprintf(line, sizeof(line), "USERNAME: %s", sess.typed);
        put_row(ROW_USER, line, CONSOLE_ATTR_NORMAL);
    }
    if (sess.st == LG_S_CHECK) {
        put_row(ROW_PASS, "PASSWORD:", CONSOLE_ATTR_NORMAL);
        put_row(ROW_CHECK, "CHECKING CREDENTIALS...", CONSOLE_ATTR_DIM);
    }
    con_status_redraw();
    set_cursor(sess.st == LG_S_PASS ? ROW_PASS : ROW_USER);
}

/* Non-zero while the user name read line runs over the login screen. */
static int reading_user;

int sess_prompt_on_top(void)
{
    if (sess.in || sess.st != LG_S_USER) {
        return 0;
    }
    /* The user name read is the login's child: the unit is on top. */
    return uos_screen_unit_on_top(&sess_login_screen);
}

/* Starts the user name read. A posted call and login_uncover() both come
 * here: the first one that finds the login on top with no read starts it,
 * and the other finds the read running and does nothing. A covered login
 * starts nothing; it owes the read, and login_uncover() starts it. */
static void start_user(void *arg)
{
    int rc;

    (void)arg;
    if (sess.st != LG_S_USER || reading_user || con_line_active() ||
        !uos_screen_on_top(&sess_login_screen)) {
        return;
    }
    if (sess.preset[0] != '\0') {
        /* LOGIN <user>: the name is given, the password is asked. */
        k_snprintf(sess.typed, sizeof(sess.typed), "%s", sess.preset);
        k_memset(sess.preset, 0, sizeof(sess.preset));
        sess.msg_text[0] = '\0';
        UOS_MARK("login state=user given=yes");
        step(LG_E_USER_OK);
        login_draw(&sess_login_screen);
        start_pass();
        return;
    }
    set_cursor(ROW_USER);
    rc = con_read_line(&sess_login_screen, "USERNAME:", CON_RL_NOHIST | CON_RL_ALTKEY, UAF_NAME_MAX, on_user, NULL);
    reading_user = rc == 0;
    UOS_MARK("login state=user rc=%d", rc);
}

static void start_pass(void)
{
    int rc;

    set_cursor(ROW_PASS);
    rc = con_read_line(&sess_login_screen, "PASSWORD:", CON_RL_MASK | CON_RL_NOHIST, UAF_PASSWORD_MAX, on_pass, NULL);
    UOS_MARK("login state=pass rc=%d", rc);
}

/* Applies a login-screen event that stays on the login screen. */
static void step(lg_event_t e)
{
    lg_step_t r = lg_next(sess.st, e);

    if (!r.changed) {
        return;
    }
    sess.st = r.next;
    if (r.msg != LG_M_NONE) {
        k_snprintf(sess.msg_text, sizeof(sess.msg_text), "%s", lg_msg_text(r.msg));
    }
    uos_screen_invalidate();
}

void sess_prompt_later(void)
{
    later(start_user);
}

static void on_user(int result, const char *text, unsigned len, void *ctx)
{
    (void)ctx;
    reading_user = 0;
    if (result == CON_ALT) {
        /* F2 on an empty name: the password game, when one is installed. */
        UOS_MARK("login state=hack");
        if (sess.hack != NULL && sess.hack() == 0) {
            return;
        }
        later(start_user);
        return;
    }
    if (result != CON_OK) {
        step(LG_E_USER_CANCEL);
    } else if (len == 0) {
        step(LG_E_USER_EMPTY);
    } else {
        k_snprintf(sess.typed, sizeof(sess.typed), "%s", text);
        sess.msg_text[0] = '\0';
        step(LG_E_USER_OK);
    }
    if (sess.st == LG_S_PASS) {
        login_draw(&sess_login_screen);
        start_pass();
    } else {
        later(start_user);
    }
}

static void check_step(void *arg);

static void on_pass(int result, const char *text, unsigned len, void *ctx)
{
    (void)ctx;
    (void)len;
    if (result != CON_OK) {
        step(LG_E_PASS_CANCEL);
        sess.typed[0] = '\0';
        later(start_user);
        return;
    }
    /* The check copies the password; the console wipes its own buffer when
     * this callback returns. */
    lg_check_begin(&chk, &sess.uaf, sess.typed, text);
    step(LG_E_PASS_OK);
    chk_t0 = uos_ticks();
    chk_us0 = timer_now_us();
    chk_maxstep_us = 0;
    login_draw(&sess_login_screen);
    sess_serial("CHECKING CREDENTIALS...");
    UOS_MARK("login state=check");
    later(check_step);
}

static void check_done(void)
{
    char name[UAF_NAME_MAX + 1];
    int idx = -1;
    uint32_t steps = chk.steps;
    uaf_err_t rc = lg_check_end(&chk, &idx);
    unsigned ticks = (unsigned)(uos_ticks() - chk_t0);
    unsigned us = (unsigned)(timer_now_us() - chk_us0);
    int locked;
    uint32_t now;

    lg_name_word(sess.typed, name);
    /* A disabled account counts as a failure and shows the same message:
     * the screen never tells that the password was right. */
    if (rc == UAF_OK) {
        sess_begin(idx, "login");           /* clears the failure count */
        UOS_MARK("login user=%s result=ok left=%u steps=%u ticks=%u us=%u maxstep_us=%u", name,
                 lk_tries_left(&sess.lk), (unsigned)steps, ticks, us, (unsigned)chk_maxstep_us);
        sess.typed[0] = '\0';
        sess.msg_text[0] = '\0';
        sess_go(LG_E_GOOD);
        return;
    }
    /* One reading for the lock and its start marker: a tick between two
     * readings would make the marker later than the lock. A lock that
     * already runs keeps its own start tick. */
    now = uos_ticks();
    locked = lk_fail(&sess.lk, now * (1000u / UOS_TICK_HZ));
    if (locked == LK_STARTED) {
        sess.lock_tick = now;
    }
    UOS_MARK("login user=%s result=bad left=%u steps=%u ticks=%u us=%u maxstep_us=%u", name,
             lk_tries_left(&sess.lk), (unsigned)steps, ticks, us, (unsigned)chk_maxstep_us);
    sess.typed[0] = '\0';
    if (locked) {
        sess_go(LG_E_BAD_LOCKED);
        return;
    }
    step(LG_E_BAD);
    (void)lg_denied_line(sess.msg_text, sizeof(sess.msg_text), lk_tries_left(&sess.lk));
    sess_serial(sess.msg_text);
    later(start_user);
}

static void check_step(void *arg)
{
    uint64_t t0 = timer_now_us();
    int r;
    uint32_t dt;

    (void)arg;
    if (sess.st != LG_S_CHECK) {
        lg_check_wipe(&chk);
        return;
    }
    r = lg_check_step(&chk, LG_STEP_BLOCKS);
    dt = (uint32_t)(timer_now_us() - t0);
    if (dt > chk_maxstep_us) {
        chk_maxstep_us = dt;
    }
    if (r == 0) {
        later(check_step);
    } else {
        check_done();
    }
}

static void first_start(void *arg)
{
    (void)arg;
    if (sess.st == LG_S_OFF) {
        sess_go(lk_locked(&sess.lk, sess_now_ms()) ? LG_E_START_LOCKED : LG_E_START);
    }
}

static void login_enter(uos_screen_t *s)
{
    (void)s;
    if (sess.st == LG_S_OFF) {
        /* From the first screen: the state machine starts on the next pass. */
        header(0);
        later(first_start);
        return;
    }
    header(1);
    if (sess.msg_text[0] != '\0') {
        sess_serial(sess.msg_text);
    }
    if (sess.st == LG_S_USER) {
        later(start_user);
    }
}

/* Keys that reach the login screen come while no read runs: during the
 * check, or between two prompts. They do nothing. */
static int login_event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)s;
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

/* On top again, after the screen over it closed. At the user name prompt
 * the login owes a read: start it, so keys reach a prompt. In the other
 * states a read runs, the check steps on its own, or the state machine
 * moves to another screen. */
static void login_uncover(uos_screen_t *s)
{
    (void)s;
    start_user(NULL);
}

uos_screen_t sess_login_screen = {
    .name = "login",
    .uncover = login_uncover,
    .enter = login_enter,
    .event = login_event,
    .draw = login_draw,
    .full = 1,
};

/* ---- The lockout screen ---- */

static uos_timer_id_t lock_timer;
static uos_screen_t lock_screen;

static const char *lock_text(void)
{
    return sess.lock_hack ? "TERMINAL LOCKED: PASSWORD RECOVERY FAILED."
                          : "TERMINAL LOCKED: TOO MANY FAILED LOGINS.";
}

static void lock_draw(uos_screen_t *s)
{
    char line[UOS_LINE_MAX + 1];

    (void)s;
    header(0);
    put_row(ROW_MSG, lock_text(), CONSOLE_ATTR_BRIGHT);
    (void)lg_locked_line(line, sizeof(line), lk_remaining_s(&sess.lk, sess_now_ms()));
    put_row(ROW_USER, line, CONSOLE_ATTR_NORMAL);
    put_row(ROW_PASS, "INPUT IS REFUSED UNTIL THEN.", CONSOLE_ATTR_DIM);
    con_status_redraw();
}

/* `now` is the tick that saw the lock expire. */
static void lock_end(uint32_t now)
{
    if (sess.st != LG_S_LOCKED) {
        return;
    }
    (void)uos_timer_cancel(lock_timer);
    lock_timer = UOS_TIMER_NONE;
    UOS_MARK("lockout state=end t=%u why=%s", (unsigned)now, sess.lock_hack ? "hack" : "login");
    sess.lock_hack = 0;
    sess_go(LG_E_LOCK_END);
}

static void lock_second(void *arg)
{
    uint32_t now = uos_ticks();
    uint32_t left;

    (void)arg;
    left = lk_remaining_s(&sess.lk, now * (1000u / UOS_TICK_HZ));
    if (left == 0) {
        lock_end(now);
        return;
    }
    UOS_MARK("lockout s=%u", (unsigned)left);
    uos_screen_dirty(&lock_screen);
}

static void lock_enter(uos_screen_t *s)
{
    char line[UOS_LINE_MAX + 1];
    uint32_t left = lk_remaining_s(&sess.lk, sess_now_ms());

    (void)s;
    UOS_MARK("lockout state=start s=%u t=%u why=%s", (unsigned)left, (unsigned)sess.lock_tick,
             sess.lock_hack ? "hack" : "login");
    header(1);
    sess_serial(lock_text());
    (void)lg_locked_line(line, sizeof(line), left);
    sess_serial(line);
    if (uos_timer_start(LOCK_TICKS, LOCK_TICKS, lock_second, NULL, &lock_timer) != 0) {
        lock_timer = UOS_TIMER_NONE;
        uos_set_error("timer");
    }
}

static void lock_leave(uos_screen_t *s)
{
    (void)s;
    if (lock_timer != UOS_TIMER_NONE) {
        (void)uos_timer_cancel(lock_timer);
        lock_timer = UOS_TIMER_NONE;
    }
}

/* Every key is refused. The tick ends the lock on time; the timer above
 * only redraws the countdown. */
static int lock_event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)s;
    if (ev->type == UOS_EV_TICK) {
        uint32_t now = uos_ticks();

        if (!lk_locked(&sess.lk, now * (1000u / UOS_TICK_HZ))) {
            lock_end(now);
        }
        return 0;
    }
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

static uos_screen_t lock_screen = {
    .name = "lockout",
    .enter = lock_enter,
    .leave = lock_leave,
    .event = lock_event,
    .draw = lock_draw,
    .full = 1,
};

/* ---- The welcome ---- */

static uos_timer_id_t welcome_timer;

static void welcome_draw(uos_screen_t *s)
{
    char line[UOS_LINE_MAX + 1];
    unsigned i;

    (void)s;
    header(0);
    for (i = 0; i < LG_WELCOME_ROWS; i++) {
        (void)lg_welcome_line(sess.user, sess.privs, i, line, sizeof(line));
        put_row(ROW_MSG + i, line, i == 0 ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL);
    }
    con_status_redraw();
}

static void welcome_done(void *arg)
{
    uos_screen_t *next = NULL;

    (void)arg;
    if (sess.st != LG_S_WELCOME || !sess.in) {
        return;
    }
    if (sess.next != NULL) {
        next = sess.next();
    }
    if (next == NULL) {
        /* No service registered a screen: no session without a screen. */
        UOS_MARK("session screen=none");
        (void)sess_logout(SESS_END_SHELL);
        return;
    }
    sess.st = lg_next(sess.st, LG_E_WELCOME_DONE).next;
    sess_show_only(next);
    UOS_MARK("session screen=%s", next->name);
}

static void welcome_enter(uos_screen_t *s)
{
    char line[UOS_LINE_MAX + 1];
    unsigned i;

    (void)s;
    for (i = 0; i < LG_WELCOME_ROWS; i++) {
        (void)lg_welcome_line(sess.user, sess.privs, i, line, sizeof(line));
        sess_serial(line);
    }
    if (uos_timer_start(WELCOME_TICKS, 0, welcome_done, NULL, &welcome_timer) != 0) {
        welcome_timer = UOS_TIMER_NONE;
        later(welcome_done);
    }
}

static void welcome_leave(uos_screen_t *s)
{
    (void)s;
    if (welcome_timer != UOS_TIMER_NONE) {
        (void)uos_timer_cancel(welcome_timer);
        welcome_timer = UOS_TIMER_NONE;
    }
}

static int welcome_event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)s;
    if (uos_ev_fresh_key(ev) || ev->type == UOS_EV_CHAR) {
        welcome_done(NULL);
    }
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

static uos_screen_t welcome_screen = {
    .name = "welcome",
    .enter = welcome_enter,
    .leave = welcome_leave,
    .event = welcome_event,
    .draw = welcome_draw,
    .full = 1,
};

/* ---- Moving between them ---- */

void sess_show_only(uos_screen_t *s)
{
    /* Every screen above the bottom one leaves first: a read or a pager
     * ends without calling back. */
    (void)uos_screen_unwind();
    reading_user = 0;
    if (!uos_screen_on_top(s)) {
        (void)uos_screen_replace(s);
    } else {
        uos_screen_invalidate();
    }
}

void sess_go(lg_event_t e)
{
    lg_step_t r = lg_next(sess.st, e);

    if (!r.changed) {
        return;
    }
    sess.st = r.next;
    sess.msg_text[0] = '\0';
    if (r.msg != LG_M_NONE) {
        k_snprintf(sess.msg_text, sizeof(sess.msg_text), "%s", lg_msg_text(r.msg));
    }
    switch (sess.st) {
    case LG_S_USER:
        sess.typed[0] = '\0';
        if (uos_screen_on_top(&sess_login_screen)) {
            /* The first start: the login screen is already shown. */
            uos_screen_invalidate();
            later(start_user);
        } else {
            sess_show_only(&sess_login_screen);
        }
        break;
    case LG_S_LOCKED:
        sess_show_only(&lock_screen);
        break;
    case LG_S_WELCOME:
        sess_show_only(&welcome_screen);
        break;
    default:
        break;
    }
}
