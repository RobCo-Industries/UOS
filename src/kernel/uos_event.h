/*
 * Kernel events and the event queue. Pure: src/kernel/event.c.
 *
 * The event loop reads one queue. Input, timer expiries, ticks and posted
 * callbacks all arrive here, in order. The queue does no locking: the
 * kernel masks IRQs around it.
 */
#ifndef UOS_EVENT_H
#define UOS_EVENT_H

#include <stdint.h>

/* Event types. */
#define UOS_EV_NONE     0
#define UOS_EV_KEY      1   /* code: INPUT_KEY_*, value: INPUT_PRESS or INPUT_REPEAT */
#define UOS_EV_CHAR     2   /* code: the character */
#define UOS_EV_TICK     3   /* value: ticks since the previous TICK event, at least 1 */
#define UOS_EV_TIMER    4   /* timer: the id that expired, arg: its argument */
#define UOS_EV_CALL     5   /* fn(arg) runs in the loop, not passed to screens */

typedef void (*uos_call_fn)(void *arg);

typedef struct uos_event {
    uint8_t type;       /* UOS_EV_* */
    uint8_t source;     /* INPUT_SRC_* for KEY and CHAR, else 0 */
    uint16_t code;
    int32_t value;
    uint32_t timer;     /* TIMER: the uos_timer_id_t */
    uos_call_fn fn;     /* CALL: the function; TIMER: the timer's callback or NULL */
    void *arg;          /* CALL and TIMER: the argument */
} uos_event_t;

/* Queue length, a power of two. */
#define UOS_EVQ_LEN         32u

#define UOS_EVQ_ERR_FULL    (-1)    /* the queue is full: the event is dropped and counted */
#define UOS_EVQ_ERR_ARG     (-2)    /* NULL */

typedef struct {
    uos_event_t buf[UOS_EVQ_LEN];
    uint32_t head;      /* next write, free-running */
    uint32_t tail;      /* next read, free-running */
    uint32_t dropped;   /* events refused because the queue was full */
} uos_evq_t;

void uos_evq_init(uos_evq_t *q);
/* Appends a copy. Returns 0, UOS_EVQ_ERR_FULL or UOS_EVQ_ERR_ARG. */
int uos_evq_push(uos_evq_t *q, const uos_event_t *ev);
/* Removes the oldest event into *ev. Returns 1, or 0 when empty or on NULL. */
int uos_evq_pop(uos_evq_t *q, uos_event_t *ev);
/* Events waiting. 0 for NULL. */
unsigned uos_evq_count(const uos_evq_t *q);

/* Name of an event type: "key", "char", "tick", "timer", "call", or "none". */
const char *uos_ev_name(unsigned type);

#endif
