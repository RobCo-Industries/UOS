/*
 * The holotape reader screen. Prefix holo_. The shell's RUN verb or a
 * terminal menu starts it with holo_screen_start().
 *
 * Context: loop only (a screen callback, a timer, a posted call). Never
 * from a maintenance command function directly, an IRQ or the panic path.
 */
#ifndef UOS_HOLOSCR_H
#define UOS_HOLOSCR_H

#define HOLO_ERR_BUSY       (-1)    /* the reader is already open */
#define HOLO_ERR_SCREEN     (-2)    /* the screen stack is full */

/* Called once when the person leaves the reader, after its screen popped. */
typedef void (*holo_done_fn)(void *ctx);

/*
 * Pushes the reader on the screen stack. It scans SYS$HOLO, one tape per
 * loop pass, then lists the tapes. Reads go through the file system with
 * the logged-in user's rights, or a user's rights with no privilege when
 * nobody is logged in. `done` may be NULL. Returns 0, HOLO_ERR_BUSY or
 * HOLO_ERR_SCREEN.
 */
int holo_screen_start(holo_done_fn done, void *ctx);

/*
 * As holo_screen_start(), then opens tape `tape` once the scan ends, as
 * ENTER on it would: its entry list shows. `tape` is an 8.3 name, any
 * case; with no type it names a .HTP file. NULL or "" is the list. A tape
 * the list does not hold leaves the list shown with "No holotape NAME".
 */
int holo_screen_start_tape(const char *tape, holo_done_fn done, void *ctx);

/* Non-zero while the reader is on the stack. Any context. */
int holo_screen_active(void);

#endif
