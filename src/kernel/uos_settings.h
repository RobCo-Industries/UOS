/*
 * System settings from /UOS/UOS.CFG. Pure: src/kernel/settings.c.
 *
 * Every key is optional. A value that is not of the key's type is skipped
 * and counted in `bad`; the default stays. A number outside its range is
 * clamped to the nearest limit and counted in `clamped`. An unknown key
 * or section is counted in `unknown`. When a key repeats, the last one
 * wins. A line that does not parse stops the parser: the keys before it
 * apply, and the report holds the line.
 */
#ifndef UOS_SETTINGS_H
#define UOS_SETTINGS_H

#include <stddef.h>

#define UOS_CFG_PATH            "/UOS/UOS.CFG"
#define UOS_CFG_FILE_MAX        4096    /* bytes of UOS.CFG read */

#define UOS_NAME_MAX            31      /* [system] name, characters */
#define UOS_USER_MAX            15      /* [hack] user, characters */
#define UOS_DEFAULT_NAME        "ROBCO TERMINAL"
#define UOS_DEFAULT_HACK_USER   "GUEST"

/* [hack] difficulty. */
#define UOS_DIFF_NOVICE         0
#define UOS_DIFF_ADVANCED       1
#define UOS_DIFF_EXPERT         2
#define UOS_DIFF_MASTER         3

/* Ranges and defaults. */
#define UOS_RETRIES_MIN         1
#define UOS_RETRIES_MAX         9
#define UOS_RETRIES_DEF         3
#define UOS_LOGIN_LOCK_MIN      0
#define UOS_LOGIN_LOCK_MAX      3600
#define UOS_LOGIN_LOCK_DEF      30
#define UOS_LOGIN_IDLE_MIN      0       /* 0: a session never ends on idle time */
#define UOS_LOGIN_IDLE_MAX      86400
#define UOS_LOGIN_IDLE_DEF      0
#define UOS_ATTEMPTS_MIN        1
#define UOS_ATTEMPTS_MAX        9
#define UOS_ATTEMPTS_DEF        4
#define UOS_HACK_LOCK_MIN       0
#define UOS_HACK_LOCK_MAX       3600
#define UOS_HACK_LOCK_DEF       60
#define UOS_HWDOG_MS_MIN        1000    /* [watchdog] timeout_ms */
#define UOS_HWDOG_MS_MAX        15000
#define UOS_HWDOG_MS_DEF        8000

/* scheme when UOS.CFG does not set it. */
#define UOS_SCHEME_UNSET        (-1)

typedef struct {
    char name[UOS_NAME_MAX + 1];
    int scheme;                 /* CONSOLE_SCHEME_* 0 to 3, or UOS_SCHEME_UNSET */
    unsigned login_retries;
    unsigned login_lockout_s;
    unsigned login_idle_s;      /* seconds without a key before logout, 0 never */
    int hack_enabled;           /* 1 or 0 */
    char hack_user[UOS_USER_MAX + 1];
    unsigned hack_attempts;
    int hack_difficulty;        /* UOS_DIFF_* */
    unsigned hack_lockout_s;
    int hwdog_enabled;          /* [watchdog] enabled: 1 or 0, off by default */
    unsigned hwdog_timeout_ms;  /* [watchdog] timeout_ms */
    int buttons_enabled;        /* [input] buttons: 1 or 0, on by default */
} uos_settings_t;

/* Parse results. */
#define UOS_CFG_OK              0
#define UOS_CFG_SYNTAX          (-1)    /* a line does not parse: see err_line */
#define UOS_CFG_ARG             (-2)    /* NULL */

typedef struct {
    unsigned keys;              /* keys applied, clamped ones included */
    unsigned bad;               /* values skipped */
    unsigned clamped;           /* numbers brought into range */
    unsigned unknown;           /* keys or sections UOS does not know */
    unsigned err_line;          /* UOS_CFG_SYNTAX: the line, from 1 */
} uos_settings_report_t;

void uos_settings_default(uos_settings_t *s);

/*
 * Parses `len` bytes of UOS.CFG into *s, which should hold the defaults
 * first. Fills *r. Returns UOS_CFG_OK, UOS_CFG_SYNTAX or UOS_CFG_ARG.
 */
int uos_settings_parse(const char *text, size_t len, uos_settings_t *s,
                       uos_settings_report_t *r);

/* "green", "amber", "blue", "white"; "default" for UOS_SCHEME_UNSET; "unknown" otherwise. */
const char *uos_scheme_name(int scheme);
/* "novice", "advanced", "expert", "master", "unknown" otherwise. */
const char *uos_difficulty_name(int diff);

#endif
