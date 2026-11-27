/* The UOS.CFG reader. Pure. */

#include <stdint.h>

#include <robco/ini.h>
#include <robco/klibc.h>

#include "uos_settings.h"

static const char *const schemes[] = { "green", "amber", "blue", "white" };
static const char *const diffs[] = { "novice", "advanced", "expert", "master" };

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    uos_settings_t *s;
    uos_settings_report_t *r;
} ctx_t;

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive equality. */
static int same(const char *a, const char *b)
{
    while (*a != '\0' && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Index of `v` in names[], or -1. */
static int pick(const char *v, const char *const *names, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        if (same(v, names[i])) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * A decimal number, no sign, at most 9 digits. Returns 0 and the value,
 * or -1. Larger numbers are clamped by the caller as "big".
 */
static int number(const char *v, uint32_t *out, int *big)
{
    uint32_t n = 0;
    unsigned digits = 0;

    *big = 0;
    if (*v == '\0') {
        return -1;
    }
    for (; *v != '\0'; v++) {
        if (*v < '0' || *v > '9') {
            return -1;
        }
        if (digits >= 9) {
            *big = 1;
        } else {
            n = n * 10u + (uint32_t)(*v - '0');
        }
        if (n != 0 || *v != '0') {
            digits++;
        }
    }
    *out = n;
    return 0;
}

static void set_range(ctx_t *c, const char *v, unsigned *dst, unsigned lo, unsigned hi)
{
    uint32_t n;
    int big;

    if (number(v, &n, &big) != 0) {
        c->r->bad++;
        return;
    }
    if (big || n > hi) {
        n = hi;
        c->r->clamped++;
    } else if (n < lo) {
        n = lo;
        c->r->clamped++;
    }
    *dst = n;
    c->r->keys++;
}

static void set_choice(ctx_t *c, const char *v, int *dst, const char *const *names, unsigned n)
{
    int i = pick(v, names, n);

    if (i < 0) {
        c->r->bad++;
        return;
    }
    *dst = i;
    c->r->keys++;
}

/* Printable ASCII, 1 to UOS_NAME_MAX characters. */
static void set_name(ctx_t *c, const char *v)
{
    size_t n = k_strlen(v);
    size_t i;

    if (n == 0 || n > UOS_NAME_MAX) {
        c->r->bad++;
        return;
    }
    for (i = 0; i < n; i++) {
        if (v[i] < 0x20 || v[i] > 0x7e) {
            c->r->bad++;
            return;
        }
    }
    k_memcpy(c->s->name, v, n + 1);
    c->r->keys++;
}

/* A user name: 1 to UOS_USER_MAX of A-Z 0-9 $ _, any case, stored upper-case. */
static void set_user(ctx_t *c, const char *v)
{
    size_t n = k_strlen(v);
    size_t i;
    char out[UOS_USER_MAX + 1];

    if (n == 0 || n > UOS_USER_MAX) {
        c->r->bad++;
        return;
    }
    for (i = 0; i < n; i++) {
        char ch = v[i];

        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '$' || ch == '_')) {
            c->r->bad++;
            return;
        }
        out[i] = ch;
    }
    out[n] = '\0';
    k_memcpy(c->s->hack_user, out, n + 1);
    c->r->keys++;
}

static void set_bool(ctx_t *c, const char *v, int *dst)
{
    static const char *const yes[] = { "yes", "on", "true", "1" };
    static const char *const no[] = { "no", "off", "false", "0" };

    if (pick(v, yes, COUNT(yes)) >= 0) {
        *dst = 1;
    } else if (pick(v, no, COUNT(no)) >= 0) {
        *dst = 0;
    } else {
        c->r->bad++;
        return;
    }
    c->r->keys++;
}

static int on_key(const char *section, const char *key, const char *value, unsigned line,
                  void *arg)
{
    ctx_t *c = arg;
    uos_settings_t *s = c->s;

    (void)line;
    if (same(section, "system")) {
        if (same(key, "name")) {
            set_name(c, value);
        } else if (same(key, "scheme")) {
            set_choice(c, value, &s->scheme, schemes, COUNT(schemes));
        } else {
            c->r->unknown++;
        }
    } else if (same(section, "login")) {
        if (same(key, "retries")) {
            set_range(c, value, &s->login_retries, UOS_RETRIES_MIN, UOS_RETRIES_MAX);
        } else if (same(key, "lockout_s")) {
            set_range(c, value, &s->login_lockout_s, UOS_LOGIN_LOCK_MIN, UOS_LOGIN_LOCK_MAX);
        } else if (same(key, "idle_s")) {
            set_range(c, value, &s->login_idle_s, UOS_LOGIN_IDLE_MIN, UOS_LOGIN_IDLE_MAX);
        } else {
            c->r->unknown++;
        }
    } else if (same(section, "hack")) {
        if (same(key, "enabled")) {
            set_bool(c, value, &s->hack_enabled);
        } else if (same(key, "user")) {
            set_user(c, value);
        } else if (same(key, "attempts")) {
            set_range(c, value, &s->hack_attempts, UOS_ATTEMPTS_MIN, UOS_ATTEMPTS_MAX);
        } else if (same(key, "difficulty")) {
            set_choice(c, value, &s->hack_difficulty, diffs, COUNT(diffs));
        } else if (same(key, "lockout_s")) {
            set_range(c, value, &s->hack_lockout_s, UOS_HACK_LOCK_MIN, UOS_HACK_LOCK_MAX);
        } else {
            c->r->unknown++;
        }
    } else if (same(section, "watchdog")) {
        if (same(key, "enabled")) {
            set_bool(c, value, &s->hwdog_enabled);
        } else if (same(key, "timeout_ms")) {
            set_range(c, value, &s->hwdog_timeout_ms, UOS_HWDOG_MS_MIN, UOS_HWDOG_MS_MAX);
        } else {
            c->r->unknown++;
        }
    } else if (same(section, "input")) {
        if (same(key, "buttons")) {
            set_bool(c, value, &s->buttons_enabled);
        } else {
            c->r->unknown++;
        }
    } else if (same(section, "audio")) {
        if (same(key, "sound")) {
            set_bool(c, value, &s->sound_enabled);
        } else if (same(key, "fan")) {
            set_bool(c, value, &s->fan_enabled);
        } else if (same(key, "volume")) {
            set_range(c, value, &s->volume, UOS_VOLUME_MIN, UOS_VOLUME_MAX);
        } else if (same(key, "trace")) {
            set_bool(c, value, &s->trace_enabled);
        } else {
            c->r->unknown++;
        }
    } else {
        c->r->unknown++;
    }
    return 0;
}

void uos_settings_default(uos_settings_t *s)
{
    if (s == NULL) {
        return;
    }
    k_memset(s, 0, sizeof(*s));
    k_memcpy(s->name, UOS_DEFAULT_NAME, sizeof(UOS_DEFAULT_NAME));
    s->scheme = UOS_SCHEME_UNSET;
    s->login_retries = UOS_RETRIES_DEF;
    s->login_lockout_s = UOS_LOGIN_LOCK_DEF;
    s->login_idle_s = UOS_LOGIN_IDLE_DEF;
    s->hack_enabled = 1;
    k_memcpy(s->hack_user, UOS_DEFAULT_HACK_USER, sizeof(UOS_DEFAULT_HACK_USER));
    s->hack_attempts = UOS_ATTEMPTS_DEF;
    s->hack_difficulty = UOS_DIFF_ADVANCED;
    s->hack_lockout_s = UOS_HACK_LOCK_DEF;
    s->hwdog_enabled = 0;
    s->hwdog_timeout_ms = UOS_HWDOG_MS_DEF;
    s->buttons_enabled = 1;
    s->sound_enabled = 0;
    s->fan_enabled = 0;
    s->volume = UOS_VOLUME_DEF;
    s->trace_enabled = 0;
}

int uos_settings_parse(const char *text, size_t len, uos_settings_t *s,
                       uos_settings_report_t *r)
{
    ctx_t c;
    unsigned line = 0;
    int rc;

    if (s == NULL || r == NULL || (text == NULL && len != 0)) {
        return UOS_CFG_ARG;
    }
    k_memset(r, 0, sizeof(*r));
    c.s = s;
    c.r = r;
    rc = ini_parse(text, len, on_key, &c, &line);
    if (rc < 0) {
        r->err_line = line;
        return UOS_CFG_SYNTAX;
    }
    return UOS_CFG_OK;
}

const char *uos_scheme_name(int scheme)
{
    if (scheme == UOS_SCHEME_UNSET) {
        return "default";
    }
    return (scheme >= 0 && (unsigned)scheme < COUNT(schemes)) ? schemes[scheme] : "unknown";
}

const char *uos_difficulty_name(int diff)
{
    return (diff >= 0 && (unsigned)diff < COUNT(diffs)) ? diffs[diff] : "unknown";
}
