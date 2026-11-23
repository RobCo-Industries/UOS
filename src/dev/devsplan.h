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
#define DEVS_ERR_FAULT      (-43)   /* a write to the safe level is owed */

/* Tries of each safe-state write at shutdown before the next device. */
#define DEVS_SAFE_TRIES     3u

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
 * Pin faults. A write toward a device's safe level that fails leaves the
 * device in fault: the state stays the last one written, the write is
 * owed, devs_tick() retries it every tick until it succeeds, and every
 * command to the device is refused with DEVS_ERR_FAULT. The safe level is
 * the `initial` level, and off for ARM. DEVS_ERR_NOEXP never starts a
 * fault: with no expander driver the pin was never driven.
 */

/* The pin level device `idx` rests at when safe. */
int devs_safe_level(const dv_set_t *set, unsigned idx);

/* What happened to a device, for the glue's markers. */
typedef enum {
    DEVS_EV_ENDED = 0,  /* a pulse end was written */
    DEVS_EV_FAIL,       /* an operation failed, nothing is owed */
    DEVS_EV_FAULT,      /* an operation failed, a fault started */
    DEVS_EV_CLEARED,    /* an owed write succeeded, the fault is over */
    DEVS_EV_ARMHELD,    /* boot: ARM stays off, another output faulted */
    DEVS_EV_SAFE,       /* shutdown: one safe-state write, rc 0 or not */
} devs_ev_kind_t;

typedef struct {
    uint8_t dev;        /* device index */
    uint8_t kind;       /* devs_ev_kind_t */
    uint8_t tries;      /* DEVS_EV_SAFE: calls made, 1 to DEVS_SAFE_TRIES */
    uint8_t level;      /* the level written or owed */
    uint16_t fails;     /* DEVS_EV_CLEARED: failed tries before it */
    int rc;             /* 0, or the code of the last failure */
} devs_ev_t;

/*
 * A command on device `idx`: dv_command() at `now_ms`, then the
 * operations due now (after_ms 0) through `io`. The pulse end is left to
 * dv_tick(). A device in fault refuses it with DEVS_ERR_FAULT. When the
 * write fails, the device's state is put back as it was; if the write
 * went to the safe level, a fault starts and the code is DEVS_ERR_FAULT,
 * otherwise the failure's code. Returns 0 or a DV_ERR_* / DEVS_ERR_* code.
 */
int devs_cmd(dv_set_t *set, const devs_io_t *io, unsigned idx, dv_action_t act,
             uint32_t privs, uint32_t now_ms, dv_rec_t *rec, uint32_t tick);

/*
 * One tick: retries each owed write first, then ends the pulses due at
 * `now_ms` through dv_tick() and writes them. A failed pulse end starts
 * a fault: the device stays on, as its pin is. `ev` (DV_MAX_DEVICES
 * entries, may be NULL) gets one event per device that ended (ENDED),
 * started a fault (FAULT) or left one (CLEARED); a retry that fails again
 * adds to `fails` only. Returns the count of events.
 */
int devs_tick(dv_set_t *set, const devs_io_t *io, uint32_t now_ms, dv_rec_t *rec,
              uint32_t tick, devs_ev_t *ev);

/*
 * Boot: dv_reset(), then devs_boot_plan() applied. An output whose write
 * fails is off with a fault, its CONFIG_OUTPUT owed; when any output other
 * than ARM faulted, ARM is written off instead of its initial level.
 * `ev` (DV_MAX_DEVICES + 1 entries) gets FAIL, FAULT and ARMHELD events;
 * `*nev` their count. Returns the count of operations or a DV_ERR_* code.
 */
int devs_boot_run(dv_set_t *set, const devs_io_t *io, dv_rec_t *rec, uint32_t tick,
                  devs_ev_t *ev, unsigned *nev);

/*
 * Shutdown: devs_down_plan() applied, each write tried up to
 * DEVS_SAFE_TRIES times, then the next device, whatever the result. A
 * write that succeeds sets the state and ends any fault; one that fails
 * leaves the state and the device in fault. Clears every pending pulse.
 * `ops` and `ev` (DV_MAX_DEVICES entries each) get the plan and one SAFE
 * event per write. Returns the count or a DV_ERR_* code.
 */
int devs_down_run(dv_set_t *set, const devs_io_t *io, dv_rec_t *rec, uint32_t tick,
                  dv_op_t *ops, devs_ev_t *ev);

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
