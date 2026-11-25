/* Host tests of src/kernel/hwdog.c: when the hardware watchdog starts,
 * when the loop kicks it, and what a shutdown does with it. */

#include "rc_test.h"

#include "uos_hwdog.h"
#include "uos_service.h"

TEST(plan_off_unless_enabled_and_in_range)
{
    CHECK_EQ(uos_hwdog_plan(0, 8000), 0u);
    CHECK_EQ(uos_hwdog_plan(1, 8000), 8000u);
    CHECK_EQ(uos_hwdog_plan(1, 1000), 1000u);
    CHECK_EQ(uos_hwdog_plan(1, 15000), 15000u);
    CHECK_EQ(uos_hwdog_plan(1, 999), 0u);
    CHECK_EQ(uos_hwdog_plan(1, 15001), 0u);
    CHECK_EQ(uos_hwdog_plan(1, 0), 0u);
}

TEST(never_kicked_before_a_start)
{
    uos_hwdog_t w = { 0 };

    CHECK_EQ(uos_hwdog_kick_due(&w, 1000, 1), 0);
    CHECK_EQ(uos_hwdog_kick_due(NULL, 1000, 1), 0);
}

TEST(kicks_every_quarter_of_the_timeout)
{
    uos_hwdog_t w;

    /* 8000 ms at 100 Hz: 800 ticks, a kick every 200. */
    uos_hwdog_started(&w, 8000, 50, 100);
    CHECK_EQ(w.period, 200u);
    CHECK_EQ(uos_hwdog_kick_due(&w, 50, 1), 0);
    CHECK_EQ(uos_hwdog_kick_due(&w, 249, 1), 0);
    CHECK_EQ(uos_hwdog_kick_due(&w, 250, 1), 1);
    uos_hwdog_kicked(&w, 250);
    CHECK_EQ(uos_hwdog_kick_due(&w, 251, 1), 0);
    CHECK_EQ(uos_hwdog_kick_due(&w, 450, 1), 1);
    /* A short timeout at a slow tick still kicks every tick. */
    uos_hwdog_started(&w, 1000, 0, 2);
    CHECK_EQ(w.period, 1u);
    CHECK_EQ(uos_hwdog_kick_due(&w, 1, 1), 1);
}

TEST(kick_across_the_tick_wrap)
{
    uos_hwdog_t w;

    uos_hwdog_started(&w, 8000, 0xFFFFFFC0u, 100);
    CHECK_EQ(uos_hwdog_kick_due(&w, 0x00000040u, 1), 0);
    CHECK_EQ(uos_hwdog_kick_due(&w, 0x00000090u, 1), 1);
}

/* The loop proves itself alive by calling from its tick; a test hang or a
 * stopped loop passes loop_alive 0 and the board resets. */
TEST(no_kick_from_a_dead_loop)
{
    uos_hwdog_t w;

    uos_hwdog_started(&w, 8000, 0, 100);
    CHECK_EQ(uos_hwdog_kick_due(&w, 1000, 0), 0);
    CHECK_EQ(uos_hwdog_kick_due(&w, 1000, 1), 1);
}

TEST(shutdown_holds_kicks_halt_stops)
{
    uos_hwdog_t w;

    uos_hwdog_started(&w, 8000, 0, 100);
    CHECK_EQ(uos_hwdog_shutdown(&w, UOS_SHUTDOWN_PANIC), UOS_HWDOG_LEAVE);
    CHECK_EQ(uos_hwdog_kick_due(&w, 100000, 1), 0);
    uos_hwdog_started(&w, 8000, 0, 100);
    CHECK_EQ(uos_hwdog_shutdown(&w, UOS_SHUTDOWN_HALT), UOS_HWDOG_STOP);
    CHECK_EQ(uos_hwdog_kick_due(&w, 100000, 1), 0);
    uos_hwdog_started(&w, 8000, 0, 100);
    CHECK_EQ(uos_hwdog_shutdown(&w, UOS_SHUTDOWN_REBOOT), UOS_HWDOG_LEAVE);
    CHECK_EQ(uos_hwdog_kick_due(&w, 100000, 1), 0);
    CHECK_EQ(uos_hwdog_shutdown(NULL, UOS_SHUTDOWN_HALT), UOS_HWDOG_STOP);
}
