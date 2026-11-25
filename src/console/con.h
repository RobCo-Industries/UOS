/*
 * The console service: screen services every later screen uses to read
 * a line from a person, to show a long text, and to print. Prefix con_.
 * The library's own console API is console_*; this sits on top of it.
 *
 * Context: every function runs in the main loop only (a screen
 * callback, a timer, a posted call or a service init after the screen
 * step). None may run from a maintenance command function directly:
 * post to the loop first. None may run from the panic path.
 *
 * Every function that draws does nothing on the screen when there is no
 * framebuffer console; reading and paging still work and still call back.
 *
 * The read screen and the pager are screens on the kernel's stack. A read
 * may start while the pager is shown, and a pager while a line is read:
 * the newer screen takes the keys until it pops, then the one below has
 * them again with its state kept.
 */
#ifndef UOS_CON_H
#define UOS_CON_H

#include <stddef.h>
#include <stdint.h>

#include "dcl.h"
#include "lineedit.h"

/* Errors. */
#define CON_ERR_ARG     (-1)    /* NULL, empty, or a number out of range */
#define CON_ERR_BUSY    (-2)    /* a read while a read is active, or a pager while a pager is */
#define CON_ERR_DOWN    (-3)    /* the service did not start */
#define CON_ERR_SCREEN  (-4)    /* the screen stack is full */
#define CON_ERR_MEM     (-5)    /* the kernel heap refused a buffer */

/* Results given to the callbacks. */
#define CON_OK          0       /* the person accepted the line, or left the pager */
#define CON_CANCEL      1       /* the person pressed Ctrl-C in a read or the pager */

/* ---- Read a line ---- */

/* Flags of con_read_line(). */
#define CON_RL_MASK     LE_F_MASK       /* a password: '*' shown, never stored */
#define CON_RL_NOHIST   LE_F_NOHIST     /* neither stored in nor read from the history */

/*
 * Called once when the read ends. `result` is CON_OK or CON_CANCEL.
 * `text` holds `len` characters and a NUL (empty on CON_CANCEL); the
 * console owns it and it stays valid only until the callback returns.
 * The read screen is already popped, so the callback may start another
 * read or push a screen.
 */
typedef void (*con_line_fn)(int result, const char *text, unsigned len, void *ctx);

/*
 * Pushes a screen that reads one line. The prompt (cut to 40
 * characters, a blank added when it does not end in one) and the line go on the row of the console cursor, or the
 * next row when the cursor is not in column 0; a row past the last one
 * above the status row scrolls the screen up. `max` 1 to 255 characters.
 * Keys are those of lineedit.h. F4, or any GPIO button, opens the
 * on-screen keyboard. Returns 0, CON_ERR_ARG, CON_ERR_BUSY (one read at a
 * time), CON_ERR_DOWN or CON_ERR_SCREEN. `prompt` is copied.
 */
int con_read_line(const char *prompt, unsigned flags, unsigned max, con_line_fn cb, void *ctx);

/* Non-zero while a line is being read. */
int con_line_active(void);

/* Opens or closes the on-screen keyboard of the active read. 0, or
 * CON_ERR_BUSY when no line is being read. */
int con_osk_open(void);
int con_osk_close(void);

/* The keyboard's state for listings: open, layer, highlight, input row. */
void con_osk_state(int *open, unsigned *layer, unsigned *row, unsigned *col,
                   unsigned *line_row);

/* ---- Page a text ---- */

/* Texts longer than this are refused. */
#define CON_PAGE_MAX    (1024u * 1024u)

/* Called once when the person leaves the pager: CON_OK for the keys that
 * end it, CON_CANCEL for Ctrl-C. */
typedef void (*con_done_fn)(int result, void *ctx);

/*
 * Pushes a full-screen pager over `text` of `len` bytes: a title row
 * (cut to 78), the body, and a status row "line A to B of N" with the
 * keys. Bytes outside printable ASCII show as '.'. The console copies
 * the text into the kernel heap, so the caller may free it once this
 * returns. An empty text shows "(no text)". Returns 0, CON_ERR_ARG,
 * CON_ERR_BUSY (one pager at a time), CON_ERR_DOWN, CON_ERR_MEM or
 * CON_ERR_SCREEN.
 */
int con_page_text(const char *title, const char *text, size_t len, con_done_fn cb, void *ctx);

/* Non-zero while a pager is shown. */
int con_page_active(void);

/* ---- Output ---- */

/*
 * Prints `text` and a new line at the cursor with a CONSOLE_ATTR_*
 * attribute, and the same line on the serial console. The screen
 * scrolls at the bottom; the status row is kept.
 */
void con_print(const char *text, unsigned attr);

/*
 * Prints a message of the command language, "%FAC-S-IDENT, text", as
 * dcl_format_message() formats it, one con_print() per line. Returns
 * the length or CON_ERR_ARG.
 */
int con_message(dcl_msg_t id, const char *arg, uint32_t num);

/* Blanks `count` rows from `first`; rows past the bottom are skipped. */
void con_clear_rows(unsigned first, unsigned count);

/*
 * Sets the status row, the bottom row, in inverse, cut to 79 characters.
 * It stays until con_status_clear(); con_print() keeps it. The text is
 * copied.
 */
void con_status_set(const char *text);
void con_status_clear(void);
/* Draws the status row again when one is set. A screen whose draw()
 * clears the console calls it last. */
void con_status_redraw(void);
/* The status text, "" when none. */
const char *con_status_text(void);
/* The rows text may use: all but the status row while one is set. */
unsigned con_text_rows(void);

/*
 * Pushes a screen that shows `text` (NULL: "Press a key") on the status
 * row and waits for any key or character, which it uses. Then it pops,
 * restores the status row and calls cb(CON_OK, ctx). Returns 0,
 * CON_ERR_BUSY while another pause waits, CON_ERR_DOWN or CON_ERR_SCREEN.
 */
int con_pause(const char *text, con_done_fn cb, void *ctx);

/* ---- Service ---- */

/* Non-zero once the service started. */
int con_ready(void);

#endif
