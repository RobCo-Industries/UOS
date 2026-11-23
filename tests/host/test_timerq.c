/* Host tests of src/kernel/timerq.c and src/kernel/event.c. */

#include "rc_test.h"

#include "uos_event.h"
#include "uos_timer.h"

static int calls;

static void cb(void *arg)
{
    (void)arg;
    calls++;
}

/* Runs expire then claim, as the loop does. Returns the timers delivered. */
static unsigned run(uos_timerq_t *tq, uint32_t now)
{
    uos_timer_id_t ids[UOS_TIMER_MAX];
    unsigned i, n = uos_tq_expire(tq, now, ids, UOS_TIMER_MAX), got = 0;

    for (i = 0; i < n; i++) {
        uos_call_fn fn = NULL;
        void *arg = NULL;

        if (uos_tq_claim(tq, ids[i], &fn, &arg)) {
            got++;
        }
    }
    return got;
}

TEST(oneshot_fires_once_at_its_tick)
{
    uos_timerq_t tq;
    uos_timer_id_t id = 0;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 100, 5, 0, cb, NULL, &id), 0);
    CHECK(id != UOS_TIMER_NONE);
    CHECK_EQ(run(&tq, 104), 0u);
    CHECK_EQ(run(&tq, 105), 1u);
    CHECK_EQ(run(&tq, 106), 0u);
    CHECK_EQ(run(&tq, 1000), 0u);
    CHECK_EQ(uos_tq_live(&tq), 0u);
    /* Claimed: the id is spent. */
    CHECK_EQ(uos_tq_cancel(&tq, id), UOS_TIMER_ERR_ID);
}

TEST(zero_delay_fires_at_next_expire)
{
    uos_timerq_t tq;
    uos_timer_id_t id;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 7, 0, 0, NULL, NULL, &id), 0);
    CHECK_EQ(run(&tq, 7), 1u);
}

TEST(periodic_fires_every_period)
{
    uos_timerq_t tq;
    uos_timer_id_t id;
    uint32_t t;
    unsigned n = 0;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 10, 10, cb, NULL, &id), 0);
    for (t = 1; t <= 100; t++) {
        n += run(&tq, t);
    }
    CHECK_EQ(n, 10u);
    CHECK_EQ(uos_tq_live(&tq), 1u);
    CHECK_EQ(uos_tq_cancel(&tq, id), 0);
    for (; t <= 200; t++) {
        n += run(&tq, t);
    }
    CHECK_EQ(n, 10u);
}

TEST(periodic_catches_up_one_per_call)
{
    uos_timerq_t tq;
    uos_timer_id_t id;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 10, 10, NULL, NULL, &id), 0);
    /* Late by three periods: one expiry per call until it is back in step. */
    CHECK_EQ(run(&tq, 35), 1u);
    CHECK_EQ(run(&tq, 35), 1u);
    CHECK_EQ(run(&tq, 35), 1u);
    CHECK_EQ(run(&tq, 35), 0u);
    CHECK_EQ(run(&tq, 40), 1u);
}

TEST(wrap_of_the_tick_counter)
{
    uos_timerq_t tq;
    uos_timer_id_t id;
    uint32_t left = 0;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0xfffffffau, 10, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_next(&tq, 0xfffffffau, &left), 1);
    CHECK_EQ(left, 10u);
    CHECK_EQ(run(&tq, 0xffffffffu), 0u);
    CHECK_EQ(run(&tq, 3), 0u);
    CHECK_EQ(uos_tq_next(&tq, 3, &left), 1);
    CHECK_EQ(left, 1u);
    CHECK_EQ(run(&tq, 4), 1u);
}

TEST(periodic_across_the_wrap)
{
    uos_timerq_t tq;
    uos_timer_id_t id;
    uint32_t t = 0xffffff00u;
    unsigned i, n = 0;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, t, 16, 16, NULL, NULL, &id), 0);
    for (i = 0; i < 0x200; i++) {
        n += run(&tq, t + i + 1);
    }
    CHECK_EQ(n, 0x200u / 16u);
}

TEST(cancel_before_due_never_fires)
{
    uos_timerq_t tq;
    uos_timer_id_t id;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 5, 0, cb, NULL, &id), 0);
    CHECK_EQ(uos_tq_cancel(&tq, id), 0);
    CHECK_EQ(run(&tq, 5), 0u);
    CHECK_EQ(run(&tq, 500), 0u);
    CHECK_EQ(uos_tq_cancel(&tq, id), UOS_TIMER_ERR_ID);
}

TEST(cancel_after_expiry_is_queued_never_fires)
{
    uos_timerq_t tq;
    uos_timer_id_t id, due[UOS_TIMER_MAX];

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 5, 0, cb, NULL, &id), 0);
    CHECK_EQ(uos_tq_expire(&tq, 5, due, UOS_TIMER_MAX), 1u);
    CHECK_EQ(due[0], id);
    /* The expiry waits in the queue; the timer is cancelled meanwhile. */
    CHECK_EQ(uos_tq_cancel(&tq, id), 0);
    CHECK_EQ(uos_tq_claim(&tq, due[0], NULL, NULL), 0);
}

TEST(cancelled_periodic_with_queued_expiry)
{
    uos_timerq_t tq;
    uos_timer_id_t id, due[UOS_TIMER_MAX];

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 2, 2, cb, NULL, &id), 0);
    CHECK_EQ(uos_tq_expire(&tq, 2, due, UOS_TIMER_MAX), 1u);
    CHECK_EQ(uos_tq_cancel(&tq, id), 0);
    CHECK_EQ(uos_tq_claim(&tq, due[0], NULL, NULL), 0);
    CHECK_EQ(run(&tq, 10), 0u);
}

TEST(reused_slot_does_not_revive_an_old_id)
{
    uos_timerq_t tq;
    uos_timer_id_t old, fresh;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 5, 0, NULL, NULL, &old), 0);
    CHECK_EQ(uos_tq_cancel(&tq, old), 0);
    CHECK_EQ(uos_tq_start(&tq, 0, 5, 0, NULL, NULL, &fresh), 0);
    CHECK(fresh != old);
    CHECK_EQ(uos_tq_cancel(&tq, old), UOS_TIMER_ERR_ID);
    CHECK_EQ(uos_tq_claim(&tq, old, NULL, NULL), 0);
    CHECK_EQ(uos_tq_live(&tq), 1u);
}

TEST(claim_returns_callback_and_arg)
{
    uos_timerq_t tq;
    uos_timer_id_t id, due[UOS_TIMER_MAX];
    uos_call_fn fn = NULL;
    void *arg = NULL;
    int x;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 1, 0, cb, &x, &id), 0);
    CHECK_EQ(uos_tq_expire(&tq, 1, due, UOS_TIMER_MAX), 1u);
    CHECK_EQ(uos_tq_claim(&tq, due[0], &fn, &arg), 1);
    CHECK(fn == cb);
    CHECK(arg == &x);
}

TEST(table_full_at_the_limit)
{
    uos_timerq_t tq;
    uos_timer_id_t id;
    unsigned i;

    uos_tq_init(&tq);
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        CHECK_EQ(uos_tq_start(&tq, 0, 10, 0, NULL, NULL, &id), 0);
    }
    CHECK_EQ(uos_tq_start(&tq, 0, 10, 0, NULL, NULL, &id), UOS_TIMER_ERR_FULL);
    CHECK_EQ(uos_tq_live(&tq), (unsigned)UOS_TIMER_MAX);
}

TEST(expire_respects_max)
{
    uos_timerq_t tq;
    uos_timer_id_t id, due[2];

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, 1, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_start(&tq, 0, 1, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_start(&tq, 0, 1, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_expire(&tq, 1, due, 2), 2u);
    CHECK_EQ(uos_tq_expire(&tq, 1, due, 2), 1u);
    CHECK_EQ(uos_tq_expire(&tq, 1, due, 2), 0u);
    CHECK_EQ(tq.expired, 3u);
}

TEST(limits_and_bad_arguments)
{
    uos_timerq_t tq;
    uos_timer_id_t id;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_start(&tq, 0, UOS_TIMER_MAX_TICKS, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_start(&tq, 0, UOS_TIMER_MAX_TICKS + 1u, 0, NULL, NULL, &id),
             UOS_TIMER_ERR_ARG);
    CHECK_EQ(uos_tq_start(&tq, 0, 1, UOS_TIMER_MAX_TICKS + 1u, NULL, NULL, &id),
             UOS_TIMER_ERR_ARG);
    CHECK_EQ(uos_tq_start(&tq, 0, 1, 0, NULL, NULL, NULL), UOS_TIMER_ERR_ARG);
    CHECK_EQ(uos_tq_start(NULL, 0, 1, 0, NULL, NULL, &id), UOS_TIMER_ERR_ARG);
    CHECK_EQ(uos_tq_cancel(&tq, UOS_TIMER_NONE), UOS_TIMER_ERR_ID);
    CHECK_EQ(uos_tq_cancel(&tq, 0xffffffffu), UOS_TIMER_ERR_ID);
    CHECK_EQ(uos_tq_cancel(NULL, id), UOS_TIMER_ERR_ID);
    CHECK_EQ(uos_tq_claim(NULL, id, NULL, NULL), 0);
    CHECK_EQ(uos_tq_expire(NULL, 0, &id, 1), 0u);
    CHECK_EQ(uos_tq_live(NULL), 0u);
}

TEST(next_deadline)
{
    uos_timerq_t tq;
    uos_timer_id_t id;
    uint32_t left = 99;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_next(&tq, 0, &left), 0);
    CHECK_EQ(left, 99u);
    CHECK_EQ(uos_tq_start(&tq, 0, 30, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_start(&tq, 0, 20, 0, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_next(&tq, 5, &left), 1);
    CHECK_EQ(left, 15u);
    CHECK_EQ(uos_tq_next(&tq, 25, &left), 1);
    CHECK_EQ(left, 0u);
}

TEST(slot_listing)
{
    uos_timerq_t tq;
    uos_timer_id_t id, got;
    uint32_t due, period;

    uos_tq_init(&tq);
    CHECK_EQ(uos_tq_slot(&tq, 0, 0, &got, &due, &period), 0);
    CHECK_EQ(uos_tq_start(&tq, 10, 50, 25, NULL, NULL, &id), 0);
    CHECK_EQ(uos_tq_slot(&tq, 0, 20, &got, &due, &period), 1);
    CHECK_EQ(got, id);
    CHECK_EQ(due, 40u);
    CHECK_EQ(period, 25u);
    CHECK_EQ(uos_tq_slot(&tq, UOS_TIMER_MAX, 0, &got, &due, &period), 0);
}

TEST(event_queue_order_and_limit)
{
    uos_evq_t q;
    uos_event_t ev = { 0 }, out;
    unsigned i;

    uos_evq_init(&q);
    CHECK_EQ(uos_evq_pop(&q, &out), 0);
    for (i = 0; i < UOS_EVQ_LEN; i++) {
        ev.code = (uint16_t)i;
        CHECK_EQ(uos_evq_push(&q, &ev), 0);
    }
    CHECK_EQ(uos_evq_count(&q), UOS_EVQ_LEN);
    CHECK_EQ(uos_evq_push(&q, &ev), UOS_EVQ_ERR_FULL);
    CHECK_EQ(q.dropped, 1u);
    for (i = 0; i < UOS_EVQ_LEN; i++) {
        CHECK_EQ(uos_evq_pop(&q, &out), 1);
        CHECK_EQ(out.code, i);
    }
    CHECK_EQ(uos_evq_pop(&q, &out), 0);
    CHECK_EQ(uos_evq_push(NULL, &ev), UOS_EVQ_ERR_ARG);
    CHECK_EQ(uos_evq_push(&q, NULL), UOS_EVQ_ERR_ARG);
    CHECK_EQ(uos_evq_count(NULL), 0u);
}

TEST(event_queue_wraps_its_counters)
{
    uos_evq_t q;
    uos_event_t ev = { .type = UOS_EV_KEY }, out;

    uos_evq_init(&q);
    q.head = q.tail = 0xfffffffeu;
    CHECK_EQ(uos_evq_push(&q, &ev), 0);
    CHECK_EQ(uos_evq_push(&q, &ev), 0);
    CHECK_EQ(uos_evq_push(&q, &ev), 0);
    CHECK_EQ(uos_evq_count(&q), 3u);
    CHECK_EQ(uos_evq_pop(&q, &out), 1);
    CHECK_EQ(out.type, UOS_EV_KEY);
}

TEST(event_names)
{
    CHECK_STR(uos_ev_name(UOS_EV_KEY), "key");
    CHECK_STR(uos_ev_name(UOS_EV_CHAR), "char");
    CHECK_STR(uos_ev_name(UOS_EV_TICK), "tick");
    CHECK_STR(uos_ev_name(UOS_EV_TIMER), "timer");
    CHECK_STR(uos_ev_name(UOS_EV_CALL), "call");
    CHECK_STR(uos_ev_name(99), "none");
}
