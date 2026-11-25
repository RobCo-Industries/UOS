/*
 * The device service's rules: debounce, the boot and shutdown order with
 * the ARM relay, and pin operations mapped to calls. Pure: devsplan.h.
 */

#include <robco/klibc.h>

#include "devsplan.h"
#include "vfs.h"

void devs_deb_seed(devs_deb_t *d, int level)
{
    if (d == NULL) {
        return;
    }
    d->stable = level != 0;
    d->cand = d->stable;
    d->count = 0;
}

int devs_deb_feed(devs_deb_t *d, int level, unsigned need)
{
    uint8_t l = level != 0;

    if (d == NULL) {
        return 0;
    }
    if (need == 0) {
        need = 1;
    }
    if (l == d->stable) {
        d->cand = l;
        d->count = 0;
        return 0;
    }
    if (l != d->cand) {
        d->cand = l;
        d->count = 0;
    }
    if (d->count < 255) {
        d->count++;
    }
    if (d->count >= need) {
        d->stable = l;
        d->count = 0;
        return 1;
    }
    return 0;
}

int devs_arm_index(const dv_set_t *set)
{
    unsigned i;

    if (set == NULL) {
        return -1;
    }
    for (i = 0; i < set->count && i < DV_MAX_DEVICES; i++) {
        if (set->dev[i].type == DV_TYPE_RELAY && k_strcmp(set->dev[i].name, DEVS_ARM_NAME) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int devs_boot_plan(dv_set_t *set, dv_op_t *ops, unsigned max)
{
    int n = dv_boot_ops(set, ops, max);
    int arm = devs_arm_index(set);
    dv_op_t a;
    int i;

    if (n <= 0 || arm < 0) {
        return n;
    }
    /* One operation per device in table order: ops[arm] is ARM's. */
    a = ops[arm];
    for (i = arm; i + 1 < n; i++) {
        ops[i] = ops[i + 1];
    }
    ops[n - 1] = a;
    return n;
}

static void down_op(const dv_set_t *set, unsigned i, int on, dv_op_t *o)
{
    const dv_device_t *d = &set->dev[i];

    k_memset(o, 0, sizeof(*o));
    o->kind = DV_OP_CONFIG_OUTPUT;
    o->chip = d->chip;
    o->pin = d->pin;
    o->dev = (uint8_t)i;
    o->level = (uint8_t)((on != 0) == (d->active_high != 0));
}

int devs_down_plan(const dv_set_t *set, dv_op_t *ops, unsigned max)
{
    unsigned i, n = 0, need = 0;
    int arm;

    if (set == NULL || ops == NULL || set->count > DV_MAX_DEVICES) {
        return DV_ERR_ARG;
    }
    for (i = 0; i < set->count; i++) {
        need += set->dev[i].type != DV_TYPE_SENSOR;
    }
    if (max < need) {
        return DV_ERR_SPACE;
    }
    arm = devs_arm_index(set);
    if (arm >= 0) {
        down_op(set, (unsigned)arm, 0, &ops[n++]);
    }
    for (i = set->count; i-- > 0;) {
        if (set->dev[i].type == DV_TYPE_SENSOR || (int)i == arm) {
            continue;
        }
        down_op(set, i, set->dev[i].initial_on, &ops[n++]);
    }
    return (int)n;
}

int devs_apply(const devs_io_t *io, const dv_set_t *set, const dv_op_t *op, dv_rec_t *rec,
               uint32_t tick)
{
    int rc;

    if (io == NULL || set == NULL || op == NULL) {
        return DV_ERR_ARG;
    }
    if (op->chip == DV_CHIP_PI) {
        switch (op->kind) {
        case DV_OP_WRITE:
            rc = io->pi_write(op->pin, op->level);
            break;
        case DV_OP_CONFIG_OUTPUT:
            rc = io->pi_config_output(op->pin, op->level);
            break;
        case DV_OP_CONFIG_INPUT:
            rc = io->pi_config_input(op->pin, op->pull);
            break;
        default:
            return DV_ERR_ARG;
        }
        if (rc < 0) {
            return DEVS_ERR_IO;
        }
    } else {
        const dv_expander_t *chip;

        if (op->chip >= DV_MAX_EXPANDERS) {
            return DV_ERR_ARG;
        }
        chip = &set->exp[op->chip];
        rc = op->kind == DV_OP_WRITE ? io->exp_write(chip, op->pin, op->level)
                                     : io->exp_config(chip, op->pin, op);
        if (rc < 0) {
            return rc;
        }
    }
    if (rec != NULL && op->kind != DV_OP_CONFIG_INPUT) {
        dv_rec_add_chip(rec, op->chip, op->pin, op->level, tick);
    }
    return 0;
}

int devs_read(const devs_io_t *io, const dv_set_t *set, unsigned idx)
{
    const dv_device_t *d;
    int rc;

    if (io == NULL || set == NULL || idx >= set->count) {
        return DV_ERR_ARG;
    }
    d = &set->dev[idx];
    if (d->chip == DV_CHIP_PI) {
        rc = io->pi_read(d->pin);
        return rc < 0 ? DEVS_ERR_IO : rc != 0;
    }
    if (d->chip >= DV_MAX_EXPANDERS) {
        return DV_ERR_ARG;
    }
    rc = io->exp_read(&set->exp[d->chip], d->pin);
    return rc < 0 ? rc : rc != 0;
}

int devs_safe_level(const dv_set_t *set, unsigned idx)
{
    const dv_device_t *d;
    int on;

    if (set == NULL || idx >= set->count) {
        return DV_ERR_ARG;
    }
    d = &set->dev[idx];
    on = (int)idx == devs_arm_index(set) ? 0 : d->initial_on != 0;
    return (on != 0) == (d->active_high != 0);
}

static uint8_t on_of(const dv_device_t *d, unsigned level)
{
    return (uint8_t)((level != 0) == (d->active_high != 0));
}

/* Starts a fault for a failed `op`. 0 when `rc` starts none (no driver). */
static int fault_start(dv_set_t *set, const dv_op_t *op, int rc)
{
    dv_dstate_t *s = &set->st[op->dev];

    if (rc == DEVS_ERR_NOEXP || rc == DEVS_ERR_SKIP) {
        return 0;
    }
    s->pending = 0;
    s->fault = 1;
    s->owed = op->level;
    s->owed_kind = op->kind;
    s->fails = 1;
    return 1;
}

static void ev_put(devs_ev_t *ev, unsigned *n, unsigned i, unsigned kind, const dv_op_t *op,
                   int rc)
{
    if (ev != NULL) {
        k_memset(&ev[*n], 0, sizeof(ev[*n]));
        ev[*n].dev = (uint8_t)i;
        ev[*n].kind = (uint8_t)kind;
        ev[*n].level = op != NULL ? op->level : 0;
        ev[*n].rc = rc;
    }
    (*n)++;
}

static void owed_op(const dv_set_t *set, unsigned i, dv_op_t *o)
{
    k_memset(o, 0, sizeof(*o));
    o->kind = set->st[i].owed_kind;
    o->chip = set->dev[i].chip;
    o->pin = set->dev[i].pin;
    o->level = set->st[i].owed;
    o->dev = (uint8_t)i;
}

int devs_cmd(dv_set_t *set, const devs_io_t *io, unsigned idx, dv_action_t act,
             uint32_t privs, uint32_t now_ms, dv_rec_t *rec, uint32_t tick)
{
    dv_op_t ops[DV_CMD_OPS];
    dv_dstate_t saved;
    int n, i, rc;

    if (set == NULL || io == NULL || idx >= set->count) {
        return DV_ERR_ARG;
    }
    if (set->st[idx].fault) {
        return DEVS_ERR_FAULT;
    }
    saved = set->st[idx];
    n = dv_command(set, idx, act, privs, now_ms, ops, DV_CMD_OPS);
    if (n < 0) {
        return n;
    }
    for (i = 0; i < n; i++) {
        if (ops[i].after_ms != 0) {
            continue;       /* the pulse end: dv_tick() emits it when due */
        }
        rc = devs_apply(io, set, &ops[i], rec, tick);
        if (rc < 0) {
            set->st[idx] = saved;
            if (ops[i].level == devs_safe_level(set, idx) && fault_start(set, &ops[i], rc)) {
                return DEVS_ERR_FAULT;
            }
            return rc;
        }
    }
    if (n > 0) {
        set->st[idx].unreached = 0;
    }
    return 0;
}

unsigned devs_unreachable(const dv_set_t *set)
{
    unsigned i, n = 0;
    int arm = devs_arm_index(set);

    if (set == NULL) {
        return 0;
    }
    for (i = 0; i < set->count && i < DV_MAX_DEVICES; i++) {
        if ((int)i != arm && set->dev[i].type != DV_TYPE_SENSOR &&
            (set->st[i].fault || set->st[i].unreached)) {
            n++;
        }
    }
    return n;
}

int devs_status(const dv_set_t *set, unsigned idx, char *buf, size_t size)
{
    int n = dv_status_line(set, idx, buf, size);
    unsigned u;
    int m;

    if (n < 0 || (int)idx != devs_arm_index(set) || (u = devs_unreachable(set)) == 0) {
        return n;
    }
    m = k_snprintf(buf + n, size - (size_t)n, " UNREACH=%u", u);
    if (m < 0 || (size_t)(n + m) >= size) {
        buf[0] = '\0';
        return DV_ERR_SPACE;
    }
    return n + m;
}

int devs_tick(dv_set_t *set, const devs_io_t *io, uint32_t now_ms, dv_rec_t *rec,
              uint32_t tick, devs_ev_t *ev)
{
    dv_op_t ops[DV_MAX_DEVICES];
    dv_dstate_t before[DV_MAX_DEVICES];
    unsigned i, nev = 0;
    int n, j, rc;

    if (set == NULL || io == NULL || set->count > DV_MAX_DEVICES) {
        return 0;
    }
    /* Owed writes first: a device in fault has no pulse pending. */
    for (i = 0; i < set->count; i++) {
        dv_dstate_t *s = &set->st[i];
        dv_op_t o;

        if (!s->fault) {
            continue;
        }
        owed_op(set, i, &o);
        rc = devs_apply(io, set, &o, rec, tick);
        if (rc < 0) {
            if (s->fails < 0xFFFFu) {
                s->fails++;
            }
            continue;
        }
        s->fault = 0;
        s->unreached = 0;
        s->level = o.level;
        s->on = on_of(&set->dev[i], o.level);
        ev_put(ev, &nev, i, DEVS_EV_CLEARED, &o, 0);
        if (ev != NULL) {
            ev[nev - 1].fails = s->fails;
        }
        s->fails = 0;
    }
    k_memcpy(before, set->st, sizeof(before));
    n = dv_tick(set, now_ms, ops, DV_MAX_DEVICES);
    for (j = 0; j < n; j++) {
        unsigned d = ops[j].dev;

        rc = devs_apply(io, set, &ops[j], rec, tick);
        if (rc == 0) {
            ev_put(ev, &nev, d, DEVS_EV_ENDED, &ops[j], 0);
        } else if (fault_start(set, &ops[j], rc)) {
            /* The pin still holds the pulse's level: so does the state. */
            set->st[d].on = before[d].on;
            set->st[d].level = before[d].level;
            ev_put(ev, &nev, d, DEVS_EV_FAULT, &ops[j], rc);
        } else {
            ev_put(ev, &nev, d, DEVS_EV_FAIL, &ops[j], rc);
        }
    }
    return (int)nev;
}

int devs_boot_run(dv_set_t *set, const devs_io_t *io, dv_rec_t *rec, uint32_t tick,
                  devs_ev_t *ev, unsigned *nev)
{
    dv_op_t ops[DV_MAX_DEVICES];
    unsigned k = 0, faulted = 0;
    int arm, n, i, rc;

    if (set == NULL || io == NULL || ev == NULL || nev == NULL) {
        return DV_ERR_ARG;
    }
    *nev = 0;
    dv_reset(set);
    arm = devs_arm_index(set);
    n = devs_boot_plan(set, ops, DV_MAX_DEVICES);
    for (i = 0; i < n; i++) {
        unsigned d = ops[i].dev;
        dv_dstate_t *s = &set->st[d];

        if ((int)d == arm && faulted != 0 && s->on) {
            /* ARM goes on last, and only when every other output reached its level. */
            ops[i].level = (uint8_t)devs_safe_level(set, d);
            s->on = 0;
            s->level = ops[i].level;
            ev_put(ev, &k, d, DEVS_EV_ARMHELD, &ops[i], 0);
        }
        rc = devs_apply(io, set, &ops[i], rec, tick);
        if (rc == 0) {
            continue;
        }
        if (ops[i].kind != DV_OP_CONFIG_OUTPUT) {
            ev_put(ev, &k, d, DEVS_EV_FAIL, &ops[i], rc);
            continue;
        }
        /* Never configured: the pin drives nothing, the state is off. Any
         * failure counts, a retried fault or not: ARM stays off. */
        s->on = 0;
        s->level = set->dev[d].active_high ? 0 : 1;
        faulted += (int)d != arm;
        if (fault_start(set, &ops[i], rc)) {
            ev_put(ev, &k, d, DEVS_EV_FAULT, &ops[i], rc);
        } else {
            s->unreached = 1;
            ev_put(ev, &k, d, DEVS_EV_FAIL, &ops[i], rc);
        }
    }
    *nev = k;
    return n;
}

int devs_down_run(dv_set_t *set, const devs_io_t *io, dv_rec_t *rec, uint32_t tick,
                  dv_op_t *ops, devs_ev_t *ev)
{
    unsigned i, k = 0, t;
    int n, rc = 0;

    if (set == NULL || io == NULL || ops == NULL || ev == NULL) {
        return DV_ERR_ARG;
    }
    n = devs_down_plan(set, ops, DV_MAX_DEVICES);
    for (i = 0; n > 0 && i < (unsigned)n; i++) {
        unsigned d = ops[i].dev;
        dv_dstate_t *s = &set->st[d];

        for (t = 1; t <= DEVS_SAFE_TRIES; t++) {
            rc = devs_apply(io, set, &ops[i], rec, tick);
            if (rc == 0 || rc == DEVS_ERR_SKIP) {
                break;
            }
        }
        s->pending = 0;
        if (rc == 0) {
            s->fault = 0;
            s->fails = 0;
            s->unreached = 0;
            s->level = ops[i].level;
            s->on = on_of(&set->dev[d], ops[i].level);
        } else if (!s->fault) {
            (void)fault_start(set, &ops[i], rc);
        }
        ev_put(ev, &k, d, DEVS_EV_SAFE, &ops[i], rc);
        ev[k - 1].tries = (uint8_t)(rc == 0 ? t : rc == DEVS_ERR_SKIP ? 0 : DEVS_SAFE_TRIES);
    }
    for (i = 0; i < set->count; i++) {
        set->st[i].pending = 0;
    }
    return n;
}

int devs_vfs_code(int code)
{
    switch (code) {
    case DV_ERR_NOSUCHDEV:
    case DEVS_ERR_DOWN:
        return VFS_E_NODEV;
    case DV_ERR_NOPRIV:
        return VFS_E_NOPRIV;
    case DV_ERR_READONLY:
        return VFS_E_READONLY;
    case DV_ERR_SPACE:
        return VFS_E_TOOBIG;
    case DEVS_ERR_IO:
    case DEVS_ERR_NOEXP:
    case DEVS_ERR_FAULT:
    case DEVS_ERR_SKIP:
        return VFS_E_WRITE;
    default:
        return code >= 0 ? code : VFS_E_ARG;
    }
}

const char *devs_strerror(int code)
{
    switch (code) {
    case DEVS_ERR_NOEXP:
        return "%DEVICE-E-EXPABSENT, expander chip did not answer at boot";
    case DEVS_ERR_SKIP:
        return "%DEVICE-W-NOTWRITTEN, expander pin not written on panic";
    case DEVS_ERR_IO:
        return "%DEVICE-E-IOERR, the GPIO layer refused the operation";
    case DEVS_ERR_DOWN:
        return "%DEVICE-E-NODEVICES, no devices are configured";
    case DEVS_ERR_FAULT:
        return "%DEVICE-E-PINFAULT, a pin write failed and is retried; command refused";
    default:
        return dv_strerror(code);
    }
}

int devs_msg_ident(const char *msg, char *out, size_t size)
{
    const char *p;
    size_t n = 0;

    if (msg == NULL || out == NULL || size == 0) {
        return DV_ERR_ARG;
    }
    out[0] = '\0';
    /* Skip "%FACILITY-S-". */
    p = msg;
    for (int dashes = 0; *p != '\0' && dashes < 2; p++) {
        dashes += *p == '-';
    }
    while (p[n] != '\0' && p[n] != ',') {
        if (n + 1 >= size) {
            out[0] = '\0';
            return DV_ERR_SPACE;
        }
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
    return (int)n;
}
