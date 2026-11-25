/*
 * The device service: reads /UOS/DEVICES.CFG at init, sets every pin to
 * its initial level, runs commands, ends pulses and polls sensors from the
 * tick, and drives every output to its safe state at shutdown. The rules
 * are pure, in devstate.c, devcfg.c and devsplan.c. This file holds the
 * GPIO and expander calls, the markers, the software watchdog and the
 * `dev` command.
 *
 * Safety, see docs/ARCHITECTURE.md, "devices":
 *   - the safe state of an output is its `initial` state; ARM is off;
 *   - boot: every output at its initial level in file order, ARM last;
 *   - shutdown (reboot, halt, panic, watchdog): ARM off first, then every
 *     other output at its initial level in reverse file order, with direct
 *     GPIO writes only: no timers, no allocation, IRQs on or off; each
 *     write tried DEVS_SAFE_TRIES times, then the next device;
 *   - a failed write to the safe level is owed and retried every tick;
 *     the state stays the one last written (devsplan.h, "Pin faults");
 *   - pulse ends come from dv_tick() with the tick value, so a late tick
 *     ends a pulse late, never not at all;
 *   - expander chips are attached once at init; a chip that does not
 *     answer leaves its outputs unreached and is not tried again;
 *   - the panic path and the software watchdog start no I2C transfer:
 *     expander outputs keep their level, ARM on a Pi pin cuts their
 *     supply (see drive_safe()).
 */

#include <robco/gpio.h>
#include <robco/gpiox.h>
#include <robco/i2c.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/power.h>
#include <robco/tick.h>
#include <robco/uart.h>
#include <robco/wdog.h>

#include "devs.h"
#include "uos.h"
#include "uos_hwdog.h"
#include "uos_maint.h"
#include "uos_service.h"
#include "vfs.h"

#define DEVS_CFG_PATH       "/UOS/DEVICES.CFG"
#define DEVS_CFG_MAX        8192u               /* bytes of DEVICES.CFG read */
#define MS_PER_TICK         (1000u / UOS_TICK_HZ)
#define POLL_TICKS          UOS_MS_TO_TICKS(DEVS_POLL_MS)
#define WDOG_TICKS          UOS_MS_TO_TICKS(DEVS_WDOG_MS)
#define EXP_I2C_HZ          100000u             /* the rate main.c sets for the RTC */


static dv_set_t set;
static dv_cfg_result_t res;
static dv_rec_t rec;
static devs_deb_t deb[DV_MAX_DEVICES];
static uint32_t last_poll;
static int down_done;

/* The software watchdog: a tick callback, IRQ context. */
static volatile uint32_t wd_kick;
static volatile int wd_armed;
static volatile int wd_stop;        /* no more kicks: `dev hang`, or after a shutdown */
static volatile int wd_fired;

/* The hardware watchdog, UOS.CFG [watchdog]: rules in uos_hwdog.h. */
static uos_hwdog_t hw;
static uint32_t hw_plan;            /* timeout to start with at the first tick, or 0 */

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* `dev sense`: a level per sensor that replaces the pin read, or -1. */
static int8_t forced[DV_MAX_DEVICES];
/* `dev fail`: the next `fail_left` writes to Pi pin `fail_pin` fail. */
static volatile int fail_pin = -1;
static volatile uint32_t fail_left;

static int fail_now(unsigned pin)
{
    if ((int)pin != fail_pin || fail_left == 0) {
        return 0;
    }
    fail_left--;
    return 1;
}
#endif

/* ---- The calls behind the operations ------------------------------------- */

static int pi_write(unsigned pin, int level)
{
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (fail_now(pin)) {
        return -1;
    }
#endif
    return gpio_write(pin, level);
}

static int pi_config_output(unsigned pin, int level)
{
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (fail_now(pin)) {
        return -1;
    }
#endif
    return gpio_config_output(pin, level);
}

static int pi_config_input(unsigned pin, unsigned pull)
{
    return gpio_config_input(pin, pull);
}

static int pi_read(unsigned pin)
{
    return gpio_read(pin);
}

/*
 * Expander pins, through the library's PCA9554 driver. Table index N is
 * chip N of DEVICES.CFG. A chip that did not attach answers
 * GPIOX_ERR_DETACHED without a bus transfer: DEVS_ERR_NOEXP, no fault,
 * the output stays unreached. Any other driver error is DEVS_ERR_IO, so
 * a failed write to the safe level is owed and retried every tick.
 */
static int exp_code(int rc)
{
    if (rc >= 0) {
        return rc;
    }
    return rc == GPIOX_ERR_DETACHED ? DEVS_ERR_NOEXP : DEVS_ERR_IO;
}

static unsigned exp_index(const dv_expander_t *chip)
{
    return (unsigned)(chip - set.exp);
}

static int exp_write(const dv_expander_t *chip, unsigned pin, int level)
{
    return exp_code(gpiox_write(exp_index(chip), pin, level));
}

static int exp_config(const dv_expander_t *chip, unsigned pin, const dv_op_t *op)
{
    /* The PCA9554 has a fixed pull-up on each input: op->pull is not used. */
    if (op->kind == DV_OP_CONFIG_INPUT) {
        return exp_code(gpiox_config(exp_index(chip), pin, GPIOX_IN, 0));
    }
    return exp_code(gpiox_config(exp_index(chip), pin, GPIOX_OUT, op->level));
}

static int exp_read(const dv_expander_t *chip, unsigned pin)
{
    int level = 0;
    int rc = gpiox_read(exp_index(chip), pin, &level);

    return rc < 0 ? exp_code(rc) : level;
}

/*
 * The expander calls of the panic path and the software watchdog: none
 * starts an I2C transfer. A panic may come from inside the driver or the
 * I2C controller, and the shadow registers may be the damaged memory. The
 * watchdog runs in IRQ context, which i2c.h does not allow, and the hung
 * loop may sit inside a transfer. Neither case trusts the bus.
 */
static int exp_skip_write(const dv_expander_t *chip, unsigned pin, int level)
{
    (void)chip;
    (void)pin;
    (void)level;
    return DEVS_ERR_SKIP;
}

static int exp_skip_config(const dv_expander_t *chip, unsigned pin, const dv_op_t *op)
{
    (void)chip;
    (void)pin;
    (void)op;
    return DEVS_ERR_SKIP;
}

static int exp_skip_read(const dv_expander_t *chip, unsigned pin)
{
    (void)chip;
    (void)pin;
    return DEVS_ERR_SKIP;
}

static const devs_io_t io = {
    pi_write, pi_config_output, pi_config_input, pi_read, exp_write, exp_config, exp_read,
};

static const devs_io_t io_noi2c = {
    pi_write, pi_config_output, pi_config_input, pi_read,
    exp_skip_write, exp_skip_config, exp_skip_read,
};

/* ---- Markers -------------------------------------------------------------- */

static const char *ident(int code, char *buf, size_t size)
{
    if (devs_msg_ident(devs_strerror(code), buf, size) <= 0) {
        k_snprintf(buf, size, "CODE%d", code);
    }
    return buf;
}

/* "ev=devfail name= op= why=" for an operation that failed. */
static void mark_fail(unsigned idx, const char *op, int rc)
{
    char w[24];

    UOS_MARK("devfail name=%s op=%s rc=%d why=%s", set.dev[idx].name, op, rc,
             ident(rc, w, sizeof(w)));
}

/* A fault starts: "ev=devfault name= result=failing op= owed= rc= why=". */
static void mark_fault(unsigned idx, const char *op, unsigned level, int rc)
{
    char w[24];

    UOS_MARK("devfault name=%s result=failing op=%s owed=%u rc=%d why=%s", set.dev[idx].name,
             op, level, rc, ident(rc, w, sizeof(w)));
}

static void mark_state(unsigned idx, const char *by)
{
    UOS_MARK("dev name=%s state=%s by=%s", set.dev[idx].name, dv_state_word(&set, idx), by);
}

static uint32_t now_ms(uint32_t tick)
{
    return tick * MS_PER_TICK;
}

/* ---- Safe state ----------------------------------------------------------- */

/* "12" for a Pi pin, "X0.3" for an expander pin. */
static const char *pin_text(const dv_op_t *op, char *buf, size_t size)
{
    if (op->chip == DV_CHIP_PI) {
        k_snprintf(buf, size, "%u", (unsigned)op->pin);
    } else if (gpiox_pin_name(op->chip, op->pin, buf, size) < 0) {
        k_snprintf(buf, size, "X?");
    }
    return buf;
}

/*
 * Every output to its safe state: ARM off first, then the others at their
 * initial level, last device first. Each write is tried DEVS_SAFE_TRIES
 * times, then the next device comes, so one bad pin keeps no other one
 * active. No timers, no allocation: any context, IRQs on or off. Runs once
 * per boot.
 *
 * Expander pins: on reboot and halt they go through the driver, polled,
 * each transfer bounded by its system timer deadline. On panic and from
 * the software watchdog they are skipped (io_noi2c): no I2C transfer
 * starts, the pins keep their level, and each gives a devsafe marker with
 * tries=0. ARM, which the board puts on a Pi pin, goes off first and cuts
 * the supply of every load. With the hardware watchdog on, the reset
 * that follows sets every expander output to its initial level at boot.
 */
static void drive_safe(const char *why, const devs_io_t *with)
{
    dv_op_t ops[DV_MAX_DEVICES];
    devs_ev_t ev[DV_MAX_DEVICES];
    char p[GPIOX_NAME_SIZE + 4];
    int n, i;

    if (down_done) {
        return;
    }
    down_done = 1;
    n = devs_down_run(&set, with, &rec, uos_ticks(), ops, ev);
    for (i = 0; i < n; i++) {
        UOS_MARK("devsafe name=%s pin=%s level=%u rc=%d tries=%u", set.dev[ev[i].dev].name,
                 pin_text(&ops[i], p, sizeof(p)), ops[i].level, ev[i].rc, ev[i].tries);
    }
    UOS_MARK("devdown why=%s count=%d", why, n < 0 ? 0 : n);
}

/* ---- The software watchdog -------------------------------------------- */

/*
 * IRQ context, every tick. When the loop has not kicked for DEVS_WDOG_MS,
 * the system is hung: drive the outputs safe and reset. It cannot run
 * with IRQs off, so it does not reset after a panic or a halt.
 */
static void wd_check(void)
{
    if (!wd_armed || wd_fired || uos_ticks() - wd_kick < WDOG_TICKS) {
        return;
    }
    wd_fired = 1;
    UOS_MARK("wdog result=expired timeout_ms=%u", DEVS_WDOG_MS);
    drive_safe("wdog", &io_noi2c);
    uart_flush();
    power_reboot();
}

static void wd_start(void)
{
    unsigned outputs = 0, i;
    int rc = 0;

    for (i = 0; i < set.count; i++) {
        outputs += set.dev[i].type != DV_TYPE_SENSOR;
    }
    if (outputs != 0) {
        wd_kick = uos_ticks();
        rc = tick_register(wd_check);
        wd_armed = rc == 0;
    }
    UOS_MARK("wdog result=%s timeout_ms=%u outputs=%u rc=%d", wd_armed ? "on" : "off",
             DEVS_WDOG_MS, outputs, rc);
}

/* ---- The hardware watchdog --------------------------------------------- */

/*
 * QEMU 10.1 does not count the PM watchdog: wdog_start() resets the board
 * at once. So it is a setting, off by default and on no test card. The
 * marker before the start is the last line a QEMU run prints.
 */
static void hw_start(uint32_t now)
{
    uint32_t ms = hw_plan;
    int rc;

    hw_plan = 0;
    UOS_MARK("hwdog result=start timeout_ms=%u", (unsigned)ms);
    uart_flush();
    rc = wdog_start(ms);
    if (rc == 0) {
        uos_hwdog_started(&hw, ms, now, UOS_TICK_HZ);
    }
    UOS_MARK("hwdog result=%s timeout_ms=%u rc=%d", rc == 0 ? "on" : "err", (unsigned)ms, rc);
}

/* From the loop's tick only, never from an IRQ: a hung loop sends no kick. */
static void hw_tick(uint32_t now)
{
    /* After every service is up and the kernel said ready. */
    if (hw_plan != 0 && uos.phase == UOS_PHASE_READY) {
        hw_start(now);
    }
    if (uos_hwdog_kick_due(&hw, now, !wd_stop)) {
        (void)wdog_kick();
        uos_hwdog_kicked(&hw, now);
    }
}

/* After the outputs are safe. A panic leaves it running: the board resets
 * when the timeout ends. A halt stops it: the terminal stays halted. */
static void hw_shutdown(int why)
{
    int act;

    hw_plan = 0;
    if (hw.timeout_ms == 0) {
        return;
    }
    act = uos_hwdog_shutdown(&hw, why);
    if (act == UOS_HWDOG_STOP) {
        wdog_stop();
    }
    UOS_MARK("hwdog result=%s why=%s remaining_ms=%u", act == UOS_HWDOG_STOP ? "stopped" : "left",
             uos_shutdown_name(why), (unsigned)wdog_remaining_ms());
}

/* ---- Load and boot -------------------------------------------------------- */

static void mark_cfg_lists(void)
{
    char w[24];
    unsigned i;

    for (i = 0; i < res.err_count && i < DV_MAX_ERRORS; i++) {
        UOS_MARK("deverr name=%s line=%u why=%s", res.errs[i].name[0] ? res.errs[i].name : "none",
                 res.errs[i].line, ident(res.errs[i].code, w, sizeof(w)));
    }
    for (i = 0; i < res.warn_count && i < DV_MAX_ERRORS; i++) {
        UOS_MARK("devwarn name=%s line=%u why=%s", res.warns[i].name, res.warns[i].line,
                 ident(res.warns[i].code, w, sizeof(w)));
    }
}

/*
 * The refused pins: the UOS mask. The library cannot say which pins the
 * input layer claimed; when its GPIO backend runs, every button and dial
 * pin of the board support spec is refused too.
 */
static uint32_t refused_mask(void)
{
    uint32_t m = DV_REFUSED_UOS;

    if (input_gpio_status() != INPUT_GPIO_OFF) {
        m |= DV_PINS_BUTTONS | DV_PINS_DIAL;
    }
    return m;
}

static void load(void)
{
    char summary[128];
    size_t len = 0;
    char *buf;
    int rc;

    k_memset(&set, 0, sizeof(set));
    k_memset(&res, 0, sizeof(res));
    if (uos.card != UOS_CARD_OK) {
        UOS_MARK("devcfg result=skipped loaded=0");
        return;
    }
    rc = vfs_sys_read_all(DEVS_CFG_PATH, DEVS_CFG_MAX, &buf, &len);
    if (rc == VFS_E_NOTFOUND || rc == VFS_E_NOTREE) {
        UOS_MARK("devcfg result=missing loaded=0");
        return;
    }
    if (rc != VFS_OK) {
        UOS_MARK("devcfg result=bad why=%s loaded=0", vfs_strerror(rc));
        return;
    }
    rc = dv_cfg_parse_pins(buf, len, refused_mask(), &set, &res);
    vfs_free(buf);
    if (dv_cfg_summary(&res, summary, sizeof(summary)) < 0) {
        summary[0] = '\0';
    }
    UOS_MARK("devcfg result=%s %s", rc == DV_OK ? "ok" : "syntax", summary);
    mark_cfg_lists();
}

/* "absent", "bus" and so on: the ident of a gpiox code, for markers. */
static const char *exp_why(int rc)
{
    switch (rc) {
    case 0:                 return "ok";
    case GPIOX_ERR_ABSENT:  return "absent";
    case GPIOX_ERR_ADDR:    return "addr";
    case GPIOX_ERR_BUS:     return "bus";
    default:                return "error";
    }
}

/*
 * Attaches each declared chip once, before the boot writes. A chip that
 * does not answer stays detached: its outputs end up unreached and ARM
 * stays off. The probe is one bounded I2C transfer, so a missing chip
 * never blocks boot. main.c set the bus up for the RTC; when that failed,
 * one more try here.
 */
static void attach_chips(void)
{
    unsigned c;
    int rc;

    for (c = 0; c < DV_MAX_EXPANDERS; c++) {
        if (!set.exp[c].declared) {
            continue;
        }
        if (i2c_get_hz() == 0) {
            (void)i2c_init(EXP_I2C_HZ);
        }
        if (gpiox_present(c)) {
            (void)gpiox_detach(c);
        }
        rc = gpiox_attach(c, set.exp[c].addr);
        UOS_MARK("devexp chip=%u addr=0x%02x result=%s rc=%d", c, (unsigned)set.exp[c].addr,
                 exp_why(rc), rc);
    }
}

/* A sensor's level: the pin, or in test builds the level `dev sense` set. */
static int sense_level(unsigned i)
{
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (forced[i] >= 0) {
        return forced[i];
    }
#endif
    return devs_read(&io, &set, i);
}

static void boot(void)
{
    devs_ev_t ev[DV_MAX_DEVICES + 1];
    uint32_t t = uos_ticks();
    int arm = devs_arm_index(&set);
    unsigned k, nev = 0;
    int n, i, rc;

    n = devs_boot_run(&set, &io, &rec, t, ev, &nev);
    for (k = 0; k < nev; k++) {
        if (ev[k].kind == DEVS_EV_ARMHELD) {
            UOS_MARK("devarm name=%s result=held why=unreachable count=%u",
                     set.dev[ev[k].dev].name, devs_unreachable(&set));
            continue;
        }
        mark_fail(ev[k].dev, "config", ev[k].rc);
        if (ev[k].kind == DEVS_EV_FAULT) {
            mark_fault(ev[k].dev, "config", ev[k].level, ev[k].rc);
        }
    }
    for (i = 0; i < (int)set.count; i++) {
        if (set.dev[i].type != DV_TYPE_SENSOR) {
            continue;
        }
        rc = sense_level((unsigned)i);
        if (rc < 0) {
            mark_fail((unsigned)i, "read", rc);
            continue;
        }
        devs_deb_seed(&deb[i], rc);
        (void)dv_sensor_update(&set, (unsigned)i, rc);
    }
    last_poll = t;
    UOS_MARK("devboot count=%d arm=%s", n < 0 ? 0 : n, arm >= 0 ? set.dev[arm].name : "none");
}

/* ---- The public calls ---------------------------------------------------- */

/*
 * An operator may switch ARM on by hand while an output is unreachable.
 * The answer and a marker say so plainly.
 */
static void arm_warn(unsigned idx, const char **text)
{
    static char msg[48];
    unsigned u = devs_unreachable(&set);

    if ((int)idx != devs_arm_index(&set) || !set.st[idx].on || u == 0) {
        return;
    }
    UOS_MARK("devarm name=%s result=on why=unreachable count=%u", set.dev[idx].name, u);
    if (text != NULL) {
        k_snprintf(msg, sizeof(msg), "%s, %u outputs unreachable", dv_state_word(&set, idx), u);
        *text = msg;
    }
}

int devs_command(const char *name, const char *action, uint32_t privs, const char *by,
                 const char **text)
{
    uint32_t t = uos_ticks();
    int idx, rc, was;

    if (set.count == 0) {
        rc = DEVS_ERR_DOWN;
    } else if (name == NULL || action == NULL) {
        rc = DV_ERR_ARG;
    } else {
        rc = idx = dv_find(&set, name);
        if (idx >= 0) {
            was = set.st[idx].fault;
            rc = devs_cmd(&set, &io, (unsigned)idx, dv_action_parse(action), privs, now_ms(t),
                          &rec, t);
            if (rc == DEVS_ERR_NOEXP || rc == DEVS_ERR_IO) {
                mark_fail((unsigned)idx, "write", rc);
            }
            if (rc == DEVS_ERR_FAULT && !was) {
                /* This command's write failed: the fault starts now. */
                mark_fault((unsigned)idx, "write", set.st[idx].owed, DEVS_ERR_IO);
            }
            if (rc == 0) {
                mark_state((unsigned)idx, by != NULL ? by : "none");
                if (text != NULL) {
                    *text = dv_state_word(&set, (unsigned)idx);
                }
                arm_warn((unsigned)idx, text);
                return 0;
            }
        }
    }
    if (text != NULL) {
        *text = devs_strerror(rc);
    }
    return rc;
}

unsigned devs_count(void)
{
    return set.count;
}

int devs_status_header(char *buf, size_t size)
{
    return dv_status_header(buf, size);
}

int devs_status_line(unsigned index, char *buf, size_t size)
{
    if (index >= set.count) {
        return set.count == 0 ? DEVS_ERR_DOWN : DV_ERR_NOSUCHDEV;
    }
    return devs_status(&set, index, buf, size);
}

const char *devs_message(int code)
{
    return devs_strerror(code);
}

static int hook_read(const char *name, char *buf, size_t size)
{
    int idx;

    if (set.count == 0) {
        return DEVS_ERR_DOWN;
    }
    idx = dv_find(&set, name);
    return idx < 0 ? idx : devs_status(&set, (unsigned)idx, buf, size);
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int hook_write(const char *name, const char *text, uint32_t privs)
{
    char word[16];
    size_t n = 0;

    if (text == NULL) {
        return DV_ERR_ARG;
    }
    while (is_blank(*text)) {
        text++;
    }
    while (text[n] != '\0' && !is_blank(text[n])) {
        if (n + 1 >= sizeof(word)) {
            return DV_ERR_BADACTION;
        }
        word[n] = text[n];
        n++;
    }
    word[n] = '\0';
    for (; text[n] != '\0'; n++) {
        if (!is_blank(text[n])) {
            return DV_ERR_BADACTION;
        }
    }
    return devs_command(name, word, privs, "dev", NULL);
}

static int hook_list(unsigned index, char *buf, size_t size)
{
    if (index >= set.count) {
        return DV_ERR_NOSUCHDEV;
    }
    if (buf == NULL || size <= k_strlen(set.dev[index].name)) {
        return DV_ERR_SPACE;
    }
    k_snprintf(buf, size, "%s", set.dev[index].name);
    return (int)k_strlen(buf);
}

static const devs_hooks_t hooks = { hook_read, hook_write, hook_list };

/* The same three calls with the file system's codes: DV_ERR_NOPRIV is
 * -15 here and VFS_E_NOMEM there, so the codes go through
 * devs_vfs_code(). The prototypes match field by field. */
static int dvfs_read(const char *name, char *buf, size_t size)
{
    return devs_vfs_code(hook_read(name, buf, size));
}

static int dvfs_write(const char *name, const char *text, uint32_t privs)
{
    return devs_vfs_code(hook_write(name, text, privs));
}

static int dvfs_list(unsigned index, char *buf, size_t size)
{
    return devs_vfs_code(hook_list(index, buf, size));
}

static const vfs_dev_hooks_t vfs_hooks = { dvfs_read, dvfs_write, dvfs_list };

const devs_hooks_t *devs_hooks(void)
{
    return &hooks;
}

/* ---- The `dev` maintenance command ---------------------------------------- */

static int parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0, base = 10, d;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s >= '0' && *s <= '9') {
            d = (uint32_t)(*s - '0');
        } else if (base == 16 && ((*s | 0x20) >= 'a' && (*s | 0x20) <= 'f')) {
            d = (uint32_t)((*s | 0x20) - 'a' + 10);
        } else {
            return -1;
        }
        if (v > (0xffffffffu - d) / base) {
            return -1;
        }
        v = v * base + d;
    }
    *out = v;
    return 0;
}

static int sub_list(void)
{
    char line[DV_STATUS_LINE_MAX];
    unsigned i;

    uos_maint_reply("dev count=%u", set.count);
    if (set.count == 0) {
        return 0;
    }
    if (dv_status_header(line, sizeof(line)) >= 0) {
        uos_maint_reply("%s", line);
    }
    for (i = 0; i < set.count; i++) {
        if (devs_status(&set, i, line, sizeof(line)) >= 0) {
            uos_maint_reply("%s", line);
        }
    }
    return 0;
}

static int sub_set(int argc, char **argv)
{
    uint32_t privs = DV_PRIV_SYSTEM;
    const char *text = "";

    if (argc < 3 || argc > 4) {
        return uos_maint_fail("usage: dev set <name> <action> [privs]");
    }
    if (argc == 4 && parse_u32(argv[3], &privs) != 0) {
        return uos_maint_fail("bad privs");
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (devs_command(argv[1], argv[2], privs, "maint", &text) != 0) {
        return uos_maint_fail(text);
    }
    uos_maint_reply("dev name=%s state=%s", set.dev[dv_find(&set, argv[1])].name, text);
    return 0;
}

static int sub_rec(void)
{
    char line[48];
    unsigned i;

    uos_maint_reply("rec count=%u total=%u", rec.count, (unsigned)rec.total);
    for (i = 0; i < rec.count; i++) {
        if (dv_rec_format(&rec, i, line, sizeof(line)) >= 0) {
            uos_maint_reply("%s", line);
        }
    }
    return 0;
}

static int sub_hwdog(void)
{
    uos_maint_reply("hwdog running=%d timeout_ms=%u remaining_ms=%u held=%u", wdog_running() != 0,
                    (unsigned)wdog_timeout_ms(), (unsigned)wdog_remaining_ms(),
                    (unsigned)hw.held);
    return 0;
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
static int sub_sense(int argc, char **argv)
{
    int idx;

    if (argc != 3 || (k_strcmp(argv[2], "0") != 0 && k_strcmp(argv[2], "1") != 0)) {
        return uos_maint_fail("usage: dev sense <name> <0|1>");
    }
    idx = dv_find(&set, argv[1]);
    if (idx < 0) {
        return uos_maint_fail(devs_strerror(idx));
    }
    if (set.dev[idx].type != DV_TYPE_SENSOR) {
        return uos_maint_fail("not a sensor");
    }
    forced[idx] = (int8_t)(argv[2][0] - '0');
    uos_maint_reply("dev name=%s level=%d", set.dev[idx].name, forced[idx]);
    return 0;
}

static int sub_fail(int argc, char **argv)
{
    uint32_t n = 0;
    int idx;

    if (argc != 3 || parse_u32(argv[2], &n) != 0) {
        return uos_maint_fail("usage: dev fail <name> <writes>");
    }
    idx = dv_find(&set, argv[1]);
    if (idx < 0) {
        return uos_maint_fail(devs_strerror(idx));
    }
    if (set.dev[idx].chip != DV_CHIP_PI) {
        return uos_maint_fail("not a Pi pin");
    }
    fail_left = 0;
    fail_pin = set.dev[idx].pin;
    fail_left = n;
    uos_maint_reply("dev name=%s fail=%u", set.dev[idx].name, (unsigned)n);
    return 0;
}

static int sub_hang(void)
{
    if (!wd_armed) {
        return uos_maint_fail("watchdog off");
    }
    wd_stop = 1;
    UOS_MARK("devhang tick=%u", (unsigned)uos_ticks());
    return 0;
}

/* Keeps the loop busy for `arg` ticks, IRQs on: a slow loop for the tests. */
static void stall(void *arg)
{
    uint32_t t0 = uos_ticks(), n = (uint32_t)(uintptr_t)arg;

    UOS_MARK("devstall ticks=%u start=%u", (unsigned)n, (unsigned)t0);
    while (uos_ticks() - t0 < n) {
        __asm__ volatile("wfi");
    }
    UOS_MARK("devstall done=%u", (unsigned)uos_ticks());
}

static int sub_stall(int argc, char **argv)
{
    uint32_t n = 0;

    if (argc != 2 || parse_u32(argv[1], &n) != 0 || n < 1 || n > 1000) {
        return uos_maint_fail("usage: dev stall <ticks 1 to 1000>");
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    return uos_post(stall, (void *)(uintptr_t)n) == 0 ? 0 : uos_maint_fail("full");
}
#endif

static int cmd_dev(int argc, char **argv)
{
    if (argc == 1 && k_strcmp(argv[0], "list") == 0) {
        return sub_list();
    }
    if (argc >= 1 && k_strcmp(argv[0], "set") == 0) {
        return sub_set(argc, argv);
    }
    if (argc == 1 && k_strcmp(argv[0], "rec") == 0) {
        return sub_rec();
    }
    if (argc == 1 && k_strcmp(argv[0], "hwdog") == 0) {
        return sub_hwdog();
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (argc >= 1 && k_strcmp(argv[0], "sense") == 0) {
        return sub_sense(argc, argv);
    }
    if (argc >= 1 && k_strcmp(argv[0], "fail") == 0) {
        return sub_fail(argc, argv);
    }
    if (argc == 1 && k_strcmp(argv[0], "hang") == 0) {
        return sub_hang();
    }
    if (argc >= 1 && k_strcmp(argv[0], "stall") == 0) {
        return sub_stall(argc, argv);
    }
#endif
    return uos_maint_fail("usage: dev list|set|rec|hwdog");
}

/* ---- The service ------------------------------------------------------------ */

static int devs_init(void)
{
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    k_memset(forced, 0xff, sizeof(forced));
#endif
    dv_rec_init(&rec);
    /* The file system starts first: DEV: names reach this service from now. */
    vfs_dev_register(&vfs_hooks);
    load();
    attach_chips();
    if (set.count != 0) {
        boot();
    }
    wd_start();
    hw_plan = uos_hwdog_plan(uos_settings()->hwdog_enabled, uos_settings()->hwdog_timeout_ms);
    if (hw_plan == 0) {
        UOS_MARK("hwdog result=off why=setting");
    }
    return uos_maint_register("dev", cmd_dev, "devices: dev list|set|rec|hwdog");
}

static void devs_tick_fn(uint32_t now)
{
    devs_ev_t ev[DV_MAX_DEVICES];
    unsigned i;
    int n;

    if (!wd_stop) {
        wd_kick = now;
    }
    hw_tick(now);
    if (set.count == 0 || down_done) {
        return;
    }
    n = devs_tick(&set, &io, now_ms(now), &rec, now, ev);
    for (i = 0; i < (unsigned)n; i++) {
        unsigned d = ev[i].dev;

        if (ev[i].kind == DEVS_EV_ENDED) {
            mark_state(d, "pulse");
        } else if (ev[i].kind == DEVS_EV_FAULT) {
            mark_fault(d, "pulse", ev[i].level, ev[i].rc);
        } else if (ev[i].kind == DEVS_EV_CLEARED) {
            UOS_MARK("devfault name=%s result=cleared fails=%u state=%s", set.dev[d].name,
                     (unsigned)ev[i].fails, dv_state_word(&set, d));
        } else {
            mark_fail(d, "pulse", ev[i].rc);
        }
    }
    if (now - last_poll < POLL_TICKS) {
        return;
    }
    last_poll = now;
    for (i = 0; i < set.count; i++) {
        int level;

        if (set.dev[i].type != DV_TYPE_SENSOR) {
            continue;
        }
        level = sense_level(i);
        if (level >= 0 && devs_deb_feed(&deb[i], level, DEVS_DEBOUNCE_READS)) {
            (void)dv_sensor_update(&set, i, level);
            mark_state(i, "sensor");
        }
    }
}

static void devs_shutdown(int why)
{
    drive_safe(uos_shutdown_name(why), why == UOS_SHUTDOWN_PANIC ? &io_noi2c : &io);
    /* After the outputs are safe: no more kicks. */
    wd_stop = 1;
    hw_shutdown(why);
}

const uos_service_t uos_svc_devices = { "devices", devs_init, devs_tick_fn, devs_shutdown };
