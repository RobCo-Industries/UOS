/*
 * The console service's decisions (pure, prefix cv_): where the input
 * line goes, when the on-screen keyboard opens, the pager's status row,
 * the byte filter of the pager, and the texts of the maintenance
 * commands. The thin files in src/console/ only draw what these say.
 */
#ifndef UOS_CONVIEW_H
#define UOS_CONVIEW_H

#include <stddef.h>
#include <stdint.h>

#include <robco/input.h>

#include "tui.h"

/* The key that opens the on-screen keyboard from a serial terminal. */
#define CV_OSK_KEY          INPUT_KEY_F4

/* The status row of the pager: at most this many characters. */
#define CV_STATUS_MAX       78u
#define CV_STATUS_BUF       (CV_STATUS_MAX + 1u)

/* Prompts are cut to this many characters. */
#define CV_PROMPT_MAX       40u

/* Flags of the `readline` maintenance command, as for le_init(). */
#define CV_ERR_ARG          (-1)

/*
 * The row of the input line. `want` is the row asked for; while the
 * keyboard covers `count` rows from `first`, a row under it moves to
 * the row just above it. Returns the row.
 */
unsigned cv_input_row(unsigned want, unsigned first, unsigned count);

/*
 * Non-zero when `ev` opens the keyboard: the keyboard is closed and the
 * event is a KEY press or repeat from a GPIO button, or a press of
 * CV_OSK_KEY from any source.
 */
int cv_opens_osk(const input_event_t *ev, int osk_open);

/* The key that ends a read with CON_ALT when the caller asked for it. */
#define CV_ALT_KEY          INPUT_KEY_F2

/* The GPIO button that does the same: the panel has no F2. */
#define CV_ALT_BUTTON       INPUT_KEY_BACK

/*
 * Non-zero when `ev` ends the read with CON_ALT: the read has the alt key
 * flag (`alt` non-zero), the line is empty, the keyboard is closed and the
 * event is a press of CV_ALT_KEY. A GPIO button has no F2: there the key
 * is CV_ALT_BUTTON, and any other button opens the keyboard. A BACK that
 * does not come from a button never ends the read with CON_ALT. On a line
 * with text, F2 keeps its meaning: the end of the line.
 */
int cv_alt_key(const input_event_t *ev, int alt, unsigned len, int osk_open);

/*
 * The columns the line gets after a prompt of `prompt_len` characters on
 * a row of `cols` columns: the last column stays free. At least
 * LE_MIN_WIDTH, at most LE_MAX_WIDTH. The prompt is cut to CV_PROMPT_MAX
 * first.
 */
unsigned cv_line_width(size_t prompt_len, unsigned cols);

/*
 * The pager's status row: "line A to B of N" then the key hints, cut to
 * CV_STATUS_MAX. out needs CV_STATUS_BUF bytes. Returns the length or
 * CV_ERR_ARG.
 */
int cv_pager_status(const pg_t *p, char *out, size_t size);

/*
 * Copies `len` bytes, each byte outside printable ASCII as '.', except
 * tab, LF and CR, which the pager reads as layout.
 */
void cv_printable(char *dst, const char *src, size_t len);

/*
 * Writes `len` bytes of text in double quotes into out of `size` bytes,
 * NUL-terminated, for a marker: a space, a quote, '%' and every byte
 * outside printable ASCII become %XX in upper-case hex, so the field
 * holds no blank. Stops before `limit` text bytes or before out is full,
 * and closes the quote. Returns 1 when the text was cut, 0 when whole,
 * CV_ERR_ARG when size < 3.
 */
int cv_quote(char *out, size_t size, const char *text, size_t len, size_t limit);

/*
 * Reads the flags word of `readline`: "-" for none, else letters, 'm'
 * masked and 'h' no history, each once. Returns 0 with *flags as LE_F_*,
 * or CV_ERR_ARG.
 */
int cv_parse_flags(const char *word, unsigned *flags);

/* Reads a decimal 1 to `max`. Returns 0 with *out, or CV_ERR_ARG. */
int cv_parse_count(const char *word, unsigned max, unsigned *out);

#endif
