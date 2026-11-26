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
#include "sess.h"
#include "uos_screen.h"

/* Errors of sh_run_line() and sh_program_register(). */
#define SH_ERR_ARG      (-1)    /* NULL, empty or comment only, or longer than DCL_LINE_MAX */
#define SH_ERR_BUSY     (-2)    /* a command is running */
#define SH_ERR_NOUSER   (-3)    /* nobody is logged in */
#define SH_ERR_DOWN     (-4)    /* the shell service has not started */
#define SH_ERR_DUP      (-5)    /* the program name is registered already */
#define SH_ERR_FULL     (-6)    /* SH_PROG_MAX programs already */
#define SH_ERR_COVERED  (-7)    /* another screen covers the shell */

/* Flags of sh_run_line(). */
#define SH_RUN_QUIET    0x01u   /* print no output or message on the console: only the result
                                   line counts; a question the command asks still shows */
#define SH_RUN_CAPTURE  0x02u   /* print nothing on the console; keep the output and the
                                   messages for the callback, see sh_output() */
#define SH_CAPTURE_MAX  (64u * 1024u)   /* bytes kept by SH_RUN_CAPTURE; the rest is cut */

/* Severity classes given to the callback. */
#define SH_OK           0       /* the final message is S or I */
#define SH_WARN         1       /* W */
#define SH_FAIL         2       /* E or F */
#define SH_CANCELLED    3       /* Ctrl-C, or the session ended while it ran; also the
                                   status a program gives done() for Ctrl-C */

/*
 * Called exactly once when a command that sh_run_line() started ends,
 * never after an error return. `result` is the final message's
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
 * read on top of the caller's screen with con_read_line(). Ctrl-C at a
 * question, in the pager of TYPE /PAGE or at the shell ends the command
 * with SH_CANCELLED. A missing parameter or a bad line runs as a command
 * that fails: only the shell prompt asks for a parameter. `cb` may be
 * NULL. Returns 0 when the command started (cb follows, maybe before this
 * returns), or SH_ERR_DOWN, SH_ERR_ARG, SH_ERR_BUSY, SH_ERR_NOUSER,
 * SH_ERR_COVERED, checked in that order, with no callback. SH_ERR_COVERED:
 * the shell screen is not on the stack yet, such as during the welcome, or
 * another screen covers the shell, so no command, question or program
 * starts. A command that runs when a screen opens over the shell waits:
 * its next step runs once the shell is on top again. The prompt treats an
 * empty or comment-only line itself: it shows a new prompt.
 */
int sh_run_line(const char *line, unsigned flags, sh_done_fn cb, void *ctx);

/*
 * loop: as sh_run_line(), for a screen that is not the shell, such as the
 * terminal menu. The job belongs to `host`: its questions and its pager
 * open over `host`, and the job is covered while another screen covers
 * the unit of `host`. A covered job waits as it does under the shell:
 * the host's uncover() calls sh_host_uncover(), which runs the owed step
 * or end. The host's leave() calls sh_host_gone(). SH_ERR_COVERED when
 * the unit of `host` is not the top. The shell itself need not be on the
 * stack.
 */
int sh_run_line_on(uos_screen_t *host, const char *line, unsigned flags, sh_done_fn cb,
                   void *ctx);

/* loop: the host's uncover() calls it. Runs the step or the end the job
 * of `host` owes, once. 1 when it did, else 0. */
int sh_host_uncover(uos_screen_t *host);

/* loop: the host's leave() calls it. A job of `host` becomes an orphan:
 * it takes no step and prints nothing, and the next tick ends it. */
void sh_host_gone(uos_screen_t *host);

/* loop: Ctrl-C for the running job, as at the shell. Does nothing with no
 * job. */
void sh_cancel(void);

/* loop, inside an sh_done_fn only: the text SH_RUN_CAPTURE kept, lines
 * ended by LF, the final message last. NULL with *len 0 otherwise. */
const char *sh_output(size_t *len);

/*
 * loop: registers the function the shell asks first at each login for
 * the session's screen. NULL from it gives the shell. Once per boot:
 * SH_ERR_DUP after. SH_ERR_DOWN before the shell service started.
 */
int sh_session_screen_register(sess_next_fn fn);

/* any: non-zero while a command runs. */
int sh_busy(void);

/*
 * loop: registers a program for RUN. `name` is 1 to 12 of A-Z 0-9 $ _,
 * any case; the RUN verb accepts the names of the command table: HACK,
 * TERMINAL and HOLOTAPE. The menu, holo and hack services register
 * those three. The HOLOTAPE verb runs the HOLOTAPE program with its
 * tape name, or "", as `args`; RUN passes "".
 *
 * start(args, done, tag) runs from the loop when RUN names the program.
 * It pushes its own screen and returns 0, then calls done(tag, status,
 * text) once when it ends, with the `tag` it received: status 0 for
 * success, SH_CANCELLED when the person pressed Ctrl-C, else a failure;
 * text is one result line or NULL. A program that pushes a screen owns
 * the keys, so its screen must end the run with SH_CANCELLED on Ctrl-C
 * (the character 3); without a screen, the shell takes Ctrl-C itself. The tag names
 * the run: a done() that comes after its run was cancelled, even while a
 * later RUN waits, is ignored with an "ev=sh stale=prog" marker. A
 * non-zero return means it did not start and done is not called. A program whose name has no entry answers
 * "%UOS-W-NOTINST, <NAME> is not installed in this build".
 * Call it once the shell service started, from a later service's init or
 * after. Returns 0, SH_ERR_ARG (bad name or NULL start), SH_ERR_DOWN,
 * SH_ERR_DUP or SH_ERR_FULL.
 */
int sh_program_register(const char *name, sh_prog_start_fn start);

#endif
