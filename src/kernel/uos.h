/*
 * Kernel services: the hardware side of UOS. Screens, commands and
 * services call these. Pure files never include this header.
 *
 * Everything here runs in the main loop's context unless a function says
 * otherwise. There is one thread of control: a screen's callback runs to
 * its end before the next event is read, so callbacks must not wait.
 */
#ifndef UOS_H
#define UOS_H

#include <stdint.h>

#include "bootlines.h"
#include "uos_event.h"
#include "uos_screen.h"
#include "uos_settings.h"
#include "uos_timer.h"

/* The tick rate. Timers count in these ticks. */
#define UOS_TICK_HZ             100u
/* Ticks for `ms` milliseconds, rounded up. */
#define UOS_MS_TO_TICKS(ms)     (((uint32_t)(ms) * UOS_TICK_HZ + 999u) / 1000u)

/* ---- Markers and text ------------------------------------------------- */

/* Prints "@RC stage=uos ev=<fmt>" as one line on the PL011. Any context. */
void uos_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define UOS_MARK(...) uos_mark(__VA_ARGS__)

/* One line on the screen console, when there is one, and on the PL011. */
void uos_say(const char *line);

/*
 * The PL011 line being edited. The console's read screen echoes the line
 * without a line end and calls uos_serial_open_line(1). Before a marker
 * or any other line goes out, uos_serial_break() ends that partial line,
 * so each marker starts on a line of its own; the next echo draws the
 * edited line again. Any context, panic path included.
 */
void uos_serial_open_line(int open);
void uos_serial_break(void);

/*
 * The one path for fatal errors. Masks IRQs, prints "ev=panic", shows the
 * reason on a cleared screen and keeps the maintenance port answering
 * (`state`, `dump`, `reboot`). Never returns.
 */
void uos_panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* Routes undefined instructions and aborts to the panic path. Once, at entry. */
void uos_panic_init(void);

/*
 * Shuts the services down, prints "ev=reboot source=<source>", turns the
 * MMU and caches off so every write reaches memory, then resets the board
 * through the watchdog. Main context. Never returns.
 */
void uos_reboot(const char *source) __attribute__((noreturn));

/*
 * Masks IRQs, shuts the services down, prints "ev=halt", then answers the
 * maintenance port for good, as the panic path does. Never returns.
 */
void uos_halt(void) __attribute__((noreturn));

/* ---- System state ----------------------------------------------------- */

#define UOS_PHASE_INIT          0   /* bring-up */
#define UOS_PHASE_READY         1   /* the event loop runs */
#define UOS_PHASE_PANIC         2   /* stopped by uos_panic() */
#define UOS_PHASE_HALT          3   /* stopped by uos_halt() */

#define UOS_USER_NAME_MAX       16  /* a SYSUAF.DAT name, UAF_NAME_MAX */

typedef struct {
    int phase;                  /* UOS_PHASE_* */
    int fb_ok;                  /* the screen console works */
    int trusted;                /* the boot info passed the trust rule */
    int card;                   /* UOS_CARD_* */
    int cfg_bad;                /* UOS.CFG had bad, clamped or unknown values, or a bad line */
    int rtc;                    /* a real-time clock answered */
    char user[UOS_USER_NAME_MAX + 1];   /* logged-in user, "" for none */
    char error[32];             /* last error, short, no spaces; "" for none */
} uos_state_t;

extern uos_state_t uos;

/* Records the last error for `state`. `what` is short, without spaces. */
void uos_set_error(const char *what);

/* The settings from UOS.CFG, or the defaults. */
const uos_settings_t *uos_settings(void);

/* ---- Event loop ------------------------------------------------------- */

/* Ticks since boot. Wraps at 2^32: compare with differences. Any context. */
uint32_t uos_ticks(void);

/*
 * Queues an event. The loop delivers KEY, CHAR, TICK and TIMER events to
 * the screens and runs CALL events. Any context. Returns 0 or
 * UOS_EVQ_ERR_FULL (the event is dropped and counted).
 */
int uos_post_event(const uos_event_t *ev);

/*
 * Input not handled yet: the events still in the input layer plus the
 * keys, characters and port calls the loop took into its queue and has
 * not handled. A maintenance command runs inside input_poll(), between
 * the two, so a state reply must use this and not input_pending(). Loop
 * only.
 */
unsigned uos_input_pending(void);

/*
 * Queues a call of fn(arg) from the loop. Main context, never an
 * interrupt handler. 0 or UOS_EVQ_ERR_FULL.
 *
 * Inside a maintenance command the call goes through the input queue,
 * behind every key received before the command: what the system received
 * first is handled first, whatever path it came in on. The call then also
 * counts in uos_input_pending() until it has run.
 */
int uos_post(uos_call_fn fn, void *arg);

/* maint.c only: non-zero while a maintenance command runs. Main context. */
void uos_port_command(int running);

/*
 * Starts a timer: first expiry after `delay` ticks, then every `period`
 * ticks, or once when `period` is 0. On expiry the loop runs fn(arg), or,
 * when fn is NULL, gives the screens a UOS_EV_TIMER event with the id
 * and arg. Returns 0 with *id set, or a UOS_TIMER_ERR_* code.
 */
int uos_timer_start(uint32_t delay, uint32_t period, uos_call_fn fn, void *arg,
                    uos_timer_id_t *id);

/* Stops a timer. It never fires after this returns. 0 or UOS_TIMER_ERR_ID. */
int uos_timer_cancel(uos_timer_id_t id);

/* The kernel's timer table, read only, for listings. */
const uos_timerq_t *uos_timer_table(void);

/* The screen stack. See uos_screen.h for the rules. */
int uos_screen_push(uos_screen_t *s);
/* Pushes `s` as a child of `owner`, such as a read line or a pager a
 * screen opens for itself. NULL is a plain push. See uos_screen.h. */
int uos_screen_push_child(uos_screen_t *s, uos_screen_t *owner);
/* Pops `s` only when it is the top. Otherwise a "screen pop=refused"
 * marker and UOS_SCREEN_ERR_TOP; a test build panics. */
int uos_screen_pop(uos_screen_t *s);
/* Pops every screen above the bottom one: a reset of the whole stack. */
int uos_screen_unwind(void);
/* 1 when `s` is the top screen: the one test for "this screen may act". */
int uos_screen_on_top(const uos_screen_t *s);
/* 1 when `s` or a child it owns is the top: "is the unit of `s` covered"
 * for work it owes. A command that stands in for a key uses
 * uos_screen_on_top(): the child takes the keys. */
int uos_screen_unit_on_top(const uos_screen_t *s);
int uos_screen_replace(uos_screen_t *s);
uos_screen_t *uos_screen_top(void);
/* The screen at depth `i`, 0 at the bottom; NULL at or past the depth. */
uos_screen_t *uos_screen_at(unsigned i);
/* Asks for a redraw of the top screen after the current event. */
void uos_screen_invalidate(void);
/* Asks for a redraw of every visible screen, bottom-up, as after a pop. */
void uos_screen_expose(void);
/* `s` changed: redraws it through the stack when it shows, else
 * nothing until it is uncovered. See uos_ss_dirty(). */
void uos_screen_dirty(const uos_screen_t *s);

/* Empties the queue, the timers and the screen stack. Once, before the first screen. */
void uos_loop_init(void);

/* Runs the loop for good: input, timers, ticks, posted calls, draws. */
void uos_loop_run(void) __attribute__((noreturn));

/* Events dropped because the queue was full. */
uint32_t uos_events_dropped(void);

/* ---- Maintenance port ------------------------------------------------- */

/* Starts the input layer: the console backend and the maintenance port commands. */
void uos_maint_start(void);

/* Answers the maintenance port with IRQs off, starting it first when it is not up.
 * Never returns. For uos_panic(). */
void uos_maint_idle(void) __attribute__((noreturn));

/* ---- The first screen ------------------------------------------------- */

/* The first screen: the banner, then the system screen when no login can run. */
extern uos_screen_t uos_first_screen;

/*
 * The screen that replaces the first screen after the banner, such as the
 * login. NULL keeps the first screen. Set it before the loop runs, from a
 * service init. The ready marker names the screen shown.
 */
void uos_first_set_next(uos_screen_t *s);

/*
 * The two notice rows of the system screen, copied and cut to 79
 * characters; NULL restores the default text. `reboot_key` non-zero lets
 * F4 restart the terminal from that screen. Loop or service init.
 */
void uos_first_set_notice(const char *row0, const char *row1, int reboot_key);

/* The tick of the last key or character the loop took from the input
 * layer. Any context. */
uint32_t uos_last_input(void);

#endif
