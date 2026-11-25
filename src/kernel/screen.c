/* The screen stack. Pure. */

#include <robco/input.h>
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
    ss->uncovered = NULL;
    if (s->enter != NULL) {
        s->enter(s);
    }
    return 0;
}

int uos_ss_pop(uos_sstack_t *ss, uos_screen_t *s)
{
    if (ss == NULL || s == NULL) {
        return UOS_SCREEN_ERR_ARG;
    }
    if (ss->depth == 0) {
        return UOS_SCREEN_ERR_LAST;
    }
    if (ss->stack[ss->depth - 1] != s) {
        return UOS_SCREEN_ERR_TOP;
    }
    if (ss->depth <= 1) {
        return UOS_SCREEN_ERR_LAST;
    }
    ss->stack[--ss->depth] = NULL;
    ss->dirty = 1;
    ss->exposed = 1;
    ss->uncovered = ss->stack[ss->depth - 1];
    if (s->leave != NULL) {
        s->leave(s);
    }
    return 0;
}

int uos_ss_unwind(uos_sstack_t *ss)
{
    int n = 0;

    while (ss != NULL && ss->depth > 1 && uos_ss_pop(ss, ss->stack[ss->depth - 1]) == 0) {
        n++;
    }
    return n;
}

int uos_ss_on_top(const uos_sstack_t *ss, const uos_screen_t *s)
{
    return s != NULL && uos_ss_top(ss) == s;
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
    ss->exposed = 1;
    ss->uncovered = NULL;
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

static int pass_down(uos_sstack_t *ss, const uos_event_t *ev);

int uos_ss_event(uos_sstack_t *ss, const uos_event_t *ev)
{
    uos_screen_t *top;
    unsigned depth;
    int used;

    if (ss == NULL || ev == NULL) {
        return 0;
    }
    if (ev->type != UOS_EV_KEY || (ev->value != INPUT_PRESS && ev->value != INPUT_REPEAT)) {
        return pass_down(ss, ev);
    }
    if (ev->value == INPUT_REPEAT) {
        if (ss->spent && ss->spent_code == ev->code) {
            return 1;
        }
        return pass_down(ss, ev);
    }
    ss->spent = 0;
    top = uos_ss_top(ss);
    depth = ss->depth;
    used = pass_down(ss, ev);
    if (ss->depth != depth || uos_ss_top(ss) != top) {
        ss->spent = 1;
        ss->spent_code = ev->code;
    }
    return used;
}

static int pass_down(uos_sstack_t *ss, const uos_event_t *ev)
{
    unsigned i;

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

void uos_ss_expose(uos_sstack_t *ss)
{
    if (ss != NULL) {
        ss->dirty = 1;
        ss->exposed = 1;
    }
}

/* The lowest screen that shows: the highest full one, or the bottom. */
static unsigned visible_base(const uos_sstack_t *ss)
{
    unsigned i;

    for (i = ss->depth; i > 0; i--) {
        if (ss->stack[i - 1]->full) {
            return i - 1;
        }
    }
    return 0;
}

int uos_ss_draw(uos_sstack_t *ss)
{
    unsigned i, depth;
    int n = 0;

    if (ss == NULL || ss->depth == 0 || !ss->dirty) {
        return 0;
    }
    ss->dirty = 0;
    depth = ss->depth;
    i = ss->exposed ? visible_base(ss) : depth - 1u;
    ss->exposed = 0;
    for (; i < depth && depth == ss->depth; i++) {
        uos_screen_t *s = ss->stack[i];

        if (s->draw != NULL) {
            s->draw(s);
        }
        n++;
    }
    /* The mark clears before the call: uncover() may push, pop or draw,
     * and it runs once per uncovering. */
    if (ss->uncovered != NULL) {
        uos_screen_t *s = ss->uncovered;

        ss->uncovered = NULL;
        if (s == uos_ss_top(ss) && s->uncover != NULL) {
            s->uncover(s);
        }
    }
    return n;
}
