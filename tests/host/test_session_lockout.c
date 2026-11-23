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
    CHECK_EQ(lk_fail(&lk, 1000), 1);
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
    lk_success(&lk);
    CHECK_EQ(lk_tries_left(&lk), 3);
    CHECK_EQ(lk_fail(&lk, 3), 0);
    CHECK_EQ(lk_fail(&lk, 4), 0);
    CHECK_EQ(lk_fail(&lk, 5), 1);
}

TEST(one_retry)
{
    lk_t lk;

    lk_init(&lk, 1, 1);
    CHECK_EQ(lk_fail(&lk, 0), 1);
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
    CHECK_EQ(lk_fail(&lk, 5), 1);
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
    CHECK_EQ(lk_fail(&lk, start), 1);
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
    CHECK_EQ(lk_fail(&lk, 0), 1);
    CHECK_EQ(lk_remaining_s(&lk, 0), 2);
    CHECK_EQ(lk_locked(&lk, 2000), 0);
}
