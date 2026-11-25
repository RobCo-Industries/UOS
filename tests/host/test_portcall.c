/* Host tests of src/kernel/portcall.c: keys and port calls keep their order. */

#include <string.h>

#include "rc_test.h"

#include "uos_event.h"
#include "uos_portcall.h"

/* The loop's two queues and what it handled, in order. */
static input_queue_t inq;
static uos_evq_t evq;
static uos_pc_t pc;
static char out[256];

static void reset(void)
{
    input_queue_init(&inq);
    uos_evq_init(&evq);
    uos_pc_init(&pc);
    out[0] = '\0';
}

static void note(char kind, unsigned n)
{
    size_t len = strlen(out);

    out[len] = kind;
    out[len + 1] = (char)('0' + n);
    out[len + 2] = '\0';
}

static void call(void *arg)
{
    note('c', (unsigned)(uintptr_t)arg);
}

/* A key from the console or `key <name>`: press and release. */
static void key(unsigned code)
{
    input_event_t ev = { .type = INPUT_EV_KEY, .source = INPUT_SRC_SCRIPT, .code = (uint16_t)code,
                         .value = INPUT_PRESS };

    CHECK_EQ(input_queue_push(&inq, &ev), 0);
    ev.value = INPUT_RELEASE;
    CHECK_EQ(input_queue_push(&inq, &ev), 0);
}

/* A port command that posts, as uos_post() does inside one. */
static int post(unsigned n)
{
    input_event_t ev;

    if (uos_pc_make(&pc, call, (void *)(uintptr_t)n, &ev) != 0) {
        return -1;
    }
    if (input_queue_push(&inq, &ev) != 0) {
        uos_pc_cancel(&pc, &ev);
        return -1;
    }
    return 0;
}

/* take_input(): up to `max` input events into the loop's queue. */
static void take(unsigned max)
{
    input_event_t in;
    uos_event_t ev;

    while (max-- > 0 && input_queue_pop(&inq, &in)) {
        if (uos_pc_convert(&pc, &in, &ev)) {
            CHECK_EQ(uos_evq_push(&evq, &ev), 0);
        }
    }
}

/* dispatch(): keys are noted, calls run. */
static void run(void)
{
    uos_event_t ev;

    while (uos_evq_pop(&evq, &ev)) {
        if (ev.type == UOS_EV_CALL) {
            CHECK_EQ(ev.value, 1);
            ev.fn(ev.arg);
        } else {
            CHECK_EQ(ev.type, UOS_EV_KEY);
            note('k', ev.code);
        }
    }
}

TEST(key_then_call_runs_the_key_first)
{
    reset();
    key(INPUT_KEY_BACK);
    CHECK_EQ(post(1), 0);
    take(64);
    run();
    CHECK_STR(out, "k6c1");
}

TEST(call_then_key_runs_the_call_first)
{
    reset();
    CHECK_EQ(post(1), 0);
    key(INPUT_KEY_BACK);
    take(64);
    run();
    CHECK_STR(out, "c1k6");
}

TEST(interleavings_keep_arrival_order_at_any_take_size)
{
    unsigned step;

    for (step = 1; step <= 8; step++) {
        reset();
        key(1);
        CHECK_EQ(post(1), 0);
        CHECK_EQ(post(2), 0);
        key(2);
        take(step);
        key(3);
        CHECK_EQ(post(3), 0);
        take(step);
        run();
        CHECK_EQ(post(4), 0);
        key(4);
        take(64);
        run();
        CHECK_STR(out, "k1c1c2k2k3c3c4k4");
        CHECK_EQ(pc.used, 0u);
    }
}

TEST(full_table_refuses_and_a_take_frees_a_slot)
{
    unsigned i;

    reset();
    for (i = 0; i < UOS_PC_LEN; i++) {
        CHECK_EQ(post(i % 10u), 0);
    }
    CHECK_EQ(post(0), -1);
    take(1);
    CHECK_EQ(post(9), 0);
}

TEST(full_input_queue_refuses_and_frees_the_slot)
{
    unsigned i;

    reset();
    for (i = 0; i < INPUT_QUEUE_LEN / 2u; i++) {
        key(1);
    }
    CHECK_EQ(post(1), -1);
    CHECK_EQ(pc.used, 0u);
}

TEST(releases_dials_and_stale_calls_are_dropped)
{
    input_event_t in = { .type = INPUT_EV_KEY, .code = 1, .value = INPUT_RELEASE };
    uos_event_t ev;

    reset();
    CHECK_EQ(uos_pc_convert(&pc, &in, &ev), 0);
    in.type = INPUT_EV_DIAL;
    CHECK_EQ(uos_pc_convert(&pc, &in, &ev), 0);
    in.type = UOS_INPUT_EV_CALL;
    in.code = 3;
    CHECK_EQ(uos_pc_convert(&pc, &in, &ev), 0);
    in.code = UOS_PC_LEN;
    CHECK_EQ(uos_pc_convert(&pc, &in, &ev), 0);
    in.type = INPUT_EV_CHAR;
    in.code = 'a';
    CHECK_EQ(uos_pc_convert(&pc, &in, &ev), 1);
    CHECK_EQ(ev.type, UOS_EV_CHAR);
    CHECK_EQ(ev.code, 'a');
}
