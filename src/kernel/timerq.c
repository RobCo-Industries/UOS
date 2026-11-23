/* Kernel timers in ticks. Pure. */

#include <robco/klibc.h>

#include "uos_timer.h"

/* id = gen << 8 | (slot index + 1). */
#define ID_INDEX(id)    ((unsigned)((id) & 0xffu) - 1u)
#define ID_GEN(id)      ((uint16_t)((id) >> 8))

static int due_at(uint32_t now, uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

void uos_tq_init(uos_timerq_t *tq)
{
    unsigned i;

    if (tq == NULL) {
        return;
    }
    k_memset(tq, 0, sizeof(*tq));
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        tq->slot[i].gen = 1;
    }
}

static uos_timer_slot_t *lookup(uos_timerq_t *tq, uos_timer_id_t id)
{
    unsigned i;

    if (tq == NULL || id == UOS_TIMER_NONE) {
        return NULL;
    }
    i = ID_INDEX(id);
    if (i >= UOS_TIMER_MAX || tq->slot[i].state == UOS_TS_FREE ||
        tq->slot[i].gen != ID_GEN(id)) {
        return NULL;
    }
    return &tq->slot[i];
}

static void release(uos_timer_slot_t *s)
{
    s->state = UOS_TS_FREE;
    s->fn = NULL;
    s->arg = NULL;
    /* A new generation: every id of this start is now stale. 0 is skipped. */
    s->gen++;
    if (s->gen == 0) {
        s->gen = 1;
    }
}

int uos_tq_start(uos_timerq_t *tq, uint32_t now, uint32_t delay, uint32_t period,
                 uos_call_fn fn, void *arg, uos_timer_id_t *id)
{
    unsigned i;

    if (tq == NULL || id == NULL || delay > UOS_TIMER_MAX_TICKS ||
        period > UOS_TIMER_MAX_TICKS) {
        return UOS_TIMER_ERR_ARG;
    }
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        uos_timer_slot_t *s = &tq->slot[i];

        if (s->state != UOS_TS_FREE) {
            continue;
        }
        s->due = now + delay;
        s->period = period;
        s->fn = fn;
        s->arg = arg;
        s->state = UOS_TS_ARMED;
        *id = ((uint32_t)s->gen << 8) | (i + 1u);
        return 0;
    }
    return UOS_TIMER_ERR_FULL;
}

int uos_tq_cancel(uos_timerq_t *tq, uos_timer_id_t id)
{
    uos_timer_slot_t *s = lookup(tq, id);

    if (s == NULL) {
        return UOS_TIMER_ERR_ID;
    }
    release(s);
    return 0;
}

unsigned uos_tq_expire(uos_timerq_t *tq, uint32_t now, uos_timer_id_t *out, unsigned max)
{
    unsigned i, n = 0;

    if (tq == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < UOS_TIMER_MAX && n < max; i++) {
        uos_timer_slot_t *s = &tq->slot[i];

        if (s->state != UOS_TS_ARMED || !due_at(now, s->due)) {
            continue;
        }
        out[n++] = ((uint32_t)s->gen << 8) | (i + 1u);
        tq->expired++;
        if (s->period == 0) {
            s->state = UOS_TS_FIRED;
        } else {
            s->due += s->period;
        }
    }
    return n;
}

int uos_tq_claim(uos_timerq_t *tq, uos_timer_id_t id, uos_call_fn *fn, void **arg)
{
    uos_timer_slot_t *s = lookup(tq, id);

    if (s == NULL) {
        return 0;
    }
    if (fn != NULL) {
        *fn = s->fn;
    }
    if (arg != NULL) {
        *arg = s->arg;
    }
    if (s->state == UOS_TS_FIRED) {
        release(s);
    }
    return 1;
}

int uos_tq_next(const uos_timerq_t *tq, uint32_t now, uint32_t *ticks)
{
    unsigned i;
    int found = 0;
    uint32_t best = 0;

    if (tq == NULL || ticks == NULL) {
        return 0;
    }
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        const uos_timer_slot_t *s = &tq->slot[i];
        uint32_t left;

        if (s->state != UOS_TS_ARMED) {
            continue;
        }
        left = due_at(now, s->due) ? 0 : s->due - now;
        if (!found || left < best) {
            best = left;
            found = 1;
        }
    }
    if (found) {
        *ticks = best;
    }
    return found;
}

unsigned uos_tq_live(const uos_timerq_t *tq)
{
    unsigned i, n = 0;

    if (tq == NULL) {
        return 0;
    }
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        if (tq->slot[i].state != UOS_TS_FREE) {
            n++;
        }
    }
    return n;
}

int uos_tq_slot(const uos_timerq_t *tq, unsigned i, uint32_t now, uos_timer_id_t *id,
                uint32_t *due_in, uint32_t *period)
{
    const uos_timer_slot_t *s;

    if (tq == NULL || i >= UOS_TIMER_MAX || tq->slot[i].state == UOS_TS_FREE) {
        return 0;
    }
    s = &tq->slot[i];
    if (id != NULL) {
        *id = ((uint32_t)s->gen << 8) | (i + 1u);
    }
    if (due_in != NULL) {
        *due_in = (s->state == UOS_TS_FIRED || due_at(now, s->due)) ? 0 : s->due - now;
    }
    if (period != NULL) {
        *period = s->period;
    }
    return 1;
}
