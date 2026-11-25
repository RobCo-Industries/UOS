/*
 * Screens and the screen stack. Pure: src/kernel/screen.c. The kernel
 * owns one stack and wraps it in uos_screen_push() and friends, see uos.h.
 *
 * A screen is a small struct of callbacks. Any callback may be NULL.
 * The top screen gets every event first. When its event() returns 0
 * the event goes to the screen below, and so on down; 1 stops it. So a
 * pager or the on-screen keyboard can sit on another screen and pass on
 * the keys it does not use.
 *
 * Rules:
 *   push     the new screen's enter() runs; the screen below stays, with
 *            no leave(). The stack is marked dirty.
 *   pop      the top's leave() runs; the screen below becomes the top.
 *            Dirty. The last screen cannot be popped: replace it.
 *   replace  the top's leave(), then the new screen's enter(). Dirty.
 *            On an empty stack it is a push.
 *   draw     when dirty, the top's draw() runs once and the flag clears.
 *            A full screen's draw() clears the display and paints all of
 *            it. A partial screen, such as the read line or the pause
 *            line, paints only its own rows.
 *   expose   a pop, a replace or uos_ss_expose() also marks the stack
 *            exposed: rows of the screens below may show stale cells.
 *            The next draw then runs every visible screen's draw(),
 *            bottom-up, from the highest screen with `full` set, or from
 *            the bottom when none has it. No screen draws what is under
 *            it. A push or an invalidate alone draws the top only.
 * A screen may push, pop or replace from inside its own callbacks.
 */
#ifndef UOS_SCREEN_H
#define UOS_SCREEN_H

#include "uos_event.h"

typedef struct uos_screen uos_screen_t;

struct uos_screen {
    const char *name;                                       /* for `state`, short, no spaces */
    void (*enter)(uos_screen_t *s);
    void (*leave)(uos_screen_t *s);
    int (*event)(uos_screen_t *s, const uos_event_t *ev);   /* 1: used, 0: pass down */
    void (*draw)(uos_screen_t *s);
    void *ctx;                                              /* the screen's own data */
    int full;               /* 1: draw() clears and paints the whole display */
};

#define UOS_SCREEN_DEPTH        8

#define UOS_SCREEN_ERR_ARG      (-1)    /* NULL, or the screen is already on the stack */
#define UOS_SCREEN_ERR_FULL     (-2)    /* UOS_SCREEN_DEPTH screens already */
#define UOS_SCREEN_ERR_LAST     (-3)    /* pop of the only screen, or of an empty stack */

typedef struct {
    uos_screen_t *stack[UOS_SCREEN_DEPTH];
    unsigned depth;
    int dirty;
    int exposed;            /* redraw from the highest full screen up */
    int spent;              /* 1: repeats of spent_code are dropped */
    uint16_t spent_code;    /* the key whose press changed the stack */
} uos_sstack_t;

void uos_ss_init(uos_sstack_t *ss);
int uos_ss_push(uos_sstack_t *ss, uos_screen_t *s);
int uos_ss_pop(uos_sstack_t *ss);
int uos_ss_replace(uos_sstack_t *ss, uos_screen_t *s);
/* The top screen, or NULL. */
uos_screen_t *uos_ss_top(const uos_sstack_t *ss);
/* Passes `ev` down from the top until a screen uses it. Returns 1 if one did.
 * A key press that changes the stack spends its key: the repeats of that
 * key are dropped until the next press, so a held key does not run on
 * into the screen that comes up. */
int uos_ss_event(uos_sstack_t *ss, const uos_event_t *ev);
/* Marks the stack dirty. */
void uos_ss_invalidate(uos_sstack_t *ss);
/* Marks the stack dirty and exposed: the next draw repaints every
 * visible screen, as after a pop. For a partial screen that shrinks,
 * such as the read line when its keyboard closes. */
void uos_ss_expose(uos_sstack_t *ss);
/* Draws when dirty: the top, or every visible screen when exposed.
 * Returns the number of draw() calls made, 0 when not dirty. */
int uos_ss_draw(uos_sstack_t *ss);

#endif
