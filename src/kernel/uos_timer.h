/*
 * Kernel timers: one-shot and periodic, counted in ticks. Pure:
 * src/kernel/timerq.c. The kernel wraps it in uos_timer_start() and
 * uos_timer_cancel(), see uos.h.
 *
 * Tick arithmetic is wrap-safe: a deadline is due when (now - due),
 * taken as a signed 32-bit value, is 0 or more. So a delay or a period
 * is at most UOS_TIMER_MAX_TICKS.
 *
 * An id names one start of one slot. Cancelling it, or a one-shot timer
 * being claimed, ends the id: an expiry already queued for it is then
 * stale and uos_tq_claim() refuses it. So a cancelled timer never fires,
 * even when its expiry is already in the event queue.
 */
#ifndef UOS_TIMER_H
#define UOS_TIMER_H

#include <stdint.h>

#include "uos_event.h"

#define UOS_TIMER_MAX           16          /* timers alive at once */
#define UOS_TIMER_MAX_TICKS     0x7fffffffu /* longest delay or period */

#define UOS_TIMER_ERR_ARG       (-1)    /* NULL, or a delay or period over the limit */
#define UOS_TIMER_ERR_FULL      (-2)    /* every slot is in use */
#define UOS_TIMER_ERR_ID        (-3)    /* the id is not a live timer */

/* 0 is never a valid id. */
typedef uint32_t uos_timer_id_t;
#define UOS_TIMER_NONE          0u

/* Slot states. */
#define UOS_TS_FREE             0
#define UOS_TS_ARMED            1   /* waiting for its deadline */
#define UOS_TS_FIRED            2   /* a one-shot that expired, not yet claimed */

typedef struct {
    uint32_t due;       /* tick of the next expiry */
    uint32_t period;    /* 0: one-shot */
    uos_call_fn fn;
    void *arg;
    uint16_t gen;       /* part of the id; changes at every start */
    uint8_t state;      /* UOS_TS_* */
} uos_timer_slot_t;

typedef struct {
    uos_timer_slot_t slot[UOS_TIMER_MAX];
    uint32_t expired;   /* expiries handed out by uos_tq_expire() */
} uos_timerq_t;

void uos_tq_init(uos_timerq_t *tq);

/*
 * Starts a timer that first expires `delay` ticks after `now`, then every
 * `period` ticks, or once when `period` is 0. A delay of 0 expires at the
 * next uos_tq_expire(). Stores the id in *id. Returns 0,
 * UOS_TIMER_ERR_ARG or UOS_TIMER_ERR_FULL.
 */
int uos_tq_start(uos_timerq_t *tq, uint32_t now, uint32_t delay, uint32_t period,
                 uos_call_fn fn, void *arg, uos_timer_id_t *id);

/* Ends a live timer, armed or fired. Returns 0 or UOS_TIMER_ERR_ID. */
int uos_tq_cancel(uos_timerq_t *tq, uos_timer_id_t id);

/*
 * Collects up to `max` timers due at `now` into out[] and returns their
 * count. A one-shot timer becomes FIRED; a periodic one moves its deadline
 * on by one period, so a late caller catches up one expiry per call.
 * It cannot tell whether an expiry was delivered: the loop uses
 * uos_tq_deliver() instead.
 */
unsigned uos_tq_expire(uos_timerq_t *tq, uint32_t now, uos_timer_id_t *out, unsigned max);

/*
 * Delivery that never loses an expiry. uos_tq_due() names the due timer
 * whose deadline passed longest ago, without changing it: returns 1 and
 * its id, or 0 when none is due. uos_tq_ack() records that the expiry of
 * that id was queued: a one-shot timer becomes FIRED, a periodic one
 * moves its deadline on by one period. Returns 0, or UOS_TIMER_ERR_ID
 * when the id is not an armed timer. Until the ack, the expiry stays due.
 */
int uos_tq_due(const uos_timerq_t *tq, uint32_t now, uos_timer_id_t *id);
int uos_tq_ack(uos_timerq_t *tq, uos_timer_id_t id);

/*
 * Queues one UOS_EV_TIMER event per due expiry while `q` has room, and
 * acks each one queued. Returns the count queued. An expiry that finds
 * the queue full stays due for the next call: a one-shot timer fires
 * once, as soon as there is room; a periodic timer late by k periods
 * fires k times, one event per missed period. Never counts a drop.
 */
unsigned uos_tq_deliver(uos_timerq_t *tq, uint32_t now, uos_evq_t *q);

/*
 * Checks an expired id before it is delivered. Returns 1 and the
 * callback and argument when the id is still live (a FIRED one-shot is
 * freed then), or 0 when it was cancelled or is unknown.
 */
int uos_tq_claim(uos_timerq_t *tq, uos_timer_id_t id, uos_call_fn *fn, void **arg);

/*
 * Ticks from `now` to the nearest deadline, 0 when one is due. Returns 1,
 * or 0 when no timer is armed (*ticks is then left alone).
 */
int uos_tq_next(const uos_timerq_t *tq, uint32_t now, uint32_t *ticks);

/* Timers armed or fired. */
unsigned uos_tq_live(const uos_timerq_t *tq);

/*
 * Slot `i`, for listings: returns 1 and its id, the ticks until its
 * deadline (0 when due or fired) and its period, or 0 for a free slot or
 * `i` out of range.
 */
int uos_tq_slot(const uos_timerq_t *tq, unsigned i, uint32_t now, uos_timer_id_t *id,
                uint32_t *due_in, uint32_t *period);

#endif
