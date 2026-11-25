/*
 * The password game screen's own decisions, pure, prefix hg_: the screen
 * layout, the text of each row, the keys, the settings check and the
 * screen's state machine. The game itself is hack.h; the screen is
 * hackscr.c.
 */
#ifndef UOS_HACKUI_H
#define UOS_HACKUI_H

#include <stddef.h>
#include <stdint.h>

#include "hack.h"

/* Screen rows. The board takes HK_ROWS rows from HG_ROW_BOARD. */
#define HG_ROW_TITLE        0u
#define HG_ROW_TARGET       1u
#define HG_ROW_ATTEMPTS     3u
#define HG_ROW_BOARD        5u
#define HG_ROW_CURSOR       (HG_ROW_BOARD + HK_ROWS + 1u)   /* 22 */
#define HG_ROW_MSG          (HG_ROW_CURSOR + 1u)            /* 23 */
#define HG_ROW_KEYS         (HG_ROW_MSG + 1u)               /* 24 */
#define HG_ROWS             (HG_ROW_KEYS + 1u)

/* Screen columns of a board row: address, a blank, 12 cells; two blanks;
 * the same for the right column; three blanks; the history line. */
#define HG_COL_CELLS0       7u
#define HG_COL_CELLS1       28u
#define HG_COL_HISTORY      43u
#define HG_LINE_SIZE        81u     /* 80 characters and a NUL */

/* Where cell `i` of the board (0 to 383) is drawn. HK_ERR_ARG past the end. */
int hg_cell_pos(unsigned i, unsigned *row, unsigned *col);

/*
 * Board row `r` (0 to 15) as drawn: both columns with their addresses,
 * then the history line that belongs on that row. History lines sit at
 * the bottom, the newest on row 15. Returns the length, or -1.
 */
int hg_board_line(const hk_game_t *g, unsigned r, char *buf, size_t size);

/* The board columns of row `r` only, no history: "0xF964 ABCD.. 0xFA24 ...". -1 on error. */
int hg_board_cells(const hk_game_t *g, unsigned r, char *buf, size_t size);

/* "ATTEMPTS LEFT: 3  # # #". Length or -1. */
int hg_attempts_line(unsigned left, char *buf, size_t size);

/* "> " and the highlighted text: a word, a bracket pair, or one cell. Length or -1. */
int hg_cursor_line(const hk_game_t *g, char *buf, size_t size);

/* The fixed rows: title, target ("TARGET ACCOUNT: GUEST"), keys. Length or -1. */
int hg_title_line(char *buf, size_t size);
int hg_target_line(const char *user, char *buf, size_t size);
int hg_keys_line(char *buf, size_t size);

/* What a key does in the game. */
typedef enum hg_key {
    HG_K_NONE = 0,
    HG_K_MOVE,
    HG_K_SELECT,
    HG_K_BACK,
} hg_key_t;

/* A KEY event's meaning: press and repeat move and select, a release does
 * nothing; BACK only on a press. `code` is an INPUT_KEY_*. */
hg_key_t hg_key(unsigned code, int value, hk_dir_t *dir);

/*
 * The game's settings from UOS.CFG [hack] values, checked again against
 * hack.h's ranges: HK_ERR_ARG and *out untouched when one is out of range.
 */
int hg_config(unsigned attempts, unsigned lockout_s, uint32_t ticks_per_s, hk_config_t *out);

/* "/UOS/WORDS/W07.TXT" for a length of 4 to 12. -1 for another length. */
int hg_words_path(unsigned len, char *buf, size_t size);

/* ---- The screen's states ---- */

typedef enum hg_state {
    HG_S_OFF = 0,           /* not on the stack */
    HG_S_ERROR,             /* the game cannot run: a message, BACK leaves */
    HG_S_PLAYING,
    HG_S_WON,               /* the password: a pause, then the session starts */
    HG_S_LOST,              /* no attempt left: a pause, then the lockout */
    HG_S_NUM
} hg_state_t;

typedef enum hg_event {
    HG_E_START_OK = 0,      /* the board is built */
    HG_E_START_FAIL,        /* refused, or the word list is missing or damaged */
    HG_E_WIN,
    HG_E_LOSE,
    HG_E_BACK,              /* the person leaves */
    HG_E_PAUSE_END,         /* the pause after a win or a loss is over */
    HG_E_NUM
} hg_event_t;

/* The next state; an event that does not apply keeps the state. */
hg_state_t hg_next(hg_state_t s, hg_event_t e);

/* "off", "error", "playing", "won", "lost". */
const char *hg_state_name(hg_state_t s);

/* "none", "denied", "win", "lockout", "dud", "reset". */
const char *hg_event_name(hk_event_t e);

/* The message row after a start error: the word list, or the game's own code. */
typedef enum hg_start {
    HG_START_OK = 0,
    HG_START_NOCARD,        /* no card, or no /UOS folder */
    HG_START_NOLIST,        /* the list for the chosen length is missing */
    HG_START_BADLIST,       /* it does not parse, or holds too few words */
    HG_START_NOMEM,         /* the heap has no room for it */
    HG_START_CONFIG,        /* the settings were refused */
    HG_START_NUM
} hg_start_t;

/* The start result for a read's return code (VFS_OK or VFS_E_*) and the
 * parse or pick code (HK_OK or HK_ERR_*). */
hg_start_t hg_start_result(int read_rc, int hk_rc);

/* "ok", "nocard", "nolist", "badlist", "nomem", "config". */
const char *hg_start_name(hg_start_t s);

/* One line for the message row; the list's path when `path` is not NULL. */
int hg_start_text(hg_start_t s, const char *path, char *buf, size_t size);

#endif
