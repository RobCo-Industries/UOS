/* Host tests for src/dev: devcfg.c and devstate.c. */

#include "rc_test.h"

#include "dev.h"

static dv_set_t g_set;
static dv_cfg_result_t g_res;

static int parse(const char *text)
{
    int rc = dv_cfg_parse(text, strlen(text), &g_set, &g_res);

    dv_reset(&g_set);
    return rc;
}

/*
 * One good door on pin 4, then `extra` lines appended. When `extra`
 * starts with type or gpio, that key replaces the good one: a key given
 * twice refuses the section.
 */
static int parse_one(const char *extra)
{
    static char buf[1024];
    const char *type = strncmp(extra, "type", 4) == 0 ? "" : "type = door\n";
    const char *gpio = strncmp(extra, "gpio", 4) == 0 ? "" : "gpio = 4\n";

    snprintf(buf, sizeof(buf), "[device D1]\n%s%s%s\n", type, gpio, extra);
    return parse(buf);
}

static const char *design_example =
    "[device DOOR1]\n"
    "type = door            ; door | light | turret | lock | relay | sensor\n"
    "gpio = 17              ; BCM number\n"
    "active = high          ; high | low: the level that means \"on\"\n"
    "initial = off          ; on | off: the state set at boot (outputs only)\n"
    "pulse_ms = 0           ; 0 = level; otherwise the output goes on for this long, then off\n"
    "label = Workshop door\n"
    "privs = 2              ; privilege mask needed to change it (default OPER)\n";

static const char *four =
    "; four devices\n"
    "[device DOOR1]\n"
    "type = door\n"
    "gpio = 4\n"
    "label = Workshop door\n"
    "[device light1]\n"
    "type = LIGHT\n"
    "gpio = 25\n"
    "active = low\n"
    "initial = on\n"
    "label = Hall lights\n"
    "privs = 0\n"
    "[device VAULT]\n"
    "type = lock\n"
    "gpio = 20\n"
    "pulse_ms = 500\n"
    "label = Vault strike\n"
    "privs = 0x6\n"
    "[device PIR_1]\n"
    "type = sensor\n"
    "gpio = 21\n"
    "active = low\n"
    "label = Corridor motion\n";

/* ---- parser ---- */

TEST(cfg_design_example_refuses_pin_17)
{
    CHECK_EQ(parse(design_example), DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.skipped, 1);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_PINUSED);
    CHECK_EQ(g_res.errs[0].line, 3);
    CHECK_STR(g_res.errs[0].name, "DOOR1");
}

TEST(cfg_design_example_on_free_pin)
{
    char buf[1024];
    char *p;

    snprintf(buf, sizeof(buf), "%s", design_example);
    p = strstr(buf, "gpio = 17");
    p[7] = ' ';
    p[8] = '4';
    CHECK_EQ(parse(buf), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.unknown, 0);
    CHECK_STR(g_set.dev[0].name, "DOOR1");
    CHECK_STR(g_set.dev[0].label, "Workshop door");
    CHECK_EQ(g_set.dev[0].type, DV_TYPE_DOOR);
    CHECK_EQ(g_set.dev[0].pin, 4);
    CHECK_EQ(g_set.dev[0].active_high, 1);
    CHECK_EQ(g_set.dev[0].initial_on, 0);
    CHECK_EQ(g_set.dev[0].pulse_ms, 0);
    CHECK_EQ(g_set.dev[0].privs, 2);
}

TEST(cfg_four_devices)
{
    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(g_res.loaded, 4);
    CHECK_EQ(g_res.skipped, 0);
    CHECK_EQ(g_res.err_count, 0);
    CHECK_STR(g_set.dev[1].name, "LIGHT1");
    CHECK_EQ(g_set.dev[1].type, DV_TYPE_LIGHT);
    CHECK_EQ(g_set.dev[1].active_high, 0);
    CHECK_EQ(g_set.dev[1].initial_on, 1);
    CHECK_EQ(g_set.dev[1].privs, 0);
    CHECK_EQ(g_set.dev[2].pulse_ms, 500);
    CHECK_EQ(g_set.dev[2].privs, 6);
    CHECK_EQ(g_set.dev[0].privs, DV_PRIVS_DEFAULT);
    CHECK_EQ(g_set.dev[3].type, DV_TYPE_SENSOR);
}

TEST(cfg_defaults)
{
    CHECK_EQ(parse_one(""), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_set.dev[0].active_high, 1);
    CHECK_EQ(g_set.dev[0].initial_on, 0);
    CHECK_EQ(g_set.dev[0].pulse_ms, 0);
    CHECK_STR(g_set.dev[0].label, "");
    CHECK_EQ(g_set.dev[0].privs, DV_PRIV_OPER);
}

static void expect_bad(const char *extra, int code)
{
    CHECK_EQ(parse_one(extra), DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.skipped, 1);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, code);
    CHECK_STR(g_res.errs[0].name, "D1");
}

TEST(cfg_bad_fields_alone)
{
    expect_bad("type = robot", DV_ERR_TYPE);
    expect_bad("gpio = x", DV_ERR_GPIO);
    expect_bad("gpio = -4", DV_ERR_GPIO);
    expect_bad("gpio = 4000", DV_ERR_GPIO);
    expect_bad("gpio = 28", DV_ERR_PINUSED);
    expect_bad("gpio = 53", DV_ERR_PINUSED);
    expect_bad("gpio = 999", DV_ERR_PINUSED);
    expect_bad("active = middle", DV_ERR_ACTIVE);
    expect_bad("initial = maybe", DV_ERR_INITIAL);
    expect_bad("pulse_ms = 9", DV_ERR_PULSE);
    expect_bad("pulse_ms = 10001", DV_ERR_PULSE);
    expect_bad("pulse_ms = abc", DV_ERR_PULSE);
    expect_bad("pulse_ms = 100000", DV_ERR_PULSE);
    expect_bad("label = 12345678901234567890123456789012", DV_ERR_LABEL);
    expect_bad("label = \"tab\\nnewline\"", DV_ERR_LABEL);
    expect_bad("privs = 0x100000000", DV_ERR_PRIVS);
    expect_bad("privs = 4294967296", DV_ERR_PRIVS);
    expect_bad("privs = oper", DV_ERR_PRIVS);
    expect_bad("pulse_ms = 100\ninitial = on", DV_ERR_INITIAL);
    CHECK_EQ(g_res.errs[0].line, 1);
}

TEST(cfg_field_limits_good)
{
    CHECK_EQ(parse_one("pulse_ms = 10"), DV_OK);
    CHECK_EQ(g_set.dev[0].pulse_ms, 10);
    CHECK_EQ(parse_one("pulse_ms = 10000"), DV_OK);
    CHECK_EQ(g_set.dev[0].pulse_ms, 10000);
    CHECK_EQ(parse_one("label = 1234567890123456789012345678901"), DV_OK);
    CHECK_EQ(strlen(g_set.dev[0].label), 31);
    CHECK_EQ(parse_one("privs = 0xFFFFFFFF"), DV_OK);
    CHECK_EQ(g_set.dev[0].privs, 0xFFFFFFFFu);
    CHECK_EQ(parse_one("privs = 4294967295\nactive = LOW\ninitial = ON"), DV_OK);
    CHECK_EQ(g_set.dev[0].active_high, 0);
    CHECK_EQ(g_set.dev[0].initial_on, 1);
}

TEST(cfg_missing_type_and_gpio)
{
    CHECK_EQ(parse("[device A]\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_TYPE);
    CHECK_EQ(parse("[device A]\ntype = door\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_GPIO);
}

TEST(cfg_sensor_rules)
{
    CHECK_EQ(parse("[device S]\ntype = sensor\ngpio = 4\npulse_ms = 100\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_PULSE);
    CHECK_EQ(parse("[device S]\ntype = sensor\ngpio = 4\ninitial = on\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_set.dev[0].initial_on, 0);
}

TEST(cfg_names)
{
    CHECK_EQ(parse("[device ABCDEFGH]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(parse("[device abc_9]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_STR(g_set.dev[0].name, "ABC_9");
    CHECK_EQ(parse("[device ABCDEFGHI]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NAME);
    CHECK_STR(g_res.errs[0].name, "ABCDEFGH");
    CHECK_EQ(parse("[device A-B]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NAME);
    CHECK_EQ(parse("[device A.B]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NAME);
    CHECK_EQ(parse("[device A B]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NAME);
}

TEST(cfg_duplicates)
{
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 4\n"
                   "[device a]\ntype = light\ngpio = 25\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_DUPNAME);
    CHECK_EQ(g_res.errs[0].line, 4);
    CHECK_STR(g_res.errs[0].name, "a");
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 4\n"
                   "[device B]\ntype = light\ngpio = 4\n"
                   "[device C]\ntype = light\ngpio = 25\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_PINSHARED);
    CHECK_STR(g_res.errs[0].name, "B");
    CHECK_STR(g_set.dev[1].name, "C");
}

TEST(cfg_refused_pins_each)
{
    static const unsigned refused[] = {
        0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 14, 15, 16, 17, 18,
        22, 23, 24, 26, 27, 28, 34, 39, 40, 41, 45, 48, 53,
    };
    static const unsigned allowed[] = { 4, 12, 13, 19, 20, 21, 25 };
    char buf[128];
    unsigned i;
    unsigned free_count = 0;

    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        CHECK(dv_pin_refused(refused[i]));
        snprintf(buf, sizeof(buf), "[device P]\ntype = relay\ngpio = %u\n", refused[i]);
        CHECK_EQ(parse(buf), DV_OK);
        CHECK_EQ(g_res.loaded, 0);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_PINUSED);
    }
    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        CHECK(!dv_pin_refused(allowed[i]));
        snprintf(buf, sizeof(buf), "[device P]\ntype = relay\ngpio = %u\n", allowed[i]);
        CHECK_EQ(parse(buf), DV_OK);
        CHECK_EQ(g_res.loaded, 1);
    }
    for (i = 0; i < 64; i++) {
        free_count += !dv_pin_refused(i);
    }
    CHECK_EQ(free_count, 7);
}

TEST(cfg_one_device_over_the_limit)
{
    static char buf[4096];
    static const unsigned pins[] = { 4, 12, 13, 19, 20, 21, 25 };
    size_t n = 0;
    unsigned i;

    CHECK_EQ(DV_MAX_DEVICES, 24);
    /* Only 7 pins are free: 7 load, sections 8 to 24 share a pin and are
     * skipped, section 25 is ignored without checks. */
    for (i = 0; i < DV_MAX_DEVICES + 1; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n,
                              "[device D%u]\ntype = relay\ngpio = %u\n", i, pins[i % 7]);
    }
    CHECK_EQ(parse(buf), DV_OK);
    CHECK_EQ(g_res.loaded, 7);
    CHECK_EQ(g_res.skipped, DV_MAX_DEVICES - 7);
    CHECK_EQ(g_res.ignored, 1);
    CHECK_EQ(g_res.err_count, DV_MAX_DEVICES - 7);
    CHECK_EQ(g_res.errs[DV_MAX_ERRORS - 1].code, DV_ERR_PINSHARED);
}

TEST(cfg_last_checked_next_ignored)
{
    static char buf[4096];
    size_t n = 0;
    unsigned i;

    /* 23 bad sections, a good 24th, a good 25th with another pin. */
    for (i = 0; i < DV_MAX_DEVICES - 1; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "[device B%u]\ntype = x\n", i);
    }
    n += (size_t)snprintf(buf + n, sizeof(buf) - n,
                          "[device G24]\ntype = door\ngpio = 12\n"
                          "[device G25]\ntype = door\ngpio = 25\nbogus = 1\n");
    CHECK_EQ(parse(buf), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_STR(g_set.dev[0].name, "G24");
    CHECK_EQ(g_res.skipped, DV_MAX_DEVICES - 1);
    CHECK_EQ(g_res.ignored, 1);
    CHECK_EQ(g_res.unknown, 0);
    CHECK_EQ(dv_find(&g_set, "G25"), DV_ERR_NOSUCHDEV);
}

TEST(cfg_refused_mask_is_a_parameter)
{
    static const unsigned dial[] = { 16, 23, 24 };
    static const unsigned buttons[] = { 5, 6, 17, 22, 26, 27 };
    char buf[128];
    unsigned i;
    unsigned free_uos = 0;

    for (i = 0; i < 3; i++) {
        CHECK(dv_pin_refused(dial[i]));
        CHECK(!dv_pin_refused_in(DV_REFUSED_UOS, dial[i]));
        snprintf(buf, sizeof(buf), "[device P]\ntype = relay\ngpio = %u\n", dial[i]);
        CHECK_EQ(dv_cfg_parse(buf, strlen(buf), &g_set, &g_res), DV_OK);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_PINUSED);
        CHECK_EQ(dv_cfg_parse_pins(buf, strlen(buf), DV_REFUSED_UOS, &g_set, &g_res), DV_OK);
        CHECK_EQ(g_res.loaded, 1);
        CHECK_EQ(g_set.dev[0].pin, dial[i]);
    }
    for (i = 0; i < 6; i++) {
        CHECK(dv_pin_refused_in(DV_REFUSED_UOS, buttons[i]));
        snprintf(buf, sizeof(buf), "[device P]\ntype = relay\ngpio = %u\n", buttons[i]);
        CHECK_EQ(dv_cfg_parse_pins(buf, strlen(buf), DV_REFUSED_UOS, &g_set, &g_res), DV_OK);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_PINUSED);
    }
    /* An empty mask still refuses pins from DV_PIN_LIMIT up. */
    CHECK(!dv_pin_refused_in(0, 0));
    CHECK(dv_pin_refused_in(0, DV_PIN_LIMIT));
    CHECK(dv_pin_refused_in(0xFFFFFFFFu, 4));
    CHECK_EQ(DV_REFUSED_DEFAULT, DV_REFUSED_UOS | DV_PINS_DIAL);
    for (i = 0; i < 64; i++) {
        free_uos += !dv_pin_refused_in(DV_REFUSED_UOS, i);
    }
    CHECK_EQ(free_uos, 10);
}

TEST(cfg_warns_active_high_output_on_reset_pullup)
{
    char buf[128];
    unsigned pin;

    /* Pins 0 to 8 are pulled up at reset; 4 is the only one free. */
    CHECK_EQ(parse("[device D1]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.warn_count, 1);
    CHECK_EQ(g_res.warns[0].code, DV_WARN_PULLUP);
    CHECK_EQ(g_res.warns[0].line, 1);
    CHECK_STR(g_res.warns[0].name, "D1");
    CHECK_EQ(g_res.err_count, 0);
    /* No warning: active low, a sensor, or a pin pulled down at reset. */
    CHECK_EQ(parse("[device D1]\ntype = door\ngpio = 4\nactive = low\n"), DV_OK);
    CHECK_EQ(g_res.warn_count, 0);
    CHECK_EQ(parse("[device S1]\ntype = sensor\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.warn_count, 0);
    for (pin = 9; pin < DV_PIN_LIMIT; pin++) {
        if (dv_pin_refused_in(0, pin)) {
            continue;
        }
        snprintf(buf, sizeof(buf), "[device D1]\ntype = door\ngpio = %u\n", pin);
        CHECK_EQ(dv_cfg_parse_pins(buf, strlen(buf), 0, &g_set, &g_res), DV_OK);
        CHECK_EQ(g_res.warn_count, 0);
    }
    for (pin = 0; pin <= 8; pin++) {
        snprintf(buf, sizeof(buf), "[device D1]\ntype = relay\ngpio = %u\n", pin);
        CHECK_EQ(dv_cfg_parse_pins(buf, strlen(buf), 0, &g_set, &g_res), DV_OK);
        CHECK_EQ(g_res.warn_count, 1);
    }
    CHECK_STR(dv_strerror(DV_WARN_PULLUP),
              "%DEVICE-W-PULLUP, active-high output on a pin pulled up at reset");
}

TEST(cfg_warnings_counted_past_the_details)
{
    static char buf[4096];
    size_t n = 0;
    unsigned i;

    /* Ten warned outputs with an empty mask: pins 0 to 8, then 0 again. */
    for (i = 0; i < 10; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n,
                              "[device W%u]\ntype = relay\ngpio = %u\n", i, i < 9 ? i : 20);
    }
    n += (size_t)snprintf(buf + n, sizeof(buf) - n, "[device W10]\ntype = relay\ngpio = 21\n");
    CHECK_EQ(dv_cfg_parse_pins(buf, n, 0, &g_set, &g_res), DV_OK);
    CHECK_EQ(g_res.loaded, 11);
    CHECK_EQ(g_res.warn_count, 9);
    CHECK_STR(g_res.warns[DV_MAX_ERRORS - 1].name, "W7");
}

TEST(cfg_unknown_keys)
{
    /* Keys outside a device section are counted and ignored. */
    CHECK_EQ(parse("top = 1\n[general]\nx = 2\n[device A]\ntype = door\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.unknown, 2);
    CHECK_EQ(g_res.err_count, 0);
    CHECK_EQ(g_set.dev[0].pin, 4);
    CHECK_EQ(parse("[device]\ntype = door\n[devices A]\ngpio = 4\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.unknown, 2);
    /* Inside one, an unknown key refuses the section, and is still counted. */
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 4\ncolour = red\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.skipped, 1);
    CHECK_EQ(g_res.unknown, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_KEY);
    CHECK_EQ(g_res.errs[0].line, 4);
}

TEST(cfg_bad_key_refuses_device)
{
    static const char *const text[] = {
        "[device LIGHT1]\ntype = light\ngpio = 4\nactve = low\n"
        "[device DOOR1]\ntype = door\ngpio = 25\n",
        "[device LIGHT1]\ntype = light\ngpio = 4\nActive = low\n"
        "[device DOOR1]\ntype = door\ngpio = 25\n",
    };
    unsigned i;

    for (i = 0; i < 2; i++) {
        CHECK_EQ(parse(text[i]), DV_OK);
        CHECK_EQ(g_res.loaded, 1);
        CHECK_EQ(g_res.skipped, 1);
        CHECK_EQ(g_res.unknown, 1);
        CHECK_EQ(g_res.err_count, 1);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_KEY);
        CHECK_EQ(g_res.errs[0].line, 4);
        CHECK_STR(g_res.errs[0].name, "LIGHT1");
        CHECK_STR(g_set.dev[0].name, "DOOR1");
    }
    CHECK_STR(dv_strerror(DV_ERR_KEY), "%DEVICE-E-BADKEY, unknown key in the section");
}

TEST(cfg_dup_key_refuses_device)
{
    CHECK_EQ(parse("[device LIGHT1]\ntype = light\ngpio = 4\nactive = low\nactive = high\n"
                   "[device DOOR1]\ntype = door\ngpio = 25\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.skipped, 1);
    CHECK_EQ(g_res.unknown, 0);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_DUPKEY);
    CHECK_EQ(g_res.errs[0].line, 5);
    CHECK_STR(g_res.errs[0].name, "LIGHT1");
    CHECK_STR(g_set.dev[0].name, "DOOR1");
    CHECK_STR(dv_strerror(DV_ERR_DUPKEY), "%DEVICE-E-DUPKEY, key given twice in the section");
}

TEST(cfg_bad_key_refuses_expander)
{
    CHECK_EQ(parse("[device ARM]\ntype = relay\ngpio = 4\n"
                   "[expander 0]\naddr = 0x20\nspeed = 100\n"
                   "[device LAMP]\ntype = light\ngpio = X0.3\n"
                   "[device DOOR1]\ntype = door\ngpio = 25\n"), DV_OK);
    CHECK_EQ(g_set.exp[0].declared, 0);
    CHECK_EQ(g_res.loaded, 2);
    CHECK_EQ(g_res.unknown, 1);
    CHECK_EQ(g_res.err_count, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_KEY);
    CHECK_EQ(g_res.errs[0].line, 6);
    CHECK_STR(g_res.errs[0].name, "X0");
    CHECK_EQ(g_res.errs[1].code, DV_ERR_NOCHIP);
    CHECK_STR(g_res.errs[1].name, "LAMP");
    CHECK_STR(g_set.dev[0].name, "ARM");
    CHECK_STR(g_set.dev[1].name, "DOOR1");
}

TEST(cfg_empty_and_comments)
{
    CHECK_EQ(dv_cfg_parse("", 0, &g_set, &g_res), DV_OK);
    CHECK_EQ(g_set.count, 0);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(dv_cfg_parse(NULL, 0, &g_set, &g_res), DV_OK);
    CHECK_EQ(parse("; nothing\n# here\n\n"), DV_OK);
    CHECK_EQ(g_set.count, 0);
    CHECK_EQ(g_res.err_count, 0);
}

TEST(cfg_crlf)
{
    CHECK_EQ(parse("[device A]\r\ntype = lock\r\ngpio = 4\r\nlabel = Back door\r\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_STR(g_set.dev[0].label, "Back door");
    CHECK_EQ(g_set.dev[0].type, DV_TYPE_LOCK);
}

TEST(cfg_syntax_error_keeps_finished)
{
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 4\n[device B]\ntype = light\n"
                   "this is junk\ngpio = 25\n"), DV_ERR_SYNTAX);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.parse_line, 6);
    CHECK(g_res.parse_rc < 0);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_SYNTAX);
    CHECK_EQ(g_set.count, 1);
}

TEST(cfg_args)
{
    CHECK_EQ(dv_cfg_parse(NULL, 3, &g_set, &g_res), DV_ERR_ARG);
    CHECK_EQ(dv_cfg_parse("", 0, NULL, &g_res), DV_ERR_ARG);
    CHECK_EQ(dv_cfg_parse("", 0, &g_set, NULL), DV_ERR_ARG);
}

TEST(cfg_error_detail_cap)
{
    static char buf[4096];
    size_t n = 0;
    unsigned i;

    for (i = 0; i < 10; i++) {
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "[device E%u]\ntype = x\n", i);
    }
    CHECK_EQ(parse(buf), DV_OK);
    CHECK_EQ(g_res.err_count, 10);
    CHECK_EQ(g_res.skipped, 10);
    CHECK_STR(g_res.errs[7].name, "E7");
}

TEST(cfg_summary)
{
    char buf[128];
    char small[10];

    CHECK_EQ(parse(four), DV_OK);
    CHECK(dv_cfg_summary(&g_res, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "loaded=4 skipped=0 unknown=0 ignored=0 errors=0 parse=0 warnings=1");
    CHECK_EQ(dv_cfg_summary(&g_res, small, sizeof(small)), DV_ERR_SPACE);
    CHECK_STR(small, "");
    CHECK_EQ(dv_cfg_summary(NULL, buf, sizeof(buf)), DV_ERR_ARG);
}

TEST(cfg_err_tail)
{
    char buf[64];
    char want[64];
    char small[8];

    CHECK(dv_cfg_err_tail(1, 1, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "cannot be read");
    CHECK(dv_cfg_err_tail(1, 0, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "has 1 error: the serial line names it");
    CHECK(dv_cfg_err_tail(2, 0, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "has 2 errors: the serial line names each");
    /* K, the last count the markers name in full. */
    CHECK(dv_cfg_err_tail(DV_MAX_ERRORS, 0, buf, sizeof(buf)) > 0);
    snprintf(want, sizeof(want), "has %u errors: the serial line names each",
             (unsigned)DV_MAX_ERRORS);
    CHECK_STR(buf, want);
    /* K + 1, the first count the markers cut. */
    CHECK(dv_cfg_err_tail(DV_MAX_ERRORS + 1, 0, buf, sizeof(buf)) > 0);
    snprintf(want, sizeof(want), "has %u errors: the serial line names the first %u",
             (unsigned)DV_MAX_ERRORS + 1, (unsigned)DV_MAX_ERRORS);
    CHECK_STR(buf, want);
    CHECK_EQ(dv_cfg_err_tail(2, 0, small, sizeof(small)), DV_ERR_SPACE);
    CHECK_STR(small, "");
    CHECK_EQ(dv_cfg_err_tail(2, 0, NULL, 4), DV_ERR_ARG);
}

/* ---- expander pins ---- */

static const char *with_expanders =
    "[expander 0]\n"
    "addr = 0x20\n"
    "[expander 3]\n"
    "addr = 0x3F\n"
    "[device LAMP]\n"
    "type = light\n"
    "gpio = X0.0\n"
    "[device GATE]\n"
    "type = door\n"
    "gpio = x3.7\n"
    "pulse_ms = 300\n"
    "[device PUMP]\n"
    "type = relay\n"
    "gpio = 12\n"
    "[device ARM]\n"
    "type = relay\n"
    "gpio = 13\n";

TEST(cfg_expander_pins_load)
{
    CHECK_EQ(parse(with_expanders), DV_OK);
    CHECK_EQ(g_res.loaded, 4);
    CHECK_EQ(g_res.err_count, 0);
    CHECK_EQ(g_res.unknown, 0);
    CHECK_EQ(g_set.exp[0].declared, 1);
    CHECK_EQ(g_set.exp[0].addr, 0x20);
    CHECK_EQ(g_set.exp[1].declared, 0);
    CHECK_EQ(g_set.exp[3].addr, 0x3F);
    CHECK_EQ(g_set.dev[0].chip, 0);
    CHECK_EQ(g_set.dev[0].pin, 0);
    CHECK_EQ(g_set.dev[1].chip, 3);
    CHECK_EQ(g_set.dev[1].pin, 7);
    CHECK_EQ(g_set.dev[2].chip, DV_CHIP_PI);
    CHECK_EQ(g_set.dev[2].pin, 12);
}

TEST(cfg_expander_declared_after_its_devices)
{
    CHECK_EQ(parse("[device LAMP]\ntype = light\ngpio = X1.4\n"
                   "[expander 1]\naddr = 0x27\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_set.dev[0].chip, 1);
    CHECK_EQ(g_set.dev[0].pin, 4);
}

TEST(cfg_expander_pin_needs_its_chip)
{
    CHECK_EQ(parse("[device A]\ntype = relay\ngpio = 12\n"
                   "[device LAMP]\ntype = light\ngpio = X2.1\n"
                   "[device B]\ntype = relay\ngpio = 13\n"
                   "[expander 0]\naddr = 0x20\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 2);
    CHECK_EQ(g_res.skipped, 1);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NOCHIP);
    CHECK_EQ(g_res.errs[0].line, 4);
    CHECK_STR(g_res.errs[0].name, "LAMP");
    CHECK_STR(g_set.dev[0].name, "A");
    CHECK_STR(g_set.dev[1].name, "B");
    CHECK_EQ(g_set.count, 2);
    /* A chip whose section is bad is not declared either. */
    CHECK_EQ(parse("[expander 0]\naddr = 0x30\n[device LAMP]\ntype = light\ngpio = X0.1\n"),
             DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.err_count, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPADDR);
    CHECK_STR(g_res.errs[0].name, "X0");
    CHECK_EQ(g_res.errs[1].code, DV_ERR_NOCHIP);
}

TEST(cfg_expander_addresses)
{
    static const unsigned good[] = { 0x20, 0x23, 0x27, 0x38, 0x3C, 0x3F };
    static const char *const bad[] = {
        "0x1F", "0x28", "0x37", "0x40", "0", "32x", "0x", "0x020", "",
    };
    char buf[128];
    unsigned i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        snprintf(buf, sizeof(buf), "[expander 2]\naddr = 0x%X\n", good[i]);
        CHECK_EQ(parse(buf), DV_OK);
        CHECK_EQ(g_res.err_count, 0);
        CHECK_EQ(g_set.exp[2].declared, 1);
        CHECK_EQ(g_set.exp[2].addr, good[i]);
    }
    /* Decimal works too: 32 is 0x20. */
    CHECK_EQ(parse("[expander 2]\naddr = 32\n"), DV_OK);
    CHECK_EQ(g_set.exp[2].addr, 0x20);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(buf, sizeof(buf), "[expander 2]\naddr = %s\n", bad[i]);
        CHECK_EQ(parse(buf), DV_OK);
        CHECK_EQ(g_set.exp[2].declared, 0);
        CHECK_EQ(g_res.err_count, 1);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPADDR);
    }
    /* An unknown key, and a second addr key. */
    CHECK_EQ(parse("[expander 1]\naddr = 0x20\nother = 1\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_KEY);
    CHECK_EQ(g_res.unknown, 1);
    CHECK_EQ(g_set.exp[1].declared, 0);
    CHECK_EQ(parse("[expander 1]\naddr = 0x20\naddr = 0x21\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_DUPKEY);
    CHECK_EQ(g_res.errs[0].line, 3);
    CHECK_EQ(g_set.exp[1].declared, 0);
}

TEST(cfg_expander_numbers_and_duplicates)
{
    static const char *const bad[] = { "4", "9", "-1", "01", "A", "0 0" };
    char buf[128];
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(buf, sizeof(buf), "[expander %s]\naddr = 0x20\n", bad[i]);
        CHECK_EQ(parse(buf), DV_OK);
        CHECK_EQ(g_res.err_count, 1);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPANDER);
        CHECK_EQ(g_res.errs[0].line, 1);
    }
    /* The same chip twice: the second one is refused. */
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n[x]\ny = 1\n[expander 0]\naddr = 0x21\n"), DV_OK);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPANDER);
    CHECK_EQ(g_set.exp[0].addr, 0x20);
    /* Two chips on one address: the second one is refused. */
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n[expander 1]\naddr = 0x20\n"), DV_OK);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPADDR);
    CHECK_STR(g_res.errs[0].name, "X1");
    CHECK_EQ(g_set.exp[0].declared, 1);
    CHECK_EQ(g_set.exp[1].declared, 0);
}

/* ini_parse_sections() shows headers: an empty section is a named error. */
TEST(cfg_empty_sections_counted)
{
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 12\n"
                   "[device EMPTY]\n"
                   "[expander 1]\n"
                   "; a comment is not a key\n"
                   "[device B]\ntype = light\ngpio = 13\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 2);
    CHECK_EQ(g_res.skipped, 1);
    CHECK_EQ(g_res.err_count, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EMPTY);
    CHECK_EQ(g_res.errs[0].line, 4);
    CHECK_STR(g_res.errs[0].name, "EMPTY");
    CHECK_EQ(g_res.errs[1].code, DV_ERR_EMPTY);
    CHECK_EQ(g_res.errs[1].line, 5);
    CHECK_STR(g_res.errs[1].name, "X1");
    CHECK_EQ(g_set.exp[1].declared, 0);
    /* The last section of the file, empty, counts too. */
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 12\n[device Z]\n"), DV_OK);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EMPTY);
    CHECK_STR(g_res.errs[0].name, "Z");
    /* An empty section of another kind is not a device: nothing counts. */
    CHECK_EQ(parse("[notes]\n[device A]\ntype = door\ngpio = 12\n"), DV_OK);
    CHECK_EQ(g_res.err_count, 0);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_STR(dv_strerror(DV_ERR_EMPTY), "%DEVICE-E-EMPTY, section has no keys");
}

/* A header repeated right after itself is a new section, not a merge. */
TEST(cfg_repeated_sections_counted)
{
    CHECK_EQ(parse("[device A]\ntype = door\ngpio = 12\n"
                   "[device A]\ntype = light\ngpio = 13\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_DUPNAME);
    CHECK_EQ(g_res.errs[0].line, 4);
    CHECK_STR(g_res.errs[0].name, "A");
    CHECK_EQ(g_set.dev[0].pin, 12);
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n[expander 0]\naddr = 0x21\n"), DV_OK);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPANDER);
    CHECK_EQ(g_res.errs[0].line, 3);
    CHECK_EQ(g_set.exp[0].addr, 0x20);
}

/* An empty or bad first section still takes its header: the repeat is
 * refused, the first section wins even when it loaded nothing. */
TEST(cfg_repeat_after_empty_or_bad_section)
{
    CHECK_EQ(parse("[device A]\n[device A]\ntype = door\ngpio = 12\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.skipped, 2);
    CHECK_EQ(g_res.err_count, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EMPTY);
    CHECK_EQ(g_res.errs[1].code, DV_ERR_DUPNAME);
    CHECK_EQ(g_res.errs[1].line, 2);
    CHECK_EQ(parse("[device a]\ntype = bogus\ngpio = 12\n[device A]\ntype = door\ngpio = 13\n"),
             DV_OK);
    CHECK_EQ(g_res.loaded, 0);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_TYPE);
    CHECK_EQ(g_res.errs[1].code, DV_ERR_DUPNAME);
    CHECK_EQ(g_res.errs[1].line, 4);
    CHECK_EQ(parse("[expander 0]\n[expander 0]\naddr = 0x20\n"), DV_OK);
    CHECK_EQ(g_set.exp[0].declared, 0);
    CHECK_EQ(g_res.err_count, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EMPTY);
    CHECK_EQ(g_res.errs[1].code, DV_ERR_EXPANDER);
    CHECK_EQ(g_res.errs[1].line, 2);
    CHECK_EQ(parse("[expander 1]\naddr = 0x99\n[expander 1]\naddr = 0x21\n"), DV_OK);
    CHECK_EQ(g_set.exp[1].declared, 0);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_EXPADDR);
    CHECK_EQ(g_res.errs[1].code, DV_ERR_EXPANDER);
}

/* ---- the ARM rule ---- */

static const char *exp_no_arm =
    "[expander 0]\naddr = 0x20\n"
    "[device LAMP]\ntype = light\ngpio = X0.3\ninitial = on\n"   /* 3 */
    "[device PIR]\ntype = sensor\ngpio = X0.4\n"
    "[device PUMP]\ntype = relay\ngpio = 12\n";

TEST(arm_rule_missing_refuses_expander_outputs)
{
    dv_op_t ops[4];
    char buf[DV_STATUS_LINE_MAX];

    CHECK_EQ(parse(exp_no_arm), DV_OK);
    CHECK_EQ(dv_arm_ok(&g_set), 0);
    CHECK_EQ(g_res.loaded, 3);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NOARM);
    CHECK_EQ(g_res.errs[0].line, 3);
    CHECK_STR(g_res.errs[0].name, "LAMP");
    CHECK_EQ(g_set.dev[0].refused, DV_ERR_NOARM);
    CHECK_EQ(g_set.dev[1].refused, 0);      /* an expander input still works */
    CHECK_EQ(g_set.dev[2].refused, 0);      /* a Pi pin too */
    /* Never configured: no boot operation, state off. */
    CHECK_EQ(dv_boot_ops(&g_set, ops, 4), 2);
    CHECK_EQ(ops[0].dev, 1);
    CHECK_EQ(ops[1].dev, 2);
    CHECK_EQ(g_set.st[0].on, 0);
    /* Every command fails with the reason, before any other check. */
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ON, DV_PRIV_SYSTEM, 0, ops, 4), DV_ERR_NOARM);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_NONE, 0, 0, ops, 4), DV_ERR_NOARM);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_ON, DV_PRIV_SYSTEM, 0, ops, 4), 1);
    CHECK(dv_status_line(&g_set, 0, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "LAMP     LIGHT                                  OFF      X0.3 REFUSED=NOARM");
    CHECK_STR(dv_strerror(DV_ERR_NOARM),
              "%DEVICE-E-NOARM, expander output needs an ARM relay on a Pi pin off at reset");
}

TEST(arm_rule_arm_on_expander_refused)
{
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n"
                   "[device ARM]\ntype = relay\ngpio = X0.1\n"
                   "[device LAMP]\ntype = light\ngpio = X0.2\n"), DV_OK);
    CHECK_EQ(dv_arm_ok(&g_set), 0);
    CHECK_EQ(g_res.err_count, 2);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_NOARM);
    CHECK_STR(g_res.errs[0].name, "ARM");
    CHECK_EQ(g_set.dev[0].refused, DV_ERR_NOARM);
    CHECK_EQ(g_set.dev[1].refused, DV_ERR_NOARM);
}

TEST(arm_rule_arm_on_pi_pin)
{
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n"
                   "[device LAMP]\ntype = light\ngpio = X0.2\n"
                   "[device ARM]\ntype = relay\ngpio = 25\n"), DV_OK);
    CHECK_EQ(dv_arm_ok(&g_set), 1);
    CHECK_EQ(g_res.err_count, 0);
    CHECK_EQ(g_set.dev[0].refused, 0);
    /* No expander output: nothing to refuse, whatever ARM is. */
    CHECK_EQ(parse("[device PUMP]\ntype = relay\ngpio = 12\n"), DV_OK);
    CHECK_EQ(g_res.err_count, 0);
    /* An ARM that is not a relay is not ARM. */
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n"
                   "[device LAMP]\ntype = light\ngpio = X0.2\n"
                   "[device ARM]\ntype = light\ngpio = 25\n"), DV_OK);
    CHECK_EQ(g_set.dev[0].refused, DV_ERR_NOARM);
}

/* ARM must be off from the Pi reset until boot: its reset pull is not its
 * active level. Pins 0 to 8 pull up, 9 to 27 pull down. */
TEST(arm_rule_arm_off_at_reset)
{
    static const struct { const char *pin; const char *active; int ok; } c[] = {
        { "25", "high", 1 }, { "25", "low", 0 }, { "4", "high", 0 }, { "4", "low", 1 },
    };
    char buf[256];
    unsigned i;

    for (i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
        snprintf(buf, sizeof(buf), "[expander 0]\naddr = 0x20\n"
                 "[device LAMP]\ntype = light\ngpio = X0.2\n"
                 "[device ARM]\ntype = relay\ngpio = %s\nactive = %s\n", c[i].pin, c[i].active);
        CHECK_EQ(dv_cfg_parse_pins(buf, strlen(buf), DV_REFUSED_UOS, &g_set, &g_res), DV_OK);
        CHECK_EQ(g_set.count, 2);
        CHECK_EQ(dv_on_at_reset(&g_set.dev[1]), !c[i].ok);
        CHECK_EQ(dv_arm_ok(&g_set), c[i].ok);
        CHECK_EQ(g_set.dev[0].refused, c[i].ok ? 0 : DV_ERR_NOARM);
        CHECK_EQ(g_set.dev[1].refused, 0);
    }
}

TEST(cfg_expander_pin_syntax)
{
    static const char *const bad[] = {
        "X4.0", "X0.8", "X0", "X0.", "X.1", "X00.1", "X0.10", "X0-1", "XA.1", "Y0.1", "X0.1x",
    };
    char buf[160];
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(buf, sizeof(buf), "[expander 0]\naddr = 0x20\n[device D]\ntype = relay\ngpio = %s\n",
                 bad[i]);
        CHECK_EQ(parse(buf), DV_OK);
        CHECK_EQ(g_res.loaded, 0);
        CHECK_EQ(g_res.errs[0].code, DV_ERR_GPIO);
    }
}

TEST(cfg_expander_pin_shared)
{
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n[expander 1]\naddr = 0x21\n"
                   "[device A]\ntype = relay\ngpio = X0.4\n"
                   "[device B]\ntype = relay\ngpio = X0.4\n"
                   "[device C]\ntype = relay\ngpio = X1.4\n"
                   "[device D]\ntype = relay\ngpio = 4\n"
                   "[device ARM]\ntype = relay\ngpio = 13\n"), DV_OK);
    /* Same chip and pin: refused. Same pin on another chip or the Pi: fine. */
    CHECK_EQ(g_res.loaded, 4);
    CHECK_EQ(g_res.err_count, 1);
    CHECK_EQ(g_res.errs[0].code, DV_ERR_PINSHARED);
    CHECK_STR(g_res.errs[0].name, "B");
}

TEST(cfg_expander_no_pullup_warning)
{
    CHECK_EQ(parse("[expander 0]\naddr = 0x20\n[device A]\ntype = relay\ngpio = X0.4\n"), DV_OK);
    CHECK_EQ(g_res.loaded, 1);
    CHECK_EQ(g_res.warn_count, 0);
}

TEST(expander_ops_name_the_chip)
{
    dv_op_t ops[4];
    char buf[DV_STATUS_LINE_MAX];

    CHECK_EQ(parse(with_expanders), DV_OK);
    CHECK_EQ(dv_boot_ops(&g_set, ops, 4), 4);
    CHECK_EQ(ops[0].chip, 0);
    CHECK_EQ(ops[0].pin, 0);
    CHECK_EQ(ops[1].chip, 3);
    CHECK_EQ(ops[1].pin, 7);
    CHECK_EQ(ops[2].chip, DV_CHIP_PI);
    CHECK_EQ(ops[2].pin, 12);
    CHECK_EQ(dv_command(&g_set, 1, DV_ACT_OPEN, DV_PRIV_OPER, 1000, ops, 4), 2);
    CHECK_EQ(ops[0].chip, 3);
    CHECK_EQ(ops[0].pin, 7);
    CHECK_EQ(ops[1].chip, 3);
    CHECK_EQ(ops[1].after_ms, 300);
    CHECK_EQ(dv_tick(&g_set, 1300, ops, 4), 1);
    CHECK_EQ(ops[0].chip, 3);
    CHECK_EQ(ops[0].pin, 7);
    CHECK(dv_status_line(&g_set, 1, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "GATE     DOOR                                   CLOSED   X3.7");
    CHECK(dv_status_line(&g_set, 2, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "PUMP     RELAY                                  OFF      GPIO 12");
}

TEST(expander_messages)
{
    CHECK_STR(dv_strerror(DV_ERR_EXPANDER), "%DEVICE-E-BADEXP, expander number must be 0 to 3, once");
    CHECK_STR(dv_strerror(DV_ERR_EXPADDR),
              "%DEVICE-E-BADADDR, expander addr must be 0x20 to 0x27 or 0x38 to 0x3F, once");
    CHECK_STR(dv_strerror(DV_ERR_NOCHIP), "%DEVICE-E-NOCHIP, expander pin on an undeclared chip");
}

/* ---- commands ---- */

static unsigned idx_of(const char *name)
{
    int i = dv_find(&g_set, name);

    CHECK(i >= 0);
    return i < 0 ? 0 : (unsigned)i;
}

TEST(state_words_and_find)
{
    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_find(&g_set, "door1"), 0);
    CHECK_EQ(dv_find(&g_set, "Vault"), 2);
    CHECK_EQ(dv_find(&g_set, "VAUL"), DV_ERR_NOSUCHDEV);
    CHECK_EQ(dv_find(&g_set, "VAULTX"), DV_ERR_NOSUCHDEV);
    CHECK_EQ(dv_find(&g_set, NULL), DV_ERR_ARG);
    CHECK_STR(dv_state_word(&g_set, 0), "CLOSED");
    CHECK_STR(dv_state_word(&g_set, 1), "OFF");
    CHECK_STR(dv_state_word(&g_set, 2), "LOCKED");
    CHECK_STR(dv_state_word(&g_set, 3), "CLEAR");
    CHECK_STR(dv_state_word(&g_set, 9), "");
}

TEST(action_parse)
{
    CHECK_EQ(dv_action_parse("on"), DV_ACT_ON);
    CHECK_EQ(dv_action_parse("Off"), DV_ACT_OFF);
    CHECK_EQ(dv_action_parse("TOGGLE"), DV_ACT_TOGGLE);
    CHECK_EQ(dv_action_parse("pulse"), DV_ACT_PULSE);
    CHECK_EQ(dv_action_parse("open"), DV_ACT_OPEN);
    CHECK_EQ(dv_action_parse("close"), DV_ACT_CLOSE);
    CHECK_EQ(dv_action_parse("lock"), DV_ACT_LOCK);
    CHECK_EQ(dv_action_parse("unlock"), DV_ACT_UNLOCK);
    CHECK_EQ(dv_action_parse("activate"), DV_ACT_ACTIVATE);
    CHECK_EQ(dv_action_parse("standby"), DV_ACT_STANDBY);
    CHECK_EQ(dv_action_parse("ONN"), DV_ACT_NONE);
    CHECK_EQ(dv_action_parse("O"), DV_ACT_NONE);
    CHECK_EQ(dv_action_parse(""), DV_ACT_NONE);
    CHECK_EQ(dv_action_parse(NULL), DV_ACT_NONE);
}

/* Expected result of each action per type: 1 on, 0 off, -1 BADACTION. */
TEST(command_matrix)
{
    static const char *types[] = { "door", "light", "turret", "lock", "relay" };
    static const int expect[5][10] = {
        /* ON OFF TOG PUL OPEN CLOSE UNLOCK LOCK ACT STBY */
        { 1, 0, 1, -2, 1, 0, -1, -1, -1, -1 },   /* door */
        { 1, 0, 1, -2, -1, -1, -1, -1, -1, -1 }, /* light */
        { 1, 0, 1, -2, -1, -1, -1, -1, 1, 0 },   /* turret */
        { 1, 0, 1, -2, -1, -1, 1, 0, -1, -1 },   /* lock */
        { 1, 0, 1, -2, -1, -1, -1, -1, -1, -1 }, /* relay */
    };
    char buf[128];
    dv_op_t ops[2];
    unsigned t;
    unsigned a;

    for (t = 0; t < 5; t++) {
        for (a = 0; a < 10; a++) {
            int rc;

            snprintf(buf, sizeof(buf), "[device X]\ntype = %s\ngpio = 4\n", types[t]);
            CHECK_EQ(parse(buf), DV_OK);
            rc = dv_command(&g_set, 0, (dv_action_t)(DV_ACT_ON + a), DV_PRIV_OPER, 0, ops, 2);
            if (expect[t][a] == -1) {
                CHECK_EQ(rc, DV_ERR_BADACTION);
            } else if (expect[t][a] == -2) {
                CHECK_EQ(rc, DV_ERR_NOPULSE);
            } else {
                CHECK_EQ(rc, 1);
                CHECK_EQ(g_set.st[0].on, expect[t][a]);
                CHECK_EQ(ops[0].kind, DV_OP_WRITE);
                CHECK_EQ(ops[0].pin, 4);
                CHECK_EQ(ops[0].level, expect[t][a]);
                CHECK_EQ(ops[0].after_ms, 0);
            }
        }
    }
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_NONE, DV_PRIV_OPER, 0, ops, 2), DV_ERR_BADACTION);
}

TEST(command_words_per_type)
{
    dv_op_t ops[2];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_OPEN, DV_PRIV_OPER, 0, ops, 2), 1);
    CHECK_STR(dv_state_word(&g_set, 0), "OPEN");
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_CLOSE, DV_PRIV_OPER, 0, ops, 2), 1);
    CHECK_STR(dv_state_word(&g_set, 0), "CLOSED");
    /* light is active low: on drives 0 */
    CHECK_EQ(dv_command(&g_set, 1, DV_ACT_ON, 0, 0, ops, 2), 1);
    CHECK_EQ(ops[0].level, 0);
    CHECK_EQ(ops[0].pin, 25);
    CHECK_STR(dv_state_word(&g_set, 1), "ON");
    CHECK_EQ(dv_command(&g_set, 1, DV_ACT_TOGGLE, 0, 0, ops, 2), 1);
    CHECK_EQ(ops[0].level, 1);
    CHECK_STR(dv_state_word(&g_set, 1), "OFF");
}

TEST(command_turret_words)
{
    dv_op_t ops[2];

    CHECK_EQ(parse("[device T]\ntype = turret\ngpio = 13\n"), DV_OK);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ACTIVATE, DV_PRIV_OPER, 0, ops, 2), 1);
    CHECK_STR(dv_state_word(&g_set, 0), "ACTIVE");
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_STANDBY, DV_PRIV_OPER, 0, ops, 2), 1);
    CHECK_STR(dv_state_word(&g_set, 0), "STANDBY");
    CHECK_EQ(parse("[device R]\ntype = relay\ngpio = 13\n"), DV_OK);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ON, DV_PRIV_OPER, 0, ops, 2), 1);
    CHECK_STR(dv_state_word(&g_set, 0), "ON");
}

TEST(command_sensor_readonly)
{
    dv_op_t ops[2];
    unsigned a;

    CHECK_EQ(parse(four), DV_OK);
    for (a = DV_ACT_NONE; a <= DV_ACT_STANDBY; a++) {
        CHECK_EQ(dv_command(&g_set, 3, (dv_action_t)a, DV_PRIV_SYSTEM, 0, ops, 2), DV_ERR_READONLY);
    }
    CHECK_EQ(dv_sensor_update(&g_set, 3, 0), DV_OK);
    CHECK_STR(dv_state_word(&g_set, 3), "TRIPPED");
    CHECK_EQ(dv_sensor_update(&g_set, 3, 1), DV_OK);
    CHECK_STR(dv_state_word(&g_set, 3), "CLEAR");
    CHECK_EQ(dv_sensor_update(&g_set, 0, 1), DV_ERR_ARG);
    CHECK_EQ(dv_sensor_update(&g_set, 9, 1), DV_ERR_ARG);
}

TEST(command_privileges)
{
    dv_op_t ops[2];
    unsigned vault;

    CHECK_EQ(parse(four), DV_OK);
    vault = idx_of("VAULT");    /* privs 0x6: OPER and SECURITY */
    /* DOOR1: default OPER */
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_OPEN, 0, 0, ops, 2), DV_ERR_NOPRIV);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_OPEN, 0x8, 0, ops, 2), DV_ERR_NOPRIV);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_OPEN, DV_PRIV_OPER, 0, ops, 2), 1);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_OPEN, DV_PRIV_SYSTEM, 0, ops, 2), 1);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_OPEN, 0xE, 0, ops, 2), 1);
    /* VAULT needs both bits */
    CHECK_EQ(dv_command(&g_set, vault, DV_ACT_OFF, DV_PRIV_OPER, 0, ops, 2), DV_ERR_NOPRIV);
    CHECK_EQ(dv_command(&g_set, vault, DV_ACT_OFF, 0x4, 0, ops, 2), DV_ERR_NOPRIV);
    CHECK_EQ(dv_command(&g_set, vault, DV_ACT_OFF, 0x6, 0, ops, 2), 1);
    CHECK_EQ(dv_command(&g_set, vault, DV_ACT_OFF, DV_PRIV_SYSTEM, 0, ops, 2), 1);
    /* LIGHT1: privs 0, anyone */
    CHECK_EQ(dv_command(&g_set, 1, DV_ACT_ON, 0, 0, ops, 2), 1);
    /* bad action is reported before privileges */
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_LOCK, 0, 0, ops, 2), DV_ERR_BADACTION);
    /* refusal leaves state alone */
    CHECK_EQ(g_set.st[vault].on, 0);
}

TEST(command_args_and_space)
{
    dv_op_t ops[2];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_command(&g_set, 4, DV_ACT_ON, DV_PRIV_SYSTEM, 0, ops, 2), DV_ERR_ARG);
    CHECK_EQ(dv_command(NULL, 0, DV_ACT_ON, DV_PRIV_SYSTEM, 0, ops, 2), DV_ERR_ARG);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ON, DV_PRIV_SYSTEM, 0, NULL, 2), DV_ERR_ARG);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ON, DV_PRIV_SYSTEM, 0, ops, 1), DV_ERR_SPACE);
    CHECK_EQ(g_set.st[0].on, 0);
}

/* ---- pulses ---- */

TEST(pulse_start_and_end_exact)
{
    dv_op_t ops[16];
    unsigned v;

    CHECK_EQ(parse(four), DV_OK);
    v = idx_of("VAULT");
    CHECK_EQ(dv_command(&g_set, v, DV_ACT_UNLOCK, DV_PRIV_SYSTEM, 1000, ops, 2), 2);
    CHECK_EQ(ops[0].level, 1);
    CHECK_EQ(ops[0].after_ms, 0);
    CHECK_EQ(ops[1].level, 0);
    CHECK_EQ(ops[1].after_ms, 500);
    CHECK_EQ(ops[1].pin, 20);
    CHECK_STR(dv_state_word(&g_set, v), "UNLOCKED");
    CHECK_EQ(dv_tick(&g_set, 1499, ops, 16), 0);
    CHECK_STR(dv_state_word(&g_set, v), "UNLOCKED");
    CHECK_EQ(dv_tick(&g_set, 1500, ops, 16), 1);
    CHECK_EQ(ops[0].kind, DV_OP_WRITE);
    CHECK_EQ(ops[0].pin, 20);
    CHECK_EQ(ops[0].level, 0);
    CHECK_EQ(ops[0].dev, v);
    CHECK_STR(dv_state_word(&g_set, v), "LOCKED");
    CHECK_EQ(dv_tick(&g_set, 1501, ops, 16), 0);
}

TEST(pulse_late_tick_still_ends)
{
    dv_op_t ops[16];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, DV_PRIV_SYSTEM, 0, ops, 2), 2);
    CHECK_EQ(dv_tick(&g_set, 9000, ops, 16), 1);
}

TEST(pulse_across_wrap)
{
    dv_op_t ops[16];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_ON, DV_PRIV_SYSTEM, 0xFFFFFF00u, ops, 2), 2);
    CHECK_EQ(g_set.st[2].due, 0xFFFFFF00u + 500u);
    CHECK_EQ(dv_tick(&g_set, 0xFFFFFFFFu, ops, 16), 0);
    CHECK_EQ(dv_tick(&g_set, 0, ops, 16), 0);
    CHECK_EQ(dv_tick(&g_set, 499 - 0x100 - 1, ops, 16), 0);
    CHECK_EQ(dv_tick(&g_set, 500 - 0x100, ops, 16), 1);
}

TEST(pulse_cancelled_by_new_command)
{
    dv_op_t ops[16];

    CHECK_EQ(parse(four), DV_OK);
    /* OFF cancels */
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, DV_PRIV_SYSTEM, 100, ops, 2), 2);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_LOCK, DV_PRIV_SYSTEM, 200, ops, 2), 1);
    CHECK_EQ(ops[0].level, 0);
    CHECK_EQ(g_set.st[2].pending, 0);
    CHECK_EQ(dv_tick(&g_set, 600, ops, 16), 0);
    /* PULSE again restarts the timer */
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, DV_PRIV_SYSTEM, 1000, ops, 2), 2);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, DV_PRIV_SYSTEM, 1300, ops, 2), 2);
    CHECK_EQ(dv_tick(&g_set, 1500, ops, 16), 0);
    CHECK_EQ(dv_tick(&g_set, 1800, ops, 16), 1);
    /* TOGGLE during a pulse turns it off */
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, DV_PRIV_SYSTEM, 2000, ops, 2), 2);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_TOGGLE, DV_PRIV_SYSTEM, 2100, ops, 2), 1);
    CHECK_EQ(g_set.st[2].on, 0);
    CHECK_EQ(dv_tick(&g_set, 3000, ops, 16), 0);
    /* a refused command does not cancel */
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, DV_PRIV_SYSTEM, 4000, ops, 2), 2);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_PULSE, 0, 4100, ops, 2), DV_ERR_NOPRIV);
    CHECK_EQ(dv_tick(&g_set, 4500, ops, 16), 1);
}

TEST(pulse_two_devices)
{
    dv_op_t ops[16];

    CHECK_EQ(parse("[device A]\ntype = relay\ngpio = 4\npulse_ms = 100\n"
                   "[device B]\ntype = relay\ngpio = 12\npulse_ms = 10\n"), DV_OK);
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ON, DV_PRIV_OPER, 0, ops, 2), 2);
    CHECK_EQ(dv_command(&g_set, 1, DV_ACT_ON, DV_PRIV_OPER, 90, ops, 2), 2);
    CHECK_EQ(dv_tick(&g_set, 99, ops, 16), 0);
    CHECK_EQ(dv_tick(&g_set, 100, ops, 16), 2);
    CHECK_EQ(ops[0].pin, 4);
    CHECK_EQ(ops[1].pin, 12);
    /* limited output: the rest stays pending */
    CHECK_EQ(dv_command(&g_set, 0, DV_ACT_ON, DV_PRIV_OPER, 200, ops, 2), 2);
    CHECK_EQ(dv_command(&g_set, 1, DV_ACT_ON, DV_PRIV_OPER, 200, ops, 2), 2);
    CHECK_EQ(dv_tick(&g_set, 400, ops, 1), 1);
    CHECK_EQ(ops[0].pin, 4);
    CHECK_EQ(dv_tick(&g_set, 400, ops, 1), 1);
    CHECK_EQ(ops[0].pin, 12);
    CHECK_EQ(dv_tick(&g_set, 400, NULL, 0), 0);
}

TEST(pulse_toggle_from_off_pulses)
{
    dv_op_t ops[2];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_command(&g_set, 2, DV_ACT_TOGGLE, DV_PRIV_SYSTEM, 0, ops, 2), 2);
    CHECK_EQ(g_set.st[2].pending, 1);
}

/* ---- boot ---- */

TEST(boot_ops)
{
    dv_op_t ops[16];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_boot_ops(&g_set, ops, 3), DV_ERR_SPACE);
    CHECK_EQ(dv_boot_ops(&g_set, ops, 16), 4);
    CHECK_EQ(ops[0].kind, DV_OP_CONFIG_OUTPUT);
    CHECK_EQ(ops[0].pin, 4);
    CHECK_EQ(ops[0].level, 0);
    CHECK_EQ(ops[1].kind, DV_OP_CONFIG_OUTPUT);
    CHECK_EQ(ops[1].pin, 25);
    CHECK_EQ(ops[1].level, 0);      /* initial on, active low */
    CHECK_STR(dv_state_word(&g_set, 1), "ON");
    CHECK_EQ(ops[2].kind, DV_OP_CONFIG_OUTPUT);
    CHECK_EQ(ops[2].level, 0);
    CHECK_EQ(ops[3].kind, DV_OP_CONFIG_INPUT);
    CHECK_EQ(ops[3].pin, 21);
    CHECK_EQ(ops[3].pull, DV_PULL_UP);
    CHECK_EQ(parse("[device S]\ntype = sensor\ngpio = 4\n"), DV_OK);
    CHECK_EQ(dv_boot_ops(&g_set, ops, 1), 1);
    CHECK_EQ(ops[0].pull, DV_PULL_DOWN);
    CHECK_EQ(dv_boot_ops(NULL, ops, 1), DV_ERR_ARG);
}

/* ---- status lines ---- */

TEST(status_lines_exact)
{
    char buf[DV_STATUS_LINE_MAX];
    char small[20];
    dv_op_t ops[16];

    CHECK_EQ(parse(four), DV_OK);
    CHECK_EQ(dv_boot_ops(&g_set, ops, 16), 4);
    CHECK_EQ(dv_status_header(buf, sizeof(buf)), 60);
    CHECK_STR(buf, "NAME     TYPE   LABEL                           STATE    PIN");
    CHECK_EQ(dv_status_line(&g_set, 0, buf, sizeof(buf)), 63);
    CHECK_STR(buf, "DOOR1    DOOR   Workshop door                   CLOSED   GPIO 4");
    CHECK_EQ(dv_status_line(&g_set, 1, buf, sizeof(buf)), 64);
    CHECK_STR(buf, "LIGHT1   LIGHT  Hall lights                     ON       GPIO 25");
    CHECK_EQ(dv_status_line(&g_set, 2, buf, sizeof(buf)), 64);
    CHECK_STR(buf, "VAULT    LOCK   Vault strike                    LOCKED   GPIO 20");
    CHECK_EQ(dv_status_line(&g_set, 3, buf, sizeof(buf)), 64);
    CHECK_STR(buf, "PIR_1    SENSOR Corridor motion                 CLEAR    GPIO 21");
    CHECK_EQ(dv_status_line(&g_set, 3, small, sizeof(small)), DV_ERR_SPACE);
    CHECK_STR(small, "");
    CHECK_EQ(dv_status_header(small, sizeof(small)), DV_ERR_SPACE);
    CHECK_EQ(dv_status_line(&g_set, 4, buf, sizeof(buf)), DV_ERR_ARG);
}

TEST(status_line_longest_fits_80)
{
    char buf[DV_STATUS_LINE_MAX];

    CHECK_EQ(parse("[device ABCDEFGH]\ntype = turret\ngpio = 25\n"
                   "label = 1234567890123456789012345678901\n"), DV_OK);
    CHECK_EQ(dv_status_line(&g_set, 0, buf, sizeof(buf)), 64);
    CHECK_STR(buf, "ABCDEFGH TURRET 1234567890123456789012345678901 STANDBY  GPIO 25");
}

TEST(messages_exact)
{
    CHECK_STR(dv_strerror(DV_ERR_NOSUCHDEV), "%DEVICE-W-NOSUCHDEV, no such device");
    CHECK_STR(dv_strerror(DV_ERR_NOPRIV), "%DEVICE-W-NOPRIV, insufficient privilege");
    CHECK_STR(dv_strerror(DV_ERR_READONLY), "%DEVICE-W-READONLY, device is read-only");
    CHECK_STR(dv_strerror(DV_ERR_BADACTION), "%DEVICE-W-BADACTION, action not valid for this device");
    CHECK_STR(dv_strerror(DV_ERR_NOPULSE), "%DEVICE-W-NOPULSE, device has no pulse length");
    CHECK_STR(dv_strerror(DV_ERR_PINUSED), "%DEVICE-E-PINUSED, pin is reserved by the system");
    CHECK_STR(dv_strerror(1), "%DEVICE-F-UNKNOWN, unknown error");
}

TEST(messages_every_code_distinct)
{
    int c;
    int d;

    for (c = DV_OK; c >= DV_ERR_DUPKEY; c--) {
        CHECK(strncmp(dv_strerror(c), "%DEVICE-", 8) == 0);
        CHECK(strcmp(dv_strerror(c), dv_strerror(1)) != 0);
        for (d = c - 1; d >= DV_ERR_DUPKEY; d--) {
            CHECK(strcmp(dv_strerror(c), dv_strerror(d)) != 0);
        }
    }
}

TEST(type_names)
{
    CHECK_STR(dv_type_name(DV_TYPE_DOOR), "door");
    CHECK_STR(dv_type_name(DV_TYPE_SENSOR), "sensor");
    CHECK_STR(dv_type_name(DV_TYPE_COUNT), "");
}

/* ---- record ring ---- */

TEST(rec_ring_wrap)
{
    static dv_rec_t r;
    char buf[64];
    unsigned i;

    dv_rec_init(&r);
    CHECK_EQ(dv_rec_format(&r, 0, buf, sizeof(buf)), DV_ERR_ARG);
    dv_rec_add(&r, 4, 1, 10);
    CHECK_EQ(r.count, 1);
    CHECK(dv_rec_format(&r, 0, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "pin=4 level=1 tick=10");
    for (i = 1; i < DV_REC_SIZE; i++) {
        dv_rec_add(&r, i % 28, (int)(i & 1), 10 + i);
    }
    CHECK_EQ(r.count, DV_REC_SIZE);
    CHECK(dv_rec_format(&r, 0, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "pin=4 level=1 tick=10");
    dv_rec_add(&r, 25, 7, 0xFFFFFFFFu);
    CHECK_EQ(r.count, DV_REC_SIZE);
    CHECK_EQ(r.total, DV_REC_SIZE + 1);
    CHECK(dv_rec_format(&r, 0, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "pin=1 level=1 tick=11");
    CHECK(dv_rec_format(&r, DV_REC_SIZE - 1, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "pin=25 level=1 tick=4294967295");
    CHECK_EQ(dv_rec_format(&r, DV_REC_SIZE, buf, sizeof(buf)), DV_ERR_ARG);
    CHECK_EQ(dv_rec_format(&r, 0, buf, 5), DV_ERR_SPACE);
    CHECK_STR(buf, "");
}

/* ---- the card's DEVICES.CFG ---- */

TEST(card_devices_cfg_loads)
{
    static char buf[4096];
    FILE *f = fopen("card/UOS/DEVICES.CFG", "rb");
    size_t n;

    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK_EQ(dv_cfg_parse(buf, n, &g_set, &g_res), DV_OK);
    CHECK_EQ(g_res.loaded, 4);
    CHECK_EQ(g_res.err_count, 0);
    CHECK_EQ(g_res.warn_count, 0);
    CHECK_EQ(dv_cfg_parse_pins(buf, n, DV_REFUSED_UOS, &g_set, &g_res), DV_OK);
    CHECK_EQ(g_res.loaded, 4);
    CHECK_EQ(g_res.err_count, 0);
    /* No active-high output on a pin pulled up at reset. */
    CHECK_EQ(g_res.warn_count, 0);
    CHECK_EQ(g_res.unknown, 0);
    CHECK_EQ(g_set.dev[2].pulse_ms, 2000);
    CHECK_EQ(g_set.dev[3].type, DV_TYPE_SENSOR);
}
