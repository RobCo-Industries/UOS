/*
 * The password hacking minigame: pure model, prefix hk_.
 *
 * Word lists, candidate selection, the memory dump board, the cursor,
 * the game state machine and the lockout timer. No hardware, no
 * allocation, no globals. Every random choice comes from an hk_rng_t the
 * caller seeds. Time comes in as a tick value.
 */
#ifndef UOS_HACK_H
#define UOS_HACK_H

#include <stddef.h>
#include <stdint.h>

/* Board shape. */
#define HK_COLS             2
#define HK_ROWS             16
#define HK_ROW_CHARS        12
#define HK_COL_CHARS        (HK_ROWS * HK_ROW_CHARS)  /* 192 */
#define HK_BOARD_CHARS      (HK_COLS * HK_COL_CHARS)  /* 384 */
#define HK_ADDR_MIN         0x1000u  /* lowest start address */
#define HK_ADDR_SPAN        0xEE80u  /* start is HK_ADDR_MIN + rng_below(span) */
#define HK_ADDR_TEXT_SIZE   7        /* "0xF964" and a NUL */

/* Words. */
#define HK_WORD_MIN         4
#define HK_WORD_MAX         12
#define HK_WORDS_MAX        16       /* candidate words in one game */
#define HK_LIST_MAX         512      /* words kept from one list */
#define HK_LIST_MIN_FILE    64       /* words a card list must hold (tool rule) */

/* Brackets and history. */
#define HK_MIN_PAIRS        4        /* usable bracket pairs on every board */
#define HK_HISTORY_LINES    16
#define HK_LINE_SIZE        16       /* longest line is 12 characters */
#define HK_RESET_CHANCE     5        /* a bracket resets tries with chance 1 in 5 */
#define HK_DUD_CHAR         '.'

/* Game settings (UOS.CFG [hack]). */
#define HK_ATTEMPTS_MIN     1
#define HK_ATTEMPTS_MAX     9
#define HK_ATTEMPTS_DEFAULT 4
#define HK_LOCKOUT_S_MAX    3600
#define HK_LOCKOUT_S_DEFAULT 60
#define HK_TPS_MAX          1000000u /* ticks per second, at most */

/* Junk characters, as in the design. Contains the eight bracket characters. */
#define HK_JUNK_CHARS       "!\"#$%&'()*+,-./:;<=>?@[\\]^_{|}"

typedef enum {
    HK_OK = 0,
    HK_ERR_ARG,      /* a NULL pointer or a value out of range */
    HK_ERR_LENGTH,   /* a word of the wrong length */
    HK_ERR_CHAR,     /* a character outside A to Z */
    HK_ERR_DUP,      /* the same word twice */
    HK_ERR_TOO_MANY, /* more than HK_LIST_MAX words */
    HK_ERR_TOO_FEW,  /* fewer words than the difficulty needs */
    HK_ERR_STATE,    /* the game is not in a state that allows this */
    HK_ERR_COUNT
} hk_err_t;

typedef enum {
    HK_NOVICE = 0,
    HK_ADVANCED,
    HK_EXPERT,
    HK_MASTER,
    HK_DIFF_COUNT
} hk_difficulty_t;

typedef struct {
    uint8_t min_len;
    uint8_t max_len;
    uint8_t words;
} hk_diff_info_t;

/* xorshift32 (13, 17, 5). A zero seed is replaced by 0x9E3779B9. */
typedef struct {
    uint32_t state;
} hk_rng_t;

/* A parsed list. Words point into the caller's buffer, not NUL terminated. */
typedef struct {
    const char *word[HK_LIST_MAX];
    uint16_t count;
    uint8_t len;
    uint32_t err_line;          /* 1-based line of the first error, 0 if none */
    uint16_t scratch[HK_LIST_MAX]; /* used by hk_pick */
} hk_list_t;

/* The candidate set. Words point into the list's buffer. */
typedef struct {
    const char *word[HK_WORDS_MAX];
    uint8_t count;
    uint8_t len;
    uint8_t password;           /* index of the password in word[] */
    uint8_t spread_ok;          /* 1: the band rule held; 0: fallback used */
} hk_pick_t;

typedef struct {
    char cell[HK_BOARD_CHARS];   /* index = col * 192 + row * 12 + x */
    uint16_t start_addr;
    uint8_t count;               /* words on the board */
    uint8_t len;
    uint16_t word_pos[HK_WORDS_MAX];
    int16_t pair_close[HK_BOARD_CHARS]; /* closing index for a usable opener, else -1 */
} hk_board_t;

typedef enum {
    HK_LEFT = 0,
    HK_RIGHT,
    HK_UP,
    HK_DOWN
} hk_dir_t;

typedef enum {
    HK_HL_CHAR = 0,
    HK_HL_WORD,
    HK_HL_PAIR
} hk_hl_t;

typedef enum {
    HK_PLAYING = 0,
    HK_WON,
    HK_LOCKED,
    HK_CANCELLED
} hk_state_t;

typedef enum {
    HK_EV_NONE = 0,     /* nothing happened */
    HK_EV_DENIED,       /* a wrong guess */
    HK_EV_WIN,          /* the password */
    HK_EV_LOCKOUT,      /* a wrong guess used the last attempt */
    HK_EV_DUD,          /* a bracket removed a dud */
    HK_EV_RESET         /* a bracket reset the attempts */
} hk_event_t;

typedef struct {
    uint8_t attempts;     /* HK_ATTEMPTS_MIN to HK_ATTEMPTS_MAX */
    uint16_t lockout_s;   /* 0 to HK_LOCKOUT_S_MAX */
    uint32_t ticks_per_s; /* 1 to HK_TPS_MAX */
} hk_config_t;

typedef struct {
    hk_board_t board;
    hk_rng_t rng;
    hk_config_t cfg;
    hk_state_t state;
    uint8_t attempts;
    uint8_t password;              /* index into board.word_pos */
    uint8_t dud[HK_WORDS_MAX];     /* 1 when the word was removed */
    uint8_t used[HK_BOARD_CHARS];  /* 1 when the bracket opener was used */
    uint16_t cursor;
    uint32_t lock_start;
    uint32_t lock_ticks;
    char history[HK_HISTORY_LINES][HK_LINE_SIZE];
    uint8_t hist_head;             /* index of the oldest line */
    uint8_t hist_count;
} hk_game_t;

/* Error text, one line. Unknown codes give "unknown error". */
const char *hk_strerror(int err);

/* Generator. */
void hk_rng_seed(hk_rng_t *r, uint32_t seed);
uint32_t hk_rng_next(hk_rng_t *r);
/* 0 to n - 1 by multiply and shift; n = 0 gives 0. */
uint32_t hk_rng_below(hk_rng_t *r, uint32_t n);

/* Difficulty. Names: "novice", "advanced", "expert", "master", case-insensitive. */
int hk_difficulty_parse(const char *name, hk_difficulty_t *out);
const char *hk_difficulty_name(hk_difficulty_t d);
/* NULL for an out-of-range value. */
const hk_diff_info_t *hk_difficulty_info(hk_difficulty_t d);
/* The word length for one game, min_len to max_len; 0 for a bad value. */
unsigned hk_pick_length(hk_difficulty_t d, hk_rng_t *r);

/* Positions where a and b hold the same letter, over len characters. */
unsigned hk_likeness(const char *a, const char *b, unsigned len);

/* Parses one word list. See docs/modules/hack.md for the rules. */
int hk_list_parse(const char *buf, size_t size, unsigned word_len, hk_list_t *out);

/* Chooses the password and the other candidates. Uses list->scratch. */
int hk_pick(hk_list_t *list, hk_difficulty_t d, hk_rng_t *r, hk_pick_t *out);

/* Builds the board from a candidate set. */
int hk_board_build(hk_board_t *b, const hk_pick_t *p, hk_rng_t *r);
uint16_t hk_board_address(const hk_board_t *b, unsigned col, unsigned row);
/* Writes "0x" and four upper-case hex digits. HK_ERR_ARG if size < 7. */
int hk_format_address(uint16_t addr, char *buf, size_t size);
/* Index of the word covering cell i, or -1. */
int hk_board_word_at(const hk_board_t *b, unsigned i);
unsigned hk_board_pair_count(const hk_board_t *b);

/* Game. Builds the board from p; copies r so the caller's state is untouched. */
int hk_game_init(hk_game_t *g, const hk_pick_t *p, const hk_config_t *cfg, const hk_rng_t *r);
void hk_move(hk_game_t *g, hk_dir_t dir);
/* What the cursor highlights; start and len give the cells (len may span rows). */
hk_hl_t hk_highlight(const hk_game_t *g, unsigned *start, unsigned *len);
/* SELECT at the cursor. now is the tick, used when the game locks. */
hk_event_t hk_select(hk_game_t *g, uint32_t now);
void hk_cancel(hk_game_t *g);
/* The state; for HK_LOCKED, *secs gets the whole seconds left, rounded up. */
hk_state_t hk_result(const hk_game_t *g, uint32_t now, uint32_t *secs);
unsigned hk_history_count(const hk_game_t *g);
/* Line i, 0 the oldest kept; NULL when i is out of range. */
const char *hk_history_line(const hk_game_t *g, unsigned i);

#endif
