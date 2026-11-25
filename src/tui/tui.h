#ifndef UOS_TUI_H
#define UOS_TUI_H

/*
 * The menu-driven terminal screen. Pure: no files, no hardware, no
 * allocation, no globals. Three parts:
 *   tuicfg.c  parses TERMINAL.CFG text into a tui_menu_t
 *   tui.c     the screen model: a stack of menus, keys in, actions out,
 *             and the layout of the 80 by 30 screen in character cells
 *   pager.c   a pager over a text buffer the caller owns (prefix pg_)
 */

#include <stddef.h>
#include <stdint.h>

#include "uos_privs.h"

#include <robco/input.h>

/* ---- Limits ---- */

#define TUI_MAX_ENTRIES     16      /* entries kept per menu file */
#define TUI_ENTRY_NUM_MAX   999     /* N in [entry N] is 1 to 999 */
#define TUI_TITLE_MAX       60      /* characters in a title */
#define TUI_LABEL_MAX       60      /* characters in a label */
#define TUI_COMMAND_MAX     127     /* characters in a command target */
#define TUI_FILE_MAX        12      /* an 8.3 name: 8 + '.' + 3 */
#define TUI_DEVNAME_MAX     15      /* characters in a device name */
#define TUI_DEVWORD_MAX     15      /* characters in a device action word */
#define TUI_TARGET_MAX      127     /* bytes in any stored target */
#define TUI_CFG_ERR_LOG     8       /* errors kept with their line; all are counted */
#define TUI_SEEN_MAX        32      /* entry numbers remembered for the duplicate check */
#define TUI_DEPTH_MAX       8       /* menus nest up to 8 deep */
#define TUI_HEADER_LINES    8       /* header text lines shown */
#define TUI_SYSNAME_MAX     31      /* characters in the system name */
#define TUI_COLS            80      /* screen width in cells */
#define TUI_ROWS            30      /* screen height in cells */
#define TUI_TEXT_COLS       78      /* usable width: column 1 to 78 */
#define TUI_HINT_MAX        78      /* characters in the footer hint */
#define TUI_MSG_MAX         78      /* characters in the result or error line */
#define TUI_LINES_MAX       TUI_ROWS /* lines tui_layout() can produce */

/* Privilege bit 0 (SYSTEM) sees every entry. */
#define TUI_PRIV_SYSTEM     UOS_PRIV_SYSTEM
_Static_assert(TUI_PRIV_SYSTEM == 0x1u, "tui privilege bit matches uos_privs.h");

/* ---- Entry types ---- */

enum {
    TUI_T_TEXT = 1,     /* target: 8.3 file in the home, shown with the pager */
    TUI_T_MENU,         /* target: 8.3 file in the home ending in .CFG */
    TUI_T_COMMAND,      /* target: a command line, 1 to 127 characters */
    TUI_T_DEVICE,       /* target: "NAME ACTION" */
    TUI_T_HOLOTAPE,     /* target: empty for the list, or an 8.3 tape name */
    TUI_T_LOGOUT,       /* target: must be empty */
};

/* ---- Error codes ---- */

#define TUI_OK              0
#define TUI_E_ARG           (-1)    /* NULL pointer or bad argument */
#define TUI_E_SYNTAX        (-2)    /* the INI text does not parse; the file is refused */
#define TUI_E_SECTION       (-3)    /* unknown section, its keys are ignored */
#define TUI_E_KEY           (-4)    /* unknown key */
#define TUI_E_DUP_KEY       (-5)    /* a key given twice, the first wins */
#define TUI_E_ENTRY_NUM     (-6)    /* [entry N] with N not 1 to 999 */
#define TUI_E_DUP_ENTRY     (-7)    /* [entry N] given twice, the first wins */
#define TUI_E_TOO_MANY      (-8)    /* more than 16 entries */
#define TUI_E_LABEL         (-9)    /* label missing, too long or not printable */
#define TUI_E_TYPE          (-10)   /* type missing or unknown */
#define TUI_E_TARGET        (-11)   /* target wrong for the type */
#define TUI_E_PRIVS         (-12)   /* privs not a 32-bit number */
#define TUI_E_TITLE         (-13)   /* title too long or not printable */
#define TUI_E_HEADER        (-14)   /* header not an 8.3 file name */
#define TUI_E_DEPTH         (-15)   /* a menu would nest deeper than 8 */
#define TUI_E_STATE         (-16)   /* call not valid in this state */
#define TUI_E_SPACE         (-17)   /* caller buffer too small */
#define TUI_E_EMPTY         (-18)   /* [terminal] or [entry N] with no keys */
#define TUI_E_DUP_SECTION   (-19)   /* [terminal] given twice, the first wins */
#define TUI_E_COUNT         19

/* One line of text for an error code. Unknown codes give "unknown error". */
const char *tui_strerror(int code);

/* ---- Parsed menu, tuicfg.c ---- */

typedef struct {
    uint16_t num;                       /* N of [entry N] */
    uint8_t type;                       /* TUI_T_* */
    uint32_t privs;                     /* bits needed to see it */
    char label[TUI_LABEL_MAX + 1];
    char target[TUI_TARGET_MAX + 1];    /* normalised: names upper-case, "NAME ACTION" */
} tui_entry_t;

typedef struct {
    int16_t code;                       /* TUI_E_* */
    uint16_t line;                      /* INI line, 0 when none */
    uint16_t entry;                     /* N of the entry, 0 when none */
} tui_cfg_err_t;

typedef struct {
    char title[TUI_TITLE_MAX + 1];
    char header[TUI_FILE_MAX + 1];      /* "" for none */
    uint8_t count;                      /* visible entries, sorted by N */
    uint8_t hidden;                     /* valid entries hidden by privileges */
    uint16_t errors;                    /* every error counted */
    tui_cfg_err_t err[TUI_CFG_ERR_LOG]; /* the first TUI_CFG_ERR_LOG errors */
    tui_entry_t entry[TUI_MAX_ENTRIES];
} tui_menu_t;

/*
 * Parses TERMINAL.CFG text. Entries `user_privs` does not cover are left
 * out and counted in `hidden`; SYSTEM (bit 0) sees all. Bad entries and
 * keys are skipped and counted in `errors`. Returns TUI_OK (check
 * m->errors), TUI_E_ARG, or TUI_E_SYNTAX when the INI text itself is bad:
 * then `*err_line` (if not NULL) holds the line and the menu is empty.
 */
int tui_cfg_parse(const char *text, size_t len, uint32_t user_privs,
                  tui_menu_t *m, unsigned *err_line);

/* 1 if `s` is a plain 8.3 name: 1-8 of A-Z 0-9 $ _ -, optional '.' and 0-3 more. Case-insensitive. */
int tui_is_83(const char *s);

/* Name of a TUI_T_* type ("text", ...) or "?". */
const char *tui_type_name(int type);

/* ---- The screen model, tui.c ---- */

/* Keys: INPUT_KEY_* codes, or a character with TUI_CHAR(). */
#define TUI_CHAR(c)         (0x100u | ((unsigned)(c) & 0xffu))

/* Actions returned to the caller. */
enum {
    TUI_ACT_NONE = 0,       /* nothing to do but draw */
    TUI_ACT_LOAD_MENU,      /* load file `name`, give it with tui_give_menu() */
    TUI_ACT_LOAD_HEADER,    /* load file `name`, give it with tui_give_header() */
    TUI_ACT_SHOW_TEXT,      /* show file `name` with the pager, then tui_done() */
    TUI_ACT_COMMAND,        /* run `text`, then tui_done() with one result line */
    TUI_ACT_DEVICE,         /* do `word` on device `name`, then tui_done() with one line */
    TUI_ACT_HOLOTAPE,       /* holotape reader: `name` "" for the list; then tui_done() */
    TUI_ACT_LOGOUT,         /* the person logs out; the model stops */
};

/* States. */
enum {
    TUI_ST_WAIT = 0,        /* an action is out; keys are ignored */
    TUI_ST_MENU,            /* a menu, or an empty screen with a message */
    TUI_ST_CONFIRM,         /* "Log out?" */
    TUI_ST_DONE,            /* logged out */
};

typedef struct {
    tui_menu_t menu;
    char file[TUI_FILE_MAX + 1];
    uint8_t sel;            /* selected visible entry */
    uint8_t top;            /* first entry in the window */
    uint8_t nhead;          /* header lines held */
    char head[TUI_HEADER_LINES][TUI_TEXT_COLS + 1];
} tui_level_t;

typedef struct {
    uint32_t privs;
    uint8_t state;          /* TUI_ST_* */
    uint8_t depth;          /* levels on the stack, 0 before the first menu */
    uint8_t pending;        /* TUI_ACT_* the model waits for */
    char sysname[TUI_SYSNAME_MAX + 1];
    char msg[TUI_MSG_MAX + 1];  /* result or error line, "" for none */
    /* The action out: */
    char name[TUI_FILE_MAX + 1 > TUI_DEVNAME_MAX + 1 ? TUI_FILE_MAX + 1 : TUI_DEVNAME_MAX + 1];
    char word[TUI_DEVWORD_MAX + 1];
    char text[TUI_COMMAND_MAX + 1];
    tui_level_t level[TUI_DEPTH_MAX];
} tui_t;

/*
 * Starts the model for a user. `root` is the menu file, normally
 * "TERMINAL.CFG"; `sysname` may be NULL (shown as "UOS"), longer names
 * are cut at 31. Returns TUI_ACT_LOAD_MENU, or TUI_E_ARG.
 */
int tui_init(tui_t *t, uint32_t user_privs, const char *sysname, const char *root);

/* One key. Returns a TUI_ACT_*. Keys in WAIT and DONE are ignored. */
int tui_key(tui_t *t, unsigned key);

/* An input event: KEY press or repeat, or CHAR. Others give TUI_ACT_NONE. */
int tui_event(tui_t *t, const input_event_t *ev);

/*
 * Answers TUI_ACT_LOAD_MENU with the file text. Parses it and pushes it.
 * Returns TUI_ACT_LOAD_HEADER when the menu names a header, else
 * TUI_ACT_NONE. A text that does not parse shows "Bad menu file NAME
 * (line L)" and returns TUI_ACT_NONE. TUI_E_STATE when no menu was asked.
 * Errors inside the file show "NAME: E errors" on the message line.
 */
int tui_give_menu(tui_t *t, const char *text, size_t len);

/*
 * Answers TUI_ACT_LOAD_HEADER. Keeps up to 8 lines, each cut at 78
 * characters, tabs as spaces, other control bytes as '?'. CR LF and LF.
 */
int tui_give_header(tui_t *t, const char *text, size_t len);

/*
 * Answers any action with a failure: `line` is shown on the message line,
 * cut at 78 characters. A sub-menu that fails leaves the parent shown.
 */
int tui_give_error(tui_t *t, const char *line);

/*
 * Ends an action: back to the menu with the selection kept. `line`, NULL
 * or "", is the result shown until the next key.
 */
int tui_done(tui_t *t, const char *line);

/* Rows of entries in the window for the current level. */
unsigned tui_rows(const tui_t *t);

/* ---- Layout ---- */

#define TUI_A_NORMAL        0       /* CONSOLE_ATTR_NORMAL */
#define TUI_A_DIM           1       /* CONSOLE_ATTR_DIM */
#define TUI_A_INVERSE       4       /* CONSOLE_ATTR_INVERSE */

/* Fixed rows of the screen. */
#define TUI_ROW_SYSNAME     0
#define TUI_ROW_TITLE       1
#define TUI_ROW_RULE        2
#define TUI_ROW_HEAD        3       /* first header line */
#define TUI_ROW_LAST_ENTRY  26
#define TUI_ROW_MSG         27
#define TUI_ROW_CRUMB       28
#define TUI_ROW_FOOTER      29

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t attr;                   /* TUI_A_* */
    char text[TUI_COLS + 1];
} tui_line_t;

/*
 * Fills out[] with the screen lines, top to bottom; blank rows are left
 * out. Returns the count, or TUI_E_SPACE when `max` is too small (then
 * nothing is promised in out[]). TUI_LINES_MAX always suffices.
 */
int tui_layout(const tui_t *t, tui_line_t *out, unsigned max);

/* ---- Pager, pager.c ---- */

#define PG_MARK_EVERY       64      /* one index entry per 64 wrapped lines */
#define PG_MIN_WIDTH        8
#define PG_MAX_WIDTH        TUI_COLS
#define PG_MAX_HEIGHT       TUI_ROWS
#define PG_TAB              8
/*
 * Index entries needed for a text of `len` bytes: every wrapped line takes
 * at least one byte, so there are at most len + 1 lines. 64 KiB: 1026
 * entries, 4104 bytes.
 */
#define PG_MARKS_FOR(len)   ((uint32_t)(len) / PG_MARK_EVERY + 2u)

#define PG_OK               0
#define PG_E_ARG            (-1)    /* NULL pointer, width or height out of range */
#define PG_E_SPACE          (-2)    /* index or line buffer too small */
#define PG_E_RANGE          (-3)    /* no such line */

#define PG_EXIT             1       /* pg_key(): the person left the pager */

typedef struct {
    const char *text;
    uint32_t len;
    uint16_t width;
    uint16_t height;
    uint32_t total;         /* wrapped lines */
    uint32_t top;           /* first line shown */
    uint32_t *marks;        /* marks[i]: offset of line i * 64 */
    uint32_t nmarks;
} pg_t;

/*
 * Wraps `text` (the caller keeps it alive and unchanged) to `width` 8..80
 * for `height` 1..30 rows. `marks` holds `cap` entries; PG_MARKS_FOR(len)
 * always suffices. Returns PG_OK, PG_E_ARG or PG_E_SPACE.
 */
int pg_init(pg_t *p, const char *text, size_t len, unsigned width, unsigned height,
            uint32_t *marks, uint32_t cap);

uint32_t pg_lines(const pg_t *p);
uint32_t pg_pages(const pg_t *p);       /* ceil(lines / height); 0 for no lines */
uint32_t pg_max_top(const pg_t *p);

void pg_line_down(pg_t *p);
void pg_line_up(pg_t *p);
void pg_page_down(pg_t *p);
void pg_page_up(pg_t *p);
void pg_home(pg_t *p);
void pg_end(pg_t *p);

/* Lines to draw: first and count, count <= height. */
void pg_visible(const pg_t *p, uint32_t *first, uint32_t *count);

/*
 * Copies wrapped line `n` into out, tabs expanded, other control bytes as
 * '?'. `size` must be at least width + 1. Returns the length, PG_E_SPACE
 * or PG_E_RANGE.
 */
int pg_line(const pg_t *p, uint32_t n, char *out, size_t size);

/* "line A to B of N"; "line 0 to 0 of 0" for no lines. Returns the length or PG_E_SPACE. */
int pg_status(const pg_t *p, char *out, size_t size);

/*
 * Keys: UP, DOWN a line; LEFT, F1 a page up; RIGHT, SELECT, F2 a page
 * down; F3 home; F4 end; BACK leaves (returns PG_EXIT). Characters: 'k'
 * 'j' lines, 'b' and space pages, 'g' 'G' home and end, 'q' leaves.
 */
int pg_key(pg_t *p, unsigned key);

#endif
