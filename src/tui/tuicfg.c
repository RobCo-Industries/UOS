/*
 * TERMINAL.CFG parser. Pure: text in, tui_menu_t out.
 *
 * ini_parse_sections() reports every section header. A [terminal] or
 * [entry N] section with no keys is a counted TUI_E_EMPTY. A repeated
 * [terminal] is TUI_E_DUP_SECTION and a repeated [entry N]
 * TUI_E_DUP_ENTRY: the first one wins, also when the repeat follows it
 * directly.
 */

#include "tui.h"

#include <robco/ini.h>
#include <robco/klibc.h>

static const char *const tui_err_text[TUI_E_COUNT + 1] = {
    "ok",
    "bad argument",
    "menu file does not parse",
    "unknown section",
    "unknown key",
    "key given twice",
    "bad entry number",
    "entry number given twice",
    "more than 16 entries",
    "bad or missing label",
    "bad or missing type",
    "bad target for the type",
    "bad privilege mask",
    "bad title",
    "bad header file name",
    "menus nested too deep",
    "not possible now",
    "buffer too small",
    "section has no keys",
    "section given twice",
};

const char *tui_strerror(int code)
{
    if (code > 0 || code < -TUI_E_COUNT) {
        return "unknown error";
    }
    return tui_err_text[-code];
}

static const char *const tui_types[] = {
    "?", "text", "menu", "command", "device", "holotape", "logout",
};

const char *tui_type_name(int type)
{
    if (type < TUI_T_TEXT || type > TUI_T_LOGOUT) {
        return tui_types[0];
    }
    return tui_types[type];
}

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int is_name_char(char c)
{
    c = up(c);
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_' || c == '-';
}

int tui_is_83(const char *s)
{
    unsigned n = 0;

    if (s == 0) {
        return 0;
    }
    while (is_name_char(*s)) {
        s++;
        n++;
    }
    if (n < 1 || n > 8) {
        return 0;
    }
    if (*s == 0) {
        return 1;
    }
    if (*s != '.') {
        return 0;
    }
    s++;
    n = 0;
    while (is_name_char(*s)) {
        s++;
        n++;
    }
    return *s == 0 && n <= 3;
}

static int printable(const char *s, unsigned max)
{
    unsigned n = 0;

    for (; *s; s++, n++) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7e) {
            return 0;
        }
    }
    return n <= max;
}

static void copy_upper(char *dst, const char *src)
{
    while (*src) {
        *dst++ = up(*src++);
    }
    *dst = 0;
}

enum { SEC_NONE, SEC_TERMINAL, SEC_ENTRY, SEC_SKIP };

/* Bits of keys seen in the current section. */
#define K_LABEL   0x1u
#define K_TYPE    0x2u
#define K_TARGET  0x4u
#define K_PRIVS   0x8u
#define K_TITLE   0x10u
#define K_HEADER  0x20u

typedef struct {
    tui_menu_t *m;
    uint32_t privs;
    int kind;
    int bad;                    /* the entry has an error */
    unsigned keys;
    unsigned sec_line;
    unsigned valid;             /* valid entries, visible and hidden */
    unsigned tkeys;             /* keys seen in [terminal] */
    unsigned nkeys;             /* keys in the current section */
    int in_section;             /* a section, or keys before any header, started */
    int terminal_seen;          /* a [terminal] header came already */
    /* Every [entry N] header, valid or not: bit N. A fixed list would
     * miss repeats once full. */
    uint8_t seen[(TUI_ENTRY_NUM_MAX + 8) / 8];
    tui_entry_t e;
} cfg_ctx_t;

static void add_err(cfg_ctx_t *c, int code, unsigned line, unsigned entry)
{
    tui_menu_t *m = c->m;

    if (m->errors < TUI_CFG_ERR_LOG) {
        m->err[m->errors].code = (int16_t)code;
        m->err[m->errors].line = (uint16_t)line;
        m->err[m->errors].entry = (uint16_t)entry;
    }
    if (m->errors < 0xffffu) {
        m->errors++;
    }
}

/* Parses "entry N". Returns N, 0 when the rest is not a number 1..999, -1 when not "entry". */
static int entry_num(const char *s)
{
    int n = 0;
    unsigned digits = 0;

    if (k_strncmp(s, "entry", 5) != 0 || (s[5] != ' ' && s[5] != '\t')) {
        return -1;
    }
    s += 5;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '0') {
        return 0;
    }
    for (; *s >= '0' && *s <= '9'; s++) {
        if (++digits > 3) {
            return 0;
        }
        n = n * 10 + (*s - '0');
    }
    if (*s != 0 || digits == 0) {
        return 0;
    }
    return n;
}

static int parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    unsigned digits = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (s += 2; *s; s++, digits++) {
            char c = up(*s);
            uint32_t d;

            if (c >= '0' && c <= '9') {
                d = (uint32_t)(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                d = (uint32_t)(c - 'A' + 10);
            } else {
                return 0;
            }
            if (digits >= 8) {
                return 0;
            }
            v = (v << 4) | d;
        }
    } else {
        for (; *s; s++, digits++) {
            uint32_t d;

            if (*s < '0' || *s > '9') {
                return 0;
            }
            d = (uint32_t)(*s - '0');
            if (v > (0xffffffffu - d) / 10u) {
                return 0;
            }
            v = v * 10u + d;
        }
    }
    if (digits == 0) {
        return 0;
    }
    *out = v;
    return 1;
}

static int parse_type(const char *s)
{
    int t;

    for (t = TUI_T_TEXT; t <= TUI_T_LOGOUT; t++) {
        if (k_strcmp(s, tui_types[t]) == 0) {
            return t;
        }
    }
    return 0;
}

/* Reads one word of up to `max` characters into w (upper-case). Returns the rest or 0. */
static const char *word(const char *s, char *w, unsigned max, int letters_only)
{
    unsigned n = 0;

    while (*s && *s != ' ' && *s != '\t') {
        char c = up(*s);
        int ok = (c >= 'A' && c <= 'Z') ||
                 (!letters_only && ((c >= '0' && c <= '9') || c == '_'));

        if (!ok || n >= max) {
            return 0;
        }
        w[n++] = c;
        s++;
    }
    if (n == 0) {
        return 0;
    }
    w[n] = 0;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* Checks and normalises e->target for e->type. Returns 1 when good. */
static int check_target(tui_entry_t *e)
{
    char *t = e->target;
    unsigned n = (unsigned)k_strlen(t);

    switch (e->type) {
    case TUI_T_TEXT:
        if (!tui_is_83(t)) {
            return 0;
        }
        copy_upper(t, t);
        return 1;
    case TUI_T_MENU:
        if (!tui_is_83(t) || n < 5) {
            return 0;
        }
        copy_upper(t, t);
        return k_strcmp(t + n - 4, ".CFG") == 0;
    case TUI_T_COMMAND:
        return n >= 1 && n <= TUI_COMMAND_MAX && printable(t, TUI_COMMAND_MAX);
    case TUI_T_DEVICE: {
        char name[TUI_DEVNAME_MAX + 1];
        char act[TUI_DEVWORD_MAX + 1];
        const char *s = word(t, name, TUI_DEVNAME_MAX, 0);

        if (s == 0 || (s = word(s, act, TUI_DEVWORD_MAX, 1)) == 0 || *s != 0) {
            return 0;
        }
        k_snprintf(t, TUI_TARGET_MAX + 1, "%s %s", name, act);
        return 1;
    }
    case TUI_T_HOLOTAPE:
        if (n == 0) {
            return 1;
        }
        if (!tui_is_83(t)) {
            return 0;
        }
        copy_upper(t, t);
        return 1;
    case TUI_T_LOGOUT:
        return n == 0;
    default:
        return 0;
    }
}

static int visible(uint32_t user, uint32_t need)
{
    return (user & TUI_PRIV_SYSTEM) != 0 || (need & ~user) == 0;
}

static void finish_entry(cfg_ctx_t *c)
{
    tui_menu_t *m = c->m;
    tui_entry_t *e = &c->e;
    unsigned i;

    if (c->bad) {
        return;
    }
    if (!(c->keys & K_LABEL)) {
        add_err(c, TUI_E_LABEL, c->sec_line, e->num);
        return;
    }
    if (!(c->keys & K_TYPE)) {
        add_err(c, TUI_E_TYPE, c->sec_line, e->num);
        return;
    }
    if (!check_target(e)) {
        add_err(c, TUI_E_TARGET, c->sec_line, e->num);
        return;
    }
    if (c->valid >= TUI_MAX_ENTRIES) {
        add_err(c, TUI_E_TOO_MANY, c->sec_line, e->num);
        return;
    }
    c->valid++;
    if (!visible(c->privs, e->privs)) {
        m->hidden++;
        return;
    }
    i = m->count;
    while (i > 0 && m->entry[i - 1].num > e->num) {
        m->entry[i] = m->entry[i - 1];
        i--;
    }
    m->entry[i] = *e;
    m->count++;
}

static void start_section(cfg_ctx_t *c, const char *section, unsigned line)
{
    int n;

    c->keys = 0;
    c->bad = 0;
    c->nkeys = 0;
    c->sec_line = line;
    if (k_strcmp(section, "terminal") == 0) {
        if (c->terminal_seen) {
            c->kind = SEC_SKIP;
            add_err(c, TUI_E_DUP_SECTION, line, 0);
            return;
        }
        c->terminal_seen = 1;
        c->kind = SEC_TERMINAL;
        return;
    }
    n = entry_num(section);
    if (n < 0) {
        c->kind = SEC_SKIP;
        add_err(c, TUI_E_SECTION, line, 0);
        return;
    }
    if (n == 0) {
        c->kind = SEC_SKIP;
        add_err(c, TUI_E_ENTRY_NUM, line, 0);
        return;
    }
    if (c->seen[n / 8] & (1u << (n % 8))) {
        c->kind = SEC_SKIP;
        add_err(c, TUI_E_DUP_ENTRY, line, (unsigned)n);
        return;
    }
    c->seen[n / 8] |= (uint8_t)(1u << (n % 8));
    c->kind = SEC_ENTRY;
    k_memset(&c->e, 0, sizeof(c->e));
    c->e.num = (uint16_t)n;
}

static void finish_section(cfg_ctx_t *c)
{
    if ((c->kind == SEC_ENTRY || c->kind == SEC_TERMINAL) && c->nkeys == 0) {
        add_err(c, TUI_E_EMPTY, c->sec_line, c->kind == SEC_ENTRY ? c->e.num : 0);
    } else if (c->kind == SEC_ENTRY) {
        finish_entry(c);
    }
    c->kind = SEC_NONE;
}

/* Returns 1 when the key was seen before in this section (error counted). */
static int dup_key(cfg_ctx_t *c, unsigned *keys, unsigned bit, unsigned line)
{
    if (*keys & bit) {
        add_err(c, TUI_E_DUP_KEY, line, c->kind == SEC_ENTRY ? c->e.num : 0);
        return 1;
    }
    *keys |= bit;
    return 0;
}

static void terminal_key(cfg_ctx_t *c, const char *key, const char *value, unsigned line)
{
    tui_menu_t *m = c->m;

    if (k_strcmp(key, "title") == 0) {
        if (dup_key(c, &c->tkeys, K_TITLE, line)) {
            return;
        }
        if (!printable(value, TUI_TITLE_MAX)) {
            add_err(c, TUI_E_TITLE, line, 0);
            return;
        }
        k_strncpy(m->title, value, sizeof(m->title));
    } else if (k_strcmp(key, "header") == 0) {
        if (dup_key(c, &c->tkeys, K_HEADER, line)) {
            return;
        }
        if (value[0] == 0) {
            return;
        }
        if (!tui_is_83(value)) {
            add_err(c, TUI_E_HEADER, line, 0);
            return;
        }
        copy_upper(m->header, value);
    } else {
        add_err(c, TUI_E_KEY, line, 0);
    }
}

static void entry_key(cfg_ctx_t *c, const char *key, const char *value, unsigned line)
{
    tui_entry_t *e = &c->e;
    int bad = 0;
    int code = 0;

    if (k_strcmp(key, "label") == 0) {
        if (dup_key(c, &c->keys, K_LABEL, line)) {
            return;
        }
        if (value[0] == 0 || !printable(value, TUI_LABEL_MAX)) {
            code = TUI_E_LABEL;
            bad = 1;
        } else {
            k_strncpy(e->label, value, sizeof(e->label));
        }
    } else if (k_strcmp(key, "type") == 0) {
        if (dup_key(c, &c->keys, K_TYPE, line)) {
            return;
        }
        e->type = (uint8_t)parse_type(value);
        if (e->type == 0) {
            code = TUI_E_TYPE;
            bad = 1;
        }
    } else if (k_strcmp(key, "target") == 0) {
        if (dup_key(c, &c->keys, K_TARGET, line)) {
            return;
        }
        k_strncpy(e->target, value, sizeof(e->target) - 1);
    } else if (k_strcmp(key, "privs") == 0) {
        if (dup_key(c, &c->keys, K_PRIVS, line)) {
            return;
        }
        if (!parse_u32(value, &e->privs)) {
            code = TUI_E_PRIVS;
            bad = 1;
        }
    } else {
        code = TUI_E_KEY;
        bad = 1;
    }
    if (bad && !c->bad) {
        c->bad = 1;
        add_err(c, code, line, e->num);
    }
}

static int on_key(const char *section, const char *key, const char *value,
                  unsigned line, void *ctx)
{
    cfg_ctx_t *c = ctx;

    if (!c->in_section) {
        /* A key before any section header. */
        c->in_section = 1;
        start_section(c, section, line);
    }
    c->nkeys++;
    if (c->kind == SEC_TERMINAL) {
        terminal_key(c, key, value, line);
    } else if (c->kind == SEC_ENTRY) {
        entry_key(c, key, value, line);
    }
    return 0;
}

/* Each header closes the section before it and opens a new one. */
static int on_section(const char *section, unsigned line, void *ctx)
{
    cfg_ctx_t *c = ctx;

    finish_section(c);
    c->in_section = 1;
    start_section(c, section, line);
    return 0;
}

int tui_cfg_parse(const char *text, size_t len, uint32_t user_privs,
                  tui_menu_t *m, unsigned *err_line)
{
    cfg_ctx_t c;
    unsigned line = 0;
    int rc;

    if (err_line) {
        *err_line = 0;
    }
    if (m == 0 || (text == 0 && len > 0)) {
        return TUI_E_ARG;
    }
    k_memset(m, 0, sizeof(*m));
    k_memset(&c, 0, sizeof(c));
    c.m = m;
    c.privs = user_privs;
    c.kind = SEC_NONE;
    rc = ini_parse_sections(text ? text : "", len, on_key, on_section, &c, &line);
    if (rc < 0) {
        k_memset(m, 0, sizeof(*m));
        if (err_line) {
            *err_line = line;
        }
        return TUI_E_SYNTAX;
    }
    finish_section(&c);
    return TUI_OK;
}
