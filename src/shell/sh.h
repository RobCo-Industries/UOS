/*
 * The command shell: the DCL prompt after login, and the one way for other
 * screens to run a command line. Prefix sh_. The rules are pure, in
 * shrules.c; the screen and the executor are shell.c; the commands are
 * shcmds.c and shuaf.c.
 *
 * Context rules, per function:
 *   loop  the main loop only: a screen callback, a timer, a posted call or
 *         a service init. Never from a maintenance command function
 *         directly (post first), an IRQ or the panic path.
 *
 * One command runs at a time, for the shell prompt and every caller
 * together. A command never blocks the loop: a long one runs in steps
 * posted to the loop.
 */
#ifndef UOS_SH_H
#define UOS_SH_H

#include "shrules.h"

/* Errors of sh_run_line() and sh_program_register(). */
#define SH_ERR_ARG      (-1)    /* NULL, empty, or longer than DCL_LINE_MAX */
#define SH_ERR_BUSY     (-2)    /* a command is running */
#define SH_ERR_NOUSER   (-3)    /* nobody is logged in */
#define SH_ERR_DOWN     (-4)    /* the shell service did not start */
#define SH_ERR_DUP      (-5)    /* the program name is registered already */
#define SH_ERR_FULL     (-6)    /* SH_PROG_MAX programs already */

/* Flags of sh_run_line(). */
#define SH_RUN_QUIET    0x01u   /* print nothing on the console: only the result line counts */

/* Severity classes given to the callback. */
#define SH_OK           0       /* the final message is S or I */
#define SH_WARN         1       /* W */
#define SH_FAIL         2       /* E or F */
#define SH_CANCELLED    3       /* Ctrl-C, or the session ended while it ran */

/*
 * Called once when the command ends. `result` is the final message's
 * first line, "%FAC-S-IDENT, text", valid only during the call. The
 * shell is free again when it runs: the callback may start another
 * command.
 */
typedef void (*sh_done_fn)(int status, const char *result, void *ctx);

/*
 * loop: runs one command line as the logged-in user, with the shell's
 * current default folder. The line is parsed with the UOS table, its
 * privileges checked against the session's mask, and the command run in
 * steps. Questions a command asks, such as /CONFIRM or a password, are
 * read on top of the caller's screen with con_read_line(). A missing
 * parameter is an error here: only the shell prompt asks for it. Returns
 * 0 when the command started (cb follows, maybe before this returns), or
 * SH_ERR_ARG, SH_ERR_BUSY, SH_ERR_NOUSER, SH_ERR_DOWN with no callback.
 */
int sh_run_line(const char *line, unsigned flags, sh_done_fn cb, void *ctx);

/* any: non-zero while a command runs. */
int sh_busy(void);

/*
 * loop: registers a program for RUN. `name` is 1 to 12 of A-Z 0-9 $ _,
 * any case; the RUN verb accepts the names of the command table: HACK,
 * TERMINAL and HOLOTAPE. The HOLOTAPE verb runs the HOLOTAPE program
 * with its parameters as `args`.
 *
 * start(args, done) runs from the loop when RUN names the program. It
 * pushes its own screen and returns 0, then calls done(status, text) once
 * when it ends: status 0 for success, else a failure; text is one result
 * line or NULL. A non-zero return means it did not start and done is not
 * called. A program whose name has no entry answers
 * "%UOS-W-NOTINST, <NAME> is not installed in this build".
 * Returns 0, SH_ERR_ARG, SH_ERR_DUP or SH_ERR_FULL.
 */
int sh_program_register(const char *name, sh_prog_start_fn start);

#endif
