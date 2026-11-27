/*
 * DEVICES.CFG parser. Pure: text in, device table out.
 *
 * ini_parse_sections() reports every section header, so a device or
 * expander section with no keys is a counted DV_ERR_EMPTY, and a repeated
 * header opens a new section: DV_ERR_DUPNAME for a device, DV_ERR_EXPANDER
 * for an expander. Every header is remembered, valid or not, so a repeat
 * is refused even when the first section was empty or bad: the first
 * section wins. Section errors carry the header's line.
 */

#include <robco/gpiox.h>
#include <robco/ini.h>
#include <robco/klibc.h>

#include "dev.h"

typedef struct {
    dv_set_t *set;
    dv_cfg_result_t *res;
    uint32_t refused;       /* refused pin mask */
    char section[INI_MAX_SECTION + 1];
    int have_section;       /* section[] holds the current one */
    int is_device;          /* the current section is [device NAME] */
    int is_expander;        /* the current section is [expander N] */
    int exp_chip;           /* N of the current expander, or -1 */
    int seen_addr;
    uint8_t exp_addr;
    unsigned lines[DV_MAX_DEVICES];  /* first line of each loaded device */
    int over;               /* past the DV_MAX_DEVICES-th device section: ignored */
    unsigned nkeys;         /* keys in the current section */
    unsigned key_seen;      /* bit per known key given in the current section */
    unsigned sections;      /* device sections seen so far */
    char seen[DV_MAX_DEVICES][DV_NAME_MAX + 1];  /* every good device name in a header */
    unsigned nseen;
    uint8_t exp_seen;       /* bit N: an [expander N] header came, good or not */
    int err;                /* first bad field, DV_ERR_* or 0 */
    unsigned err_line;
    unsigned first_line;
    int seen_type;
    int seen_gpio;
    char raw[DV_NAME_MAX + 1];
    dv_device_t d;
} dv_parse_t;

static const char *const dv_type_names[DV_TYPE_COUNT] = {
    "door", "light", "turret", "lock", "relay", "sensor",
};

static char dv_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int dv_eq_nocase(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (dv_lower(*a) != dv_lower(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/*
 * The masks in dev.h name the refused pins below DV_PIN_LIMIT. Pins 28
 * and up are always refused: audio on Pi 1 to 3 (40, 41, 45), SD routing
 * (34 to 39, 48 to 53) and pins not on the header.
 */
int dv_pin_refused_in(uint32_t refused, unsigned pin)
{
    if (pin >= DV_PIN_LIMIT) {
        return 1;
    }
    return (refused >> pin) & 1u;
}

int dv_pin_refused(unsigned pin)
{
    return dv_pin_refused_in(DV_REFUSED_DEFAULT, pin);
}

static void dv_add_err(dv_cfg_result_t *res, int code, unsigned line, const char *name)
{
    if (res->err_count < DV_MAX_ERRORS) {
        dv_cfg_err_t *e = &res->errs[res->err_count];

        e->code = code;
        e->line = line;
        k_strncpy(e->name, name, DV_NAME_MAX);
        e->name[DV_NAME_MAX] = '\0';
    }
    res->err_count++;
}

static void dv_add_warn(dv_cfg_result_t *res, int code, unsigned line, const char *name)
{
    if (res->warn_count < DV_MAX_ERRORS) {
        dv_cfg_err_t *e = &res->warns[res->warn_count];

        e->code = code;
        e->line = line;
        k_strncpy(e->name, name, DV_NAME_MAX);
        e->name[DV_NAME_MAX] = '\0';
    }
    res->warn_count++;
}

static void dv_field_err(dv_parse_t *p, int code, unsigned line)
{
    if (p->err == 0) {
        p->err = code;
        p->err_line = line;
    }
}

/* Closes an [expander N] section: declares the chip or adds an error. */
static void dv_finish_expander(dv_parse_t *p)
{
    dv_set_t *set = p->set;
    unsigned i;

    if (p->err == 0 && p->nkeys == 0) {
        dv_field_err(p, DV_ERR_EMPTY, p->first_line);
    }
    if (p->err == 0 && !p->seen_addr) {
        dv_field_err(p, DV_ERR_EXPADDR, p->first_line);
    }
    if (p->err == 0 && set->exp[p->exp_chip].declared) {
        dv_field_err(p, DV_ERR_EXPANDER, p->first_line);
    }
    for (i = 0; p->err == 0 && i < DV_MAX_EXPANDERS; i++) {
        if (set->exp[i].declared && set->exp[i].addr == p->exp_addr) {
            dv_field_err(p, DV_ERR_EXPADDR, p->first_line);
        }
    }
    if (p->err != 0) {
        dv_add_err(p->res, p->err, p->err_line, p->raw);
        return;
    }
    set->exp[p->exp_chip].declared = 1;
    set->exp[p->exp_chip].addr = p->exp_addr;
}

/* Closes the current device section: loads, skips or ignores it. */
static void dv_finish(dv_parse_t *p)
{
    dv_set_t *set = p->set;
    unsigned i;

    if (p->have_section && p->is_expander) {
        dv_finish_expander(p);
        return;
    }
    if (!p->have_section || !p->is_device) {
        return;
    }
    if (p->over) {
        p->res->ignored++;
        return;
    }
    if (p->err == 0 && p->nkeys == 0) {
        dv_field_err(p, DV_ERR_EMPTY, p->first_line);
    }
    if (p->err == 0 && !p->seen_type) {
        dv_field_err(p, DV_ERR_TYPE, p->first_line);
    }
    if (p->err == 0 && !p->seen_gpio) {
        dv_field_err(p, DV_ERR_GPIO, p->first_line);
    }
    if (p->err == 0 && p->d.type == DV_TYPE_SENSOR && p->d.pulse_ms != 0) {
        dv_field_err(p, DV_ERR_PULSE, p->first_line);
    }
    if (p->err == 0 && p->d.type == DV_TYPE_SENSOR) {
        p->d.initial_on = 0;
    }
    if (p->err == 0 && p->d.pulse_ms != 0 && p->d.initial_on) {
        dv_field_err(p, DV_ERR_INITIAL, p->first_line);
    }
    for (i = 0; p->err == 0 && i < set->count; i++) {
        if (k_strcmp(set->dev[i].name, p->d.name) == 0) {
            dv_field_err(p, DV_ERR_DUPNAME, p->first_line);
        } else if (set->dev[i].chip == p->d.chip && set->dev[i].pin == p->d.pin) {
            dv_field_err(p, DV_ERR_PINSHARED, p->first_line);
        }
    }
    if (p->err != 0) {
        p->res->skipped++;
        dv_add_err(p->res, p->err, p->err_line, p->raw);
        return;
    }
    if (p->d.chip == DV_CHIP_PI && p->d.type != DV_TYPE_SENSOR && p->d.active_high &&
        ((DV_PINS_RESET_PULLUP >> p->d.pin) & 1u)) {
        dv_add_warn(p->res, DV_WARN_PULLUP, p->first_line, p->d.name);
    }
    p->lines[set->count] = p->first_line;
    set->dev[set->count] = p->d;
    set->count++;
    p->res->loaded++;
}

/* Opens a section. Device sections are "device" then spaces then a name. */
static void dv_start(dv_parse_t *p, const char *section, unsigned line)
{
    const char *n;
    size_t len;
    size_t i;

    k_strncpy(p->section, section, INI_MAX_SECTION);
    p->section[INI_MAX_SECTION] = '\0';
    p->have_section = 1;
    p->is_device = 0;
    p->is_expander = 0;
    p->exp_chip = -1;
    p->seen_addr = 0;
    p->exp_addr = 0;
    p->over = 0;
    p->nkeys = 0;
    p->key_seen = 0;
    p->err = 0;
    p->err_line = 0;
    p->first_line = line;
    p->seen_type = 0;
    p->seen_gpio = 0;
    p->raw[0] = '\0';
    k_memset(&p->d, 0, sizeof(p->d));
    p->d.active_high = 1;
    p->d.privs = DV_PRIVS_DEFAULT;
    p->d.chip = DV_CHIP_PI;

    if (k_strncmp(section, "expander", 8) == 0 && (section[8] == ' ' || section[8] == '\t')) {
        p->is_expander = 1;
        n = section + 8;
        while (*n == ' ' || *n == '\t') {
            n++;
        }
        p->raw[0] = 'X';
        k_strncpy(p->raw + 1, n, DV_NAME_MAX - 1);
        p->raw[DV_NAME_MAX] = '\0';
        if (n[0] >= '0' && n[0] < (char)('0' + DV_MAX_EXPANDERS) && n[1] == '\0') {
            p->exp_chip = n[0] - '0';
            if (p->exp_seen & (1u << p->exp_chip)) {
                dv_field_err(p, DV_ERR_EXPANDER, line);
            }
            p->exp_seen |= (uint8_t)(1u << p->exp_chip);
        } else {
            dv_field_err(p, DV_ERR_EXPANDER, line);
        }
        return;
    }
    if (k_strncmp(section, "device", 6) != 0 || (section[6] != ' ' && section[6] != '\t')) {
        return;
    }
    p->is_device = 1;
    n = section + 6;
    while (*n == ' ' || *n == '\t') {
        n++;
    }
    k_strncpy(p->raw, n, DV_NAME_MAX);
    p->raw[DV_NAME_MAX] = '\0';
    if (p->sections >= DV_MAX_DEVICES) {
        p->over = 1;
        return;
    }
    p->sections++;
    len = k_strlen(n);
    if (len == 0 || len > DV_NAME_MAX) {
        dv_field_err(p, DV_ERR_NAME, line);
        return;
    }
    for (i = 0; i < len; i++) {
        char c = n[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            dv_field_err(p, DV_ERR_NAME, line);
            return;
        }
        p->d.name[i] = c;
    }
    p->d.name[len] = '\0';
    for (i = 0; i < p->nseen; i++) {
        if (k_strcmp(p->seen[i], p->d.name) == 0) {
            dv_field_err(p, DV_ERR_DUPNAME, line);
            return;
        }
    }
    k_memcpy(p->seen[p->nseen++], p->d.name, len + 1);
}

/* Decimal, or 0x hex, into 32 bits. Returns 0 on success. */
static int dv_parse_u32(const char *s, int allow_hex, uint32_t *out)
{
    uint32_t v = 0;
    unsigned base = 10;
    int digits = 0;

    if (allow_hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (; *s != '\0'; s++) {
        unsigned d;
        char c = dv_lower(*s);

        if (c >= '0' && c <= '9') {
            d = (unsigned)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            d = (unsigned)(c - 'a' + 10);
        } else {
            return -1;
        }
        if (v > (0xFFFFFFFFu - d) / base) {
            return -1;
        }
        v = v * base + d;
        digits++;
    }
    if (digits == 0) {
        return -1;
    }
    *out = v;
    return 0;
}

/* Keys of a [device NAME] section, case-sensitive. */
static const char *const dv_key_names[] = {
    "type", "gpio", "active", "initial", "pulse_ms", "label", "privs",
};

/*
 * Marks `key` seen in the current section. An unknown key, or one given
 * twice, refuses the section. Returns 1 when the key may be read.
 */
static int dv_key_check(dv_parse_t *p, const char *const *names, unsigned count,
                        const char *key, unsigned line)
{
    unsigned i;

    for (i = 0; i < count; i++) {
        if (k_strcmp(key, names[i]) == 0) {
            break;
        }
    }
    if (i == count) {
        p->res->unknown++;
        dv_field_err(p, DV_ERR_KEY, line);
        return 0;
    }
    if (p->key_seen & (1u << i)) {
        dv_field_err(p, DV_ERR_DUPKEY, line);
        return 0;
    }
    p->key_seen |= 1u << i;
    return 1;
}

static void dv_key(dv_parse_t *p, const char *key, const char *value, unsigned line)
{
    uint32_t v;
    unsigned i;

    if (!dv_key_check(p, dv_key_names, sizeof(dv_key_names) / sizeof(dv_key_names[0]),
                      key, line)) {
        return;
    }

    if (k_strcmp(key, "type") == 0) {
        p->seen_type = 1;
        for (i = 0; i < DV_TYPE_COUNT; i++) {
            if (dv_eq_nocase(value, dv_type_names[i])) {
                p->d.type = (uint8_t)i;
                return;
            }
        }
        dv_field_err(p, DV_ERR_TYPE, line);
    } else if (k_strcmp(key, "gpio") == 0) {
        p->seen_gpio = 1;
        if (value[0] == 'X' || value[0] == 'x') {
            /* X<chip>.<pin>: one digit each. */
            if (k_strlen(value) != 4 || value[2] != '.' ||
                value[1] < '0' || value[1] >= (char)('0' + DV_MAX_EXPANDERS) ||
                value[3] < '0' || value[3] >= (char)('0' + DV_EXP_PINS)) {
                dv_field_err(p, DV_ERR_GPIO, line);
            } else {
                p->d.chip = (uint8_t)(value[1] - '0');
                p->d.pin = (uint8_t)(value[3] - '0');
            }
        } else if (k_strlen(value) > 3 || dv_parse_u32(value, 0, &v) != 0) {
            dv_field_err(p, DV_ERR_GPIO, line);
        } else if (dv_pin_refused_in(p->refused, v)) {
            dv_field_err(p, DV_ERR_PINUSED, line);
        } else {
            p->d.chip = DV_CHIP_PI;
            p->d.pin = (uint8_t)v;
        }
    } else if (k_strcmp(key, "active") == 0) {
        if (dv_eq_nocase(value, "high")) {
            p->d.active_high = 1;
        } else if (dv_eq_nocase(value, "low")) {
            p->d.active_high = 0;
        } else {
            dv_field_err(p, DV_ERR_ACTIVE, line);
        }
    } else if (k_strcmp(key, "initial") == 0) {
        if (dv_eq_nocase(value, "on")) {
            p->d.initial_on = 1;
        } else if (dv_eq_nocase(value, "off")) {
            p->d.initial_on = 0;
        } else {
            dv_field_err(p, DV_ERR_INITIAL, line);
        }
    } else if (k_strcmp(key, "pulse_ms") == 0) {
        if (k_strlen(value) > 5 || dv_parse_u32(value, 0, &v) != 0 ||
            (v != 0 && (v < DV_PULSE_MIN_MS || v > DV_PULSE_MAX_MS))) {
            dv_field_err(p, DV_ERR_PULSE, line);
        } else {
            p->d.pulse_ms = (uint16_t)v;
        }
    } else if (k_strcmp(key, "label") == 0) {
        size_t len = k_strlen(value);

        if (len > DV_LABEL_MAX) {
            dv_field_err(p, DV_ERR_LABEL, line);
            return;
        }
        for (i = 0; i < len; i++) {
            if (value[i] < 0x20 || value[i] > 0x7e) {
                dv_field_err(p, DV_ERR_LABEL, line);
                return;
            }
        }
        k_memcpy(p->d.label, value, len + 1);
    } else if (k_strcmp(key, "privs") == 0) {
        if (dv_parse_u32(value, 1, &v) != 0) {
            dv_field_err(p, DV_ERR_PRIVS, line);
        } else {
            p->d.privs = v;
        }
    }
}

/*
 * After the whole file: skip each device on an expander that no good
 * [expander N] declared, so a chip may be declared after its devices.
 */
static void dv_check_chips(dv_parse_t *p)
{
    dv_set_t *set = p->set;
    unsigned i = 0;
    unsigned j;

    while (i < set->count) {
        const dv_device_t *d = &set->dev[i];

        if (d->chip == DV_CHIP_PI || set->exp[d->chip].declared) {
            i++;
            continue;
        }
        dv_add_err(p->res, DV_ERR_NOCHIP, p->lines[i], d->name);
        p->res->loaded--;
        p->res->skipped++;
        for (j = i + 1; j < set->count; j++) {
            set->dev[j - 1] = set->dev[j];
            p->lines[j - 1] = p->lines[j];
        }
        set->count--;
        k_memset(&set->dev[set->count], 0, sizeof(set->dev[set->count]));
    }
}

int dv_on_at_reset(const dv_device_t *d)
{
    unsigned up;

    if (d == NULL || d->chip != DV_CHIP_PI || d->type == DV_TYPE_SENSOR || d->pin >= 32) {
        return 0;
    }
    up = (DV_PINS_RESET_PULLUP >> d->pin) & 1u;
    return up == (d->active_high != 0);
}

int dv_arm_ok(const dv_set_t *set)
{
    unsigned i;

    if (set == NULL) {
        return 0;
    }
    for (i = 0; i < set->count && i < DV_MAX_DEVICES; i++) {
        const dv_device_t *d = &set->dev[i];

        if (d->type == DV_TYPE_RELAY && k_strcmp(d->name, DV_ARM_NAME) == 0) {
            return d->chip == DV_CHIP_PI && !dv_on_at_reset(d);
        }
    }
    return 0;
}

/* After the whole file and dv_check_chips(): the ARM rule of dev.h. */
static void dv_check_arm(dv_parse_t *p)
{
    dv_set_t *set = p->set;
    unsigned i;

    if (dv_arm_ok(set)) {
        return;
    }
    for (i = 0; i < set->count; i++) {
        dv_device_t *d = &set->dev[i];

        if (d->chip != DV_CHIP_PI && d->type != DV_TYPE_SENSOR) {
            d->refused = DV_ERR_NOARM;
            dv_add_err(p->res, DV_ERR_NOARM, p->lines[i], d->name);
        }
    }
}

/* Each header closes the section before it and opens a new one. */
static int dv_on_section(const char *section, unsigned line, void *ctx)
{
    dv_parse_t *p = ctx;

    dv_finish(p);
    dv_start(p, section, line);
    return 0;
}

static int dv_handler(const char *section, const char *key, const char *value,
                      unsigned line, void *ctx)
{
    dv_parse_t *p = ctx;

    if (!p->have_section) {
        /* A key before any section header. */
        dv_start(p, section, line);
    }
    p->nkeys++;
    if (p->is_expander) {
        static const char *const exp_keys[] = { "addr" };
        uint32_t v;

        if (dv_key_check(p, exp_keys, 1, key, line)) {
            p->seen_addr = 1;
            if (k_strlen(value) > 4 || dv_parse_u32(value, 1, &v) != 0 || !gpiox_addr_valid(v)) {
                dv_field_err(p, DV_ERR_EXPADDR, line);
            } else {
                p->exp_addr = (uint8_t)v;
            }
        }
        return 0;
    }
    if (!p->is_device) {
        p->res->unknown++;
        return 0;
    }
    if (p->over) {
        return 0;
    }
    dv_key(p, key, value, line);
    return 0;
}

int dv_cfg_parse(const char *text, size_t len, dv_set_t *set, dv_cfg_result_t *res)
{
    return dv_cfg_parse_pins(text, len, DV_REFUSED_DEFAULT, set, res);
}

int dv_cfg_parse_pins(const char *text, size_t len, uint32_t refused,
                      dv_set_t *set, dv_cfg_result_t *res)
{
    dv_parse_t p;
    unsigned line = 0;
    int rc;

    if (set == NULL || res == NULL || (text == NULL && len != 0)) {
        return DV_ERR_ARG;
    }
    k_memset(set, 0, sizeof(*set));
    k_memset(res, 0, sizeof(*res));
    k_memset(&p, 0, sizeof(p));
    p.set = set;
    p.res = res;
    p.refused = refused;
    if (len == 0) {
        return DV_OK;
    }
    rc = ini_parse_sections(text, len, dv_handler, dv_on_section, &p, &line);
    if (rc != 0) {
        res->parse_rc = rc;
        res->parse_line = line;
        dv_add_err(res, DV_ERR_SYNTAX, line, "");
        dv_check_chips(&p);
        dv_check_arm(&p);
        return DV_ERR_SYNTAX;
    }
    dv_finish(&p);
    dv_check_chips(&p);
    dv_check_arm(&p);
    return DV_OK;
}

int dv_cfg_summary(const dv_cfg_result_t *res, char *buf, size_t size)
{
    int n;

    if (res == NULL || buf == NULL) {
        return DV_ERR_ARG;
    }
    n = k_snprintf(buf, size, "loaded=%u skipped=%u unknown=%u ignored=%u errors=%u parse=%d warnings=%u",
                   res->loaded, res->skipped, res->unknown, res->ignored,
                   res->err_count, res->parse_rc, res->warn_count);
    if (n < 0 || (size_t)n >= size) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return DV_ERR_SPACE;
    }
    return n;
}

const char *dv_type_name(unsigned type)
{
    return type < DV_TYPE_COUNT ? dv_type_names[type] : "";
}
