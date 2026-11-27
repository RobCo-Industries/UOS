/*
 * The sound service. With `[audio] sound = on` its init starts the
 * library's audio, sets the volume and plays the rest ramp, which moves
 * the pin's rest level to the midpoint. With the default `off` it takes
 * nothing: no audio_init(), no tick slot, no trace line. Either way it
 * prints one "sound result=<on|off|error> rc=<rc>" marker.
 */

#include <robco/audio.h>

#include "uos.h"
#include "uos_service.h"
#include "sfx.h"

static int on;    /* audio_init() and the ramp succeeded */

void uos_sfx_poll(void)
{
    if (on) {
        audio_poll();
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

    if (s->sound_enabled) {
        rc = audio_init();
        if (rc == 0) {
            audio_set_volume(s->volume);
            rc = audio_rest_mid();
        }
        on = rc == 0;
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
