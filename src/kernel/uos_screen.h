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
 *   pop      takes the screen the caller expects on top. Any other top
 *            refuses the pop: a screen never pops someone else's. The
 *            top's leave() runs; the screen below becomes the top.
 *            Dirty. The last screen cannot be popped: replace it.
 *   unwind   pops every screen above the bottom one, top first. Only a
 *            reset of the whole stack uses it, such as a logout.
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
 *   uncover  a pop or an unwind that leaves `s` on top marks it
 *            uncovered. The next draw, after it paints, calls the top's
 *            uncover() once, when that top is still `s`. A push or a
 *            replace before then clears the mark: the screen is covered
 *            again or gone. A screen does here what it owes, such as the
 *            read a posted call could not start while it was covered.
 *   owner    a child screen, such as a read line or a pager, is pushed
 *            with the screen that owns it. The owner and its children
 *            form one unit. uos_ss_unit_on_top() says whether a screen
 *            or a child it owns is the top. When a pop leaves a child on
 *            top, the draw calls uncover() for the child, then for each
 *            owner down the chain, top first, each at most once. The
 *            chain stops at a screen that is no longer in the unit on
 *            top, and at one a pop inside the chain marked uncovered
 *            again: the next draw calls that one.
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
    void (*uncover)(uos_screen_t *s);                       /* on top again, after the draw */
    uos_screen_t *owner;    /* set by the stack: the screen this child belongs to, or NULL */
};

#define UOS_SCREEN_DEPTH        8

#define UOS_SCREEN_ERR_ARG      (-1)    /* NULL, or the screen is already on the stack */
#define UOS_SCREEN_ERR_FULL     (-2)    /* UOS_SCREEN_DEPTH screens already */
#define UOS_SCREEN_ERR_LAST     (-3)    /* pop of the only screen, or of an empty stack */
#define UOS_SCREEN_ERR_TOP      (-4)    /* pop of a screen that is not the top */

typedef struct {
    uos_screen_t *stack[UOS_SCREEN_DEPTH];
    unsigned depth;
    int dirty;
    int exposed;            /* redraw from the highest full screen up */
    uos_screen_t *uncovered;    /* the top a pop left: its uncover() runs after the draw */
    int spent;              /* 1: repeats of spent_code are dropped */
    uint16_t spent_code;    /* the key whose press changed the stack */
} uos_sstack_t;

void uos_ss_init(uos_sstack_t *ss);
int uos_ss_push(uos_sstack_t *ss, uos_screen_t *s);
/* Pushes `s` as a child of `owner`, which must be on the stack. A NULL
 * owner is a plain push. */
int uos_ss_push_child(uos_sstack_t *ss, uos_screen_t *s, uos_screen_t *owner);
/* Pops `s`, which must be the top: UOS_SCREEN_ERR_TOP otherwise. */
int uos_ss_pop(uos_sstack_t *ss, uos_screen_t *s);
/* Pops every screen above the bottom one. Returns how many. */
int uos_ss_unwind(uos_sstack_t *ss);
/* 1 when `s` is the top screen. A screen that is not the top takes no
 * posted action: see docs/ARCHITECTURE.md, the screen stack. */
int uos_ss_on_top(const uos_sstack_t *ss, const uos_screen_t *s);
/* 1 when `s` is the top or owns the top through its children: the unit
 * of `s` is not covered by another screen. */
int uos_ss_unit_on_top(const uos_sstack_t *ss, const uos_screen_t *s);
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
 * Then, when a pop left the top uncovered, calls its uncover() once,
 * then each owner's in the chain, as above.
 * Returns the number of draw() calls made, 0 when not dirty. */
int uos_ss_draw(uos_sstack_t *ss);

#endif
