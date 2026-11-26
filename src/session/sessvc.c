/*
 * The session service: loads /UOS/SYSUAF.DAT, applies [login] from
 * UOS.CFG, holds the session fields and answers the `sess` maintenance
 * command. The rules are pure, in login.c. The screens are sesscreen.c.
 *
 *   sess state     state= db= user= privs= flags= home= tries= locked_s=
 *                  idle_s= pending=<input events not yet taken>
 *   sess logout    posts sess_logout(SESS_END_MAINT); marker session end
 * Test builds only (UOS_TEST_HOOKS=1):
 *   sess login <user>
 *                  starts a session for <user> with no password, so later
 *                  tasks test their screens without the hash cost
 *
 * Markers:
 *   sessdb result=<ok|nocard|notree|missing|damaged> users= iterations= why= rec=
 *   sesscfg result=<ok|fallback> retries= lockout_s= idle_s=
 *   sesswarn why=lockout count=     the [login] values were refused
 *   session state=start user= privs= how=<login|test>
 *   session state=end user= reason=<user|maint|idle|shell>
 * No marker ever holds a password, a salt or a hash.
 */

#include <robco/input.h>
#include <robco/klibc.h>

#include "sessint.h"
#include "uos_maint.h"
#include "uos_service.h"
#include "vfs.h"

sess_state_t sess;

static unsigned warnings;

uint32_t sess_now_ms(void)
{
    /* 100 Hz: 10 ms a tick. The product wraps at 2^32 ms, as lockout.h
     * expects: only differences are used. */
    return uos_ticks() * (1000u / UOS_TICK_HZ);
}

/* ---- The public calls ---- */

int sess_logged_in(void)
{
    return sess.in;
}

const char *sess_user(void)
{
    return sess.in ? sess.user : "";
}

uint32_t sess_privs(void)
{
    return sess.in ? sess.privs : 0u;
}

uint32_t sess_flags(void)
{
    return sess.in ? sess.flags : 0u;
}

const char *sess_home(void)
{
    return sess.in ? sess.home : "";
}

int sess_vp_ctx(vp_ctx_t *ctx)
{
    if (ctx == NULL) {
        return SESS_ERR_ARG;
    }
    if (!sess.in) {
        return SESS_ERR_NONE;
    }
    vp_ctx_init(ctx, sess.home);
    return 0;
}

const char *sess_end_name(int reason)
{
    switch (reason) {
    case SESS_END_USER:  return "user";
    case SESS_END_MAINT: return "maint";
    case SESS_END_IDLE:  return "idle";
    case SESS_END_SHELL: return "shell";
    default:             return "unknown";
    }
}

int sess_set_next(sess_next_fn fn)
{
    if (fn == NULL) {
        return SESS_ERR_ARG;
    }
    if (sess.next_set) {
        return SESS_ERR_SET;
    }
    sess.next = fn;
    sess.next_set = 1;
    return 0;
}

const uaf_db_t *sess_db(void)
{
    return sess.db == LG_DB_OK ? &sess.uaf : NULL;
}

const uaf_rec_t *sess_find(const char *name)
{
    int i;

    if (sess.db != LG_DB_OK || name == NULL) {
        return NULL;
    }
    i = uaf_find(&sess.uaf, name);
    return i >= 0 ? &sess.uaf.rec[i] : NULL;
}

void sess_begin(int idx, const char *how)
{
    const uaf_rec_t *r = &sess.uaf.rec[idx];

    sess.in = 1;
    sess.idx = idx;
    lg_rec_name(r, sess.user);
    lg_rec_home(r, sess.home);
    sess.privs = r->privs;
    sess.flags = r->flags;
    sess.start_tick = uos_ticks();
    /* Every way into a session clears the failure count: lockout.h. */
    lk_session_start(&sess.lk);
    k_snprintf(uos.user, sizeof(uos.user), "%s", sess.user);
    UOS_MARK("session state=start user=%s privs=%u how=%s", sess.user, (unsigned)sess.privs, how);
}

int sess_logout(int reason)
{
    if (reason < 0 || reason >= SESS_END_NUM) {
        return SESS_ERR_ARG;
    }
    if (!sess.in) {
        return SESS_ERR_NONE;
    }
    UOS_MARK("session state=end user=%s reason=%s", sess.user, sess_end_name(reason));
    sess.in = 0;
    sess.idx = -1;
    k_memset(sess.user, 0, sizeof(sess.user));
    k_memset(sess.home, 0, sizeof(sess.home));
    sess.privs = 0;
    sess.flags = 0;
    sess.start_tick = 0;
    k_memset(uos.user, 0, sizeof(uos.user));
    sess_go(LG_E_LOGOUT);
    return 0;
}

int sess_logout_as(int reason, const char *name)
{
    int rc;

    if (name != NULL) {
        k_snprintf(sess.preset, sizeof(sess.preset), "%s", name);
    }
    rc = sess_logout(reason);
    if (rc != 0) {
        k_memset(sess.preset, 0, sizeof(sess.preset));
    }
    return rc;
}

/* ---- The password game ---- */

int sess_hack_check(void)
{
    const uos_settings_t *cfg = uos_settings();

    return lg_hack_allowed(cfg->hack_enabled, sess.db == LG_DB_OK, sess_find(cfg->hack_user),
                           sess.in ? LG_S_SESSION : sess.st,
                           sess.lk.locked &&
                               (uint32_t)(sess_now_ms() - sess.lk.locked_at) < sess.lk.lock_ms);
}

int sess_hack_begin(void)
{
    int rc = sess_hack_check();
    int idx;

    if (rc == LG_HACK_OK) {
        idx = uaf_find(&sess.uaf, uos_settings()->hack_user);
        sess_begin(idx, "hack");
        sess_go(LG_E_DIRECT);
        return LG_HACK_OK;
    }
    UOS_MARK("session result=refused why=%s how=hack", lg_hack_name((lg_hack_t)rc));
    return rc;
}

int sess_hack_lock(void)
{
    unsigned secs = uos_settings()->hack_lockout_s;
    uint32_t now;

    if (sess.in || sess.st != LG_S_USER || secs == 0) {
        return 0;
    }
    now = uos_ticks();
    /* One reading for the lock and its start marker; a longer lock that
     * already runs keeps its own start tick. */
    if (lk_lock(&sess.lk, now * (1000u / UOS_TICK_HZ), secs) == LK_STARTED) {
        sess.lock_tick = now;
    }
    sess.lock_hack = 1;
    sess_go(LG_E_HACK_LOCKED);
    return 1;
}

int sess_set_hack_entry(sess_hack_fn fn)
{
    if (fn == NULL) {
        return SESS_ERR_ARG;
    }
    if (sess.hack != NULL) {
        return SESS_ERR_SET;
    }
    sess.hack = fn;
    return 0;
}

void sess_resume(void)
{
    if (!sess.in && sess.st == LG_S_USER) {
        sess_show_only(&sess_login_screen);
        sess_prompt_later();
    }
}

/* ---- The maintenance command ---- */

static void post_logout(void *arg)
{
    (void)arg;
    if (sess_logout(SESS_END_MAINT) != 0) {
        UOS_MARK("session result=none");
    }
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds only: the nm test proves the shipped image has none of this. */
/* Each posted `sess login` owns one slot until its call runs: two commands
 * in one port write keep their own names and run in order. Maintenance
 * commands and posted calls both run in the loop, so no lock is needed. */
#define TEST_LOGIN_SLOTS    4u

static char uos_test_login_name[TEST_LOGIN_SLOTS][UAF_NAME_MAX + 1];
static uint8_t uos_test_login_used[TEST_LOGIN_SLOTS];

static void uos_test_sess_login(void *arg)
{
    char name[UAF_NAME_MAX + 1];
    uint32_t slot = (uint32_t)(uintptr_t)arg;
    int idx;

    k_memcpy(name, uos_test_login_name[slot], sizeof(name));
    uos_test_login_used[slot] = 0;
    idx = uaf_find(&sess.uaf, name);
    if (sess.db != LG_DB_OK || idx < 0) {
        UOS_MARK("session result=nouser");
        return;
    }
    if (sess.in || !lg_next(sess.st, LG_E_DIRECT).changed) {
        UOS_MARK("session result=busy");
        return;
    }
    sess_begin(idx, "test");
    sess_go(LG_E_DIRECT);
}

static int cmd_login(int argc, char **argv)
{
    uint32_t slot;

    if (argc != 2 || k_strlen(argv[1]) > UAF_NAME_MAX) {
        return uos_maint_fail("usage sess login <user>");
    }
    for (slot = 0; slot < TEST_LOGIN_SLOTS && uos_test_login_used[slot]; slot++) {
    }
    if (slot == TEST_LOGIN_SLOTS) {
        return uos_maint_fail("full");
    }
    k_snprintf(uos_test_login_name[slot], sizeof(uos_test_login_name[slot]), "%s", argv[1]);
    if (uos_post(uos_test_sess_login, (void *)(uintptr_t)slot) != 0) {
        return uos_maint_fail("full");
    }
    uos_test_login_used[slot] = 1;
    return 0;
}
#endif

static int cmd_sess(int argc, char **argv)
{
    if (argc == 1 && k_strcmp(argv[0], "state") == 0) {
        uos_maint_reply("sess state=%s db=%s user=%s privs=%u flags=%u home=%s tries=%u "
                        "locked_s=%u idle_s=%u pending=%u",
                        lg_state_name(sess.st), lg_db_name(sess.db),
                        sess.in ? sess.user : "none", (unsigned)sess_privs(),
                        (unsigned)sess_flags(), sess.in ? sess.home : "none",
                        lk_tries_left(&sess.lk), (unsigned)lk_remaining_s(&sess.lk, sess_now_ms()),
                        sess.idle_s, uos_input_pending());
        return 0;
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (argc == 1 && k_strcmp(argv[0], "logout") == 0) {
        return uos_post(post_logout, NULL) == 0 ? 0 : uos_maint_fail("full");
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (argc >= 1 && k_strcmp(argv[0], "login") == 0) {
        return cmd_login(argc, argv);
    }
#endif
    return uos_maint_fail("usage sess state|logout");
}

/* ---- The service ---- */

static void load_db(void)
{
    char *buf = NULL;
    size_t len = 0;
    int bad = -1;
    int rc;
    uaf_err_t err;

    sess.why[0] = '\0';
    rc = vfs_sys_read_all(SESS_UAF_PATH, UAF_MAX_IMAGE, &buf, &len);
    if (rc == VFS_E_NOCARD) {
        sess.db = LG_DB_NOCARD;
    } else if (rc == VFS_E_NOTREE) {
        sess.db = LG_DB_NOTREE;
    } else if (rc == VFS_E_NOTFOUND) {
        sess.db = LG_DB_MISSING;
    } else if (rc != VFS_OK) {
        sess.db = LG_DB_DAMAGED;
        k_snprintf(sess.why, sizeof(sess.why), "%s", vfs_strerror(rc));
    } else {
        err = uaf_load(&sess.uaf, buf, len, &bad);
        if (err == UAF_OK) {
            sess.db = LG_DB_OK;
        } else {
            sess.db = LG_DB_DAMAGED;
            k_snprintf(sess.why, sizeof(sess.why), "%s", lg_uaf_word(err));
        }
    }
    if (buf != NULL) {
        /* The image holds salts and hashes: none stays in the heap. */
        k_memset(buf, 0, len);
        vfs_free(buf);
    }
    if (sess.db != LG_DB_OK) {
        k_memset(&sess.uaf, 0, sizeof(sess.uaf));
    }
    UOS_MARK("sessdb result=%s users=%u iterations=%u why=%s rec=%d", lg_db_name(sess.db),
             sess.db == LG_DB_OK ? (unsigned)sess.uaf.count : 0u,
             sess.db == LG_DB_OK ? (unsigned)sess.uaf.iterations : 0u,
             sess.why[0] != '\0' ? sess.why : "none", bad);
}

int sess_reload(void)
{
    load_db();
    if (sess.db != LG_DB_OK) {
        uos_set_error("userdb");
        return SESS_ERR_DOWN;
    }
    if (sess.in) {
        sess.idx = uaf_find(&sess.uaf, sess.user);
    }
    return 0;
}

static void apply_cfg(void)
{
    const uos_settings_t *s = uos_settings();
    int fell = lg_lockout_apply(&sess.lk, s->login_retries, s->login_lockout_s);

    sess.idle_s = s->login_idle_s;
    if (fell) {
        warnings++;
        UOS_MARK("sesswarn why=lockout count=%u", warnings);
    }
    UOS_MARK("sesscfg result=%s retries=%u lockout_s=%u idle_s=%u", fell ? "fallback" : "ok",
             (unsigned)sess.lk.retries, (unsigned)(sess.lk.lockout_ms / 1000u), sess.idle_s);
}

static int sess_svc_init(void)
{
    char row0[UOS_LINE_MAX + 1];
    char row1[UOS_LINE_MAX + 1];

    sess.st = LG_S_OFF;
    sess.idx = -1;
    apply_cfg();
    load_db();
    if (sess.db == LG_DB_OK) {
        uos_first_set_next(&sess_login_screen);
    } else {
        if (sess.db == LG_DB_DAMAGED) {
            uos_set_error("userdb");
        }
        (void)lg_db_notice(sess.db, sess.why, 0, row0, sizeof(row0));
        (void)lg_db_notice(sess.db, sess.why, 1, row1, sizeof(row1));
        uos_first_set_notice(row0, row1, lg_db_reboot_only(sess.db));
    }
    return uos_maint_register("sess", cmd_sess, "the session: sess state|logout");
}

/* The later of the last key and the session start: the idle time counts
 * from whichever came last. */
static uint32_t idle_since(void)
{
    uint32_t key = uos_last_input();

    return (int32_t)(key - sess.start_tick) > 0 ? key : sess.start_tick;
}

static void sess_svc_tick(uint32_t now)
{
    /* An lk_ call on every tick: a lock that spans the 49.7-day wrap of
     * the millisecond count still ends on time. */
    (void)lk_locked(&sess.lk, now * (1000u / UOS_TICK_HZ));
    if (sess.in && lg_idle_expired(sess.idle_s, idle_since() * (1000u / UOS_TICK_HZ),
                                   now * (1000u / UOS_TICK_HZ))) {
        (void)sess_logout(SESS_END_IDLE);
    }
}

static void sess_svc_shutdown(int why)
{
    (void)why;
    /* Safe with IRQs off: memory only. */
    k_memset(&sess.uaf, 0, sizeof(sess.uaf));
}

const uos_service_t uos_svc_session = { "session", sess_svc_init, sess_svc_tick,
                                        sess_svc_shutdown };
