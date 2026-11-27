/*
 * UOS: the third stage of the terminal chain. MFBootAgent loads it from
 * /ROBCO/UOS.SYS into the OS slot at 0x00400000. It also boots alone.
 *
 * Bring-up runs in a fixed order with one marker per step, see
 * docs/ARCHITECTURE.md: boot, handoff, maint, board, mbox, mmu, screen,
 * tick, heap, input, selfcheck, mount, config, buttons, rtc, one service
 * line per
 * service, then the first screen and ready.
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/alloc.h>
#include <robco/board.h>
#include <robco/boot.h>
#include <robco/bootinfo.h>
#include <robco/console.h>
#include <robco/fb.h>
#include <robco/fs.h>
#include <robco/gpio.h>
#include <robco/i2c.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/mbox.h>
#include <robco/mmu.h>
#include <robco/part.h>
#include <robco/rimg.h>
#include <robco/rtc.h>
#include <robco/sd.h>
#include <robco/tick.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "uos.h"
#include "uos_buttons.h"
#include "uos_service.h"

#define SCREEN_W 640
#define SCREEN_H 480

uos_state_t uos;

static uos_settings_t settings;
static fs_boot_t vol;
static char cfg_text[UOS_CFG_FILE_MAX];

/* The files that mark the UOS boot volume. */
static const char *const vol_markers[] = { UOS_CFG_PATH, "/ROBCO/UOS.SYS" };

static volatile int serial_open;

void uos_serial_open_line(int open)
{
    serial_open = open;
}

void uos_serial_break(void)
{
    if (serial_open) {
        serial_open = 0;
        uart_putc('\n');
    }
}

void uos_mark(const char *fmt, ...)
{
    char buf[UOS_MARK_MAX];
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uos_serial_break();
    k_printf("@RC stage=" ROBCO_STAGE_NAME " ev=%s\n", buf);
}

void uos_say(const char *line)
{
    if (uos.fb_ok) {
        console_puts(line);
        console_putc('\n');
    }
    uos_serial_break();
    uart_puts(line);
    uart_putc('\n');
}

void uos_set_error(const char *what)
{
    k_snprintf(uos.error, sizeof(uos.error), "%s", what);
}

const uos_settings_t *uos_settings(void)
{
    return &settings;
}

/* The system name as one marker word: spaces become underscores. */
static const char *name_word(void)
{
    static char w[UOS_NAME_MAX + 1];
    unsigned i;

    for (i = 0; settings.name[i] != '\0' && i < UOS_NAME_MAX; i++) {
        w[i] = settings.name[i] == ' ' ? '_' : settings.name[i];
    }
    w[i] = '\0';
    return w;
}

static const char *mode_name(uint32_t mode)
{
    switch (mode) {
    case 0x13: return "svc";
    case 0x1a: return "hyp";
    default:   return "other";
    }
}

/* The colour scheme of a trusted boot info, or -1. */
static int inherited_scheme(void)
{
    const robco_bootinfo_t *bi = uos.trusted ? bootinfo_get() : NULL;

    if (bi != NULL && bi->con_scheme < CONSOLE_SCHEME_COUNT) {
        return (int)bi->con_scheme;
    }
    return -1;
}

/* Clears the screen and starts a console of our own. */
static void init_screen(void)
{
    int scheme = inherited_scheme();
    int fb, con = -1;

    fb = fb_init(SCREEN_W, SCREEN_H, 32);
    if (fb == 0) {
        con = console_init(scheme >= 0 ? (unsigned)scheme : CONSOLE_SCHEME_GREEN);
    }
    uos.fb_ok = fb == 0 && con == 0;
    if (uos.fb_ok) {
        console_clear();
        console_set_cursor(0, 0);
    }
    UOS_MARK("screen fb=%d console=%d scheme=%u source=%s", fb, con,
             scheme >= 0 ? (unsigned)scheme : CONSOLE_SCHEME_GREEN,
             scheme >= 0 ? "bootinfo" : "default");
}

static void init_heap(void)
{
    void *base = NULL;
    size_t size = robco_heap_high(&base);

    if (size != 0) {
        k_heap_init(base, size);
    }
    UOS_MARK("heap base=0x%08x size=%u", (unsigned)(uintptr_t)base, (unsigned)size);
}

/* The card: the boot volume by the shared rule, then the /UOS tree. */
static void mount_card(void)
{
    fs_stat_t st;
    int rc = sd_init();

    if (rc != 0) {
        uos.card = UOS_CARD_NONE;
        uos_set_error("nocard");
        UOS_MARK("mount result=none why=sd rc=%d", rc);
        return;
    }
    rc = fs_mount_boot("sd", sd_blkdev(), vol_markers,
                       sizeof(vol_markers) / sizeof(vol_markers[0]), &vol);
    if (rc != PART_BOOT_OK) {
        uos.card = UOS_CARD_NONE;
        uos_set_error("nofs");
        UOS_MARK("mount result=none why=%s rc=%d", part_boot_strerror(rc), rc);
        return;
    }
    if (fs_stat("/UOS", &st) != 0 || !st.is_dir) {
        uos.card = UOS_CARD_NOTREE;
        uos_set_error("notree");
        UOS_MARK("mount result=notree lba=%u", (unsigned)vol.start_lba);
        return;
    }
    uos.card = UOS_CARD_OK;
    UOS_MARK("mount result=ok lba=%u", (unsigned)vol.start_lba);
}

/*
 * The full config marker, the longest one. Its worst case: the format's own
 * text, which counts each conversion as two characters, plus every field at
 * its widest. 15 numbers at 11 characters, the name and the hack user at
 * their limits, and 22 for result, scheme and difficulty. A new field must
 * fit too, or the build fails here.
 */
#define CONFIG_FMT                                                                  \
    "config result=%s line=%u keys=%u bad=%u clamped=%u unknown=%u name=%s "        \
    "scheme=%s retries=%u login_lockout=%u hack=%d user=%s attempts=%u "            \
    "difficulty=%s hack_lockout=%u console=%u buttons=%d sound=%d fan=%d volume=%u"
#define CONFIG_MARK_WORST (sizeof(CONFIG_FMT) + 15 * 11 + UOS_NAME_MAX + UOS_USER_MAX + 22)
_Static_assert(CONFIG_MARK_WORST < UOS_MARK_MAX, "the config marker can be cut");

/* The [audio] fields, last in every config marker. */
#define AUDIO_FIELDS "sound=%d fan=%d volume=%u"
#define AUDIO_ARGS   settings.sound_enabled, settings.fan_enabled, settings.volume

/* UOS.CFG, when the card has one. The scheme applies unless the boot info gave one. */
static void read_config(void)
{
    uos_settings_report_t r;
    size_t len = 0;
    int rc;

    uos_settings_default(&settings);
    if (uos.card != UOS_CARD_OK) {
        UOS_MARK("config result=skipped name=%s buttons=%d " AUDIO_FIELDS, name_word(),
                 settings.buttons_enabled, AUDIO_ARGS);
        return;
    }
    rc = fs_read_file(UOS_CFG_PATH, cfg_text, sizeof(cfg_text), &len);
    if (rc == FS_ERR_NOENT) {
        UOS_MARK("config result=missing name=%s buttons=%d " AUDIO_FIELDS, name_word(),
                 settings.buttons_enabled, AUDIO_ARGS);
        return;
    }
    if (rc != 0) {
        uos.cfg_bad = 1;
        uos_set_error("config");
        UOS_MARK("config result=bad why=read rc=%d buttons=%d " AUDIO_FIELDS, rc,
                 settings.buttons_enabled, AUDIO_ARGS);
        return;
    }
    rc = uos_settings_parse(cfg_text, len, &settings, &r);
    uos.cfg_bad = rc != UOS_CFG_OK || r.bad != 0 || r.clamped != 0 || r.unknown != 0;
    if (uos.cfg_bad) {
        uos_set_error("config");
    }
    if (settings.scheme != UOS_SCHEME_UNSET && inherited_scheme() < 0 && uos.fb_ok) {
        console_set_scheme((unsigned)settings.scheme);
    }
    UOS_MARK(CONFIG_FMT,
             rc == UOS_CFG_OK ? "ok" : "syntax", r.err_line, r.keys, r.bad, r.clamped,
             r.unknown, name_word(), uos_scheme_name(settings.scheme),
             settings.login_retries, settings.login_lockout_s, settings.hack_enabled,
             settings.hack_user, settings.hack_attempts,
             uos_difficulty_name(settings.hack_difficulty), settings.hack_lockout_s,
             uos.fb_ok ? console_scheme() : 0u, settings.buttons_enabled, AUDIO_ARGS);
}

/* The MMU with the library's identity map, and the caches. Before the
 * framebuffer and the IRQs, as memory.md says. */
static void init_mmu(void)
{
    int rc = mmu_init();

    if (rc == 0) {
        mmu_enable();
    }
    UOS_MARK("mmu result=%s rc=%d", rc != 0 ? "err" : mmu_enabled() ? "on" : "off", rc);
}

/*
 * The six panel buttons, unless UOS.CFG switches them off. The library's
 * stuck-line guard samples for 10 ticks; the boot sleeps in wfi until it
 * ends, 30 ticks at most. Repeat is set before the guard, so a press the
 * guard reports at its end repeats, and is turned off again unless active.
 */
static void start_buttons(void)
{
    const char *repeat = "none";
    char rbuf[12];
    int setting = settings.buttons_enabled;
    int rc = 0, rrc = 0, status = INPUT_GPIO_OFF;
    uint32_t t0 = uos_ticks(), waited = 0;
    size_t i;

    if (setting) {
        /* Before the guard: a button held through it repeats from its first press. */
        rrc = input_set_repeat(UOS_BUTTONS_REPEAT_MS, UOS_BUTTONS_RATE_MS);
        for (i = 0; i < UOS_BUTTONS_COUNT && rc == 0; i++) {
            rc = input_gpio_add_button(uos_buttons[i].pin, uos_buttons[i].code, 0, GPIO_PULL_UP);
        }
        if (rc == 0) {
            rc = input_gpio_start(UOS_TICK_HZ, UOS_BUTTONS_DEBOUNCE_MS);
        }
        status = input_gpio_status();
        while (rc == 0 && status == INPUT_GPIO_CHECKING &&
               uos_ticks() - t0 < UOS_BUTTONS_WAIT_TICKS) {
            __asm__ volatile("wfi");
            status = input_gpio_status();
        }
        waited = uos_ticks() - t0;
    }
    uos.buttons = uos_buttons_result(setting, rc, status);
    if (uos.buttons == UOS_BUTTONS_ERROR) {
        uos_set_error("buttons");
    }
    if (uos.buttons == UOS_BUTTONS_ACTIVE) {
        k_snprintf(rbuf, sizeof(rbuf), "%d", rrc);
        repeat = rbuf;
    } else if (setting) {
        (void)input_set_repeat(0, 0);   /* repeat stays off unless active */
    }
    UOS_MARK("buttons result=%s rc=%d setting=%d repeat=%s ticks=%u",
             uos_buttons_result_name(uos.buttons), rc, setting, repeat, (unsigned)waited);
}

static void init_rtc(void)
{
    int rc = i2c_init(100000);

    if (rc == 0) {
        rc = rtc_init();
    }
    uos.rtc = rc == 0 && rtc_present();
    UOS_MARK("rtc present=%d rc=%d", uos.rtc, rc);
}

#if defined(UOS_TEST_EARLY_FAULT) && UOS_TEST_EARLY_FAULT
/* Test builds only: an undefined instruction just after the exception hook. */
__attribute__((noipa)) void uos_test_early_fault(void)
{
    __asm__ volatile(".word 0xe7f000f0");
}
#endif

#if defined(UOS_TEST_TICK_FAIL) && UOS_TEST_TICK_FAIL
/* Test builds only: tick_init() reports a failure. */
__attribute__((noipa)) int uos_test_tick_fail(int rc)
{
    (void)rc;
    return -1;
}
#endif

void robco_main(void)
{
    const robco_board_t *b;
    uint32_t mem_base = 0, mem_size = 0;
    int self, mini, tick, mbox;

    /* First, before any code writes initialised data, which the CRC covers. */
    self = rimg_self_check();

    uos.phase = UOS_PHASE_INIT;
    uart_init(115200);
    k_printf("\n");
    UOS_MARK("boot version=" UOS_VERSION_STR " entry_mode=%s",
             mode_name(robco_boot_args()->entry_mode));

    /* The trust rule of BOOT_PROTOCOL.md, in the library. */
    uos.trusted = bootinfo_inherited();
    {
        const robco_bootinfo_t *bi = uos.trusted ? bootinfo_get() : NULL;

        UOS_MARK("handoff trusted=%d prev=%u flags=0x%08x", uos.trusted,
                 bi != NULL ? (unsigned)bi->stage : 0u, bi != NULL ? (unsigned)bi->boot_flags : 0u);
    }
    if (!uos.trusted) {
        /* Cold: copy the firmware blob as a first stage does. */
        bootinfo_copy_fw_blob(ROBCO_HANDOFF_FDT_ADDR, ROBCO_HANDOFF_FDT_MAX);
    }
    uos_panic_init();
#if defined(UOS_TEST_EARLY_FAULT) && UOS_TEST_EARLY_FAULT
    uos_test_early_fault();
#endif

    mini = uart_mini_init(115200);
    UOS_MARK("maint mini=%d", mini);

    b = board_get();
    UOS_MARK("board midr=0x%08x soc=%s base=0x%08x rev=0x%08x", (unsigned)b->midr, b->soc_name,
             (unsigned)b->periph_base, (unsigned)b->board_rev);

    mbox = mbox_get_arm_memory(&mem_base, &mem_size);
    UOS_MARK("mbox rc=%d mem_base=0x%08x mem_size=0x%08x", mbox, (unsigned)mem_base,
             (unsigned)mem_size);

    init_mmu();
    init_screen();

    irq_init();
    tick = tick_init(UOS_TICK_HZ);
#if defined(UOS_TEST_TICK_FAIL) && UOS_TEST_TICK_FAIL
    tick = uos_test_tick_fail(tick);
#endif
    irq_cpu_enable();
    UOS_MARK("tick hz=%u rc=%d", UOS_TICK_HZ, tick);
    if (tick != 0) {
        uos_panic("TICK START FAILED");
    }

    init_heap();
    uos_maint_start();

    /* Information only: a failed check is reported and the boot goes on. */
    UOS_MARK("selfcheck rc=%s", rimg_strerror(self));

    mount_card();
    read_config();
    start_buttons();
    init_rtc();
    uos_services_init();

    uos_loop_init();
    uos.phase = UOS_PHASE_READY;
    (void)uos_screen_push(&uos_first_screen);
    uos_loop_run();
}
