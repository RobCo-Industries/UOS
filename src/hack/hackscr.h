/*
 * The password game screen. Prefix hack_. The login starts it with F2 on
 * an empty user name line; the shell's RUN verb may start it later with
 * hack_screen_start().
 *
 * Context: loop only (a screen callback, a timer, a posted call). Never
 * from a maintenance command function directly, an IRQ or the panic path.
 */
#ifndef UOS_HACKSCR_H
#define UOS_HACKSCR_H

#define HACK_ERR_BUSY       (-1)    /* the game is already on the stack */
#define HACK_ERR_SCREEN     (-2)    /* the screen stack is full */

/* How the game ended, for the done callback. */
#define HACK_DONE_LEFT      0       /* the person left, or the game could not run */
#define HACK_DONE_WON       1       /* a session started for the [hack] user */
#define HACK_DONE_LOST      2       /* the terminal locked */

/* Called once when the game's screen leaves the stack. */
typedef void (*hack_done_fn)(int how, void *ctx);

/*
 * Pushes the game. It checks the session rules first (sess_hack_check):
 * a refusal shows its reason and BACK leaves. Then it picks the word
 * length from [hack] difficulty, reads /UOS/WORDS/Wnn.TXT and builds the
 * board. A win starts a session for the [hack] user; a loss locks the
 * terminal for [hack] lockout_s with the login's lockout screen. Both
 * replace every screen on the stack. `done` may be NULL. Returns 0,
 * HACK_ERR_BUSY or HACK_ERR_SCREEN.
 */
int hack_screen_start(hack_done_fn done, void *ctx);

/* Non-zero while the game is on the stack. Any context. */
int hack_screen_active(void);

#endif
