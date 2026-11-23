/*
 * Device state, commands, pulses, status lines and the pin record ring.
 * Pure: the caller owns every struct and passes the tick in.
 */

#include <robco/klibc.h>

#include "dev.h"

/* State words per type: [type][on]. */
static const char *const dv_words[DV_TYPE_COUNT][2] = {
    { "CLOSED", "OPEN" },
    { "OFF", "ON" },
    { "STANDBY", "ACTIVE" },
    { "LOCKED", "UNLOCKED" },
    { "OFF", "ON" },
    { "CLEAR", "TRIPPED" },
};

static const struct {
    const char *word;
    dv_action_t act;
} dv_actions[] = {
    { "ON", DV_ACT_ON },
    { "OFF", DV_ACT_OFF },
    { "TOGGLE", DV_ACT_TOGGLE },
    { "PULSE", DV_ACT_PULSE },
    { "OPEN", DV_ACT_OPEN },
    { "CLOSE", DV_ACT_CLOSE },
    { "UNLOCK", DV_ACT_UNLOCK },
    { "LOCK", DV_ACT_LOCK },
    { "ACTIVATE", DV_ACT_ACTIVATE },
    { "STANDBY", DV_ACT_STANDBY },
};

static char dv_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static uint8_t dv_level(const dv_device_t *d, int on)
{
    return (uint8_t)((on != 0) == (d->active_high != 0));
}

void dv_reset(dv_set_t *set)
{
    unsigned i;

    if (set == NULL) {
        return;
    }
    for (i = 0; i < DV_MAX_DEVICES; i++) {
        k_memset(&set->st[i], 0, sizeof(set->st[i]));
        if (i < set->count) {
            set->st[i].level = dv_level(&set->dev[i], 0);
        }
    }
}

int dv_find(const dv_set_t *set, const char *name)
{
    unsigned i;

    if (set == NULL || name == NULL) {
        return DV_ERR_ARG;
    }
    for (i = 0; i < set->count; i++) {
        const char *a = set->dev[i].name;
        const char *b = name;

        while (*a != '\0' && *a == dv_upper(*b)) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return (int)i;
        }
    }
    return DV_ERR_NOSUCHDEV;
}

dv_action_t dv_action_parse(const char *word)
{
    unsigned i;

    if (word == NULL) {
        return DV_ACT_NONE;
    }
    for (i = 0; i < sizeof(dv_actions) / sizeof(dv_actions[0]); i++) {
        const char *a = dv_actions[i].word;
        const char *b = word;

        while (*a != '\0' && *a == dv_upper(*b)) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return dv_actions[i].act;
        }
    }
    return DV_ACT_NONE;
}

int dv_boot_ops(dv_set_t *set, dv_op_t *ops, unsigned max)
{
    unsigned i;

    if (set == NULL || ops == NULL || set->count > DV_MAX_DEVICES) {
        return DV_ERR_ARG;
    }
    if (max < set->count) {
        return DV_ERR_SPACE;
    }
    for (i = 0; i < set->count; i++) {
        const dv_device_t *d = &set->dev[i];
        dv_dstate_t *s = &set->st[i];
        dv_op_t *o = &ops[i];

        k_memset(o, 0, sizeof(*o));
        o->pin = d->pin;
        o->dev = (uint8_t)i;
        s->pending = 0;
        if (d->type == DV_TYPE_SENSOR) {
            o->kind = DV_OP_CONFIG_INPUT;
            o->pull = d->active_high ? DV_PULL_DOWN : DV_PULL_UP;
            s->on = 0;
            s->level = dv_level(d, 0);
        } else {
            o->kind = DV_OP_CONFIG_OUTPUT;
            s->on = d->initial_on ? 1 : 0;
            s->level = dv_level(d, s->on);
            o->level = s->level;
        }
    }
    return (int)set->count;
}

/* -1: not valid for the type; else 0 off, 1 on, 2 toggle, 3 pulse. */
static int dv_target(unsigned type, dv_action_t act)
{
    switch (act) {
    case DV_ACT_ON:
        return 1;
    case DV_ACT_OFF:
        return 0;
    case DV_ACT_TOGGLE:
        return 2;
    case DV_ACT_PULSE:
        return 3;
    case DV_ACT_OPEN:
        return type == DV_TYPE_DOOR ? 1 : -1;
    case DV_ACT_CLOSE:
        return type == DV_TYPE_DOOR ? 0 : -1;
    case DV_ACT_UNLOCK:
        return type == DV_TYPE_LOCK ? 1 : -1;
    case DV_ACT_LOCK:
        return type == DV_TYPE_LOCK ? 0 : -1;
    case DV_ACT_ACTIVATE:
        return type == DV_TYPE_TURRET ? 1 : -1;
    case DV_ACT_STANDBY:
        return type == DV_TYPE_TURRET ? 0 : -1;
    default:
        return -1;
    }
}

int dv_command(dv_set_t *set, unsigned idx, dv_action_t act, uint32_t privs,
               uint32_t now, dv_op_t *ops, unsigned max)
{
    const dv_device_t *d;
    dv_dstate_t *s;
    int t;
    int on;
    int pulse;

    if (set == NULL || ops == NULL || idx >= set->count) {
        return DV_ERR_ARG;
    }
    d = &set->dev[idx];
    s = &set->st[idx];
    if (d->type == DV_TYPE_SENSOR) {
        return DV_ERR_READONLY;
    }
    t = dv_target(d->type, act);
    if (t < 0) {
        return DV_ERR_BADACTION;
    }
    if ((privs & DV_PRIV_SYSTEM) == 0 && (privs & d->privs) != d->privs) {
        return DV_ERR_NOPRIV;
    }
    if (t == 3 && d->pulse_ms == 0) {
        return DV_ERR_NOPULSE;
    }
    if (max < DV_CMD_OPS) {
        return DV_ERR_SPACE;
    }
    on = (t == 2) ? !s->on : (t != 0);
    pulse = on && d->pulse_ms != 0;

    /* A new command always cancels a pending pulse end first. */
    s->pending = 0;
    s->on = (uint8_t)on;
    s->level = dv_level(d, on);
    k_memset(ops, 0, sizeof(*ops) * DV_CMD_OPS);
    ops[0].kind = DV_OP_WRITE;
    ops[0].pin = d->pin;
    ops[0].level = s->level;
    ops[0].dev = (uint8_t)idx;
    if (!pulse) {
        return 1;
    }
    s->pending = 1;
    s->due = now + d->pulse_ms;
    ops[1].kind = DV_OP_WRITE;
    ops[1].pin = d->pin;
    ops[1].level = dv_level(d, 0);
    ops[1].dev = (uint8_t)idx;
    ops[1].after_ms = d->pulse_ms;
    return 2;
}

int dv_tick(dv_set_t *set, uint32_t now, dv_op_t *ops, unsigned max)
{
    unsigned i;
    unsigned n = 0;

    if (set == NULL || (ops == NULL && max != 0)) {
        return DV_ERR_ARG;
    }
    for (i = 0; i < set->count && n < max; i++) {
        const dv_device_t *d = &set->dev[i];
        dv_dstate_t *s = &set->st[i];

        if (!s->pending || (int32_t)(now - s->due) < 0) {
            continue;
        }
        s->pending = 0;
        s->on = 0;
        s->level = dv_level(d, 0);
        k_memset(&ops[n], 0, sizeof(ops[n]));
        ops[n].kind = DV_OP_WRITE;
        ops[n].pin = d->pin;
        ops[n].level = s->level;
        ops[n].dev = (uint8_t)i;
        n++;
    }
    return (int)n;
}

int dv_sensor_update(dv_set_t *set, unsigned idx, int level)
{
    const dv_device_t *d;

    if (set == NULL || idx >= set->count || set->dev[idx].type != DV_TYPE_SENSOR) {
        return DV_ERR_ARG;
    }
    d = &set->dev[idx];
    set->st[idx].level = (uint8_t)(level != 0);
    set->st[idx].on = (uint8_t)((level != 0) == (d->active_high != 0));
    return DV_OK;
}

const char *dv_state_word(const dv_set_t *set, unsigned idx)
{
    if (set == NULL || idx >= set->count || set->dev[idx].type >= DV_TYPE_COUNT) {
        return "";
    }
    return dv_words[set->dev[idx].type][set->st[idx].on ? 1 : 0];
}

static int dv_fit(int n, char *buf, size_t size)
{
    if (n < 0 || (size_t)n >= size) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return DV_ERR_SPACE;
    }
    return n;
}

int dv_status_header(char *buf, size_t size)
{
    if (buf == NULL) {
        return DV_ERR_ARG;
    }
    return dv_fit(k_snprintf(buf, size, "%-8s %-6s %-31s %-8s %s",
                             "NAME", "TYPE", "LABEL", "STATE", "PIN"), buf, size);
}

int dv_status_line(const dv_set_t *set, unsigned idx, char *buf, size_t size)
{
    const dv_device_t *d;
    char type[8];
    unsigned i;

    if (set == NULL || buf == NULL || idx >= set->count) {
        return DV_ERR_ARG;
    }
    d = &set->dev[idx];
    k_strncpy(type, dv_type_name(d->type), sizeof(type) - 1);
    type[sizeof(type) - 1] = '\0';
    for (i = 0; type[i] != '\0'; i++) {
        type[i] = dv_upper(type[i]);
    }
    return dv_fit(k_snprintf(buf, size, "%-8s %-6s %-31s %-8s GPIO %u",
                             d->name, type, d->label, dv_state_word(set, idx),
                             (unsigned)d->pin), buf, size);
}

const char *dv_strerror(int code)
{
    switch (code) {
    case DV_OK:
        return "%DEVICE-S-NORMAL, normal successful completion";
    case DV_ERR_ARG:
        return "%DEVICE-F-BADARG, bad argument";
    case DV_ERR_NAME:
        return "%DEVICE-E-BADNAME, device name must be 1 to 8 of A-Z 0-9 _";
    case DV_ERR_DUPNAME:
        return "%DEVICE-E-DUPNAME, device name already used";
    case DV_ERR_TYPE:
        return "%DEVICE-E-BADTYPE, device type missing or unknown";
    case DV_ERR_GPIO:
        return "%DEVICE-E-BADGPIO, gpio missing or not a number";
    case DV_ERR_PINUSED:
        return "%DEVICE-E-PINUSED, pin is reserved by the system";
    case DV_ERR_PINSHARED:
        return "%DEVICE-E-PINSHARED, pin already used by another device";
    case DV_ERR_ACTIVE:
        return "%DEVICE-E-BADACTIVE, active must be high or low";
    case DV_ERR_INITIAL:
        return "%DEVICE-E-BADINITIAL, initial must be on or off, off for a pulse";
    case DV_ERR_PULSE:
        return "%DEVICE-E-BADPULSE, pulse_ms must be 0 or 10 to 10000";
    case DV_ERR_LABEL:
        return "%DEVICE-E-BADLABEL, label too long or not printable";
    case DV_ERR_PRIVS:
        return "%DEVICE-E-BADPRIVS, privs is not a number";
    case DV_ERR_SYNTAX:
        return "%DEVICE-E-SYNTAX, configuration file syntax error";
    case DV_ERR_NOSUCHDEV:
        return "%DEVICE-W-NOSUCHDEV, no such device";
    case DV_ERR_NOPRIV:
        return "%DEVICE-W-NOPRIV, insufficient privilege";
    case DV_ERR_READONLY:
        return "%DEVICE-W-READONLY, device is read-only";
    case DV_ERR_BADACTION:
        return "%DEVICE-W-BADACTION, action not valid for this device";
    case DV_ERR_NOPULSE:
        return "%DEVICE-W-NOPULSE, device has no pulse length";
    case DV_ERR_SPACE:
        return "%DEVICE-F-NOSPACE, output buffer too small";
    case DV_WARN_PULLUP:
        return "%DEVICE-W-PULLUP, active-high output on a pin pulled up at reset";
    default:
        return "%DEVICE-F-UNKNOWN, unknown error";
    }
}

void dv_rec_init(dv_rec_t *r)
{
    if (r != NULL) {
        k_memset(r, 0, sizeof(*r));
    }
}

void dv_rec_add(dv_rec_t *r, unsigned pin, int level, uint32_t tick)
{
    if (r == NULL) {
        return;
    }
    r->e[r->head].pin = (uint8_t)pin;
    r->e[r->head].level = (uint8_t)(level != 0);
    r->e[r->head].tick = tick;
    r->head = (r->head + 1) % DV_REC_SIZE;
    if (r->count < DV_REC_SIZE) {
        r->count++;
    }
    r->total++;
}

int dv_rec_format(const dv_rec_t *r, unsigned i, char *buf, size_t size)
{
    const dv_rec_entry_t *e;

    if (r == NULL || buf == NULL || i >= r->count) {
        return DV_ERR_ARG;
    }
    e = &r->e[(r->head + DV_REC_SIZE - r->count + i) % DV_REC_SIZE];
    return dv_fit(k_snprintf(buf, size, "pin=%u level=%u tick=%u",
                             (unsigned)e->pin, (unsigned)e->level, (unsigned)e->tick),
                  buf, size);
}
