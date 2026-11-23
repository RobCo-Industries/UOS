/*
 * DEVICES.CFG parser. Pure: text in, device table out.
 *
 * PENDING LIBRARY FEATURE: ini_parse() reports keys only, so a section
 * with no keys is never seen, and a repeated section header is not
 * reported. RobCo-Core will add ini_parse_sections() in a later pin
 * bump. Then switch to it so an empty or repeated section is a counted
 * error. See docs/ARCHITECTURE.md, "Pending library features".
 */

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
    int over;               /* past the 16th device section: ignored */
    unsigned sections;      /* device sections seen so far */
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

/* Closes the current device section: loads, skips or ignores it. */
static void dv_finish(dv_parse_t *p)
{
    dv_set_t *set = p->set;
    unsigned i;

    if (!p->have_section || !p->is_device) {
        return;
    }
    if (p->over) {
        p->res->ignored++;
        return;
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
        } else if (set->dev[i].pin == p->d.pin) {
            dv_field_err(p, DV_ERR_PINSHARED, p->first_line);
        }
    }
    if (p->err != 0) {
        p->res->skipped++;
        dv_add_err(p->res, p->err, p->err_line, p->raw);
        return;
    }
    if (p->d.type != DV_TYPE_SENSOR && p->d.active_high &&
        ((DV_PINS_RESET_PULLUP >> p->d.pin) & 1u)) {
        dv_add_warn(p->res, DV_WARN_PULLUP, p->first_line, p->d.name);
    }
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
    p->over = 0;
    p->err = 0;
    p->err_line = 0;
    p->first_line = line;
    p->seen_type = 0;
    p->seen_gpio = 0;
    p->raw[0] = '\0';
    k_memset(&p->d, 0, sizeof(p->d));
    p->d.active_high = 1;
    p->d.privs = DV_PRIVS_DEFAULT;

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

static void dv_key(dv_parse_t *p, const char *key, const char *value, unsigned line)
{
    uint32_t v;
    unsigned i;

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
        if (k_strlen(value) > 3 || dv_parse_u32(value, 0, &v) != 0) {
            dv_field_err(p, DV_ERR_GPIO, line);
        } else if (dv_pin_refused_in(p->refused, v)) {
            dv_field_err(p, DV_ERR_PINUSED, line);
        } else {
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
    } else {
        p->res->unknown++;
    }
}

static int dv_handler(const char *section, const char *key, const char *value,
                      unsigned line, void *ctx)
{
    dv_parse_t *p = ctx;

    if (!p->have_section || k_strcmp(section, p->section) != 0) {
        dv_finish(p);
        dv_start(p, section, line);
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
    rc = ini_parse(text, len, dv_handler, &p, &line);
    if (rc != 0) {
        res->parse_rc = rc;
        res->parse_line = line;
        dv_add_err(res, DV_ERR_SYNTAX, line, "");
        return DV_ERR_SYNTAX;
    }
    dv_finish(&p);
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
