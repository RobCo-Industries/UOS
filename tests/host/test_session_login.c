#include <string.h>

#include "login.h"
#include "shrules.h"

#include "rc_test.h"

static void make_db(uaf_db_t *db, uint32_t iterations)
{
    static const uint8_t s1[UAF_SALT_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static const uint8_t s2[UAF_SALT_SIZE] = { 9, 9, 9 };
    static const uint8_t s3[UAF_SALT_SIZE] = { 7 };

    CHECK_EQ(uaf_db_init(db, iterations), UAF_OK);
    CHECK_EQ(uaf_add(db, "ADMIN", "ADMIN", UAF_PRIV_SYSTEM, UAF_FLAG_NOHACK, s1,
                     "ADMIN-CHANGE-ME"), UAF_OK);
    CHECK_EQ(uaf_add(db, "GUEST", "GUEST", 0, 0, s2, "GUEST"), UAF_OK);
    CHECK_EQ(uaf_add(db, "OFF", "OFF", 0, UAF_FLAG_DISABLED, s3, "off-pass"), UAF_OK);
}

/* 1 when `n` bytes of `needle` appear in `n_hay` bytes at `hay`. */
static int contains(const void *hay, size_t n_hay, const char *needle, size_t n)
{
    const unsigned char *h = hay;
    size_t i;

    for (i = 0; i + n <= n_hay; i++) {
        if (memcmp(h + i, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Runs a whole stepped check and returns its result; *blocks gets the work. */
static uaf_err_t run(const uaf_db_t *db, const char *name, const char *pw, uint32_t budget,
                     int *index, uint32_t *blocks, uint32_t *steps)
{
    lg_check_t c;
    int r;
    uint32_t guard = 0;

    lg_check_begin(&c, db, name, pw);
    do {
        r = lg_check_step(&c, budget);
        CHECK(r >= 0);
        CHECK(++guard < 100000u);
    } while (r == 0);
    *blocks = c.blocks;
    *steps = c.steps;
    return lg_check_end(&c, index);
}

TEST(round_blocks)
{
    CHECK_EQ(lg_round_blocks(1), 1);
    CHECK_EQ(lg_round_blocks(7), 1);
    CHECK_EQ(lg_round_blocks(8), 2);
    CHECK_EQ(lg_round_blocks(32), 2);
    CHECK_EQ(lg_rounds_per_step(5, LG_STEP_BLOCKS), LG_STEP_BLOCKS);
    CHECK_EQ(lg_rounds_per_step(15, LG_STEP_BLOCKS), LG_STEP_BLOCKS / 2);
    CHECK_EQ(lg_rounds_per_step(15, 1), 1);
    CHECK_EQ(lg_rounds_per_step(15, 0), 1);
}

/* The stepped check gives what uaf_verify() gives, with the same work. */
TEST(same_as_uaf_verify)
{
    static const struct { const char *name, *pw; } cases[] = {
        { "ADMIN", "ADMIN-CHANGE-ME" }, { "admin", "ADMIN-CHANGE-ME" },
        { "ADMIN", "admin-change-me" }, { "GUEST", "GUEST" }, { "GUEST", "" },
        { "NOBODY", "GUEST" }, { "NOBODY", "ADMIN-CHANGE-ME" }, { "OFF", "off-pass" },
        { "OFF", "wrong" }, { "GUEST", "bad\tchar" }, { "", "GUEST" },
        { "GUEST", "123456789012345678901234567890123" },
    };
    uaf_db_t db;
    unsigned i;

    make_db(&db, 50);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uaf_work_t w = { 0, 0 };
        int want_idx = -2, got_idx = -2;
        uint32_t blocks, steps;
        uaf_err_t want = uaf_verify(&db, cases[i].name, cases[i].pw, &want_idx, &w);
        uaf_err_t got = run(&db, cases[i].name, cases[i].pw, 7, &got_idx, &blocks, &steps);

        CHECK_EQ(got, want);
        CHECK_EQ(got_idx, want_idx);
        CHECK_EQ(blocks, w.blocks);
    }
}

TEST(results)
{
    uaf_db_t db;
    int idx;
    uint32_t blocks, steps;

    make_db(&db, 20);
    CHECK_EQ(run(&db, "GUEST", "GUEST", LG_STEP_BLOCKS, &idx, &blocks, &steps), UAF_OK);
    CHECK_EQ(idx, 1);
    CHECK_EQ(run(&db, "GUEST", "guest", LG_STEP_BLOCKS, &idx, &blocks, &steps), UAF_E_BAD_LOGIN);
    CHECK_EQ(idx, -1);
    CHECK_EQ(run(&db, "OFF", "off-pass", LG_STEP_BLOCKS, &idx, &blocks, &steps), UAF_E_DISABLED);
    CHECK_EQ(idx, 2);
}

/* An unknown user and a known one do the same work, in the same steps. */
TEST(unknown_user_same_work)
{
    uaf_db_t db;
    int idx;
    uint32_t b1, b2, s1, s2;

    make_db(&db, UAF_ITER_DEFAULT);
    CHECK_EQ(run(&db, "ADMIN", "WRONG-PASSWORD!", LG_STEP_BLOCKS, &idx, &b1, &s1),
             UAF_E_BAD_LOGIN);
    CHECK_EQ(run(&db, "NOBODY", "WRONG-PASSWORD!", LG_STEP_BLOCKS, &idx, &b2, &s2),
             UAF_E_BAD_LOGIN);
    CHECK_EQ(b1, b2);
    CHECK_EQ(s1, s2);
    /* 2000 iterations of a long password: the worst case of session.md. */
    CHECK_EQ(b1, 3999);
    /* At most LG_STEP_BLOCKS compressions per step: 16 steps. */
    CHECK_EQ(s1, (3999 + LG_STEP_BLOCKS - 1) / LG_STEP_BLOCKS);
    CHECK_EQ(run(&db, "NOBODY", "x", LG_STEP_BLOCKS, &idx, &b2, &s2), UAF_E_BAD_LOGIN);
    CHECK_EQ(b2, 2000);
    CHECK_EQ(s2, 8);
}

/* No step does more than its budget of compressions. */
TEST(step_bound)
{
    uaf_db_t db;
    lg_check_t c;
    uint32_t before;
    int r;

    make_db(&db, UAF_ITER_DEFAULT);
    lg_check_begin(&c, &db, "ADMIN", "ADMIN-CHANGE-ME");
    do {
        before = c.blocks;
        r = lg_check_step(&c, LG_STEP_BLOCKS);
        CHECK(c.blocks - before <= LG_STEP_BLOCKS);
        CHECK(c.blocks - before >= 1);
    } while (r == 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(lg_check_step(&c, LG_STEP_BLOCKS), 1);
    CHECK_EQ(lg_check_end(&c, NULL), UAF_OK);
    CHECK_EQ(lg_check_step(&c, LG_STEP_BLOCKS), -1);
}

/* The password copy and the hashes are gone after the end and after a wipe. */
TEST(wipes)
{
    uaf_db_t db;
    lg_check_t c;
    lg_check_t zero;
    char pw[] = "ADMIN-CHANGE-ME";

    memset(&zero, 0, sizeof(zero));
    make_db(&db, 10);
    lg_check_begin(&c, &db, "ADMIN", pw);
    memset(pw, 0, sizeof(pw));                  /* the caller may wipe at once */
    CHECK(contains(&c, sizeof(c), "ADMIN-CHANGE-ME", 15));
    while (lg_check_step(&c, 3) == 0) {
    }
    CHECK_EQ(lg_check_end(&c, NULL), UAF_OK);
    CHECK_MEM(&c, &zero, sizeof(c));
    lg_check_begin(&c, &db, "ADMIN", "secret-pw");
    lg_check_step(&c, 3);
    lg_check_wipe(&c);
    CHECK_MEM(&c, &zero, sizeof(c));
}

/* An unfinished check never passes. */
TEST(unfinished_is_bad)
{
    uaf_db_t db;
    lg_check_t c;
    int idx = 5;

    make_db(&db, 100);
    lg_check_begin(&c, &db, "GUEST", "GUEST");
    lg_check_step(&c, 1);
    CHECK_EQ(lg_check_end(&c, &idx), UAF_E_BAD_LOGIN);
    CHECK_EQ(idx, -1);
}

TEST(state_machine)
{
    lg_step_t r;

    r = lg_next(LG_S_OFF, LG_E_START);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.changed, 1);
    CHECK_EQ(lg_next(LG_S_OFF, LG_E_START_LOCKED).next, LG_S_LOCKED);
    CHECK_EQ(lg_next(LG_S_USER, LG_E_USER_OK).next, LG_S_PASS);
    r = lg_next(LG_S_USER, LG_E_USER_EMPTY);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.msg, LG_M_NONE);
    r = lg_next(LG_S_USER, LG_E_USER_CANCEL);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.msg, LG_M_NONE);
    CHECK_EQ(lg_next(LG_S_PASS, LG_E_PASS_OK).next, LG_S_CHECK);
    r = lg_next(LG_S_PASS, LG_E_PASS_CANCEL);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.msg, LG_M_CANCELLED);
    CHECK_EQ(lg_next(LG_S_CHECK, LG_E_GOOD).next, LG_S_WELCOME);
    r = lg_next(LG_S_CHECK, LG_E_BAD);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.msg, LG_M_DENIED);
    CHECK_EQ(lg_next(LG_S_CHECK, LG_E_BAD_LOCKED).next, LG_S_LOCKED);
    r = lg_next(LG_S_LOCKED, LG_E_LOCK_END);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.msg, LG_M_UNLOCKED);
    CHECK_EQ(lg_next(LG_S_WELCOME, LG_E_WELCOME_DONE).next, LG_S_SESSION);
    r = lg_next(LG_S_SESSION, LG_E_LOGOUT);
    CHECK_EQ(r.next, LG_S_USER);
    CHECK_EQ(r.msg, LG_M_LOGGED_OUT);
    CHECK_EQ(lg_next(LG_S_WELCOME, LG_E_LOGOUT).next, LG_S_USER);
}

/* Events that do not apply change nothing: input while locked or during
 * the check, a logout with no session, a second start. */
TEST(state_machine_ignores)
{
    static const lg_event_t locked_ignores[] = {
        LG_E_START, LG_E_USER_OK, LG_E_USER_EMPTY, LG_E_PASS_OK, LG_E_GOOD, LG_E_BAD,
        LG_E_LOGOUT, LG_E_WELCOME_DONE,
    };
    unsigned i;
    int s, e, used = 0;

    for (i = 0; i < sizeof(locked_ignores) / sizeof(locked_ignores[0]); i++) {
        lg_step_t r = lg_next(LG_S_LOCKED, locked_ignores[i]);

        CHECK_EQ(r.next, LG_S_LOCKED);
        CHECK_EQ(r.changed, 0);
    }
    CHECK_EQ(lg_next(LG_S_CHECK, LG_E_USER_OK).changed, 0);
    CHECK_EQ(lg_next(LG_S_CHECK, LG_E_PASS_CANCEL).changed, 0);
    CHECK_EQ(lg_next(LG_S_USER, LG_E_LOGOUT).changed, 0);
    CHECK_EQ(lg_next(LG_S_USER, LG_E_GOOD).changed, 0);
    CHECK_EQ(lg_next(LG_S_SESSION, LG_E_START).changed, 0);
    CHECK_EQ(lg_next(LG_S_NUM, LG_E_START).changed, 0);
    /* A direct session: from the prompts and the lockout, never during a
     * check or a session. */
    CHECK_EQ(lg_next(LG_S_OFF, LG_E_DIRECT).next, LG_S_WELCOME);
    CHECK_EQ(lg_next(LG_S_USER, LG_E_DIRECT).next, LG_S_WELCOME);
    CHECK_EQ(lg_next(LG_S_PASS, LG_E_DIRECT).next, LG_S_WELCOME);
    CHECK_EQ(lg_next(LG_S_LOCKED, LG_E_DIRECT).next, LG_S_WELCOME);
    CHECK_EQ(lg_next(LG_S_CHECK, LG_E_DIRECT).changed, 0);
    CHECK_EQ(lg_next(LG_S_SESSION, LG_E_DIRECT).changed, 0);
    CHECK_EQ(lg_next(LG_S_WELCOME, LG_E_DIRECT).changed, 0);
    /* Otherwise only a passed check reaches the welcome. */
    for (s = 0; s < LG_S_NUM; s++) {
        for (e = 0; e < LG_E_NUM; e++) {
            lg_step_t r = lg_next((lg_state_t)s, (lg_event_t)e);

            if (r.next == LG_S_WELCOME && r.changed && e != LG_E_DIRECT) {
                CHECK_EQ(s, LG_S_CHECK);
                CHECK_EQ(e, LG_E_GOOD);
                used++;
            }
        }
    }
    CHECK_EQ(used, 1);
}

TEST(names_and_messages)
{
    char buf[80];

    CHECK_STR(lg_state_name(LG_S_USER), "user");
    CHECK_STR(lg_state_name(LG_S_SESSION), "session");
    CHECK_STR(lg_state_name(LG_S_NUM), "unknown");
    CHECK_STR(lg_msg_text(LG_M_NONE), "");
    CHECK_STR(lg_msg_text(LG_M_DENIED), "ACCESS DENIED.");
    CHECK_EQ(lg_denied_line(buf, sizeof(buf), 2), 31);
    CHECK_STR(buf, "ACCESS DENIED. 2 ATTEMPTS LEFT.");
    lg_denied_line(buf, sizeof(buf), 1);
    CHECK_STR(buf, "ACCESS DENIED. 1 ATTEMPT LEFT.");
    CHECK_EQ(lg_denied_line(buf, 10, 1), -1);
    CHECK_STR(buf, "");
    lg_locked_line(buf, sizeof(buf), 30);
    CHECK_STR(buf, "TRY AGAIN IN 30 SECONDS.");
    lg_locked_line(buf, sizeof(buf), 1);
    CHECK_STR(buf, "TRY AGAIN IN 1 SECOND.");
}

TEST(lockout_values)
{
    lk_t lk;

    CHECK_EQ(lg_lockout_apply(&lk, 5, 10), 0);
    CHECK_EQ(lk.retries, 5);
    CHECK_EQ(lk.lockout_ms, 10000);
    CHECK_EQ(lg_lockout_apply(&lk, 0, 10), 1);
    CHECK_EQ(lk.retries, 3);
    CHECK_EQ(lk.lockout_ms, 30000);
    CHECK_EQ(lg_lockout_apply(&lk, 4, 3601), 1);
    CHECK_EQ(lk.retries, 3);
    CHECK_EQ(lk.lockout_ms, 30000);
    CHECK_EQ(lg_lockout_apply(&lk, 1, 0), 0);
}

TEST(idle_rule)
{
    CHECK_EQ(lg_idle_expired(0, 0, 0xFFFFFFFFu), 0);
    CHECK_EQ(lg_idle_expired(5, 1000, 5999), 0);
    CHECK_EQ(lg_idle_expired(5, 1000, 6000), 1);
    /* Across the wrap of the millisecond count. */
    CHECK_EQ(lg_idle_expired(2, 0xFFFFFC18u, 999), 0);
    CHECK_EQ(lg_idle_expired(2, 0xFFFFFC18u, 1000), 1);
    /* A very long idle time does not wrap to a short one. */
    CHECK_EQ(lg_idle_expired(4294968u, 0, 0xFFFFFFFFu), 0);
}

TEST(db_notices)
{
    char buf[80];
    int i;

    CHECK_STR(lg_db_name(LG_DB_DAMAGED), "damaged");
    CHECK_STR(lg_db_name(LG_DB_NUM), "unknown");
    CHECK_EQ(lg_db_reboot_only(LG_DB_OK), 0);
    for (i = LG_DB_NOCARD; i < LG_DB_NUM; i++) {
        CHECK_EQ(lg_db_reboot_only((lg_db_t)i), 1);
        CHECK(lg_db_notice((lg_db_t)i, "CRC", 0, buf, sizeof(buf)) > 0);
        CHECK(lg_db_notice((lg_db_t)i, "CRC", 1, buf, sizeof(buf)) > 0);
        CHECK(strstr(buf, "F4") != NULL);
    }
    lg_db_notice(LG_DB_DAMAGED, "CRC", 0, buf, sizeof(buf));
    CHECK_STR(buf, "USER DATABASE DAMAGED (CRC). NO ONE CAN LOG IN.");
    lg_db_notice(LG_DB_DAMAGED, NULL, 0, buf, sizeof(buf));
    CHECK_STR(buf, "USER DATABASE DAMAGED (UNKNOWN). NO ONE CAN LOG IN.");
    lg_db_notice(LG_DB_MISSING, NULL, 1, buf, sizeof(buf));
    CHECK_STR(buf, "RESTORE /UOS/SYSUAF.DAT FROM A BACKUP, THEN PRESS F4 TO RESTART.");
    CHECK_EQ(lg_db_notice(LG_DB_OK, NULL, 0, buf, sizeof(buf)), -1);
    CHECK_EQ(lg_db_notice(LG_DB_MISSING, NULL, 2, buf, sizeof(buf)), -1);
    /* Every notice fits the 79 columns of the screen. */
    for (i = LG_DB_NOCARD; i < LG_DB_NUM; i++) {
        CHECK(lg_db_notice((lg_db_t)i, "RECORD", 0, buf, sizeof(buf)) <= 79);
        CHECK(lg_db_notice((lg_db_t)i, "RECORD", 1, buf, sizeof(buf)) <= 79);
    }
}

TEST(welcome_and_record_text)
{
    uaf_db_t db;
    char name[UAF_NAME_MAX + 1];
    char home[UAF_HOME_MAX + 1];
    char buf[80];
    static const uint8_t salt[UAF_SALT_SIZE] = { 0 };

    make_db(&db, 1);
    CHECK_EQ(uaf_add(&db, "ABCDEFGHIJKLMNOP", "ABCDEFGH", 0, 0, salt, "x"), UAF_OK);
    lg_rec_name(&db.rec[3], name);
    CHECK_STR(name, "ABCDEFGHIJKLMNOP");
    lg_rec_home(&db.rec[3], home);
    CHECK_STR(home, "ABCDEFGH");
    lg_rec_name(&db.rec[0], name);
    CHECK_STR(name, "ADMIN");
    lg_welcome_line("ADMIN", UAF_PRIV_SYSTEM | UAF_PRIV_OPER, 0, buf, sizeof(buf));
    CHECK_STR(buf, "WELCOME, ADMIN.");
    lg_welcome_line("ADMIN", UAF_PRIV_SYSTEM | UAF_PRIV_OPER, 1, buf, sizeof(buf));
    CHECK_STR(buf, "PRIVILEGES: SYSTEM,OPER");
    lg_welcome_line("GUEST", 0, 1, buf, sizeof(buf));
    CHECK_STR(buf, "PRIVILEGES: NONE");
    CHECK_EQ(lg_welcome_line("GUEST", 0, LG_WELCOME_ROWS, buf, sizeof(buf)), -1);
}

TEST(header_rows)
{
    char buf[80];
    unsigned i;

    lg_header_line("TEST TERMINAL", 1, buf, sizeof(buf));
    CHECK_STR(buf, "TERMINAL: TEST TERMINAL");
    for (i = 0; i < LG_HEADER_ROWS; i++) {
        int n = lg_header_line("ROBCO TERMINAL 0123456789012345", i, buf, sizeof(buf));

        CHECK(n >= 0 && n <= 79);
    }
    CHECK_EQ(lg_header_line("X", LG_HEADER_ROWS, buf, sizeof(buf)), -1);
}

TEST(notice_line)
{
    char buf[80];

    CHECK_EQ(lg_notice_line(0, 0, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "");
    lg_notice_line(1, 0, buf, sizeof(buf));
    CHECK_STR(buf, "SETTINGS: SOME VALUES IGNORED");
    lg_notice_line(0, 1, buf, sizeof(buf));
    CHECK_STR(buf, "DEVICES: SOME DEVICES REFUSED");
    CHECK(lg_notice_line(1, 1, buf, sizeof(buf)) <= 79);
    CHECK_STR(buf, "SETTINGS: SOME VALUES IGNORED. DEVICES: SOME DEVICES REFUSED");
    CHECK_EQ(lg_notice_line(1, 1, buf, 10), -1);
    CHECK_EQ(lg_notice_line(1, 0, NULL, 10), -1);
}

TEST(name_word)
{
    char w[UAF_NAME_MAX + 1];

    lg_name_word("guest", w);
    CHECK_STR(w, "GUEST");
    lg_name_word("", w);
    CHECK_STR(w, "-");
    lg_name_word("a b=c$_9", w);
    CHECK_STR(w, "A?B?C$_9");
    lg_name_word("ABCDEFGHIJKLMNOPQRST", w);
    CHECK_STR(w, "ABCDEFGHIJKLMNOP");
}

TEST(uaf_words)
{
    CHECK_STR(lg_uaf_word(UAF_E_CRC), "CRC");
    CHECK_STR(lg_uaf_word(UAF_E_NAME_PAD), "NAME");
    CHECK_STR(lg_uaf_word(UAF_E_SHORT), "SHORT");
    CHECK_STR(lg_uaf_word(UAF_OK), "UNKNOWN");
}

/* The lost password game locks from the user name prompt only. */
TEST(hack_lock_event)
{
    int s;

    CHECK_EQ(lg_next(LG_S_USER, LG_E_HACK_LOCKED).next, LG_S_LOCKED);
    CHECK_EQ(lg_next(LG_S_USER, LG_E_HACK_LOCKED).msg, LG_M_NONE);
    for (s = 0; s < LG_S_NUM; s++) {
        if (s != LG_S_USER) {
            CHECK_EQ(lg_next((lg_state_t)s, LG_E_HACK_LOCKED).changed, 0);
        }
    }
}

TEST(hack_allowed_rules)
{
    uaf_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_USER, 0), LG_HACK_OK);
    CHECK_EQ(lg_hack_allowed(0, 1, &rec, LG_S_USER, 0), LG_HACK_OFF);
    CHECK_EQ(lg_hack_allowed(1, 0, &rec, LG_S_USER, 0), LG_HACK_NODB);
    CHECK_EQ(lg_hack_allowed(1, 1, NULL, LG_S_USER, 0), LG_HACK_NOUSER);
    rec.flags = UAF_FLAG_DISABLED;
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_USER, 0), LG_HACK_DISABLED);
    rec.flags = UAF_FLAG_NOHACK;
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_USER, 0), LG_HACK_NOHACK);
    rec.flags = 0;
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_USER, 1), LG_HACK_LOCKED);
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_LOCKED, 0), LG_HACK_LOCKED);
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_PASS, 0), LG_HACK_BUSY);
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_CHECK, 0), LG_HACK_BUSY);
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_SESSION, 0), LG_HACK_BUSY);
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_OFF, 0), LG_HACK_BUSY);
    /* The first failing rule wins: off before everything. */
    rec.flags = UAF_FLAG_NOHACK;
    CHECK_EQ(lg_hack_allowed(0, 0, NULL, LG_S_SESSION, 1), LG_HACK_OFF);
    CHECK_EQ(lg_hack_allowed(1, 1, &rec, LG_S_SESSION, 1), LG_HACK_NOHACK);
    CHECK_STR(lg_hack_name(LG_HACK_NOHACK), "nohack");
    CHECK_STR(lg_hack_name(LG_HACK_NUM), "unknown");
    CHECK_STR(lg_hack_text(LG_HACK_OK), "");
    CHECK(strlen(lg_hack_text(LG_HACK_BUSY)) < 79);
}

/* The game plays only over the login prompt; a refusal from the rules wins,
 * so a session gives "busy", not "covered". */
TEST(hack_where_rules)
{
    CHECK_EQ(lg_hack_where(LG_HACK_OK, 1), LG_HACK_OK);
    CHECK_EQ(lg_hack_where(LG_HACK_OK, 0), LG_HACK_COVERED);
    CHECK_EQ(lg_hack_where(LG_HACK_BUSY, 0), LG_HACK_BUSY);
    CHECK_EQ(lg_hack_where(LG_HACK_BUSY, 1), LG_HACK_BUSY);
    CHECK_EQ(lg_hack_where(LG_HACK_LOCKED, 0), LG_HACK_LOCKED);
    CHECK_STR(lg_hack_name(LG_HACK_COVERED), "covered");
    CHECK(strlen(lg_hack_text(LG_HACK_COVERED)) > 0);
    CHECK(strlen(lg_hack_text(LG_HACK_COVERED)) < 79);
}

/* The session serial: a posted program call keeps it and runs only while
 * it still matches. Each login and each logout gives a new, non-zero one,
 * so a logout, LOGIN <user> and the same user's next login all differ. */
TEST(session_serial)
{
    uint32_t out = 0, in1, out1, in2;

    in1 = lg_serial_next(out);
    out1 = lg_serial_next(in1);
    in2 = lg_serial_next(out1);
    CHECK(in1 != 0);
    CHECK(in1 != in2);
    CHECK_EQ(lg_serial_next(0xffffffffu), 1u);   /* 0 stays "no session" */
    CHECK_EQ(sh_tag_live(1, in1, in1), 1);        /* the same session */
    CHECK_EQ(sh_tag_live(0, out1, in1), 0);       /* logged out */
    CHECK_EQ(sh_tag_live(1, in2, in1), 0);        /* a new login */
    CHECK_EQ(sh_tag_live(1, 0, 0), 0);            /* posted with no session */
}
