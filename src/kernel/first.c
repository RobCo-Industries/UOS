/*
 * The first screen: the boot banner, then either the screen a service
 * set with uos_first_set_next(), the login, or the system screen when no
 * login can run. The system screen shows the notice the session service
 * set: what is missing and what to do. It proves the event loop end to
 * end: a one-shot timer shows it, a periodic timer refreshes the clock,
 * keys update a line.
 *
 * Diagnostic keys, documented on docs/ARCHITECTURE.md:
 *   F1  timer probe: a periodic timer of UOS_PROBE_PERIOD ticks that
 *       fires UOS_PROBE_COUNT times, one "ev=timer name=probe" marker each
 *   F2  cancel probe: a one-shot timer that another timer cancels before
 *       it is due, and a witness that reports whether it ever fired
 *   F4  restarts the terminal, only when the notice allows it
 */

#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/rtc.h>
#include <robco/uart.h>

#include "uos.h"

#define BANNER_TICKS        50      /* banner alone, then the placeholder */
#define CLOCK_TICKS         UOS_TICK_HZ
#define TOP_ROW             (UOS_BOOTLINES + 2)

#define UOS_PROBE_PERIOD    10
#define UOS_PROBE_COUNT     10
#define VICTIM_TICKS        30
#define KILLER_TICKS        10
#define WITNESS_TICKS       60

static uos_first_t info;
static int shown;               /* the placeholder is on screen */
static char key_name[8];
static uos_timer_id_t clock_id;
static uint32_t banner_t0;      /* tick of enter(), for the ready marker */
static uos_screen_t *next_screen;
static char notice[2][UOS_LINE_MAX + 1];
static int notice_set;
static int reboot_key;

static unsigned probe_n;
static uint32_t probe_t0;
static uos_timer_id_t probe_id;
static unsigned victim_fired;
static uos_timer_id_t victim_id;
/* A probe runs to its end: F1 or F2 again, repeats included, is ignored until then. */
static int probe_running;
static int cancel_running;

static void put_row(unsigned row, const char *text, unsigned attr)
{
    unsigned n = (unsigned)k_strlen(text);
    unsigned cols = console_cols();

    if (!uos.fb_ok) {
        return;
    }
    if (n > cols - 1) {
        n = cols - 1;
    }
    console_set_cursor(0, row);
    console_set_attr(attr);
    console_write(text, n);
    console_set_attr(CONSOLE_ATTR_NORMAL);
    for (; n < cols - 1; n++) {
        console_putc(' ');
    }
}

static void read_clock(void)
{
    datetime_t dt;

    if (uos.rtc && clock_get(&dt) == 0) {
        info.rtc = 1;
        info.year = dt.year;
        info.month = dt.month;
        info.day = dt.day;
        info.hour = dt.hour;
        info.minute = dt.minute;
        info.second = dt.second;
    } else {
        info.rtc = 0;
    }
}

static void draw(uos_screen_t *s)
{
    char line[UOS_LINE_MAX + 1];
    unsigned i;

    (void)s;
    if (!uos.fb_ok) {
        return;
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_clear();
    put_row(0, uos_bootline(0), CONSOLE_ATTR_BRIGHT);
    for (i = 1; i < UOS_BOOTLINES; i++) {
        put_row(i, uos_bootline(i), CONSOLE_ATTR_NORMAL);
    }
    (void)uos_version_line(line, sizeof(line), UOS_VERSION_STR);
    put_row(UOS_BOOTLINES, line, CONSOLE_ATTR_DIM);
    if (!shown) {
        return;
    }
    for (i = 0; i < UOS_FIRST_ROWS; i++) {
        (void)uos_first_line(i, &info, line, sizeof(line));
        put_row(TOP_ROW + i, line, i == 0 ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL);
    }
    console_set_cursor(2, TOP_ROW + UOS_FIRST_ROWS - 1);
}

static void on_clock(void *arg)
{
    (void)arg;
    if (uos.rtc) {
        read_clock();
        uos_screen_invalidate();
    }
}

void uos_first_set_next(uos_screen_t *s)
{
    next_screen = s;
}

void uos_first_set_notice(const char *row0, const char *row1, int reboot)
{
    notice_set = row0 != NULL && row1 != NULL;
    if (notice_set) {
        k_snprintf(notice[0], sizeof(notice[0]), "%s", row0);
        k_snprintf(notice[1], sizeof(notice[1]), "%s", row1);
    }
    reboot_key = notice_set && reboot;
    info.notice[0] = notice_set ? notice[0] : NULL;
    info.notice[1] = notice_set ? notice[1] : NULL;
    if (shown) {
        uos_screen_invalidate();
    }
}

/* 1: show() found the banner covered and owes the login. */
static int owe_next;

static void show(void *arg)
{
    char line[UOS_LINE_MAX + 1];
    unsigned i;

    (void)arg;
    if (next_screen != NULL) {
        /* The login takes over: its enter() draws it. A screen that took
         * over during the banner stays; one pushed over the banner stays
         * too, and the login takes over in uncover() once it pops. */
        if (uos_screen_on_top(&uos_first_screen)) {
            (void)uos_screen_replace(next_screen);
        } else {
            owe_next = 1;
        }
        UOS_MARK("ready screen=%s card=%s rtc=%d banner=%u", next_screen->name,
                 uos_card_name(uos.card), uos.rtc, (unsigned)(uos_ticks() - banner_t0));
        return;
    }
    shown = 1;
    read_clock();
    /* Drawn now, so the screen is complete when "ready" goes out. */
    draw(&uos_first_screen);
    if (uos_timer_start(CLOCK_TICKS, CLOCK_TICKS, on_clock, NULL, &clock_id) != 0) {
        uos_set_error("timer");
    }
    /* The placeholder on the serial console too, once. */
    for (i = 0; i < UOS_FIRST_ROWS - 1; i++) {
        (void)uos_first_line(i, &info, line, sizeof(line));
        uart_puts(line);
        uart_putc('\n');
    }
    UOS_MARK("ready screen=first card=%s rtc=%d banner=%u", uos_card_name(uos.card), info.rtc,
             (unsigned)(uos_ticks() - banner_t0));
}

static void enter(uos_screen_t *s)
{
    uos_timer_id_t id;

    (void)s;
    info.name = uos_settings()->name;
    info.card = uos.card;
    info.cfg_bad = uos.cfg_bad;
    info.keys = 0;
    info.last_key = NULL;
    shown = 0;
    banner_t0 = uos_ticks();
    if (uos_timer_start(BANNER_TICKS, 0, show, NULL, &id) != 0) {
        show(NULL);
    }
}

static void leave(uos_screen_t *s)
{
    (void)s;
    owe_next = 0;
    (void)uos_timer_cancel(clock_id);
    clock_id = UOS_TIMER_NONE;
}

/* ---- Diagnostic timers ---- */

static void probe_tick(void *arg)
{
    (void)arg;
    probe_n++;
    UOS_MARK("timer name=probe n=%u ticks=%u", probe_n, (unsigned)(uos_ticks() - probe_t0));
    if (probe_n >= UOS_PROBE_COUNT) {
        (void)uos_timer_cancel(probe_id);
        probe_running = 0;
        UOS_MARK("timer name=probe done=1 count=%u ticks=%u period=%u", probe_n,
                 (unsigned)(uos_ticks() - probe_t0), UOS_PROBE_PERIOD);
    }
}

static void start_probe(void)
{
    if (probe_running) {
        return;
    }
    probe_n = 0;
    probe_t0 = uos_ticks();
    if (uos_timer_start(UOS_PROBE_PERIOD, UOS_PROBE_PERIOD, probe_tick, NULL, &probe_id) != 0) {
        UOS_MARK("timer name=probe error=start");
        return;
    }
    probe_running = 1;
}

static void victim(void *arg)
{
    (void)arg;
    victim_fired++;
    UOS_MARK("timer name=victim fired=%u", victim_fired);
}

static void killer(void *arg)
{
    (void)arg;
    UOS_MARK("timer name=cancel rc=%d", uos_timer_cancel(victim_id));
}

static void witness(void *arg)
{
    (void)arg;
    cancel_running = 0;
    UOS_MARK("timer name=witness victim=%u", victim_fired);
}

static void start_cancel_probe(void)
{
    uos_timer_id_t id;

    if (cancel_running) {
        return;
    }
    victim_fired = 0;
    if (uos_timer_start(VICTIM_TICKS, 0, victim, NULL, &victim_id) != 0 ||
        uos_timer_start(KILLER_TICKS, 0, killer, NULL, &id) != 0 ||
        uos_timer_start(WITNESS_TICKS, 0, witness, NULL, &id) != 0) {
        UOS_MARK("timer name=cancel error=start");
        return;
    }
    cancel_running = 1;
}

static int event(uos_screen_t *s, const uos_event_t *ev)
{
    const char *name;

    (void)s;
    if (ev->type == UOS_EV_KEY) {
        name = input_key_name(ev->code);
        k_snprintf(key_name, sizeof(key_name), "%s", name != NULL ? name : "?");
        if (ev->code == INPUT_KEY_F1) {
            start_probe();
        } else if (ev->code == INPUT_KEY_F2) {
            start_cancel_probe();
        } else if (ev->code == INPUT_KEY_F4 && reboot_key && shown) {
            uos_reboot("user");
        }
    } else if (ev->type == UOS_EV_CHAR) {
        if (ev->code > 0x20 && ev->code < 0x7f) {
            k_snprintf(key_name, sizeof(key_name), "'%c'", (char)ev->code);
        } else {
            k_snprintf(key_name, sizeof(key_name), "0x%02x", (unsigned)ev->code);
        }
    } else {
        return 0;
    }
    info.keys++;
    info.last_key = key_name;
    UOS_MARK("key screen=first type=%s code=%u name=%s keys=%u", uos_ev_name(ev->type),
             (unsigned)ev->code, key_name, info.keys);
    if (shown) {
        uos_screen_invalidate();
    }
    return 1;
}

/* On top again: the login a covered banner could not hand over to. The
 * replace runs leave(), which clears the debt, so it runs once. */
static void uncover(uos_screen_t *s)
{
    (void)s;
    if (owe_next && next_screen != NULL) {
        (void)uos_screen_replace(next_screen);
    }
}

uos_screen_t uos_first_screen = {
    .name = "first",
    .uncover = uncover,
    .enter = enter,
    .leave = leave,
    .event = event,
    .draw = draw,
    .full = 1,
};
