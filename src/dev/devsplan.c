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
        dv_rec_add(rec, op->pin, op->level, tick);
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

int devs_cmd(dv_set_t *set, const devs_io_t *io, unsigned idx, dv_action_t act,
             uint32_t privs, uint32_t now_ms, dv_rec_t *rec, uint32_t tick)
{
    dv_op_t ops[DV_CMD_OPS];
    dv_dstate_t saved;
    int n, i, rc;

    if (set == NULL || io == NULL || idx >= set->count) {
        return DV_ERR_ARG;
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
            return rc;
        }
    }
    return 0;
}

int devs_tick(dv_set_t *set, const devs_io_t *io, uint32_t now_ms, dv_rec_t *rec,
              uint32_t tick, uint8_t *ended)
{
    dv_op_t ops[DV_MAX_DEVICES];
    int n, i;

    if (set == NULL || io == NULL) {
        return 0;
    }
    n = dv_tick(set, now_ms, ops, DV_MAX_DEVICES);
    for (i = 0; i < n; i++) {
        /* The state is off already; a failed write leaves the pin as is. */
        (void)devs_apply(io, set, &ops[i], rec, tick);
        if (ended != NULL) {
            ended[i] = ops[i].dev;
        }
    }
    return n < 0 ? 0 : n;
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
        return VFS_E_WRITE;
    default:
        return code >= 0 ? code : VFS_E_ARG;
    }
}

const char *devs_strerror(int code)
{
    switch (code) {
    case DEVS_ERR_NOEXP:
        return "%DEVICE-E-NOEXPDRV, expander not available in this build";
    case DEVS_ERR_IO:
        return "%DEVICE-E-IOERR, the GPIO layer refused the operation";
    case DEVS_ERR_DOWN:
        return "%DEVICE-E-NODEVICES, no devices are configured";
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
