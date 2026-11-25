/*
 * The login rules. Pure: no hardware, no kernel calls, no allocation.
 * See login.h.
 */

#include <robco/klibc.h>

#include "login.h"

/* ---- The stepped password check ---- */

/* Hashed in place of a password that breaks the rules, as uaf_verify(). */
static const char dummy_pw[] = "................................";

_Static_assert(sizeof(dummy_pw) == UAF_PASSWORD_MAX + 1, "the dummy is the longest password");

void lg_check_wipe(lg_check_t *c)
{
    volatile uint8_t *p = (volatile uint8_t *)c;
    size_t i;

    /* volatile: the compiler may not drop a clear of memory it sees die. */
    for (i = 0; i < sizeof(*c); i++) {
        p[i] = 0;
    }
}

void lg_check_begin(lg_check_t *c, const uaf_db_t *db, const char *name, const char *pw)
{
    int idx;

    lg_check_wipe(c);
    c->idx = -1;
    c->iterations = db->iterations;
    if (c->iterations < UAF_ITER_MIN || c->iterations > UAF_ITER_MAX) {
        c->iterations = UAF_ITER_DEFAULT;
    }
    idx = name != NULL ? uaf_find(db, name) : -1;
    if (idx >= 0) {
        c->idx = idx;
        c->flags = db->rec[idx].flags;
        k_memcpy(c->salt, db->rec[idx].salt, UAF_SALT_SIZE);
        k_memcpy(c->want, db->rec[idx].hash, UAF_HASH_SIZE);
    }
    /* An unknown user keeps the zero salt and the zero hash. */
    c->pw_ok = pw != NULL && uaf_check_password(pw) == UAF_OK;
    if (c->pw_ok) {
        c->pw_len = (uint32_t)k_strlen(pw);
        k_memcpy(c->pw, pw, c->pw_len);
    } else {
        c->pw_len = UAF_PASSWORD_MAX;
        k_memcpy(c->pw, dummy_pw, UAF_PASSWORD_MAX);
    }
    c->state = LG_CK_RUN;
}

uint32_t lg_round_blocks(uint32_t pw_len)
{
    /* h || salt || pw and at least 9 bytes of padding in 64-byte blocks. */
    return UAF_HASH_SIZE + UAF_SALT_SIZE + pw_len + 9u <= SHA_BLOCK_SIZE ? 1u : 2u;
}

uint32_t lg_rounds_per_step(uint32_t pw_len, uint32_t budget)
{
    uint32_t n = budget / lg_round_blocks(pw_len);

    return n == 0 ? 1u : n;
}

static void round_one(lg_check_t *c)
{
    sha_ctx_t ctx;

    sha_init(&ctx);
    if (c->done > 0) {
        sha_update(&ctx, c->h, UAF_HASH_SIZE);
    }
    sha_update(&ctx, c->salt, UAF_SALT_SIZE);
    sha_update(&ctx, c->pw, c->pw_len);
    sha_final(&ctx, c->h);
    c->blocks += ctx.blocks;
    c->done++;
    k_memset(&ctx, 0, sizeof(ctx));
}

int lg_check_step(lg_check_t *c, uint32_t budget)
{
    uint32_t n;

    if (c->state != LG_CK_RUN) {
        return c->state == LG_CK_DONE ? 1 : -1;
    }
    n = lg_rounds_per_step(c->pw_len, budget);
    while (n-- > 0 && c->done < c->iterations) {
        round_one(c);
    }
    c->steps++;
    if (c->done >= c->iterations) {
        c->state = LG_CK_DONE;
        return 1;
    }
    return 0;
}

uaf_err_t lg_check_end(lg_check_t *c, int *index)
{
    int equal = c->state == LG_CK_DONE && uaf_hash_equal(c->h, c->want);
    int ok = equal && c->idx >= 0 && c->pw_ok;
    int idx = c->idx;
    uint32_t flags = c->flags;

    lg_check_wipe(c);
    if (index != NULL) {
        *index = ok ? idx : -1;
    }
    if (!ok) {
        return UAF_E_BAD_LOGIN;
    }
    return (flags & UAF_FLAG_DISABLED) ? UAF_E_DISABLED : UAF_OK;
}

/* ---- The state machine ---- */

lg_step_t lg_next(lg_state_t s, lg_event_t e)
{
    lg_step_t r = { s, LG_M_NONE, 0 };

#define TO(state, message) do { r.next = (state); r.msg = (message); r.changed = 1; } while (0)
    /* A direct session skips the prompts, never a running check or session. */
    if (e == LG_E_DIRECT) {
        if (s == LG_S_OFF || s == LG_S_USER || s == LG_S_PASS || s == LG_S_LOCKED) {
            TO(LG_S_WELCOME, LG_M_NONE);
        }
        return r;
    }
    switch (s) {
    case LG_S_OFF:
        if (e == LG_E_START) {
            TO(LG_S_USER, LG_M_NONE);
        } else if (e == LG_E_START_LOCKED) {
            TO(LG_S_LOCKED, LG_M_NONE);
        }
        break;
    case LG_S_USER:
        if (e == LG_E_USER_OK) {
            TO(LG_S_PASS, LG_M_NONE);
        } else if (e == LG_E_USER_EMPTY || e == LG_E_USER_CANCEL) {
            /* Nothing was asked: the prompt comes back, nothing counts. */
            TO(LG_S_USER, LG_M_NONE);
        }
        break;
    case LG_S_PASS:
        if (e == LG_E_PASS_OK) {
            TO(LG_S_CHECK, LG_M_NONE);
        } else if (e == LG_E_PASS_CANCEL) {
            TO(LG_S_USER, LG_M_CANCELLED);
        }
        break;
    case LG_S_CHECK:
        if (e == LG_E_GOOD) {
            TO(LG_S_WELCOME, LG_M_NONE);
        } else if (e == LG_E_BAD) {
            TO(LG_S_USER, LG_M_DENIED);
        } else if (e == LG_E_BAD_LOCKED) {
            TO(LG_S_LOCKED, LG_M_NONE);
        }
        break;
    case LG_S_LOCKED:
        if (e == LG_E_LOCK_END) {
            TO(LG_S_USER, LG_M_UNLOCKED);
        }
        break;
    case LG_S_WELCOME:
        if (e == LG_E_WELCOME_DONE) {
            TO(LG_S_SESSION, LG_M_NONE);
        } else if (e == LG_E_LOGOUT) {
            TO(LG_S_USER, LG_M_LOGGED_OUT);
        }
        break;
    case LG_S_SESSION:
        if (e == LG_E_LOGOUT) {
            TO(LG_S_USER, LG_M_LOGGED_OUT);
        }
        break;
    default:
        break;
    }
#undef TO
    return r;
}

const char *lg_state_name(lg_state_t s)
{
    static const char *const names[LG_S_NUM] = {
        "off", "user", "pass", "check", "locked", "welcome", "session",
    };

    return (unsigned)s < LG_S_NUM ? names[s] : "unknown";
}

const char *lg_msg_text(lg_msg_t m)
{
    switch (m) {
    case LG_M_DENIED:     return "ACCESS DENIED.";
    case LG_M_CANCELLED:  return "LOGIN CANCELLED.";
    case LG_M_UNLOCKED:   return "TERMINAL UNLOCKED. YOU MAY TRY AGAIN.";
    case LG_M_LOGGED_OUT: return "SESSION CLOSED.";
    default:              return "";
    }
}

/* Returns n when it fits, else empties buf and returns -1. */
static int fit(char *buf, size_t size, int n)
{
    if (n < 0 || (size_t)n >= size) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    return n;
}

int lg_denied_line(char *buf, size_t size, unsigned tries_left)
{
    if (buf == NULL) {
        return -1;
    }
    return fit(buf, size, k_snprintf(buf, size, "%s %u %s LEFT.", lg_msg_text(LG_M_DENIED),
                                     tries_left, tries_left == 1 ? "ATTEMPT" : "ATTEMPTS"));
}

int lg_locked_line(char *buf, size_t size, uint32_t seconds)
{
    if (buf == NULL) {
        return -1;
    }
    return fit(buf, size, k_snprintf(buf, size, "TRY AGAIN IN %u %s.", (unsigned)seconds,
                                     seconds == 1 ? "SECOND" : "SECONDS"));
}

/* ---- Settings ---- */

int lg_lockout_apply(lk_t *lk, unsigned retries, unsigned lockout_s)
{
    if (lk_init(lk, retries, lockout_s) == LK_OK) {
        return 0;
    }
    (void)lk_init(lk, LK_RETRIES_DEFAULT, LK_LOCKOUT_DEFAULT_S);
    return 1;
}

int lg_idle_expired(uint32_t idle_s, uint32_t last_ms, uint32_t now_ms)
{
    /* Seconds as milliseconds in 64 bits: a large idle_s never wraps. */
    return idle_s != 0 && (uint64_t)(uint32_t)(now_ms - last_ms) >= (uint64_t)idle_s * 1000u;
}

/* ---- The database state ---- */

const char *lg_db_name(lg_db_t db)
{
    static const char *const names[LG_DB_NUM] = { "ok", "nocard", "notree", "missing", "damaged" };

    return (unsigned)db < LG_DB_NUM ? names[db] : "unknown";
}

int lg_db_reboot_only(lg_db_t db)
{
    return db != LG_DB_OK && (unsigned)db < LG_DB_NUM;
}

int lg_db_notice(lg_db_t db, const char *why, unsigned row, char *buf, size_t size)
{
    static const char *const fix = "RESTORE /UOS/SYSUAF.DAT FROM A BACKUP, THEN PRESS F4 TO RESTART.";
    int n;

    if (buf == NULL || row > 1) {
        if (buf != NULL && size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    switch (db) {
    case LG_DB_NOCARD:
        n = row == 0 ? k_snprintf(buf, size, "%s", "LOGIN IS NOT AVAILABLE WITHOUT A UOS CARD.")
                     : k_snprintf(buf, size, "%s", "INSERT A UOS CARD, THEN PRESS F4 TO RESTART.");
        break;
    case LG_DB_NOTREE:
        n = row == 0 ? k_snprintf(buf, size, "%s", "LOGIN IS NOT AVAILABLE: NO /UOS FOLDER.")
                     : k_snprintf(buf, size, "%s", "INSERT A UOS CARD, THEN PRESS F4 TO RESTART.");
        break;
    case LG_DB_MISSING:
        n = row == 0 ? k_snprintf(buf, size, "%s", "USER DATABASE MISSING. NO ONE CAN LOG IN.")
                     : k_snprintf(buf, size, "%s", fix);
        break;
    case LG_DB_DAMAGED:
        n = row == 0 ? k_snprintf(buf, size, "USER DATABASE DAMAGED (%s). NO ONE CAN LOG IN.",
                                  why != NULL && why[0] != '\0' ? why : "UNKNOWN")
                     : k_snprintf(buf, size, "%s", fix);
        break;
    default:
        n = -1;
        break;
    }
    return fit(buf, size, n);
}

/* ---- Text after a good login ---- */

void lg_rec_name(const uaf_rec_t *r, char out[UAF_NAME_MAX + 1])
{
    size_t i;

    for (i = 0; i < UAF_NAME_MAX && r->name[i] != '\0'; i++) {
        out[i] = r->name[i];
    }
    out[i] = '\0';
}

void lg_rec_home(const uaf_rec_t *r, char out[UAF_HOME_MAX + 1])
{
    size_t i;

    for (i = 0; i < UAF_HOME_MAX && r->home[i] != '\0'; i++) {
        out[i] = r->home[i];
    }
    out[i] = '\0';
}

int lg_welcome_line(const char *name, uint32_t privs, unsigned row, char *buf, size_t size)
{
    char p[UAF_PRIVS_TEXT_MAX];
    int n;

    if (buf == NULL || name == NULL) {
        if (buf != NULL && size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    switch (row) {
    case 0:
        n = k_snprintf(buf, size, "WELCOME, %s.", name);
        break;
    case 1:
        if (uaf_privs_format(privs, p, sizeof(p)) != UAF_OK || p[0] == '\0') {
            k_snprintf(p, sizeof(p), "%s", "NONE");
        }
        n = k_snprintf(buf, size, "PRIVILEGES: %s", p);
        break;
    case 2:
        n = k_snprintf(buf, size, "%s", "OPENING YOUR SESSION...");
        break;
    default:
        n = -1;
        break;
    }
    return fit(buf, size, n);
}

/* ---- Screen text ---- */

int lg_header_line(const char *sysname, unsigned row, char *buf, size_t size)
{
    int n;

    if (buf == NULL || sysname == NULL) {
        if (buf != NULL && size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    /* LORE: original in-world text. */
    switch (row) {
    case 0:
        n = k_snprintf(buf, size, "%s", "ROBCO UNIFIED OPERATING SYSTEM - SECURE ACCESS NODE");
        break;
    case 1:
        n = k_snprintf(buf, size, "TERMINAL: %s", sysname);
        break;
    case 2:
        n = k_snprintf(buf, size, "%s", "--------------------------------------------------");
        break;
    case 3:
        n = k_snprintf(buf, size, "%s", "AUTHORISED PERSONNEL ONLY. EVERY ACCESS ATTEMPT IS RECORDED.");
        break;
    case 4:
        n = k_snprintf(buf, size, "%s", "IDENTIFY YOURSELF TO PROCEED.");
        break;
    case 5:
        n = 0;
        buf[0] = '\0';
        break;
    default:
        n = -1;
        break;
    }
    return fit(buf, size, n);
}

void lg_name_word(const char *typed, char out[UAF_NAME_MAX + 1])
{
    size_t i;
    char c;

    for (i = 0; typed != NULL && i < UAF_NAME_MAX && typed[i] != '\0'; i++) {
        c = typed[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_')) {
            c = '?';
        }
        out[i] = c;
    }
    if (i == 0) {
        out[i++] = '-';
    }
    out[i] = '\0';
}

const char *lg_uaf_word(uaf_err_t err)
{
    switch (err) {
    case UAF_E_SHORT:        return "SHORT";
    case UAF_E_MAGIC:        return "MAGIC";
    case UAF_E_VERSION:      return "VERSION";
    case UAF_E_RECSIZE:      return "RECSIZE";
    case UAF_E_COUNT:        return "COUNT";
    case UAF_E_HDR_RESERVED: return "RESERVED";
    case UAF_E_ITERATIONS:   return "ITERATIONS";
    case UAF_E_SIZE:         return "SIZE";
    case UAF_E_CRC:          return "CRC";
    case UAF_E_NAME:
    case UAF_E_NAME_PAD:     return "NAME";
    case UAF_E_HOME:
    case UAF_E_HOME_PAD:     return "HOME";
    case UAF_E_REC_RESERVED: return "RESERVED";
    case UAF_E_DUPLICATE:    return "DUPLICATE";
    default:                 return "UNKNOWN";
    }
}
