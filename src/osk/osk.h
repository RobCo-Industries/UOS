#ifndef UOS_OSK_H
#define UOS_OSK_H

/*
 * On-screen keyboard model (pure). A grid of keys at the bottom of the
 * 80x30 console for text entry with arrow, select and back buttons.
 * It consumes library input events and emits library input events into
 * its own output queue, which the caller drains into the line editor.
 */

#include <stddef.h>
#include <stdint.h>

#include "robco/input.h"

/* Layout. */
#define OSK_LAYERS          2u      /* SHIFT cycles 0, 1, 0, ... */
#define OSK_CHAR_ROWS       4u      /* rows of character keys */
#define OSK_CHAR_COLS       13u     /* character keys per row */
#define OSK_FUNC_KEYS       6u      /* keys on the function row */
#define OSK_ROWS            5u      /* screen rows covered: 4 character rows, 1 function row */
#define OSK_FUNC_ROW        4u      /* row index of the function row */
#define OSK_KEYS            (OSK_CHAR_ROWS * OSK_CHAR_COLS + OSK_FUNC_KEYS) /* 58 per layer */
#define OSK_CHAR_W          6u      /* cell width of a character key */
#define OSK_FUNC_W          13u     /* cell width of a function key */
#define OSK_LEFT_MARGIN     1u      /* first column of the grid */
#define OSK_TEXT_COLS       80u     /* characters in one drawn row */
#define OSK_ROW_BUF         (OSK_TEXT_COLS + 1u) /* bytes osk_row_text() needs */

/* Function keys, in their fixed order on the function row. */
#define OSK_FN_SHIFT        0u
#define OSK_FN_BACKSPACE    1u
#define OSK_FN_LEFT         2u
#define OSK_FN_RIGHT        3u
#define OSK_FN_ENTER        4u
#define OSK_FN_CLOSE        5u

/* Output queue. */
#define OSK_OUT_MAX         16u     /* events the queue holds */
#define OSK_SRC             3u      /* source field of emitted events */

/* Results of osk_handle(). */
#define OSK_IGNORED         0       /* not for the keyboard; the caller handles it */
#define OSK_CONSUMED        1       /* the keyboard used the event */

/* Errors. */
#define OSK_ERR_FULL        (-1)    /* output queue lacks room; nothing emitted */
#define OSK_ERR_ARG         (-2)    /* bad argument */
#define OSK_ERR_SPACE       (-3)    /* caller buffer too small; nothing written */
#define OSK_ERR_SCREEN      (-4)    /* screen has fewer rows than the keyboard */

typedef struct {
    uint8_t open;
    uint8_t password;
    uint8_t layer;
    uint8_t row;            /* 0 to OSK_FUNC_ROW */
    uint8_t col;            /* 0 to 12 on character rows, 0 to 5 on the function row */
    uint8_t head;           /* output queue, next read */
    uint8_t count;          /* output queue, events held */
    input_event_t out[OSK_OUT_MAX];
} osk_t;

typedef struct {
    uint8_t x;              /* first console column */
    uint8_t y;              /* row inside the keyboard, 0 to OSK_ROWS - 1 */
    uint8_t w;              /* width in cells; height is always 1 */
    uint8_t highlighted;
    uint8_t is_func;
    uint8_t ch;             /* character typed, 0 for a function key */
    uint8_t fn;             /* OSK_FN_*, valid when is_func */
} osk_cell_t;

/* Closed, layer 0, highlight on A, empty queue, password off. */
void osk_init(osk_t *k);
/* Opens at layer 0 with the highlight on A. password: mask the edited line. Clears the queue. */
void osk_open(osk_t *k, int password);
/* Closes. Events already queued stay for the caller to drain. */
void osk_close(osk_t *k);
int osk_is_open(const osk_t *k);
/* Non-zero when the line being edited must be shown masked. */
int osk_password(const osk_t *k);
unsigned osk_layer(const osk_t *k);
void osk_highlight(const osk_t *k, unsigned *row, unsigned *col);

/*
 * Feeds one input event. When closed, or for a CHAR or DIAL event, returns
 * OSK_IGNORED. Open KEY events are consumed: arrows move (press and repeat),
 * SELECT acts on the highlighted key (press; repeat only on BACKSPACE, LEFT
 * and RIGHT), BACK closes (press), releases do nothing. Returns OSK_CONSUMED,
 * OSK_ERR_FULL when an emit did not fit (the state does not change), or
 * OSK_ERR_ARG.
 */
int osk_handle(osk_t *k, const input_event_t *ev);

/* Removes the oldest emitted event into *ev. Returns 1, or 0 when empty. */
int osk_pop(osk_t *k, input_event_t *ev);
unsigned osk_pending(const osk_t *k);

/*
 * Fills buf with keyboard row `row` (0 to OSK_ROWS - 1) of the current
 * layer: OSK_TEXT_COLS characters and a NUL. Returns OSK_TEXT_COLS,
 * OSK_ERR_SPACE when size < OSK_ROW_BUF, or OSK_ERR_ARG.
 */
int osk_row_text(const osk_t *k, unsigned row, char *buf, size_t size);

/* Describes key `index` (0 to OSK_KEYS - 1) of the current layer. Returns 0 or OSK_ERR_ARG. */
int osk_key_cell(const osk_t *k, unsigned index, osk_cell_t *cell);

/*
 * Screen rows the keyboard covers on a console of `screen_rows` rows:
 * the bottom OSK_ROWS rows when open, none when closed (*count = 0).
 * Returns 0, OSK_ERR_SCREEN when screen_rows < OSK_ROWS, or OSK_ERR_ARG.
 */
int osk_screen_rows(const osk_t *k, unsigned screen_rows, unsigned *first, unsigned *count);

/* Character of a character key, 0 if out of range. Layout data, for tests and docs. */
uint8_t osk_layout_char(unsigned layer, unsigned row, unsigned col);

/* One line of text for an OSK_* result or error. */
const char *osk_strerror(int code);

#endif
