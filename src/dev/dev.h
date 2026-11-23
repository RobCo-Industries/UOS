#ifndef UOS_DEV_H
#define UOS_DEV_H

/*
 * Device control, pure parts. devcfg.c parses DEVICES.CFG into a table,
 * devstate.c keeps each device's state, turns commands into pin
 * operations and formats status lines. No hardware, no globals: the
 * caller owns every struct. Ticks are milliseconds and may wrap.
 */

#include <stddef.h>
#include <stdint.h>

#include "uos_privs.h"

/* Limits. */
#define DV_MAX_DEVICES      16      /* devices kept from DEVICES.CFG */
#define DV_NAME_MAX         8       /* characters in a device name */
#define DV_LABEL_MAX        31      /* characters in a label */
#define DV_PULSE_MIN_MS     10      /* shortest pulse, 0 means "level" */
#define DV_PULSE_MAX_MS     10000   /* longest pulse */
#define DV_PIN_LIMIT        28      /* pins from this number up are refused */
#define DV_MAX_ERRORS       8       /* config errors kept with details */
#define DV_CMD_OPS          2       /* pin operations one command needs */
#define DV_REC_SIZE         64      /* pin operations the record ring keeps */
#define DV_STATUS_LINE_MAX  80      /* bytes of a status line, with the NUL */

/* Privilege bits, as in SYSUAF.DAT. */
#define DV_PRIV_SYSTEM      UOS_PRIV_SYSTEM
#define DV_PRIV_OPER        UOS_PRIV_OPER
_Static_assert(DV_PRIV_SYSTEM == 0x1u && DV_PRIV_OPER == 0x2u,
               "dev privilege bits match uos_privs.h");
#define DV_PRIVS_DEFAULT    DV_PRIV_OPER

/* Error codes. dv_strerror() gives the message text of each. */
#define DV_OK               0
#define DV_ERR_ARG          (-1)    /* NULL pointer or bad index */
#define DV_ERR_NAME         (-2)    /* name not 1 to 8 of A-Z 0-9 _ */
#define DV_ERR_DUPNAME      (-3)    /* name already used */
#define DV_ERR_TYPE         (-4)    /* type missing or unknown */
#define DV_ERR_GPIO         (-5)    /* gpio missing or not a number */
#define DV_ERR_PINUSED      (-6)    /* gpio is a pin the system uses */
#define DV_ERR_PINSHARED    (-7)    /* gpio already taken by a device */
#define DV_ERR_ACTIVE       (-8)    /* active not high or low */
#define DV_ERR_INITIAL      (-9)    /* initial not on or off, or on with a pulse */
#define DV_ERR_PULSE        (-10)   /* pulse_ms not 0 or 10 to 10000 */
#define DV_ERR_LABEL        (-11)   /* label too long or not printable */
#define DV_ERR_PRIVS        (-12)   /* privs not a 32-bit number */
#define DV_ERR_SYNTAX       (-13)   /* the INI reader stopped on a line */
#define DV_ERR_NOSUCHDEV    (-14)   /* no device by that name */
#define DV_ERR_NOPRIV       (-15)   /* caller lacks the device's privileges */
#define DV_ERR_READONLY     (-16)   /* a sensor takes no command */
#define DV_ERR_BADACTION    (-17)   /* word not valid for the device type */
#define DV_ERR_NOPULSE      (-18)   /* PULSE on a device with pulse_ms 0 */
#define DV_ERR_SPACE        (-19)   /* output buffer too small */

typedef enum {
    DV_TYPE_DOOR = 0,
    DV_TYPE_LIGHT,
    DV_TYPE_TURRET,
    DV_TYPE_LOCK,
    DV_TYPE_RELAY,
    DV_TYPE_SENSOR,
    DV_TYPE_COUNT
} dv_type_t;

typedef enum {
    DV_ACT_NONE = 0,
    DV_ACT_ON,
    DV_ACT_OFF,
    DV_ACT_TOGGLE,
    DV_ACT_PULSE,
    DV_ACT_OPEN,        /* door: on */
    DV_ACT_CLOSE,       /* door: off */
    DV_ACT_UNLOCK,      /* lock: on */
    DV_ACT_LOCK,        /* lock: off */
    DV_ACT_ACTIVATE,    /* turret: on */
    DV_ACT_STANDBY,     /* turret: off */
} dv_action_t;

typedef struct {
    char name[DV_NAME_MAX + 1];     /* upper-case */
    char label[DV_LABEL_MAX + 1];
    uint8_t type;                   /* dv_type_t */
    uint8_t pin;                    /* BCM number */
    uint8_t active_high;            /* 1: high means on */
    uint8_t initial_on;             /* outputs only */
    uint16_t pulse_ms;              /* 0: level device */
    uint32_t privs;                 /* bits needed to change it */
} dv_device_t;

typedef struct {
    uint8_t on;             /* logical state; a sensor: tripped */
    uint8_t level;          /* last pin level written or read */
    uint8_t pending;        /* a pulse end is due */
    uint32_t due;           /* tick of the pulse end */
} dv_dstate_t;

/* The device table and its state. Fill with dv_cfg_parse(), then dv_reset(). */
typedef struct {
    dv_device_t dev[DV_MAX_DEVICES];
    dv_dstate_t st[DV_MAX_DEVICES];
    unsigned count;
} dv_set_t;

typedef struct {
    int code;                       /* DV_ERR_* */
    unsigned line;                  /* line of the bad key, or of the section's first key */
    char name[DV_NAME_MAX + 1];     /* device name as written, cut to 8 */
} dv_cfg_err_t;

typedef struct {
    unsigned loaded;        /* devices in the table */
    unsigned skipped;       /* devices refused for a bad field */
    unsigned unknown;       /* keys skipped: unknown, or outside a device */
    unsigned ignored;       /* device sections after the 16th, not checked */
    int parse_rc;           /* 0, or the ini_parse() code that stopped it */
    unsigned parse_line;    /* line of parse_rc */
    unsigned err_count;     /* errors seen; details for the first 8 */
    dv_cfg_err_t errs[DV_MAX_ERRORS];
} dv_cfg_result_t;

/* Pin operation kinds. Each maps to one gpio.h call. */
typedef enum {
    DV_OP_WRITE = 0,        /* gpio_write(pin, level) */
    DV_OP_CONFIG_OUTPUT,    /* gpio_config_output(pin, level) */
    DV_OP_CONFIG_INPUT,     /* gpio_config_input(pin, pull) */
} dv_op_kind_t;

/* Pull codes, equal to GPIO_PULL_* in gpio.h. */
#define DV_PULL_NONE    0
#define DV_PULL_DOWN    1
#define DV_PULL_UP      2

typedef struct {
    uint8_t kind;           /* dv_op_kind_t */
    uint8_t pin;
    uint8_t level;          /* 0 or 1, for WRITE and CONFIG_OUTPUT */
    uint8_t pull;           /* DV_PULL_*, for CONFIG_INPUT */
    uint8_t dev;            /* index of the device */
    uint32_t after_ms;      /* 0: now. Otherwise informative: dv_tick() emits it */
} dv_op_t;

/* ---- devcfg.c ---- */

/*
 * Parses DEVICES.CFG text into `set` (count and dev[]; states are zeroed).
 * A bad device is skipped, the others load. Returns DV_OK, DV_ERR_ARG, or
 * DV_ERR_SYNTAX when the INI reader stopped: devices finished before the
 * bad line stay loaded, the one in progress is dropped.
 */
int dv_cfg_parse(const char *text, size_t len, dv_set_t *set, dv_cfg_result_t *res);

/* 1 if the system keeps `pin` for itself, 0 if a device may use it. */
int dv_pin_refused(unsigned pin);

/*
 * Formats the result as "loaded=N skipped=N unknown=N ignored=N errors=N
 * parse=N" for the ev=config marker. DV_ERR_SPACE if it does not fit,
 * with `buf` set to "" when size > 0.
 */
int dv_cfg_summary(const dv_cfg_result_t *res, char *buf, size_t size);

/* ---- devstate.c ---- */

/* Sets every device off, no pulse pending, levels at the inactive level. */
void dv_reset(dv_set_t *set);

/* Index of the device named `name` (any case), or DV_ERR_NOSUCHDEV. */
int dv_find(const dv_set_t *set, const char *name);

/* "door", "light", ... or "" for a bad type. */
const char *dv_type_name(unsigned type);

/* Parses a command word, any case. DV_ACT_NONE if unknown. */
dv_action_t dv_action_parse(const char *word);

/*
 * Boot: one operation per device, in table order. Outputs get
 * CONFIG_OUTPUT at their initial level, sensors CONFIG_INPUT with a pull
 * toward the inactive level (active high: pull down; active low: pull up).
 * Sets each output's state to its initial one. Returns the number of
 * operations, or DV_ERR_SPACE (nothing written) if `max` < count.
 */
int dv_boot_ops(dv_set_t *set, dv_op_t *ops, unsigned max);

/*
 * Runs `act` on device `idx` for a user with privilege bits `privs`.
 * Checks in order: sensor (READONLY), word valid for the type
 * (BADACTION), privileges (NOPRIV: SYSTEM passes, else every bit of the
 * device's mask is needed), PULSE length (NOPULSE). Any pending pulse
 * end is cancelled first. A device with pulse_ms > 0 pulses for an "on"
 * word. Writes 1 or 2 operations and returns their count; ops with
 * after_ms > 0 are informative and come later from dv_tick(). Needs
 * max >= DV_CMD_OPS, else DV_ERR_SPACE. On any error, nothing changes.
 */
int dv_command(dv_set_t *set, unsigned idx, dv_action_t act, uint32_t privs,
               uint32_t now, dv_op_t *ops, unsigned max);

/*
 * Ends every pulse due at `now` (wrap-safe): writes one WRITE operation
 * each, in table order, and sets the device off. Stops when `max` is
 * reached; the rest stay pending. Returns the number written.
 */
int dv_tick(dv_set_t *set, uint32_t now, dv_op_t *ops, unsigned max);

/* Records a sensor's pin level read by the glue. DV_ERR_ARG if not a sensor. */
int dv_sensor_update(dv_set_t *set, unsigned idx, int level);

/* One state word: OPEN, CLOSED, ON, OFF, ACTIVE, STANDBY, UNLOCKED, LOCKED, TRIPPED, CLEAR. */
const char *dv_state_word(const dv_set_t *set, unsigned idx);

/*
 * SHOW DEVICES columns: name 8, type 6, label 31, state 8, "GPIO n".
 * dv_status_header() and dv_status_line() fill `buf` or return
 * DV_ERR_SPACE (buf set to "" when size > 0). Return the length otherwise.
 */
int dv_status_header(char *buf, size_t size);
int dv_status_line(const dv_set_t *set, unsigned idx, char *buf, size_t size);

/* "%DEVICE-W-NOSUCHDEV, no such device" and so on, for every DV_* code. */
const char *dv_strerror(int code);

/* ---- record ring, for the glue's maintenance port ---- */

typedef struct {
    uint8_t pin;
    uint8_t level;
    uint32_t tick;
} dv_rec_entry_t;

typedef struct {
    dv_rec_entry_t e[DV_REC_SIZE];
    unsigned head;          /* next slot to write */
    unsigned count;         /* entries held, up to DV_REC_SIZE */
    uint32_t total;         /* entries ever added */
} dv_rec_t;

void dv_rec_init(dv_rec_t *r);
void dv_rec_add(dv_rec_t *r, unsigned pin, int level, uint32_t tick);

/*
 * Formats entry `i` (0 = oldest held) as "pin=N level=N tick=N".
 * DV_ERR_ARG if i >= count, DV_ERR_SPACE if it does not fit.
 */
int dv_rec_format(const dv_rec_t *r, unsigned i, char *buf, size_t size);

#endif
