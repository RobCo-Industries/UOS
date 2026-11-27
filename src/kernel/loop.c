/*
 * The cooperative event loop: one queue, one screen stack, the timers.
 * The decisions live in the pure files event.c, timerq.c and screen.c.
 * This file feeds them from the input layer and the tick, and sleeps in
 * wfi, through input_wait(), when there is nothing to do.
 */

#include <robco/input.h>
#include <robco/irq.h>
#include <robco/tick.h>

#include "uos.h"
#include "uos_portcall.h"
#include "uos_service.h"
#include "sfx.h"

static uos_evq_t evq;
static uos_timerq_t tq;
static uos_sstack_t ss;
static uint32_t last_tick;
/* The tick of the last key or character taken, for the idle rule. */
static volatile uint32_t last_input;
/* Keys, characters and port calls taken from the input layer and not
 * dispatched yet. Loop only: take_input() and dispatch() run in the loop. */
static unsigned taken;
/* Calls posted by maintenance commands, on their way through the input queue. */
static uos_pc_t port_calls;
/* Non-zero while a maintenance command runs: uos_post() then goes through
 * the input queue. Set by maint.c, main context. */
static int in_port_command;

uint32_t uos_ticks(void)
{
    return tick_count();
}

int uos_post_event(const uos_event_t *ev)
{
    uint32_t s = irq_save();
    int rc = uos_evq_push(&evq, ev);

    irq_restore(s);
    return rc;
}

/*
 * A maintenance command's call joins the input queue behind the keys
 * received before it, so it runs after them and before later keys.
 */
static int post_port_call(uos_call_fn fn, void *arg)
{
    input_event_t in;

    if (uos_pc_make(&port_calls, fn, arg, &in) != 0) {
        return UOS_EVQ_ERR_FULL;
    }
    if (input_push(&in) == INPUT_ERR_FULL) {
        uos_pc_cancel(&port_calls, &in);
        return UOS_EVQ_ERR_FULL;
    }
    return 0;
}

int uos_post(uos_call_fn fn, void *arg)
{
    uos_event_t ev = { .type = UOS_EV_CALL, .fn = fn, .arg = arg };

    if (in_port_command) {
        return post_port_call(fn, arg);
    }
    return uos_post_event(&ev);
}

void uos_port_command(int running)
{
    in_port_command = running;
}

uint32_t uos_events_dropped(void)
{
    return evq.dropped;
}

int uos_timer_start(uint32_t delay, uint32_t period, uos_call_fn fn, void *arg,
                    uos_timer_id_t *id)
{
    return uos_tq_start(&tq, tick_count(), delay, period, fn, arg, id);
}

int uos_timer_cancel(uos_timer_id_t id)
{
    return uos_tq_cancel(&tq, id);
}

const uos_timerq_t *uos_timer_table(void)
{
    return &tq;
}

int uos_screen_push(uos_screen_t *s)
{
    return uos_ss_push(&ss, s);
}

int uos_screen_push_child(uos_screen_t *s, uos_screen_t *owner)
{
    return uos_ss_push_child(&ss, s, owner);
}

int uos_screen_unit_on_top(const uos_screen_t *s)
{
    return uos_ss_unit_on_top(&ss, s);
}

int uos_screen_pop(uos_screen_t *s)
{
    int rc = uos_ss_pop(&ss, s);

    if (rc == UOS_SCREEN_ERR_TOP) {
        uos_screen_t *top = uos_ss_top(&ss);

        UOS_MARK("screen pop=refused want=%s top=%s", s->name, top != NULL ? top->name : "none");
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
        /* A test build stops here: a caller popped a screen it does not own. */
        uos_panic("screen pop %s under %s", s->name, top != NULL ? top->name : "none");
#endif
    }
    return rc;
}

int uos_screen_unwind(void)
{
    return uos_ss_unwind(&ss);
}

int uos_screen_on_top(const uos_screen_t *s)
{
    return uos_ss_on_top(&ss, s);
}

int uos_screen_replace(uos_screen_t *s)
{
    return uos_ss_replace(&ss, s);
}

uos_screen_t *uos_screen_top(void)
{
    return uos_ss_top(&ss);
}

uos_screen_t *uos_screen_at(unsigned i)
{
    return i < ss.depth ? ss.stack[i] : NULL;
}

void uos_screen_invalidate(void)
{
    uos_ss_invalidate(&ss);
}

void uos_screen_expose(void)
{
    uos_ss_expose(&ss);
}

void uos_screen_dirty(const uos_screen_t *s)
{
    (void)uos_ss_dirty(&ss, s);
}

/* Input events become kernel events, port calls included, in queue
 * order. Releases and dial turns are dropped. */
static void take_input(const input_event_t *in)
{
    uos_event_t ev;

    if (!uos_pc_convert(&port_calls, in, &ev)) {
        return;
    }
    if (ev.type != UOS_EV_CALL) {
        last_input = tick_count();
    }
    if (uos_post_event(&ev) == 0) {
        taken++;
    }
}

unsigned uos_input_pending(void)
{
    return input_pending() + taken;
}

uint32_t uos_last_input(void)
{
    return last_input;
}

/*
 * One TICK event for the ticks since the last one, and the timers due.
 * A TICK that finds the queue full is tried again with the ticks added
 * up. A timer expiry that finds it full stays due in the timer queue.
 */
static void take_time(void)
{
    uint32_t now = tick_count();
    uint32_t s;

    if (now != last_tick) {
        uos_event_t ev = { .type = UOS_EV_TICK, .value = (int32_t)(now - last_tick) };

        if (uos_post_event(&ev) == 0) {
            last_tick = now;
        }
    }
    s = irq_save();
    (void)uos_tq_deliver(&tq, now, &evq);
    irq_restore(s);
}

/* Input fills only the room left: the rest waits in the input layer. */
static int evq_room(void)
{
    uint32_t s = irq_save();
    unsigned n = uos_evq_count(&evq);

    irq_restore(s);
    return n < UOS_EVQ_LEN;
}

static void dispatch(uos_event_t *ev)
{
    /* Only take_input() posts KEY and CHAR events, and CALL events of value 1. */
    if ((ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR ||
         (ev->type == UOS_EV_CALL && ev->value == 1)) && taken > 0) {
        taken--;
    }
    switch (ev->type) {
    case UOS_EV_CALL:
        if (ev->fn != NULL) {
            ev->fn(ev->arg);
        }
        break;
    case UOS_EV_TIMER:
        /* A timer cancelled after its expiry was queued is stale here. */
        if (!uos_tq_claim(&tq, ev->timer, &ev->fn, &ev->arg)) {
            break;
        }
        if (ev->fn != NULL) {
            ev->fn(ev->arg);
        } else {
            (void)uos_ss_event(&ss, ev);
        }
        break;
    case UOS_EV_TICK:
        uos_services_tick(tick_count());
        (void)uos_ss_event(&ss, ev);
        break;
    default:
        (void)uos_ss_event(&ss, ev);
        break;
    }
}

void uos_loop_run(void)
{
    input_event_t in;
    uos_event_t ev;

    last_tick = tick_count();
    for (;;) {
        uint32_t s;
        unsigned n;
        int got;

        /* Time first: the queue holds at most the one event input_wait()
         * took, so the TICK and all 16 expiries fit before input. */
        take_time();
        uos_sfx_poll();
        while (evq_room() && input_poll(&in)) {
            take_input(&in);
        }
        /* Only the events queued before this pass: a call that posts
         * itself again, such as a stepped copy, runs once per pass, so
         * time and input get their turn between two steps. */
        s = irq_save();
        n = uos_evq_count(&evq);
        irq_restore(s);
        while (n-- > 0) {
            s = irq_save();
            got = uos_evq_pop(&evq, &ev);
            irq_restore(s);
            if (!got) {
                break;
            }
            dispatch(&ev);
            (void)uos_ss_draw(&ss);
        }
        (void)uos_ss_draw(&ss);
        s = irq_save();
        n = uos_evq_count(&evq);
        irq_restore(s);
        /* Idle: sleep in wfi until an input event or the next tick, unless
         * a pass left work queued. */
        if (n == 0 && input_wait(&in, 1000u / UOS_TICK_HZ)) {
            take_input(&in);
        }
    }
}

void uos_loop_init(void)
{
    uos_evq_init(&evq);
    uos_tq_init(&tq);
    uos_ss_init(&ss);
    uos_pc_init(&port_calls);
}
