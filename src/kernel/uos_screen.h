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
 *            draw() paints its whole screen, since an overlay may have
 *            covered any part of it.
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
};

#define UOS_SCREEN_DEPTH        8

#define UOS_SCREEN_ERR_ARG      (-1)    /* NULL, or the screen is already on the stack */
#define UOS_SCREEN_ERR_FULL     (-2)    /* UOS_SCREEN_DEPTH screens already */
#define UOS_SCREEN_ERR_LAST     (-3)    /* pop of the only screen, or of an empty stack */

typedef struct {
    uos_screen_t *stack[UOS_SCREEN_DEPTH];
    unsigned depth;
    int dirty;
} uos_sstack_t;

void uos_ss_init(uos_sstack_t *ss);
int uos_ss_push(uos_sstack_t *ss, uos_screen_t *s);
int uos_ss_pop(uos_sstack_t *ss);
int uos_ss_replace(uos_sstack_t *ss, uos_screen_t *s);
/* The top screen, or NULL. */
uos_screen_t *uos_ss_top(const uos_sstack_t *ss);
/* Passes `ev` down from the top until a screen uses it. Returns 1 if one did. */
int uos_ss_event(uos_sstack_t *ss, const uos_event_t *ev);
/* Marks the stack dirty. */
void uos_ss_invalidate(uos_sstack_t *ss);
/* Draws the top when dirty. Returns 1 when it drew. */
int uos_ss_draw(uos_sstack_t *ss);

#endif
