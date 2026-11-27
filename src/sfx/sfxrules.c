/*
 * The pure rules of the sound service. See sfxrules.h.
 */

#include "hack.h"
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

static uint32_t pick_next(uint32_t rnd)
{
    return rnd * 1103515245u + 12345u;
}

unsigned sfx_pick_peek(const sfx_pick_t *p)
{
    return (pick_next(p->rnd) >> 16) & 1u ? UOS_SFX_ID_KEY_B : UOS_SFX_ID_KEY_A;
}

void sfx_pick_take(sfx_pick_t *p)
{
    p->rnd = pick_next(p->rnd);
}

unsigned sfx_pick_key(sfx_pick_t *p)
{
    unsigned id = sfx_pick_peek(p);

    sfx_pick_take(p);
    return id;
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

void sfx_gap_init(sfx_gap_t *g)
{
    g->last_ms = 0;
    g->started = 0;
}

int sfx_gap_open(const sfx_gap_t *g, uint32_t now_ms)
{
    return !g->started || (uint32_t)(now_ms - g->last_ms) >= SFX_SCROLL_GAP_MS;
}

void sfx_gap_mark(sfx_gap_t *g, uint32_t now_ms)
{
    g->started = 1;
    g->last_ms = now_ms;
}

int sfx_gap_pass(sfx_gap_t *g, uint32_t now_ms)
{
    if (!sfx_gap_open(g, now_ms)) {
        return 0;
    }
    sfx_gap_mark(g, now_ms);
    return 1;
}

unsigned sfx_rank(unsigned id)
{
    switch (id) {
    case UOS_SFX_ID_PASS_GOOD:
    case UOS_SFX_ID_PASS_BAD:
        return SFX_RANK_RESULT;
    case UOS_SFX_ID_CHAR_SCROLL:
        return SFX_RANK_SCROLL;
    default:
        return SFX_RANK_KEY;
    }
}

void sfx_lane_init(sfx_lane_t *l)
{
    l->set = 0;
    l->tick = 0;
    l->rank = SFX_RANK_KEY;
}

int sfx_lane_may(const sfx_lane_t *l, unsigned id, uint32_t now_tick)
{
    return !l->set || l->tick != now_tick || sfx_rank(id) >= l->rank;
}

void sfx_lane_took(sfx_lane_t *l, unsigned id, uint32_t now_tick)
{
    l->set = 1;
    l->tick = now_tick;
    l->rank = sfx_rank(id);
}

unsigned sfx_hack_sound(int ev)
{
    switch (ev) {
    case HK_EV_WIN:
        return UOS_SFX_ID_PASS_GOOD;
    case HK_EV_DENIED:
    case HK_EV_LOCKOUT:
        return UOS_SFX_ID_PASS_BAD;
    case HK_EV_DUD:
    case HK_EV_RESET:
        return UOS_SFX_PICK_KEY;
    default:
        return UOS_SFX_NONE;
    }
}

void sfx_mix_head(uint8_t *head, const uint8_t *shot, const uint8_t *bed, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        int v = (int)shot[i] + (int)bed[i] - 128;

        head[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

unsigned sfx_topup(int busy, int can_queue)
{
    if (can_queue) {
        return SFX_TOPUP_QUEUE;
    }
    return busy ? SFX_TOPUP_NONE : SFX_TOPUP_START;
}
