/*
 * The pure rules of the sound service. See sfxrules.h.
 */

#include "sfx_table.h"
#include "sfxrules.h"

static const sfx_sound_t sounds[UOS_SFX_ID_COUNT] = {
    [UOS_SFX_ID_KEY_A]       = { uos_sfx_key_a, UOS_SFX_KEY_A_LEN, UOS_SFX_KEY_A_LOOP },
    [UOS_SFX_ID_KEY_B]       = { uos_sfx_key_b, UOS_SFX_KEY_B_LEN, UOS_SFX_KEY_B_LOOP },
    [UOS_SFX_ID_CHAR_SCROLL] = { uos_sfx_char_scroll, UOS_SFX_CHAR_SCROLL_LEN,
                                 UOS_SFX_CHAR_SCROLL_LOOP },
    [UOS_SFX_ID_PASS_GOOD]   = { uos_sfx_pass_good, UOS_SFX_PASS_GOOD_LEN,
                                 UOS_SFX_PASS_GOOD_LOOP },
    [UOS_SFX_ID_PASS_BAD]    = { uos_sfx_pass_bad, UOS_SFX_PASS_BAD_LEN, UOS_SFX_PASS_BAD_LOOP },
    [UOS_SFX_ID_FAN]         = { uos_sfx_fan, UOS_SFX_FAN_LEN, UOS_SFX_FAN_LOOP },
};

const sfx_sound_t *sfx_sound(unsigned id)
{
    return id < UOS_SFX_ID_COUNT ? &sounds[id] : NULL;
}

void sfx_pick_init(sfx_pick_t *p)
{
    p->rnd = 1u;
}

unsigned sfx_pick_key(sfx_pick_t *p)
{
    p->rnd = p->rnd * 1103515245u + 12345u;
    return (p->rnd >> 16) & 1u ? UOS_SFX_ID_KEY_B : UOS_SFX_ID_KEY_A;
}

void sfx_rest_init(sfx_rest_t *r)
{
    r->ready = 0;
    r->retry = 1;
    r->shut = 0;
}

int sfx_rest_ready(sfx_rest_t *r, const sfx_rest_ops_t *ops)
{
    int rc;

    if (r->ready) {
        return 1;
    }
    if (r->shut || ops->busy()) {
        return 0;
    }
    rc = ops->rest_mid();
    if (rc == 0 && !ops->busy()) {
        r->ready = 1;
        return 1;
    }
    if (r->retry) {
        r->retry = 0;
        return 0;
    }
    if (rc == 0) {
        ops->stop();
    }
    r->shut = 1;
    return 0;
}
