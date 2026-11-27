/*
 * The sound service. With `[audio] sound = on` its init starts the
 * library's audio, sets the volume and plays the rest ramp, which moves
 * the pin's rest level to the midpoint. No effect plays until that ramp
 * has ended: see the rest gate in sfxrules.h. With the default `off` it takes
 * nothing: no audio_init(), no tick slot, no trace line. Either way it
 * prints one "sound result=<on|off|error> rc=<rc>" marker.
 *
 * The audio trace. QEMU has no PWM, so the library queues one record per
 * play and audio_poll() prints them with k_printf(). With
 * `[audio] trace = on` the poll prints them, each on a line of its own:
 * like uos_mark(), it ends a partly echoed input line first. With the
 * default `off` the poll drains the records and drops them, so typing
 * over the serial line is not flooded. The library has no call that
 * drains without printing, so the poll points the k_printf() hook at a
 * sink for the call and points it back at uart_putc() after. UOS never
 * moves the hook anywhere else.
 */

#include <robco/audio.h>
#include <robco/irq.h>
#include <robco/tick.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "uos.h"
#include "uos_service.h"
#include "sfx.h"
#include "sfx_table.h"
#include "sfxrules.h"

static int on;          /* audio_init() and the ramp succeeded */
static int trace;       /* [audio] trace */
static int line_start = 1;
static sfx_pick_t pick;
static sfx_rest_t rest;
static sfx_gap_t gap;
static sfx_lane_t lane;
static const sfx_rest_ops_t rest_ops = { audio_rest_mid, audio_busy, audio_stop };

/* The k_printf() hook while the trace is on. */
static void trace_keep(char c)
{
    if (line_start) {
        uos_serial_break();
    }
    uart_putc(c);
    line_start = c == '\n';
}

/* The k_printf() hook while the trace is off. */
static void trace_drop(char c)
{
    (void)c;
}

void uos_sfx_poll(void)
{
    uint32_t s;

    if (!on) {
        return;
    }
    if (trace) {
        k_console_set(trace_keep);
        audio_poll();
        k_console_set(uart_putc);
        return;
    }
    /* Masked: a marker from an IRQ handler must not land in the sink.
     * The PWM backend queues no record, so this is short on a board. */
    s = irq_save();
    k_console_set(trace_drop);
    audio_poll();
    k_console_set(uart_putc);
    irq_restore(s);
}

int uos_sfx(unsigned id)
{
    const sfx_sound_t *t = sfx_sound(id);

    if (!on || t == NULL) {
        return 0;
    }
    /* No effect before the rest ramp ends: a play would cut it. */
    if (!sfx_rest_ready(&rest, &rest_ops)) {
        return 0;
    }
    /* A priority play replaces one that waits for its tick: the lane keeps
     * a result sound from being erased that way by a key or a scroll. */
    if (!sfx_lane_may(&lane, id, tick_count())) {
        return 0;
    }
    /* A refused play is dropped: a sound is never worth a wait. */
    if (audio_play_pcm(t->data, t->len, UOS_SFX_RATE_HZ, AUDIO_PRIORITY) != 0) {
        return 0;
    }
    sfx_lane_took(&lane, id, tick_count());
    return 1;
}

void uos_sfx_key(void)
{
    /* The pick steps only for a key that sounds. */
    if (on && uos_sfx(sfx_pick_peek(&pick))) {
        sfx_pick_take(&pick);
    }
}

void uos_sfx_scroll(void)
{
    uint32_t now;

    if (!on) {
        return;
    }
    /* The gap starts only when the driver took the tick. */
    now = tick_ms();
    if (sfx_gap_open(&gap, now) && uos_sfx(UOS_SFX_ID_CHAR_SCROLL)) {
        sfx_gap_mark(&gap, now);
    }
}

void uos_sfx_hack(int ev)
{
    unsigned id = sfx_hack_sound(ev);

    if (id == UOS_SFX_PICK_KEY) {
        uos_sfx_key();
    } else if (id != UOS_SFX_NONE) {
        uos_sfx(id);
    }
}

void uos_sfx_stop(void)
{
    if (on) {
        audio_stop();
    }
}

static int sfx_init(void)
{
    const uos_settings_t *s = uos_settings();
    int rc = 0;

    trace = s->trace_enabled;
    sfx_pick_init(&pick);
    sfx_gap_init(&gap);
    sfx_lane_init(&lane);
    if (s->sound_enabled) {
        rc = audio_init();
        if (rc == 0) {
            audio_set_volume(s->volume);
            rc = audio_rest_mid();
        }
        on = rc == 0;
        sfx_rest_init(&rest);
        if (!on) {
            uos_set_error("sound");
        }
    }
    UOS_MARK("sound result=%s rc=%d", on ? "on" : s->sound_enabled ? "error" : "off", rc);
    uos_sfx_poll();
    return 0;
}

static void sfx_shutdown(int why)
{
    (void)why;
    uos_sfx_stop();
}

const uos_service_t uos_svc_sound = { "sound", sfx_init, NULL, sfx_shutdown };
