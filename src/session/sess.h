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

/*
 * any: the user database, read only. NULL when no database loaded. For
 * the minigame and an AUTHORIZE-style listing. The shell's AUTHORIZE and
 * SET PASSWORD write SYSUAF.DAT, then call sess_reload(). Never print a
 * salt or a hash from it.
 */
const uaf_db_t *sess_db(void);

/* any: the record of `name`, compared case-insensitively, or NULL. Read
 * only, as sess_db(). */
const uaf_rec_t *sess_find(const char *name);

/*
 * loop: reads /UOS/SYSUAF.DAT again, after AUTHORIZE or SET PASSWORD
 * wrote it. The session goes on with the privileges it started with; a
 * change applies at the next login. Prints the sessdb marker again.
 * 0, or SESS_ERR_DOWN when the file no longer loads: then no
 * one can log in after this session, as at boot.
 */
int sess_reload(void);

#endif
