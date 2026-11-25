/*
 * The login rules: the stepped password check, the login screen's state
 * machine, its messages, the lockout values and the idle rule. Pure:
 * src/session/login.c, prefix lg_. The session service in sessvc.c and
 * sesscreen.c runs them.
 */
#ifndef UOS_LOGIN_H
#define UOS_LOGIN_H

#include <stddef.h>
#include <stdint.h>

#include "lockout.h"
#include "uaf.h"

/* ---- The stepped password check ---- */

/*
 * SHA-256 compressions one step may run. One check costs 2000 to 3999 at
 * the default 2000 iterations, so it takes 8 to 16 loop passes, and time,
 * input and the watchdog get their turn between two steps.
 */
#define LG_STEP_BLOCKS      256u

/* Check states. */
#define LG_CK_IDLE          0   /* nothing to do, or wiped */
#define LG_CK_RUN           1   /* rounds left */
#define LG_CK_DONE          2   /* every round ran: lg_check_end() gives the result */

/*
 * One check. It holds a copy of the password until lg_check_end() or
 * lg_check_wipe(), which clear the whole struct. The caller owns it.
 */
typedef struct lg_check {
    uint8_t salt[UAF_SALT_SIZE];
    uint8_t want[UAF_HASH_SIZE];
    uint8_t h[UAF_HASH_SIZE];
    char pw[UAF_PASSWORD_MAX + 1];
    uint32_t pw_len;
    uint32_t iterations;
    uint32_t done;              /* rounds done */
    uint32_t blocks;            /* compressions done */
    uint32_t steps;             /* lg_check_step() calls that did work */
    int32_t idx;                /* the user, or -1 */
    uint32_t flags;             /* the user's flags, 0 for none */
    uint8_t pw_ok;
    uint8_t state;              /* LG_CK_* */
} lg_check_t;

/*
 * Starts a check of `name` and `pw` against `db`, with the rules of
 * uaf_verify(): an unknown user hashes against a zero salt and a zero
 * hash, a password that breaks the rules hashes 32 dots in its place, and
 * every case runs the full iteration count. Copies what it needs: the
 * caller may wipe `pw` once this returns.
 */
void lg_check_begin(lg_check_t *c, const uaf_db_t *db, const char *name, const char *pw);

/* Compressions per round for a password of `pw_len` characters: 1 up to
 * 7 characters, 2 from 8. The first round is always 1. */
uint32_t lg_round_blocks(uint32_t pw_len);

/* Rounds per step for a budget of `budget` compressions: at least 1. */
uint32_t lg_rounds_per_step(uint32_t pw_len, uint32_t budget);

/*
 * Runs up to lg_rounds_per_step(budget) rounds. Returns 1 when every
 * round has run (state LG_CK_DONE), 0 while rounds are left, -1 when no
 * check runs.
 */
int lg_check_step(lg_check_t *c, uint32_t budget);

/*
 * The result of a finished check, as uaf_verify() gives it: UAF_OK,
 * UAF_E_BAD_LOGIN, or UAF_E_DISABLED after a right password. *index (may
 * be NULL) gets the user on UAF_OK and UAF_E_DISABLED, else -1. Compares
 * all 32 bytes in constant time, then wipes the struct. UAF_E_BAD_LOGIN
 * when the check has not finished.
 */
uaf_err_t lg_check_end(lg_check_t *c, int *index);

/* Clears the whole struct, password and hashes included. */
void lg_check_wipe(lg_check_t *c);

/* ---- The login screen's state machine ---- */

typedef enum lg_state {
    LG_S_OFF = 0,               /* no login: no database, or not started */
    LG_S_USER,                  /* reading the user name */
    LG_S_PASS,                  /* reading the password */
    LG_S_CHECK,                 /* the stepped check runs */
    LG_S_LOCKED,                /* the lockout screen counts down */
    LG_S_WELCOME,               /* the welcome after a good login */
    LG_S_SESSION,               /* the next screen runs */
    LG_S_NUM
} lg_state_t;

typedef enum lg_event {
    LG_E_START = 0,             /* the login screen starts, not locked */
    LG_E_START_LOCKED,          /* the login screen starts while a lock runs */
    LG_E_USER_OK,               /* a user name was typed */
    LG_E_USER_EMPTY,            /* an empty user name */
    LG_E_USER_CANCEL,           /* Ctrl-C on the user name */
    LG_E_PASS_OK,               /* a password line, empty or not */
    LG_E_PASS_CANCEL,           /* Ctrl-C on the password */
    LG_E_GOOD,                  /* the check passed */
    LG_E_BAD,                   /* the check failed, tries are left */
    LG_E_BAD_LOCKED,            /* the check failed and the lockout started */
    LG_E_LOCK_END,              /* the lockout ended */
    LG_E_WELCOME_DONE,          /* the welcome time is over, or a key */
    LG_E_LOGOUT,                /* sess_logout(), any reason */
    LG_E_DIRECT,                /* a session set with no password: a won password game,
                                 * or `sess login` in test builds */
    LG_E_HACK_LOCKED,           /* the password game was lost: the lockout starts */
    LG_E_NUM
} lg_event_t;

/* Messages the login screen shows on its message row. */
typedef enum lg_msg {
    LG_M_NONE = 0,
    LG_M_DENIED,                /* the one message for any failed login */
    LG_M_CANCELLED,             /* Ctrl-C on the password */
    LG_M_UNLOCKED,              /* the lockout ended */
    LG_M_LOGGED_OUT,            /* back from a session */
    LG_M_NUM
} lg_msg_t;

typedef struct lg_step {
    lg_state_t next;
    lg_msg_t msg;
    uint8_t changed;            /* 0: the event does not apply in this state */
} lg_step_t;

/* The next state and the message for `e` in state `s`. An event that does
 * not apply keeps the state, with changed 0. */
lg_step_t lg_next(lg_state_t s, lg_event_t e);

/* "off", "user", "pass", "check", "locked", "welcome", "session". */
const char *lg_state_name(lg_state_t s);

/* The message text, "" for none. Never names a user or a password. */
const char *lg_msg_text(lg_msg_t m);

/*
 * The message row after a failed login that did not lock: the same text
 * for an unknown user and a wrong password, with the tries left.
 * Returns the length, or -1 when it does not fit (buf then holds "").
 */
int lg_denied_line(char *buf, size_t size, unsigned tries_left);

/* The countdown row of the lockout screen. Length or -1. */
int lg_locked_line(char *buf, size_t size, uint32_t seconds);

/* ---- The password game's session ---- */

typedef enum lg_hack {
    LG_HACK_OK = 0,
    LG_HACK_OFF,                /* [hack] enabled = no */
    LG_HACK_NODB,               /* no user database */
    LG_HACK_NOUSER,             /* the [hack] user does not exist */
    LG_HACK_DISABLED,           /* the account has UAF_FLAG_DISABLED */
    LG_HACK_NOHACK,             /* the account has UAF_FLAG_NOHACK */
    LG_HACK_BUSY,               /* someone is logged in, or the login is past the user name */
    LG_HACK_LOCKED,             /* the terminal is locked */
    LG_HACK_COVERED,            /* another screen is open over the login prompt */
    LG_HACK_NUM
} lg_hack_t;

/*
 * May the password game run, or log in its target? `enabled` is [hack]
 * enabled, `db_ok` non-zero when the database loaded, `rec` the target's
 * record or NULL, `st` the login state and `locked` non-zero while the
 * lockout runs. The game runs only from the user name prompt. The first
 * failing rule in the order of lg_hack_t wins.
 */
lg_hack_t lg_hack_allowed(int enabled, int db_ok, const uaf_rec_t *rec, lg_state_t st,
                          int locked);

/*
 * Where the game was started from. `allowed` is lg_hack_allowed()'s
 * answer, `on_top` non-zero when the login's user name prompt was the top
 * screen at the start. The game plays only over the login prompt: any
 * other screen on top gives LG_HACK_COVERED. A refusal in `allowed` wins.
 */
lg_hack_t lg_hack_where(lg_hack_t allowed, int on_top);

/* "ok", "off", "nodb", "nouser", "disabled", "nohack", "busy", "locked",
 * "covered". */
const char *lg_hack_name(lg_hack_t h);

/* One line for the screen, never the target's password. "" for LG_HACK_OK. */
const char *lg_hack_text(lg_hack_t h);

/* ---- Settings ---- */

/*
 * Applies [login] retries and lockout_s with lk_init(). When it refuses
 * them, applies 3 and 30 and returns 1, so the caller marks a warning.
 * Returns 0 when the values applied.
 */
int lg_lockout_apply(lk_t *lk, unsigned retries, unsigned lockout_s);

/* The idle rule: 1 when `idle_s` is not 0 and at least idle_s seconds of
 * milliseconds passed from `last_ms` to `now_ms`, across the wrap. */
int lg_idle_expired(uint32_t idle_s, uint32_t last_ms, uint32_t now_ms);

/* ---- The database state ---- */

typedef enum lg_db {
    LG_DB_OK = 0,
    LG_DB_NOCARD,               /* no card mounted */
    LG_DB_NOTREE,               /* a card, but no /UOS folder */
    LG_DB_MISSING,              /* no /UOS/SYSUAF.DAT */
    LG_DB_DAMAGED,              /* a read error, or the file fails uaf_load() */
    LG_DB_NUM
} lg_db_t;

/* "ok", "nocard", "notree", "missing", "damaged". */
const char *lg_db_name(lg_db_t db);

/* 1 when the state allows a restart from the system screen: a card
 * problem or a database problem. 0 for LG_DB_OK. */
int lg_db_reboot_only(lg_db_t db);

/*
 * The two notice rows the system screen shows instead of the login, `row`
 * 0 or 1. `why` is a short reason word for LG_DB_DAMAGED, may be NULL.
 * Returns the length, or -1 for a bad row or a line that does not fit.
 */
int lg_db_notice(lg_db_t db, const char *why, unsigned row, char *buf, size_t size);

/* ---- Text after a good login ---- */

/*
 * The user name of a record as a C string, upper-case as stored, at most
 * 16 characters. `out` holds UAF_NAME_MAX + 1 bytes.
 */
void lg_rec_name(const uaf_rec_t *r, char out[UAF_NAME_MAX + 1]);

/* The home folder of a record as a C string, `out` holds UAF_HOME_MAX + 1. */
void lg_rec_home(const uaf_rec_t *r, char out[UAF_HOME_MAX + 1]);

/*
 * Welcome rows, `row` 0 to LG_WELCOME_ROWS - 1: the name, the privileges
 * ("NONE" for none) and a line that says the session starts. The format
 * stores no last login, so there is no row for it. Length or -1.
 */
#define LG_WELCOME_ROWS     3
int lg_welcome_line(const char *name, uint32_t privs, unsigned row, char *buf, size_t size);

/* ---- Screen text ---- */

/*
 * The login header, `row` 0 to LG_HEADER_ROWS - 1, original text in the
 * style of a RobCo terminal. `sysname` is [system] name. Length or -1.
 */
#define LG_HEADER_ROWS      6
int lg_header_line(const char *sysname, unsigned row, char *buf, size_t size);

/*
 * The typed user name as one marker word: upper-case, A-Z 0-9 $ _ kept,
 * every other byte as '?', at most UAF_NAME_MAX characters, "-" when
 * empty. The name field is not secret: the screen shows it as typed.
 */
void lg_name_word(const char *typed, char out[UAF_NAME_MAX + 1]);

/* One upper-case word for a uaf_load() error, for the marker and the
 * damaged notice: "SHORT", "MAGIC", "CRC", "NAME", ... "UNKNOWN". */
const char *lg_uaf_word(uaf_err_t err);

#endif
