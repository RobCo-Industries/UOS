#include "lockout.h"

#include "rc_test.h"

TEST(init_limits)
{
    lk_t lk;

    CHECK_EQ(lk_init(&lk, 0, 30), LK_E_RETRIES);
    CHECK_EQ(lk_init(&lk, 10, 30), LK_E_RETRIES);
    CHECK_EQ(lk_init(&lk, 1, 30), LK_OK);
    CHECK_EQ(lk_init(&lk, 9, 30), LK_OK);
    CHECK_EQ(lk_init(&lk, 3, 3601), LK_E_LOCKOUT);
    CHECK_EQ(lk_init(&lk, 3, 3600), LK_OK);
    CHECK_EQ(lk_init(&lk, 3, 0), LK_OK);
    CHECK_EQ(lk_tries_left(&lk), 3);
}

TEST(strerror_text)
{
    CHECK_STR(lk_strerror(LK_OK), "ok");
    CHECK_STR(lk_strerror(LK_E_RETRIES), "retries must be 1 to 9");
    CHECK_STR(lk_strerror(LK_E_LOCKOUT), "lockout must be 0 to 3600 seconds");
    CHECK_STR(lk_strerror(LK_E_NUM), "unknown lockout error");
}

TEST(locks_at_retries)
{
    lk_t lk;

    lk_init(&lk, LK_RETRIES_DEFAULT, LK_LOCKOUT_DEFAULT_S);
    CHECK_EQ(lk_fail(&lk, 100), 0);
    CHECK_EQ(lk_tries_left(&lk), 2);
    CHECK_EQ(lk_fail(&lk, 200), 0);
    CHECK_EQ(lk_locked(&lk, 300), 0);
    CHECK_EQ(lk_fail(&lk, 1000), LK_STARTED);
    CHECK_EQ(lk_tries_left(&lk), 0);
    CHECK_EQ(lk_locked(&lk, 1000), 1);
    CHECK_EQ(lk_remaining_s(&lk, 1000), 30);
    CHECK_EQ(lk_remaining_s(&lk, 1001), 30);
    CHECK_EQ(lk_remaining_s(&lk, 1999), 30);
    CHECK_EQ(lk_remaining_s(&lk, 2000), 29);
    CHECK_EQ(lk_remaining_s(&lk, 30999), 1);
    CHECK_EQ(lk_locked(&lk, 30999), 1);
    CHECK_EQ(lk_fail(&lk, 30000), 1);      /* ignored while locked */
    CHECK_EQ(lk_locked(&lk, 31000), 0);
    CHECK_EQ(lk_remaining_s(&lk, 31000), 0);
    CHECK_EQ(lk_tries_left(&lk), 3);       /* the count starts again */
    CHECK_EQ(lk_fail(&lk, 31001), 0);
}

TEST(success_resets)
{
    lk_t lk;

    lk_init(&lk, 3, 30);
    lk_fail(&lk, 1);
    lk_fail(&lk, 2);
    lk_session_start(&lk);
    CHECK_EQ(lk_tries_left(&lk), 3);
    CHECK_EQ(lk_fail(&lk, 3), 0);
    CHECK_EQ(lk_fail(&lk, 4), 0);
    CHECK_EQ(lk_fail(&lk, 5), LK_STARTED);
}

TEST(one_retry)
{
    lk_t lk;

    lk_init(&lk, 1, 1);
    CHECK_EQ(lk_fail(&lk, 0), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 0), 1);
    CHECK_EQ(lk_locked(&lk, 999), 1);
    CHECK_EQ(lk_locked(&lk, 1000), 0);
}

TEST(zero_lockout_never_locks)
{
    lk_t lk;
    int i;

    lk_init(&lk, 2, 0);
    for (i = 0; i < 10; i++) {
        CHECK_EQ(lk_fail(&lk, (uint32_t)i), 0);
    }
    CHECK_EQ(lk_remaining_s(&lk, 10), 0);
}

TEST(max_lockout)
{
    lk_t lk;

    lk_init(&lk, 9, LK_LOCKOUT_MAX_S);
    for (int i = 0; i < 8; i++) {
        CHECK_EQ(lk_fail(&lk, 5), 0);
    }
    CHECK_EQ(lk_fail(&lk, 5), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 5), 3600);
    CHECK_EQ(lk_locked(&lk, 3600004), 1);
    CHECK_EQ(lk_locked(&lk, 3600005), 0);
}

/* The tick counter wraps at 2^32 ms, about 49.7 days. */
TEST(across_wrap)
{
    lk_t lk;
    uint32_t start = 0xFFFFFFFFu - 4999u;   /* 5 s before the wrap */

    lk_init(&lk, 3, 30);
    lk_fail(&lk, start);
    lk_fail(&lk, start);
    CHECK_EQ(lk_fail(&lk, start), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 0xFFFFFFFFu), 26);
    CHECK_EQ(lk_remaining_s(&lk, 0), 25);
    CHECK_EQ(lk_locked(&lk, 24999), 1);
    CHECK_EQ(lk_remaining_s(&lk, 24999), 1);
    CHECK_EQ(lk_locked(&lk, 25000), 0);
}

TEST(lock_starts_at_zero_tick)
{
    lk_t lk;

    lk_init(&lk, 1, 2);
    CHECK_EQ(lk_fail(&lk, 0), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 0), 2);
    CHECK_EQ(lk_locked(&lk, 2000), 0);
}

/* The password game's lock: its own length, then the login's again. */
TEST(lock_for_seconds)
{
    lk_t lk;

    lk_init(&lk, 3, 30);
    CHECK_EQ(lk_fail(&lk, 0), 0);
    CHECK_EQ(lk_lock(&lk, 1000, 0), 0);         /* 0 s does not lock */
    CHECK_EQ(lk_lock(&lk, 1000, 60), LK_STARTED);
    CHECK_EQ(lk_tries_left(&lk), 0);
    CHECK_EQ(lk_remaining_s(&lk, 1000), 60);
    CHECK_EQ(lk_locked(&lk, 60999), 1);
    CHECK_EQ(lk_locked(&lk, 61000), 0);
    CHECK_EQ(lk_tries_left(&lk), 3);            /* the count starts again */
    /* A later failure lock uses the login's own length. */
    lk_fail(&lk, 70000);
    lk_fail(&lk, 70000);
    CHECK_EQ(lk_fail(&lk, 70000), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 70000), 30);
    /* A shorter game lock does not cut a running one; a longer one wins. */
    CHECK_EQ(lk_lock(&lk, 71000, 5), 1);
    CHECK_EQ(lk_remaining_s(&lk, 71000), 29);
    CHECK_EQ(lk_lock(&lk, 71000, 3601), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 71000), 3600);
    /* Across the wrap of the millisecond count. */
    lk_init(&lk, 3, 30);
    CHECK_EQ(lk_lock(&lk, 0xFFFFF000u, 10), LK_STARTED);
    CHECK_EQ(lk_locked(&lk, 0xFFFFF000u + 9999u), 1);
    CHECK_EQ(lk_locked(&lk, 0xFFFFF000u + 10000u), 0);
}

/* ---- The lockout table of lockout.h, one test per row ---- */

/* Two failures of three, the state the rows start from. */
static void two_fails(lk_t *lk)
{
    lk_init(lk, 3, 30);
    CHECK_EQ(lk_fail(lk, 0), 0);
    CHECK_EQ(lk_fail(lk, 0), 0);
    CHECK_EQ(lk_tries_left(lk), 1);
}

/* Wrong password, unknown user, a right password on a disabled account:
 * the service calls lk_fail for each. */
TEST(row_failures_count)
{
    lk_t lk;

    two_fails(&lk);
    CHECK_EQ(lk_fail(&lk, 0), LK_STARTED);
    CHECK_EQ(lk_remaining_s(&lk, 0), 30);
}

/* A right password and a won game both start a session: same call. */
TEST(row_session_start_clears)
{
    lk_t lk;
    int i;

    two_fails(&lk);
    lk_session_start(&lk);
    CHECK_EQ(lk_tries_left(&lk), 3);
    /* After logout it takes the configured number again. */
    for (i = 0; i < 2; i++) {
        CHECK_EQ(lk_fail(&lk, 1), 0);
    }
    CHECK_EQ(lk_fail(&lk, 1), LK_STARTED);
}

/* Game started, BACK, refused, logout, idle timeout: no lk_ call, so the
 * count stays as it was and the next failure takes it on. */
TEST(row_no_call_keeps_count)
{
    lk_t lk;

    two_fails(&lk);
    CHECK_EQ(lk_locked(&lk, 5000), 0);
    CHECK_EQ(lk_tries_left(&lk), 1);
    CHECK_EQ(lk_fail(&lk, 5000), LK_STARTED);
}

/* A lost game locks and clears the count; with 0 s nothing changes. */
TEST(row_game_lost)
{
    lk_t lk;

    two_fails(&lk);
    CHECK_EQ(lk_lock(&lk, 0, 0), 0);
    CHECK_EQ(lk_tries_left(&lk), 1);
    CHECK_EQ(lk_lock(&lk, 0, 60), LK_STARTED);
    CHECK_EQ(lk_tries_left(&lk), 0);
    CHECK_EQ(lk_locked(&lk, 60000), 0);
    CHECK_EQ(lk_tries_left(&lk), 3);
}

/* The lock runs out: failures from before it do not carry over. */
TEST(row_lock_ends_fresh_count)
{
    lk_t lk;

    two_fails(&lk);
    CHECK_EQ(lk_fail(&lk, 0), LK_STARTED);
    CHECK_EQ(lk_fail(&lk, 100), 1);             /* input while locked: ignored */
    CHECK_EQ(lk_remaining_s(&lk, 100), 30);
    CHECK_EQ(lk_locked(&lk, 30000), 0);
    CHECK_EQ(lk_tries_left(&lk), 3);
}

/* A reboot: lk_init again clears a lock and the count. */
TEST(row_reboot)
{
    lk_t lk;

    two_fails(&lk);
    CHECK_EQ(lk_fail(&lk, 0), LK_STARTED);
    CHECK_EQ(lk_init(&lk, 3, 30), LK_OK);
    CHECK_EQ(lk_locked(&lk, 0), 0);
    CHECK_EQ(lk_tries_left(&lk), 3);
}

/* Alternating password tries and game starts: a game start changes
 * nothing, so the third wrong password still locks. */
TEST(row_alternating_does_not_dodge)
{
    lk_t lk;

    lk_init(&lk, 3, 30);
    CHECK_EQ(lk_fail(&lk, 0), 0);
    /* game started, BACK */
    CHECK_EQ(lk_fail(&lk, 0), 0);
    /* game started, refused */
    CHECK_EQ(lk_fail(&lk, 0), LK_STARTED);
}

/* The caller records a lock's start tick only on LK_STARTED: a failure or a
 * game loss that finds a lock running keeps the running lock's start. */
TEST(started_tells_a_new_lock_from_a_running_one)
{
    lk_t lk;

    lk_init(&lk, 2, 30);
    CHECK_EQ(lk_fail(&lk, 100), 0);
    CHECK_EQ(lk_fail(&lk, 200), LK_STARTED);
    CHECK_EQ(lk_fail(&lk, 300), 1);              /* the running lock, not a new one */
    CHECK_EQ(lk.locked_at, 200);
    CHECK_EQ(lk_lock(&lk, 400, 5), 1);           /* shorter: the running lock stays */
    CHECK_EQ(lk.locked_at, 200);
    CHECK_EQ(lk_lock(&lk, 500, 60), LK_STARTED); /* longer: a new lock from 500 */
    CHECK_EQ(lk.locked_at, 500);
    CHECK_EQ(lk_locked(&lk, 60500), 0);
    CHECK_EQ(lk_lock(&lk, 70000, 1), LK_STARTED);
    CHECK_EQ(lk_lock(&lk, 70000, 0), 1);         /* 0 s keeps the running lock */
}
