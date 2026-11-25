/* Host tests for src/dev/devsplan.c: debounce, the ARM order, the safe
 * state, operations mapped to calls through a fake GPIO layer. */

#include "rc_test.h"

#include "devsplan.h"
#include "vfs.h"

/* ---- A fake GPIO layer: a log of calls, and pin levels to read ---- */

#define LOG_MAX 64

typedef struct {
    char kind;          /* 'w' write, 'o' config output, 'i' config input, 'r' read,
                           'W' 'C' 'R' expander */
    unsigned pin;
    int arg;
} call_t;

static call_t g_log[LOG_MAX];
static unsigned g_n;
static int g_levels[64];
static int g_fail_pin = -1;     /* a Pi call on this pin fails */
static int g_fail_left = -1;    /* calls on g_fail_pin left to fail; -1 every one */
static int g_exp_rc = DEVS_ERR_NOEXP;

static int logc(char kind, unsigned pin, int arg)
{
    if (g_n < LOG_MAX) {
        g_log[g_n].kind = kind;
        g_log[g_n].pin = pin;
        g_log[g_n].arg = arg;
        g_n++;
    }
    if ((int)pin != g_fail_pin || g_fail_left == 0) {
        return 0;
    }
    if (g_fail_left > 0) {
        g_fail_left--;
    }
    return -1;
}

static int f_write(unsigned pin, int level) { return logc('w', pin, level); }
static int f_out(unsigned pin, int level) { return logc('o', pin, level); }
static int f_in(unsigned pin, unsigned pull) { return logc('i', pin, (int)pull); }
static int f_read(unsigned pin)
{
    int rc = logc('r', pin, 0);

    return rc < 0 ? rc : g_levels[pin];
}
static int f_xw(const dv_expander_t *c, unsigned pin, int level)
{
    (void)c;
    logc('W', pin, level);
    return g_exp_rc < 0 ? g_exp_rc : 0;
}
static int f_xc(const dv_expander_t *c, unsigned pin, const dv_op_t *op)
{
    (void)c;
    logc('C', pin, op->level);
    return g_exp_rc < 0 ? g_exp_rc : 0;
}
static int f_xr(const dv_expander_t *c, unsigned pin)
{
    (void)c;
    logc('R', pin, 0);
    return g_exp_rc < 0 ? g_exp_rc : 1;
}

static const devs_io_t fake = { f_write, f_out, f_in, f_read, f_xw, f_xc, f_xr };

static dv_set_t g_set;
static dv_cfg_result_t g_res;
static dv_rec_t g_rec;

static void reset_fake(void)
{
    g_n = 0;
    g_fail_pin = -1;
    g_fail_left = -1;
    g_exp_rc = DEVS_ERR_NOEXP;
    memset(g_levels, 0, sizeof(g_levels));
    dv_rec_init(&g_rec);
}

static int load(const char *text)
{
    int rc = dv_cfg_parse_pins(text, strlen(text), DV_REFUSED_UOS, &g_set, &g_res);

    dv_reset(&g_set);
    reset_fake();
    return rc;
}

/* ARM is the second device of four outputs and a sensor. */
static const char *with_arm =
    "[device DOOR1]\ntype = door\ngpio = 12\n"
    "[device ARM]\ntype = relay\ngpio = 13\ninitial = on\n"
    "[device LIGHT1]\ntype = light\ngpio = 25\nactive = low\ninitial = on\n"
    "[device MOTION]\ntype = sensor\ngpio = 21\nactive = low\n"
    "[device VAULT]\ntype = lock\ngpio = 20\npulse_ms = 2000\n";

/* ---- Debounce ---- */

TEST(deb_needs_equal_reads)
{
    devs_deb_t d;

    devs_deb_seed(&d, 0);
    CHECK_EQ(devs_deb_feed(&d, 0, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 1, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 1, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 1, 3), 1);
    CHECK_EQ(d.stable, 1);
    CHECK_EQ(devs_deb_feed(&d, 1, 3), 0);
}

TEST(deb_bounce_restarts_count)
{
    devs_deb_t d;

    devs_deb_seed(&d, 1);
    CHECK_EQ(devs_deb_feed(&d, 0, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 0, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 1, 3), 0);    /* back to stable: count gone */
    CHECK_EQ(devs_deb_feed(&d, 0, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 0, 3), 0);
    CHECK_EQ(devs_deb_feed(&d, 0, 3), 1);
    CHECK_EQ(d.stable, 0);
}

TEST(deb_need_zero_and_one)
{
    devs_deb_t d;

    devs_deb_seed(&d, 0);
    CHECK_EQ(devs_deb_feed(&d, 5, 0), 1);    /* any non-zero is 1; need 0 is 1 */
    CHECK_EQ(d.stable, 1);
    CHECK_EQ(devs_deb_feed(&d, 0, 1), 1);
    CHECK_EQ(devs_deb_feed(NULL, 0, 1), 0);
}

TEST(deb_service_constants)
{
    /* 3 reads at 50 ms: a change shows within 150 ms of stable reads. */
    CHECK_EQ(DEVS_DEBOUNCE_READS, 3);
    CHECK_EQ(DEVS_POLL_MS, 50);
    CHECK_EQ(DEVS_WDOG_MS, 15000);
}

/* ---- Order ---- */

TEST(arm_index_relay_only)
{
    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_arm_index(&g_set), 1);
    CHECK_EQ(load("[device ARM]\ntype = light\ngpio = 12\n"), DV_OK);
    CHECK_EQ(devs_arm_index(&g_set), -1);
    CHECK_EQ(load("[device arm]\ntype = relay\ngpio = 12\n"), DV_OK);
    CHECK_EQ(devs_arm_index(&g_set), 0);     /* names are stored upper-case */
    CHECK_EQ(load("[device ARM2]\ntype = relay\ngpio = 12\n"), DV_OK);
    CHECK_EQ(devs_arm_index(&g_set), -1);
    CHECK_EQ(devs_arm_index(NULL), -1);
}

TEST(boot_plan_arm_last)
{
    dv_op_t ops[DV_MAX_DEVICES];
    static const unsigned pins[] = { 12, 25, 21, 20, 13 };
    unsigned i;

    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_boot_plan(&g_set, ops, DV_MAX_DEVICES), 5);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(ops[i].pin, pins[i]);
    }
    CHECK_EQ(ops[4].kind, DV_OP_CONFIG_OUTPUT);
    CHECK_EQ(ops[4].level, 1);               /* ARM initial on, active high */
    CHECK_EQ(ops[1].level, 0);               /* LIGHT1 on, active low */
    CHECK_EQ(ops[2].kind, DV_OP_CONFIG_INPUT);
    CHECK_EQ(ops[2].pull, DV_PULL_UP);
}

TEST(boot_plan_without_arm_is_file_order)
{
    dv_op_t ops[DV_MAX_DEVICES];

    CHECK_EQ(load("[device A]\ntype = relay\ngpio = 12\n[device B]\ntype = door\ngpio = 13\n"),
             DV_OK);
    CHECK_EQ(devs_boot_plan(&g_set, ops, DV_MAX_DEVICES), 2);
    CHECK_EQ(ops[0].pin, 12);
    CHECK_EQ(ops[1].pin, 13);
    CHECK_EQ(devs_boot_plan(&g_set, ops, 1), DV_ERR_SPACE);
}

TEST(down_plan_arm_first_then_reverse)
{
    dv_op_t ops[DV_MAX_DEVICES];
    static const unsigned pins[] = { 13, 20, 25, 12 };
    static const unsigned levels[] = { 0, 0, 0, 0 };
    unsigned i;

    CHECK_EQ(load(with_arm), DV_OK);
    /* Turn things on: the plan ignores the state. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OPEN, DV_PRIV_SYSTEM, 0, NULL, 0), 0);
    CHECK_EQ(devs_down_plan(&g_set, ops, DV_MAX_DEVICES), 4);
    for (i = 0; i < 4; i++) {
        CHECK_EQ(ops[i].pin, pins[i]);
        CHECK_EQ(ops[i].level, levels[i]);
        CHECK_EQ(ops[i].kind, DV_OP_CONFIG_OUTPUT);
    }
    CHECK_EQ(g_set.st[0].on, 1);             /* no state change */
    CHECK_EQ(devs_down_plan(&g_set, ops, 3), DV_ERR_SPACE);
    CHECK_EQ(devs_down_plan(NULL, ops, 4), DV_ERR_ARG);
}

TEST(down_plan_initial_on_stays_on)
{
    dv_op_t ops[DV_MAX_DEVICES];

    CHECK_EQ(load("[device L]\ntype = light\ngpio = 12\ninitial = on\n"
                  "[device S]\ntype = sensor\ngpio = 13\n"), DV_OK);
    CHECK_EQ(devs_down_plan(&g_set, ops, 1), 1);
    CHECK_EQ(ops[0].pin, 12);
    CHECK_EQ(ops[0].level, 1);
}

/* ---- Operations to calls ---- */

TEST(apply_maps_each_kind)
{
    dv_op_t op = { 0 };

    CHECK_EQ(load(with_arm), DV_OK);
    op.chip = DV_CHIP_PI;
    op.pin = 12;
    op.level = 1;
    op.kind = DV_OP_WRITE;
    CHECK_EQ(devs_apply(&fake, &g_set, &op, &g_rec, 7), 0);
    op.kind = DV_OP_CONFIG_OUTPUT;
    op.level = 0;
    CHECK_EQ(devs_apply(&fake, &g_set, &op, &g_rec, 8), 0);
    op.kind = DV_OP_CONFIG_INPUT;
    op.pull = DV_PULL_DOWN;
    CHECK_EQ(devs_apply(&fake, &g_set, &op, &g_rec, 9), 0);
    CHECK_EQ(g_n, 3);
    CHECK_EQ(g_log[0].kind, 'w');
    CHECK_EQ(g_log[0].arg, 1);
    CHECK_EQ(g_log[1].kind, 'o');
    CHECK_EQ(g_log[2].kind, 'i');
    CHECK_EQ(g_log[2].arg, DV_PULL_DOWN);
    /* Writes are recorded; an input setup is not. */
    CHECK_EQ(g_rec.count, 2);
    CHECK_EQ(g_rec.e[0].tick, 7);
    CHECK_EQ(g_rec.e[1].level, 0);
    op.kind = 9;
    CHECK_EQ(devs_apply(&fake, &g_set, &op, &g_rec, 9), DV_ERR_ARG);
    CHECK_EQ(devs_apply(NULL, &g_set, &op, &g_rec, 9), DV_ERR_ARG);
}

TEST(apply_failure_not_recorded)
{
    dv_op_t op = { 0 };

    CHECK_EQ(load(with_arm), DV_OK);
    g_fail_pin = 12;
    op.chip = DV_CHIP_PI;
    op.pin = 12;
    op.kind = DV_OP_WRITE;
    CHECK_EQ(devs_apply(&fake, &g_set, &op, &g_rec, 1), DEVS_ERR_IO);
    CHECK_EQ(g_rec.count, 0);
}

static const char *with_exp =
    "[expander 0]\naddr = 0x20\n"
    "[device LAMP]\ntype = light\ngpio = X0.3\n"
    "[device PIR]\ntype = sensor\ngpio = X0.4\n";

TEST(apply_expander_goes_to_exp_functions)
{
    dv_op_t ops[DV_MAX_DEVICES];

    CHECK_EQ(load(with_exp), DV_OK);
    CHECK_EQ(g_set.count, 2);
    CHECK_EQ(devs_boot_plan(&g_set, ops, DV_MAX_DEVICES), 2);
    CHECK_EQ(devs_apply(&fake, &g_set, &ops[0], &g_rec, 1), DEVS_ERR_NOEXP);
    CHECK_EQ(g_log[0].kind, 'C');
    CHECK_EQ(g_log[0].pin, 3);
    CHECK_EQ(g_rec.count, 0);
    CHECK_EQ(devs_read(&fake, &g_set, 1), DEVS_ERR_NOEXP);
    /* A driver that works: the same path succeeds and records. */
    g_exp_rc = 0;
    CHECK_EQ(devs_apply(&fake, &g_set, &ops[0], &g_rec, 1), 0);
    CHECK_EQ(g_rec.count, 1);
    CHECK_EQ(devs_read(&fake, &g_set, 1), 1);
}

TEST(cmd_expander_fails_and_keeps_state)
{
    CHECK_EQ(load(with_exp), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_ON, DV_PRIV_SYSTEM, 0, &g_rec, 0),
             DEVS_ERR_NOEXP);
    CHECK_EQ(g_set.st[0].on, 0);
    CHECK_EQ(g_log[0].kind, 'W');
    CHECK_STR(devs_strerror(DEVS_ERR_NOEXP),
              "%DEVICE-E-NOEXPDRV, expander not available in this build");
}

TEST(cmd_pulse_end_only_from_tick)
{
    devs_ev_t ended[DV_MAX_DEVICES];

    CHECK_EQ(load(with_arm), DV_OK);
    /* VAULT, index 4, pin 20: 2000 ms. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, DV_PRIV_OPER, 1000, &g_rec, 100), 0);
    CHECK_EQ(g_n, 1);                         /* the on write only */
    CHECK_EQ(g_log[0].kind, 'w');
    CHECK_EQ(g_log[0].arg, 1);
    CHECK_EQ(devs_tick(&g_set, &fake, 2999, &g_rec, 299, ended), 0);
    CHECK_EQ(g_n, 1);
    /* Late is fine: a tick far past the end still ends it, once. */
    CHECK_EQ(devs_tick(&g_set, &fake, 9000, &g_rec, 900, ended), 1);
    CHECK_EQ(ended[0].dev, 4);
    CHECK_EQ(ended[0].kind, DEVS_EV_ENDED);
    CHECK_EQ(g_n, 2);
    CHECK_EQ(g_log[1].arg, 0);
    CHECK_EQ(g_set.st[4].on, 0);
    CHECK_EQ(g_rec.count, 2);
    CHECK_EQ(g_rec.e[1].tick, 900);
    CHECK_EQ(devs_tick(&g_set, &fake, 9001, &g_rec, 901, ended), 0);
}

TEST(cmd_pulse_end_across_wrap)
{
    devs_ev_t ended[DV_MAX_DEVICES];

    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_PULSE, DV_PRIV_SYSTEM, 0xFFFFFF00u, &g_rec, 0), 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 0xFFFFFFFFu, &g_rec, 0, ended), 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 2000u, &g_rec, 0, ended), 1);
}

TEST(cmd_two_pulses_and_cancel)
{
    devs_ev_t ended[DV_MAX_DEVICES];

    CHECK_EQ(load("[device A]\ntype = lock\ngpio = 12\npulse_ms = 100\n"
                  "[device B]\ntype = door\ngpio = 13\npulse_ms = 300\n"), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_PULSE, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    CHECK_EQ(devs_cmd(&g_set, &fake, 1, DV_ACT_PULSE, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 100, &g_rec, 10, ended), 1);
    CHECK_EQ(ended[0].dev, 0);
    /* A new command cancels B's end. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 1, DV_ACT_ON, DV_PRIV_SYSTEM, 150, &g_rec, 15), 0);
    CHECK_EQ(devs_cmd(&g_set, &fake, 1, DV_ACT_OFF, DV_PRIV_SYSTEM, 160, &g_rec, 16), 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 1000, &g_rec, 100, ended), 0);
}

TEST(cmd_refusals_change_nothing)
{
    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, 0, 0, &g_rec, 0), DV_ERR_NOPRIV);
    CHECK_EQ(devs_cmd(&g_set, &fake, 3, DV_ACT_ON, DV_PRIV_SYSTEM, 0, &g_rec, 0),
             DV_ERR_READONLY);
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_LOCK, DV_PRIV_SYSTEM, 0, &g_rec, 0),
             DV_ERR_BADACTION);
    CHECK_EQ(devs_cmd(&g_set, &fake, 99, DV_ACT_ON, DV_PRIV_SYSTEM, 0, &g_rec, 0), DV_ERR_ARG);
    CHECK_EQ(g_n, 0);
    CHECK_EQ(g_rec.count, 0);
}

TEST(cmd_io_failure_restores_state)
{
    CHECK_EQ(load(with_arm), DV_OK);
    g_fail_pin = 12;
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OPEN, DV_PRIV_SYSTEM, 0, &g_rec, 0), DEVS_ERR_IO);
    CHECK_EQ(g_set.st[0].on, 0);
}

/* ---- Pin faults ---- */

static void status(unsigned idx, char *buf)
{
    CHECK(dv_status_line(&g_set, idx, buf, DV_STATUS_LINE_MAX) > 0);
}

TEST(fault_pulse_end_keeps_on_and_retries)
{
    devs_ev_t ev[DV_MAX_DEVICES];
    char line[DV_STATUS_LINE_MAX];

    CHECK_EQ(load(with_arm), DV_OK);
    /* VAULT, index 4, pin 20, pulse 2000 ms. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    g_fail_pin = 20;
    g_fail_left = 1;
    CHECK_EQ(devs_tick(&g_set, &fake, 2000, &g_rec, 200, ev), 1);
    CHECK_EQ(ev[0].kind, DEVS_EV_FAULT);
    CHECK_EQ(ev[0].dev, 4);
    CHECK_EQ(ev[0].rc, DEVS_ERR_IO);
    /* The pin is still high: the status must not say LOCKED. */
    CHECK_EQ(g_set.st[4].on, 1);
    CHECK_EQ(g_set.st[4].level, 1);
    CHECK_EQ(g_set.st[4].fault, 1);
    CHECK_STR(dv_state_word(&g_set, 4), "UNLOCKED");
    status(4, line);
    CHECK(strstr(line, "UNLOCKED") != NULL);
    CHECK(strstr(line, " FAULT=1") != NULL);
    CHECK_EQ(g_rec.count, 1);                   /* the failed write is not recorded */
    /* The next tick retries the owed write, and it succeeds. */
    g_n = 0;
    CHECK_EQ(devs_tick(&g_set, &fake, 2010, &g_rec, 201, ev), 1);
    CHECK_EQ(g_n, 1);
    CHECK_EQ(g_log[0].kind, 'w');
    CHECK_EQ(g_log[0].pin, 20);
    CHECK_EQ(g_log[0].arg, 0);
    CHECK_EQ(ev[0].kind, DEVS_EV_CLEARED);
    CHECK_EQ(ev[0].fails, 1);
    CHECK_EQ(g_set.st[4].on, 0);
    CHECK_EQ(g_set.st[4].fault, 0);
    status(4, line);
    CHECK(strstr(line, "LOCKED") != NULL && strstr(line, "FAULT") == NULL);
    CHECK_EQ(g_rec.count, 2);
    /* Nothing more is owed. */
    g_n = 0;
    CHECK_EQ(devs_tick(&g_set, &fake, 2020, &g_rec, 202, ev), 0);
    CHECK_EQ(g_n, 0);
}

TEST(fault_lasts_several_ticks_and_counts)
{
    devs_ev_t ev[DV_MAX_DEVICES];
    char line[DV_STATUS_LINE_MAX];
    unsigned t;

    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_PULSE, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    g_fail_pin = 20;
    g_fail_left = 5;
    CHECK_EQ(devs_tick(&g_set, &fake, 2000, &g_rec, 1, ev), 1);
    CHECK_EQ(ev[0].kind, DEVS_EV_FAULT);
    /* Four failing retries: no event, the count grows, one try per tick. */
    for (t = 2; t <= 5; t++) {
        g_n = 0;
        CHECK_EQ(devs_tick(&g_set, &fake, 2000 + t * 10, &g_rec, t, ev), 0);
        CHECK_EQ(g_n, 1);
        CHECK_EQ(g_set.st[4].fails, t);
        CHECK_EQ(g_set.st[4].on, 1);
    }
    status(4, line);
    CHECK(strstr(line, " FAULT=5") != NULL);
    /* A command is refused while the write is owed, and writes nothing. */
    g_n = 0;
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_LOCK, DV_PRIV_SYSTEM, 3000, &g_rec, 6),
             DEVS_ERR_FAULT);
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, DV_PRIV_SYSTEM, 3000, &g_rec, 6),
             DEVS_ERR_FAULT);
    CHECK_EQ(g_n, 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 3000, &g_rec, 7, ev), 1);
    CHECK_EQ(ev[0].kind, DEVS_EV_CLEARED);
    CHECK_EQ(ev[0].fails, 5);
    CHECK_EQ(g_set.st[4].fails, 0);
    /* The device takes commands again. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_LOCK, DV_PRIV_SYSTEM, 3010, &g_rec, 8), 0);
}

TEST(fault_count_stops_at_max)
{
    devs_ev_t ev[DV_MAX_DEVICES];
    char line[DV_STATUS_LINE_MAX];

    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_PULSE, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    g_fail_pin = 20;
    CHECK_EQ(devs_tick(&g_set, &fake, 2000, &g_rec, 1, ev), 1);
    g_set.st[4].fails = 0xFFFEu;
    CHECK_EQ(devs_tick(&g_set, &fake, 2010, &g_rec, 2, ev), 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 2020, &g_rec, 3, ev), 0);
    CHECK_EQ(g_set.st[4].fails, 0xFFFFu);
    CHECK_EQ(g_set.st[4].fault, 1);             /* it never gives up */
    status(4, line);
    CHECK(strstr(line, " FAULT=65535") != NULL);
    CHECK(strlen(line) < DV_STATUS_LINE_MAX);
}

TEST(fault_pulse_start_failure_owes_nothing)
{
    devs_ev_t ev[DV_MAX_DEVICES];

    CHECK_EQ(load(with_arm), DV_OK);
    g_fail_pin = 20;
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, DV_PRIV_SYSTEM, 0, &g_rec, 0),
             DEVS_ERR_IO);
    CHECK_EQ(g_set.st[4].on, 0);
    CHECK_EQ(g_set.st[4].pending, 0);
    CHECK_EQ(g_set.st[4].fault, 0);
    g_n = 0;
    CHECK_EQ(devs_tick(&g_set, &fake, 5000, &g_rec, 1, ev), 0);
    CHECK_EQ(g_n, 0);
}

TEST(fault_command_to_safe_level)
{
    devs_ev_t ev[DV_MAX_DEVICES];

    CHECK_EQ(load(with_arm), DV_OK);
    /* DOOR1, pin 12, initial off: OPEN fails toward active, nothing owed. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OPEN, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    g_fail_pin = 12;
    g_fail_left = 2;
    /* CLOSE goes to the safe level: the fault starts, the state stays OPEN. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_CLOSE, DV_PRIV_SYSTEM, 10, &g_rec, 1),
             DEVS_ERR_FAULT);
    CHECK_STR(dv_state_word(&g_set, 0), "OPEN");
    CHECK_EQ(g_set.st[0].fault, 1);
    CHECK_EQ(g_set.st[0].owed, 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 20, &g_rec, 2, ev), 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 30, &g_rec, 3, ev), 1);
    CHECK_EQ(ev[0].kind, DEVS_EV_CLEARED);
    CHECK_STR(dv_state_word(&g_set, 0), "CLOSED");
    /* An OFF during a pulse that fails cancels the end and owes it. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, DV_PRIV_SYSTEM, 0, &g_rec, 4), 0);
    g_fail_pin = 20;
    g_fail_left = 1;
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_LOCK, DV_PRIV_SYSTEM, 100, &g_rec, 5),
             DEVS_ERR_FAULT);
    CHECK_EQ(g_set.st[4].on, 1);
    CHECK_EQ(g_set.st[4].pending, 0);
    CHECK_EQ(devs_tick(&g_set, &fake, 110, &g_rec, 6, ev), 1);
    CHECK_EQ(ev[0].kind, DEVS_EV_CLEARED);
    CHECK_EQ(g_set.st[4].on, 0);
    CHECK_STR(devs_strerror(DEVS_ERR_FAULT),
              "%DEVICE-E-PINFAULT, a pin write failed and is retried; command refused");
    CHECK_EQ(devs_vfs_code(DEVS_ERR_FAULT), VFS_E_WRITE);
}

TEST(fault_safe_level_of_arm_and_initial_on)
{
    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_safe_level(&g_set, 1), 0);    /* ARM, initial on: off is safe */
    CHECK_EQ(devs_safe_level(&g_set, 2), 0);    /* LIGHT1 on, active low */
    CHECK_EQ(devs_safe_level(&g_set, 0), 0);
    CHECK_EQ(devs_safe_level(&g_set, 9), DV_ERR_ARG);
    /* LIGHT1's OFF goes away from its safe level: nothing owed. */
    g_fail_pin = 25;
    CHECK_EQ(devs_cmd(&g_set, &fake, 2, DV_ACT_OFF, DV_PRIV_SYSTEM, 0, &g_rec, 0), DEVS_ERR_IO);
    CHECK_EQ(g_set.st[2].fault, 0);
}

TEST(fault_expander_without_driver_owes_nothing)
{
    CHECK_EQ(load(with_exp), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OFF, DV_PRIV_SYSTEM, 0, &g_rec, 0),
             DEVS_ERR_NOEXP);
    CHECK_EQ(g_set.st[0].fault, 0);
    /* A driver error is a fault like a Pi one. */
    g_exp_rc = -7;
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OFF, DV_PRIV_SYSTEM, 0, &g_rec, 0),
             DEVS_ERR_FAULT);
    CHECK_EQ(g_set.st[0].fault, 1);
}

TEST(fault_boot_holds_arm_off)
{
    devs_ev_t ev[DV_MAX_DEVICES + 1];
    unsigned nev = 0;

    CHECK_EQ(load(with_arm), DV_OK);
    g_fail_pin = 25;                            /* LIGHT1 */
    g_fail_left = 1;
    CHECK_EQ(devs_boot_run(&g_set, &fake, &g_rec, 0, ev, &nev), 5);
    CHECK_EQ(nev, 2);
    CHECK_EQ(ev[0].kind, DEVS_EV_FAULT);
    CHECK_EQ(ev[0].dev, 2);
    CHECK_EQ(ev[1].kind, DEVS_EV_ARMHELD);
    CHECK_EQ(ev[1].dev, 1);
    /* LIGHT1 was never configured: off, with its config owed. */
    CHECK_EQ(g_set.st[2].on, 0);
    CHECK_EQ(g_set.st[2].fault, 1);
    CHECK_EQ(g_set.st[2].owed_kind, DV_OP_CONFIG_OUTPUT);
    /* ARM written off, last. */
    CHECK_EQ(g_log[g_n - 1].pin, 13);
    CHECK_EQ(g_log[g_n - 1].arg, 0);
    CHECK_EQ(g_set.st[1].on, 0);
    /* The tick retries the config. */
    CHECK_EQ(devs_tick(&g_set, &fake, 10, &g_rec, 1, ev), 1);
    CHECK_EQ(g_log[g_n - 1].kind, 'o');
    CHECK_EQ(g_set.st[2].on, 1);
    /* A clean boot arms. */
    reset_fake();
    CHECK_EQ(devs_boot_run(&g_set, &fake, &g_rec, 0, ev, &nev), 5);
    CHECK_EQ(nev, 0);
    CHECK_EQ(g_set.st[1].on, 1);
    CHECK_EQ(g_log[g_n - 1].arg, 1);
    /* An ARM that fails itself holds nothing else. */
    reset_fake();
    g_fail_pin = 13;
    CHECK_EQ(devs_boot_run(&g_set, &fake, &g_rec, 0, ev, &nev), 5);
    CHECK_EQ(nev, 1);
    CHECK_EQ(ev[0].kind, DEVS_EV_FAULT);
    CHECK_EQ(g_set.st[1].on, 0);
}

/* An expander light first, then a Pi light, ARM last and initially on. */
static const char *exp_arm =
    "[expander 0]\naddr = 0x20\n"
    "[device LAMP]\ntype = light\ngpio = X0.3\ninitial = on\n"
    "[device DOOR1]\ntype = door\ngpio = 12\n"
    "[device ARM]\ntype = relay\ngpio = 13\ninitial = on\n";

/* Boots exp_arm; returns 1 when ARM was held off with LAMP unreachable. */
static int boot_held(int exp_rc, int fail_pin)
{
    devs_ev_t ev[DV_MAX_DEVICES + 1];
    unsigned nev = 0;

    if (load(exp_arm) != DV_OK) {
        return 0;
    }
    g_exp_rc = exp_rc;
    g_fail_pin = fail_pin;
    if (devs_boot_run(&g_set, &fake, &g_rec, 0, ev, &nev) != 3 || nev != 2) {
        return 0;
    }
    return ev[1].kind == DEVS_EV_ARMHELD && ev[1].dev == 2 && g_set.st[2].on == 0 &&
           g_log[g_n - 1].pin == 13 && g_log[g_n - 1].arg == 0 &&
           devs_unreachable(&g_set) == 1;
}

TEST(boot_no_expander_driver_holds_arm)
{
    char line[DV_STATUS_LINE_MAX];

    CHECK(boot_held(DEVS_ERR_NOEXP, -1));
    /* No driver: no fault, nothing owed, but unreached and off. */
    CHECK_EQ(g_set.st[0].fault, 0);
    CHECK_EQ(g_set.st[0].unreached, 1);
    CHECK_EQ(g_set.st[0].on, 0);
    status(0, line);
    CHECK(strstr(line, "X0.3 UNREACHABLE") != NULL);
    CHECK(devs_status(&g_set, 2, line, sizeof(line)) > 0);
    CHECK(strstr(line, " OFF ") != NULL);
    CHECK(strstr(line, "GPIO 13 UNREACH=1") != NULL);
    /* DOOR1 is fine: no mark on its line. */
    CHECK(devs_status(&g_set, 1, line, sizeof(line)) > 0);
    CHECK(strstr(line, "UNREACH") == NULL);
}

TEST(boot_absent_expander_holds_arm)
{
    /* A driver that finds no chip: a fault, retried. */
    CHECK(boot_held(-7, -1));
    CHECK_EQ(g_set.st[0].fault, 1);
    CHECK_EQ(g_set.st[0].unreached, 0);
}

TEST(boot_refused_pin_holds_arm)
{
    /* The driver refuses the pin's argument: counted like any failure. */
    CHECK(boot_held(DV_ERR_ARG, -1));
}

TEST(boot_pi_error_holds_arm)
{
    devs_ev_t ev[DV_MAX_DEVICES + 1];
    unsigned nev = 0;

    CHECK_EQ(load(exp_arm), DV_OK);
    g_exp_rc = 0;
    g_fail_pin = 12;                            /* DOOR1 */
    CHECK_EQ(devs_boot_run(&g_set, &fake, &g_rec, 0, ev, &nev), 3);
    CHECK_EQ(nev, 2);
    CHECK_EQ(ev[0].kind, DEVS_EV_FAULT);
    CHECK_EQ(ev[1].kind, DEVS_EV_ARMHELD);
    CHECK_EQ(g_set.st[2].on, 0);
    CHECK_EQ(devs_unreachable(&g_set), 1);
    /* Everything reached: ARM on, nothing unreachable. */
    reset_fake();
    g_exp_rc = 0;
    CHECK_EQ(devs_boot_run(&g_set, &fake, &g_rec, 0, ev, &nev), 3);
    CHECK_EQ(nev, 0);
    CHECK_EQ(g_set.st[2].on, 1);
    CHECK_EQ(devs_unreachable(&g_set), 0);
}

TEST(arm_by_hand_while_unreachable)
{
    char line[DV_STATUS_LINE_MAX];

    CHECK(boot_held(DEVS_ERR_NOEXP, -1));
    /* The operator may still arm by hand; the count stays visible. */
    CHECK_EQ(devs_cmd(&g_set, &fake, 2, DV_ACT_ON, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    CHECK_EQ(g_set.st[2].on, 1);
    CHECK(devs_status(&g_set, 2, line, sizeof(line)) > 0);
    CHECK(strstr(line, " ON ") != NULL && strstr(line, "UNREACH=1") != NULL);
    /* A write that reaches LAMP ends its mark. */
    g_exp_rc = 0;
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OFF, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    CHECK_EQ(g_set.st[0].unreached, 0);
    CHECK_EQ(devs_unreachable(&g_set), 0);
    CHECK(devs_status(&g_set, 2, line, sizeof(line)) > 0);
    CHECK(strstr(line, "UNREACH") == NULL);
}

TEST(status_line_fits_every_mark)
{
    char line[DV_STATUS_LINE_MAX];

    CHECK(boot_held(DEVS_ERR_NOEXP, -1));
    g_set.st[2].fault = 1;
    g_set.st[2].fails = 65535;
    g_set.st[0].fault = 1;
    g_set.st[0].fails = 65535;
    g_set.count = 3;
    CHECK(devs_status(&g_set, 0, line, sizeof(line)) > 0);
    CHECK(strstr(line, "FAULT=65535 UNREACHABLE") != NULL);
    CHECK(devs_status(&g_set, 2, line, sizeof(line)) > 0);
    CHECK(strstr(line, "FAULT=65535 UNREACH=1") != NULL);
    CHECK_EQ(devs_status(&g_set, 2, line, 70), DV_ERR_SPACE);
}

TEST(fault_safe_state_bounded_with_arm)
{
    dv_op_t ops[DV_MAX_DEVICES];
    devs_ev_t ev[DV_MAX_DEVICES];
    static const unsigned pins[] = { 13, 20, 25, 12 };
    unsigned i;

    CHECK_EQ(load(with_arm), DV_OK);
    CHECK_EQ(devs_cmd(&g_set, &fake, 0, DV_ACT_OPEN, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    CHECK_EQ(devs_cmd(&g_set, &fake, 1, DV_ACT_ON, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    CHECK_EQ(devs_cmd(&g_set, &fake, 4, DV_ACT_UNLOCK, DV_PRIV_SYSTEM, 0, &g_rec, 0), 0);
    reset_fake();
    g_fail_pin = 13;                            /* ARM's off write fails for good */
    CHECK_EQ(devs_down_run(&g_set, &fake, &g_rec, 5, ops, ev), 4);
    /* ARM: three tries, then the others still go safe, in order. */
    CHECK_EQ(g_n, 3 + 3);
    for (i = 0; i < 3; i++) {
        CHECK_EQ(g_log[i].pin, 13);
    }
    CHECK_EQ(ev[0].rc, DEVS_ERR_IO);
    CHECK_EQ(ev[0].tries, DEVS_SAFE_TRIES);
    CHECK_EQ(ev[0].kind, DEVS_EV_SAFE);
    for (i = 0; i < 4; i++) {
        CHECK_EQ(ops[i].pin, pins[i]);
    }
    for (i = 1; i < 4; i++) {
        CHECK_EQ(ev[i].rc, 0);
        CHECK_EQ(ev[i].tries, 1);
        CHECK_EQ(g_log[2 + i].pin, pins[i]);
    }
    /* The model keeps ARM on, as its pin is, and in fault. */
    CHECK_EQ(g_set.st[1].on, 1);
    CHECK_EQ(g_set.st[1].fault, 1);
    CHECK_EQ(g_set.st[0].on, 0);
    CHECK_EQ(g_set.st[4].on, 0);
    CHECK_EQ(g_set.st[4].pending, 0);
    /* A write that fails twice then works takes two retries. */
    CHECK_EQ(load(with_arm), DV_OK);
    g_fail_pin = 20;
    g_fail_left = 2;
    CHECK_EQ(devs_down_run(&g_set, &fake, &g_rec, 5, ops, ev), 4);
    CHECK_EQ(ev[1].rc, 0);
    CHECK_EQ(ev[1].tries, 3);
    CHECK_EQ(g_set.st[4].fault, 0);
}

TEST(read_pi_pin)
{
    CHECK_EQ(load(with_arm), DV_OK);
    g_levels[21] = 1;
    CHECK_EQ(devs_read(&fake, &g_set, 3), 1);
    g_fail_pin = 21;
    CHECK_EQ(devs_read(&fake, &g_set, 3), DEVS_ERR_IO);
    CHECK_EQ(devs_read(&fake, &g_set, 9), DV_ERR_ARG);
}

TEST(vfs_codes)
{
    CHECK_EQ(devs_vfs_code(0), 0);
    CHECK_EQ(devs_vfs_code(17), 17);
    CHECK_EQ(devs_vfs_code(DV_ERR_NOSUCHDEV), VFS_E_NODEV);
    CHECK_EQ(devs_vfs_code(DEVS_ERR_DOWN), VFS_E_NODEV);
    CHECK_EQ(devs_vfs_code(DV_ERR_NOPRIV), VFS_E_NOPRIV);
    CHECK_EQ(devs_vfs_code(DV_ERR_READONLY), VFS_E_READONLY);
    CHECK_EQ(devs_vfs_code(DV_ERR_SPACE), VFS_E_TOOBIG);
    CHECK_EQ(devs_vfs_code(DEVS_ERR_IO), VFS_E_WRITE);
    CHECK_EQ(devs_vfs_code(DEVS_ERR_NOEXP), VFS_E_WRITE);
    CHECK_EQ(devs_vfs_code(DV_ERR_BADACTION), VFS_E_ARG);
    CHECK_EQ(devs_vfs_code(DV_ERR_NOPULSE), VFS_E_ARG);
    CHECK_EQ(devs_vfs_code(DV_ERR_ARG), VFS_E_ARG);
}

TEST(messages_and_idents)
{
    char w[24];

    CHECK_STR(devs_strerror(DEVS_ERR_IO), "%DEVICE-E-IOERR, the GPIO layer refused the operation");
    CHECK_STR(devs_strerror(DEVS_ERR_DOWN), "%DEVICE-E-NODEVICES, no devices are configured");
    CHECK_STR(devs_strerror(DV_ERR_NOPRIV), "%DEVICE-W-NOPRIV, insufficient privilege");
    CHECK_EQ(devs_msg_ident(devs_strerror(DV_WARN_PULLUP), w, sizeof(w)), 6);
    CHECK_STR(w, "PULLUP");
    CHECK_EQ(devs_msg_ident(devs_strerror(DEVS_ERR_NOEXP), w, sizeof(w)), 8);
    CHECK_STR(w, "NOEXPDRV");
    CHECK_EQ(devs_msg_ident(devs_strerror(DEVS_ERR_NOEXP), w, 8), DV_ERR_SPACE);
    CHECK_STR(w, "");
    CHECK_EQ(devs_msg_ident(NULL, w, sizeof(w)), DV_ERR_ARG);
}
