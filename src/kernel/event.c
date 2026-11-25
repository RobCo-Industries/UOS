/* The kernel event queue. Pure. */

#include <robco/input.h>
#include <robco/klibc.h>

#include "uos_event.h"

void uos_evq_init(uos_evq_t *q)
{
    if (q != NULL) {
        k_memset(q, 0, sizeof(*q));
    }
}

int uos_evq_push(uos_evq_t *q, const uos_event_t *ev)
{
    if (q == NULL || ev == NULL) {
        return UOS_EVQ_ERR_ARG;
    }
    if (q->head - q->tail >= UOS_EVQ_LEN) {
        q->dropped++;
        return UOS_EVQ_ERR_FULL;
    }
    q->buf[q->head & (UOS_EVQ_LEN - 1)] = *ev;
    q->head++;
    return 0;
}

int uos_evq_pop(uos_evq_t *q, uos_event_t *ev)
{
    if (q == NULL || ev == NULL || q->head == q->tail) {
        return 0;
    }
    *ev = q->buf[q->tail & (UOS_EVQ_LEN - 1)];
    q->tail++;
    return 1;
}

unsigned uos_evq_count(const uos_evq_t *q)
{
    return q != NULL ? (unsigned)(q->head - q->tail) : 0;
}

const char *uos_ev_name(unsigned type)
{
    switch (type) {
    case UOS_EV_KEY:   return "key";
    case UOS_EV_CHAR:  return "char";
    case UOS_EV_TICK:  return "tick";
    case UOS_EV_TIMER: return "timer";
    case UOS_EV_CALL:  return "call";
    default:           return "none";
    }
}

int uos_ev_fresh_key(const uos_event_t *ev)
{
    return ev != NULL && ev->type == UOS_EV_KEY && ev->value == INPUT_PRESS;
}
