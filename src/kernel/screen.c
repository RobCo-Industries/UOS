/* The screen stack. Pure. */

#include <robco/klibc.h>

#include "uos_screen.h"

void uos_ss_init(uos_sstack_t *ss)
{
    if (ss != NULL) {
        k_memset(ss, 0, sizeof(*ss));
    }
}

static int on_stack(const uos_sstack_t *ss, const uos_screen_t *s)
{
    unsigned i;

    for (i = 0; i < ss->depth; i++) {
        if (ss->stack[i] == s) {
            return 1;
        }
    }
    return 0;
}

int uos_ss_push(uos_sstack_t *ss, uos_screen_t *s)
{
    if (ss == NULL || s == NULL || on_stack(ss, s)) {
        return UOS_SCREEN_ERR_ARG;
    }
    if (ss->depth >= UOS_SCREEN_DEPTH) {
        return UOS_SCREEN_ERR_FULL;
    }
    ss->stack[ss->depth++] = s;
    ss->dirty = 1;
    if (s->enter != NULL) {
        s->enter(s);
    }
    return 0;
}

int uos_ss_pop(uos_sstack_t *ss)
{
    uos_screen_t *s;

    if (ss == NULL) {
        return UOS_SCREEN_ERR_ARG;
    }
    if (ss->depth <= 1) {
        return UOS_SCREEN_ERR_LAST;
    }
    s = ss->stack[--ss->depth];
    ss->stack[ss->depth] = NULL;
    ss->dirty = 1;
    if (s->leave != NULL) {
        s->leave(s);
    }
    return 0;
}

int uos_ss_replace(uos_sstack_t *ss, uos_screen_t *s)
{
    uos_screen_t *old;

    if (ss == NULL || s == NULL) {
        return UOS_SCREEN_ERR_ARG;
    }
    if (ss->depth == 0) {
        return uos_ss_push(ss, s);
    }
    old = ss->stack[ss->depth - 1];
    if (old == s) {
        return UOS_SCREEN_ERR_ARG;
    }
    if (on_stack(ss, s)) {
        return UOS_SCREEN_ERR_ARG;
    }
    ss->stack[ss->depth - 1] = s;
    ss->dirty = 1;
    if (old->leave != NULL) {
        old->leave(old);
    }
    if (s->enter != NULL) {
        s->enter(s);
    }
    return 0;
}

uos_screen_t *uos_ss_top(const uos_sstack_t *ss)
{
    return (ss != NULL && ss->depth > 0) ? ss->stack[ss->depth - 1] : NULL;
}

int uos_ss_event(uos_sstack_t *ss, const uos_event_t *ev)
{
    unsigned i;

    if (ss == NULL || ev == NULL) {
        return 0;
    }
    for (i = ss->depth; i > 0; i--) {
        uos_screen_t *s = ss->stack[i - 1];
        unsigned depth = ss->depth;

        if (s->event != NULL && s->event(s, ev)) {
            return 1;
        }
        /* The handler changed the stack: the event is spent. */
        if (ss->depth != depth || i > ss->depth || ss->stack[i - 1] != s) {
            return 0;
        }
    }
    return 0;
}

void uos_ss_invalidate(uos_sstack_t *ss)
{
    if (ss != NULL) {
        ss->dirty = 1;
    }
}

int uos_ss_draw(uos_sstack_t *ss)
{
    uos_screen_t *s = uos_ss_top(ss);

    if (s == NULL || !ss->dirty) {
        return 0;
    }
    ss->dirty = 0;
    if (s->draw != NULL) {
        s->draw(s);
    }
    return 1;
}
