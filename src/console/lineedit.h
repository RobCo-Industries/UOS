/*
 * The line editor (pure, prefix le_). An event-driven model of one line
 * being typed: it takes one library input event at a time, edits a
 * buffer the caller owns, and says what to redraw. It never blocks and
 * never draws. A history ring the caller owns keeps the last lines.
 *
 * Keys, from the library's serial mapping (CR gives SELECT, BS, DEL and
 * a lone ESC give BACK, so BACK is backspace and cannot be cancel):
 *
 *   printable CHAR       insert, or overwrite in overwrite mode
 *   SELECT               accept (press only, a repeat does nothing)
 *   BACK                 delete the character before the cursor
 *   LEFT, RIGHT          move one character        Ctrl-B, Ctrl-F
 *   F1, F2               start, end of the line     Ctrl-A, Ctrl-E
 *   F3                   delete under the cursor    Ctrl-D
 *   UP, DOWN             older, newer history line  Ctrl-P, Ctrl-N
 *   Ctrl-K               kill to the end of the line
 *   Ctrl-O               insert and overwrite modes
 *   Ctrl-C               cancel
 *
 * Repeats act for LEFT, RIGHT, UP, DOWN, BACK and F3, not for SELECT,
 * F1 and F2. Releases, dials, F4 and other characters change nothing.
 */
#ifndef UOS_LINEEDIT_H
#define UOS_LINEEDIT_H

#include <stddef.h>
#include <stdint.h>

#include <robco/input.h>

#define LE_MAX          255u    /* characters in a line */
#define LE_HIST         16u     /* lines the history keeps */
#define LE_MIN_WIDTH    4u      /* narrowest visible window */
#define LE_MAX_WIDTH    80u

/* Control characters the editor reads, as INPUT_EV_CHAR codes. */
#define LE_CTRL(c)      ((uint16_t)((c) & 0x1f))

/* Flags of le_init(). */
#define LE_F_MASK       0x1u    /* shown as '*', never stored, no recall */
#define LE_F_NOHIST     0x2u    /* not stored, no recall */

/* Results of le_event(). */
#define LE_EDITING      0
#define LE_ACCEPT       1
#define LE_CANCEL       2

/* What to redraw after an event. */
#define LE_RD_NONE      0       /* nothing changed */
#define LE_RD_CURSOR    1       /* only the cursor moved */
#define LE_RD_FROM      2       /* window columns from `from` to the end */
#define LE_RD_ALL       3       /* the whole window */

#define LE_ERR_ARG      (-1)

typedef struct {
    uint8_t what;               /* LE_RD_* */
    uint8_t from;               /* first window column, for LE_RD_FROM */
    uint8_t refused;            /* the key was refused: full line, empty history end */
} le_redraw_t;

/* The newest line is entry 0. */
typedef struct {
    char line[LE_HIST][LE_MAX + 1];
    uint8_t head;               /* slot of the next store */
    uint8_t count;              /* 0 to LE_HIST */
} le_history_t;

typedef struct {
    char *buf;                  /* the caller's, max + 1 bytes, always NUL-terminated */
    uint16_t max;               /* 1 to LE_MAX */
    uint16_t len;
    uint16_t cur;               /* 0 to len */
    uint16_t scroll;            /* first character in the window */
    uint8_t width;              /* window columns */
    uint8_t flags;              /* LE_F_* */
    uint8_t overwrite;
    uint8_t state;              /* LE_EDITING, LE_ACCEPT or LE_CANCEL */
    int8_t hpos;                /* -1: the new line, else the history entry shown */
    le_history_t *hist;         /* may be NULL */
    char saved[LE_MAX + 1];     /* the new line while the history is shown */
    uint16_t saved_len;
} le_t;

void le_hist_init(le_history_t *h);
unsigned le_hist_count(const le_history_t *h);
/* Entry i, 0 the newest, or NULL. */
const char *le_hist_get(const le_history_t *h, unsigned i);
/*
 * Stores a line. An empty line, one equal to the newest entry, or one
 * longer than LE_MAX is not stored. The 17th line drops the oldest.
 * Returns 1 when stored, else 0.
 */
int le_hist_add(le_history_t *h, const char *text, size_t len);

/*
 * Starts a line in `buf` of `size` bytes, emptied. `max` 1 to LE_MAX and
 * at most size - 1. `width` LE_MIN_WIDTH to LE_MAX_WIDTH is the number of
 * screen columns the line has. Returns 0 or LE_ERR_ARG.
 */
int le_init(le_t *e, char *buf, size_t size, unsigned max, unsigned flags,
            le_history_t *hist, unsigned width);

/*
 * Feeds one input event. Writes what to redraw into *rd (may be NULL).
 * Returns LE_EDITING, LE_ACCEPT or LE_CANCEL. Once accepted or cancelled
 * the line stays so and every event does nothing. Accepting stores the
 * line in the history unless it is masked or LE_F_NOHIST.
 */
int le_event(le_t *e, const input_event_t *ev, le_redraw_t *rd);

/* Convenience: a KEY press or a CHAR, as from the given source. */
int le_key(le_t *e, unsigned key, le_redraw_t *rd);
int le_char(le_t *e, unsigned c, le_redraw_t *rd);

/*
 * The window as shown: exactly `width` characters and a NUL in out,
 * which needs width + 1 bytes. '*' per character when masked. When the
 * line does not fit, '<' in the first column means text is cut on the
 * left and '>' in the last column text cut on the right. *col (may be
 * NULL) gets the cursor's window column. Returns width, or LE_ERR_ARG.
 */
int le_display(const le_t *e, char *out, size_t size, unsigned *col);

/* The cursor's window column. */
unsigned le_cursor_col(const le_t *e);
unsigned le_len(const le_t *e);
int le_state(const le_t *e);

#endif
