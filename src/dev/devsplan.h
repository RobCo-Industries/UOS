#ifndef UOS_DEVSPLAN_H
#define UOS_DEVSPLAN_H

/*
 * The device service's own rules, pure: sensor debounce, the boot and
 * shutdown order with the ARM relay, and the mapping of a pin operation
 * to a GPIO call through a table of functions. devglue.c passes the real
 * gpio.h calls; the host tests pass fakes. No hardware, no globals.
 */

#include <stddef.h>
#include <stdint.h>

#include "dev.h"

/* Codes of the service, beside the DV_* codes of dev.h. */
#define DEVS_ERR_NOEXP      (-40)   /* expander pin: no driver in this build */
#define DEVS_ERR_IO         (-41)   /* the GPIO layer refused the call */
#define DEVS_ERR_DOWN       (-42)   /* the service has no device table */

/* Sensor polling: one read every DEVS_POLL_MS, a change after
 * DEVS_DEBOUNCE_READS equal reads in a row, so 150 ms at most. */
#define DEVS_POLL_MS        50u
#define DEVS_DEBOUNCE_READS 3u

/* The software watchdog's timeout. */
#define DEVS_WDOG_MS        15000u

/* The name of the relay that switches the supply of every load. */
#define DEVS_ARM_NAME       "ARM"

/* ---- Debounce ---- */

typedef struct {
    uint8_t stable;     /* the level reported */
    uint8_t cand;       /* the level being counted */
    uint8_t count;      /* equal reads of cand in a row */
} devs_deb_t;

/* Starts at `level`: no change is reported for it. */
void devs_deb_seed(devs_deb_t *d, int level);

/*
 * One read. Returns 1 when `need` equal reads in a row of a level other
 * than the stable one have been seen: the stable level is then that
 * level. Returns 0 otherwise. `need` 0 counts as 1.
 */
int devs_deb_feed(devs_deb_t *d, int level, unsigned need);

/* ---- Order ---- */

/* Index of the relay named ARM, or -1. Another type named ARM is not it. */
int devs_arm_index(const dv_set_t *set);

/*
 * dv_boot_ops() with the ARM relay's operation moved to the end, so it
 * changes after every other pin holds its initial level. Returns the
 * count or a DV_ERR_* code.
 */
int devs_boot_plan(dv_set_t *set, dv_op_t *ops, unsigned max);

/*
 * The safe state: every output at its `initial` level, ARM off. Writes
 * CONFIG_OUTPUT operations: ARM off first, then every other output at
 * its initial level in reverse table order. Sensors get nothing. Changes
 * no state. Returns the count, or DV_ERR_SPACE (nothing written) when
 * `max` is too small, or DV_ERR_ARG.
 */
int devs_down_plan(const dv_set_t *set, dv_op_t *ops, unsigned max);

/* ---- Applying operations ---- */

/*
 * The calls behind the operations. Pi pins go to the pi_* functions,
 * which take gpio.h's arguments and return 0 or a negative code. Expander
 * pins go to the exp_* functions with the chip's entry of dv_set_t.exp:
 * one function per operation, so a driver replaces each alone.
 */
typedef struct {
    int (*pi_write)(unsigned pin, int level);
    int (*pi_config_output)(unsigned pin, int level);
    int (*pi_config_input)(unsigned pin, unsigned pull);
    int (*pi_read)(unsigned pin);
    int (*exp_write)(const dv_expander_t *chip, unsigned pin, int level);
    int (*exp_config)(const dv_expander_t *chip, unsigned pin, const dv_op_t *op);
    int (*exp_read)(const dv_expander_t *chip, unsigned pin);
} devs_io_t;

/*
 * Applies one operation. A WRITE or CONFIG_OUTPUT that succeeds is added
 * to `rec` (may be NULL) with `tick`. Returns 0, DEVS_ERR_IO when a Pi
 * call failed, or what the expander function returned.
 */
int devs_apply(const devs_io_t *io, const dv_set_t *set, const dv_op_t *op, dv_rec_t *rec,
               uint32_t tick);

/* Reads device `idx`'s pin: 0, 1, DEVS_ERR_IO, the expander code, or DV_ERR_ARG. */
int devs_read(const devs_io_t *io, const dv_set_t *set, unsigned idx);

/*
 * A command on device `idx`: dv_command() at `now_ms`, then the
 * operations due now (after_ms 0) through `io`. The pulse end is left to
 * dv_tick(). When an operation fails, the device's state is put back as
 * it was and the code returned. Returns 0 or a DV_ERR_* / DEVS_ERR_* code.
 */
int devs_cmd(dv_set_t *set, const devs_io_t *io, unsigned idx, dv_action_t act,
             uint32_t privs, uint32_t now_ms, dv_rec_t *rec, uint32_t tick);

/*
 * Ends the pulses due at `now_ms` through dv_tick() and applies them.
 * `ended` (DV_MAX_DEVICES entries, may be NULL) gets the device indexes.
 * Returns the count of ends.
 */
int devs_tick(dv_set_t *set, const devs_io_t *io, uint32_t now_ms, dv_rec_t *rec,
              uint32_t tick, uint8_t *ended);

/*
 * The file system's VFS_E_* code for a DV_ERR_* or DEVS_ERR_* code, for the
 * DEV: hooks. A length or 0 passes through. An unknown device or no
 * device table is VFS_E_NODEV, a missing privilege VFS_E_NOPRIV, a
 * command to a sensor VFS_E_READONLY, a short buffer VFS_E_TOOBIG, a GPIO
 * or expander failure VFS_E_WRITE, any other code VFS_E_ARG.
 */
int devs_vfs_code(int code);

/* dv_strerror(), plus the DEVS_ERR_* texts. */
const char *devs_strerror(int code);

/*
 * The ident of a message: "PULLUP" from "%DEVICE-W-PULLUP, ...". Fills
 * `out` and returns its length, or DV_ERR_SPACE / DV_ERR_ARG.
 */
int devs_msg_ident(const char *msg, char *out, size_t size);

#endif
