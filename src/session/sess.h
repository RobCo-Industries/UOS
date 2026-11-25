/*
 * The session service: who is logged in, and the login screen. Prefix
 * sess_. The shell, the terminal menu, the minigame and the commands call
 * this header. The rules are pure, in login.c; the service is sessvc.c and
 * the screens are sesscreen.c.
 *
 * Context rules, per function:
 *   any   any context: the loop, a maintenance command function, an IRQ
 *         handler or the panic path. It reads state and changes nothing.
 *   loop  the main loop only: a screen callback, a timer, a posted call
 *         or a service init. Never from a maintenance command function
 *         directly (post first), an IRQ or the panic path.
 *
 * The session lives in RAM. A reboot ends it and clears the lockout.
 */
#ifndef UOS_SESS_H
#define UOS_SESS_H

#include <stdint.h>

#include "path.h"
#include "uaf.h"
#include "uos_screen.h"

/* Errors. */
#define SESS_ERR_NONE       (-1)    /* nobody is logged in */
#define SESS_ERR_SET        (-2)    /* the next screen is already registered */
#define SESS_ERR_ARG        (-3)    /* NULL or out of range */
#define SESS_ERR_DOWN       (-4)    /* no user database: no login runs */

/* Why a session ends: the `reason` of sess_logout() and the session end
 * marker. */
#define SESS_END_USER       0       /* the person asked: a key or a LOGOUT command */
#define SESS_END_MAINT      1       /* the maintenance port's `sess logout` */
#define SESS_END_IDLE       2       /* [login] idle_s passed with no key */
#define SESS_END_SHELL      3       /* a later screen ended it for its own reason */
#define SESS_END_NUM        4

/* any: non-zero while a user is logged in. */
int sess_logged_in(void);

/* any: the user name, upper-case as SYSUAF.DAT stores it, at most 16
 * characters; "" when nobody is logged in. */
const char *sess_user(void);

/* any: the privilege mask, bits of src/kernel/uos_privs.h; 0 when nobody
 * is logged in. */
uint32_t sess_privs(void);

/* any: the user's flags, UAF_FLAG_NOHACK and the others; 0 when nobody is
 * logged in. */
uint32_t sess_flags(void);

/* any: the home folder under /UOS/HOME, upper-case, at most 8 characters,
 * as vp_ctx_t.home needs it; "" when nobody is logged in. */
const char *sess_home(void);

/* any: fills a file system context for the user: the home folder as the
 * default, with vp_ctx_init(). SESS_ERR_NONE, ctx untouched, when nobody
 * is logged in. */
int sess_vp_ctx(vp_ctx_t *ctx);

/*
 * loop: ends the session: clears every session field, prints the session
 * end marker with the reason, removes every screen above the bottom one
 * (an active read or pager ends without calling back) and shows the login
 * screen. SESS_ERR_NONE when nobody is logged in, SESS_ERR_ARG for an
 * unknown reason.
 */
int sess_logout(int reason);

/* any: "user", "maint", "idle", "shell". */
const char *sess_end_name(int reason);

/*
 * The screen that starts after a good login and its welcome. Called once
 * per login from the loop; it returns the screen to show, which replaces
 * the welcome at the bottom of the stack. NULL from the function shows the
 * placeholder.
 */
typedef uos_screen_t *(*sess_next_fn)(void);

/*
 * loop: registers the next screen. Once per boot: a second call returns
 * SESS_ERR_SET and keeps the first. Until then the placeholder screen runs:
 * the user, "No shell is installed in this build." and logout on BACK.
 * Call it from a service init that starts after the session service.
 */
int sess_set_next(sess_next_fn fn);

/* ---- The password game ---- */

/*
 * any: may the password game run now for the [hack] user? An lg_hack_t
 * of login.h: LG_HACK_OK, or why not (game off, no database, no such
 * account, account disabled or UAF_FLAG_NOHACK, someone logged in or the
 * login past the user name prompt, terminal locked).
 */
int sess_hack_check(void);

/*
 * loop: the game was won. Checks again with sess_hack_check(), then starts
 * a session for the [hack] user with no password: marker "session
 * state=start ... how=hack", the welcome, then the next screen. On a
 * refusal prints "session result=refused why=<name> how=hack", changes
 * nothing and returns the lg_hack_t code. Every screen above the bottom
 * one is removed on success.
 */
int sess_hack_begin(void);

/*
 * loop: the game was lost. Locks the terminal for [hack] lockout_s with
 * the login's own lock and lockout screen: one lock state for the
 * terminal. Returns 1 when it locked; 0 when lockout_s is 0 or the login
 * is not at the user name prompt, and then nothing changes.
 */
int sess_hack_lock(void);

/*
 * The login's entry to the password game: F2 on an empty user name line
 * calls `fn`. `fn` returns 0 when its screen took over: it calls
 * sess_resume() when it gives the login back without a session. Any other
 * value brings the user name prompt back at once. Once per boot, as
 * sess_set_next(). Loop.
 */
typedef int (*sess_hack_fn)(void);
int sess_set_hack_entry(sess_hack_fn fn);

/*
 * any: non-zero when nobody is logged in, the login is at its user name
 * prompt and that prompt is the top screen: the login screen itself, or the
 * read line of its user name. Any other screen on top, a holotape reader
 * for one, gives 0. The password game plays only when this holds at its
 * start.
 */
int sess_prompt_on_top(void);

/* loop: the login screen is the top again: its user name prompt starts. */
void sess_resume(void);

/*
 * any: the user database, read only. NULL when no database loaded. For
 * the minigame and an AUTHORIZE-style listing. Nothing writes SYSUAF.DAT
 * yet. Never print a salt or a hash from it.
 */
const uaf_db_t *sess_db(void);

/* any: the record of `name`, compared case-insensitively, or NULL. Read
 * only, as sess_db(). */
const uaf_rec_t *sess_find(const char *name);

#endif
