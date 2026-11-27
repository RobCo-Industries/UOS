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
 *
 * The fan. With `[audio] fan = on` the fan loops under the other sounds,
 * by the rule of sfxrules.h. Once the rest ramp has ended, the service
 * mixes the five heads into RAM, starts the fan and queues a second pass
 * behind it. Its poll runs the top-up every SFX_TOPUP_MS. A one-shot
 * then plays as its head, with the fan's rest queued behind it. With
 * `fan = off` every sound plays its plain table.
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

/* The heads, one after the other, sized by the generated header. */
#define HEADS_LEN (UOS_SFX_KEY_A_HEAD_LEN + UOS_SFX_KEY_B_HEAD_LEN + \
                   UOS_SFX_CHAR_SCROLL_HEAD_LEN + UOS_SFX_PASS_GOOD_HEAD_LEN + \
                   UOS_SFX_PASS_BAD_HEAD_LEN)

static volatile int fan;        /* [audio] fan, until a stop */
static int fan_live;            /* the heads are mixed and the fan runs */
static uint32_t fan_ms;         /* the last top-up */
static uint8_t heads[HEADS_LEN];
static const uint8_t *head_of[UOS_SFX_ID_COUNT];

/* Starts the fan and queues its next pass. 0 or an AUDIO_ERR_* code. */
static int fan_start(void)
{
    int rc = audio_play_pcm(uos_sfx_fan, UOS_SFX_FAN_LEN, UOS_SFX_RATE_HZ, 0);

    return rc != 0 ? rc : audio_pcm_queue(uos_sfx_fan, UOS_SFX_FAN_LEN);
}

/* 1 once the fan runs. Mixes the heads and starts the fan the first
 * time the rest ramp has ended. */
static int fan_begin(void)
{
    static const unsigned shots[] = {
        UOS_SFX_ID_KEY_A, UOS_SFX_ID_KEY_B, UOS_SFX_ID_CHAR_SCROLL,
        UOS_SFX_ID_PASS_GOOD, UOS_SFX_ID_PASS_BAD,
    };
    size_t at = 0;
    unsigned i;

    if (!fan || fan_live) {
        return fan && fan_live;
    }
    if (!sfx_rest_ready(&rest, &rest_ops)) {
        return 0;
    }
    for (i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        const sfx_sound_t *t = sfx_sound(shots[i]);

        sfx_mix_head(heads + at, t->data, uos_sfx_fan, t->len);
        head_of[shots[i]] = heads + at;
        at += t->len;
    }
    fan_live = 1;
    fan_ms = tick_ms();
    (void)fan_start();
    return 1;
}

/* Every SFX_TOPUP_MS: keeps the fan going. */
static void fan_topup(void)
{
    uint32_t now = tick_ms();

    if (!fan_begin() || (uint32_t)(now - fan_ms) < SFX_TOPUP_MS) {
        return;
    }
    fan_ms = now;
    switch (sfx_topup(audio_busy(), audio_pcm_can_queue())) {
    case SFX_TOPUP_QUEUE:
        (void)audio_pcm_queue(uos_sfx_fan, UOS_SFX_FAN_LEN);
        break;
    case SFX_TOPUP_START:
        (void)fan_start();
        break;
    default:
        break;
    }
}

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
    fan_topup();
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
    int rc;

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
    /* A refused play is dropped: a sound is never worth a wait. Over the
     * fan, the head plays and the fan's rest waits behind it; a refused
     * queue leaves the rest to the top-up. */
    if (fan_begin() && head_of[id] != NULL) {
        rc = audio_play_pcm(head_of[id], t->len, UOS_SFX_RATE_HZ, AUDIO_PRIORITY);
        if (rc == 0) {
            (void)audio_pcm_queue(uos_sfx_fan + t->len, UOS_SFX_FAN_LEN - t->len);
        }
    } else {
        rc = audio_play_pcm(t->data, t->len, UOS_SFX_RATE_HZ, AUDIO_PRIORITY);
    }
    if (rc != 0) {
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
    /* No top-up after a stop: the fan stays off. */
    fan = 0;
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
        fan = on && s->fan_enabled;
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
