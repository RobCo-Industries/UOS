/* Host tests of src/sfx/sfxrules.c and the generated src/sfx/sfx_table.c. */
#include <stdint.h>
#include <string.h>

#include "rc_test.h"

#include "sfx_table.h"
#include "sfxrules.h"

/* The first 32 keys from seed 1: a for key_a, b for key_b. */
#define PICKS "aabbbbabaabbaabbbbbaaabbbabbaaab"

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

TEST(sfx_pick_sequence)
{
    sfx_pick_t p;
    char got[sizeof(PICKS)];
    unsigned i;

    sfx_pick_init(&p);
    for (i = 0; i < sizeof(PICKS) - 1; i++) {
        unsigned id = sfx_pick_key(&p);

        CHECK(id == UOS_SFX_ID_KEY_A || id == UOS_SFX_ID_KEY_B);
        got[i] = id == UOS_SFX_ID_KEY_A ? 'a' : 'b';
    }
    got[i] = 0;
    CHECK_STR(got, PICKS);
    /* A new init starts the same sequence again. */
    sfx_pick_init(&p);
    CHECK_EQ(sfx_pick_key(&p), UOS_SFX_ID_KEY_A);
}

static void table(unsigned id, const uint8_t *data, size_t len, uint32_t crc, int loop)
{
    const sfx_sound_t *s = sfx_sound(id);

    CHECK(s != NULL);
    CHECK(s->data == data);
    CHECK_EQ(s->len, len);
    CHECK_EQ(s->loop, loop);
    CHECK_EQ(crc32(s->data, s->len), crc);
}

TEST(sfx_tables_match_header)
{
    CHECK_EQ(UOS_SFX_RATE_HZ, 37500u);
    table(UOS_SFX_ID_KEY_A, uos_sfx_key_a, UOS_SFX_KEY_A_LEN, UOS_SFX_KEY_A_CRC32,
          UOS_SFX_KEY_A_LOOP);
    table(UOS_SFX_ID_KEY_B, uos_sfx_key_b, UOS_SFX_KEY_B_LEN, UOS_SFX_KEY_B_CRC32,
          UOS_SFX_KEY_B_LOOP);
    table(UOS_SFX_ID_CHAR_SCROLL, uos_sfx_char_scroll, UOS_SFX_CHAR_SCROLL_LEN,
          UOS_SFX_CHAR_SCROLL_CRC32, UOS_SFX_CHAR_SCROLL_LOOP);
    table(UOS_SFX_ID_PASS_GOOD, uos_sfx_pass_good, UOS_SFX_PASS_GOOD_LEN,
          UOS_SFX_PASS_GOOD_CRC32, UOS_SFX_PASS_GOOD_LOOP);
    table(UOS_SFX_ID_PASS_BAD, uos_sfx_pass_bad, UOS_SFX_PASS_BAD_LEN, UOS_SFX_PASS_BAD_CRC32,
          UOS_SFX_PASS_BAD_LOOP);
    table(UOS_SFX_ID_FAN, uos_sfx_fan, UOS_SFX_FAN_LEN, UOS_SFX_FAN_CRC32, UOS_SFX_FAN_LOOP);
    CHECK(sfx_sound(UOS_SFX_ID_COUNT) == NULL);
}

/* Each table's loudest sample sits exactly its peak from 128. */
TEST(sfx_tables_peaks)
{
    static const unsigned peaks[UOS_SFX_ID_COUNT] = {
        UOS_SFX_KEY_A_PEAK, UOS_SFX_KEY_B_PEAK, UOS_SFX_CHAR_SCROLL_PEAK,
        UOS_SFX_PASS_GOOD_PEAK, UOS_SFX_PASS_BAD_PEAK, UOS_SFX_FAN_PEAK,
    };
    unsigned id;

    for (id = 0; id < UOS_SFX_ID_COUNT; id++) {
        const sfx_sound_t *s = sfx_sound(id);
        unsigned top = 0;
        size_t i;

        for (i = 0; i < s->len; i++) {
            unsigned d = s->data[i] >= 128 ? s->data[i] - 128u : 128u - s->data[i];

            top = d > top ? d : top;
        }
        CHECK_EQ(top, peaks[id]);
    }
}

/* One sound over a loop never clips: the loudest one-shot plus the
 * loudest loop stays within 127 steps of the midpoint. */
TEST(sfx_sum_fits)
{
    unsigned shot = 0, loop = 0, id;
    static const unsigned peaks[UOS_SFX_ID_COUNT] = {
        UOS_SFX_KEY_A_PEAK, UOS_SFX_KEY_B_PEAK, UOS_SFX_CHAR_SCROLL_PEAK,
        UOS_SFX_PASS_GOOD_PEAK, UOS_SFX_PASS_BAD_PEAK, UOS_SFX_FAN_PEAK,
    };

    for (id = 0; id < UOS_SFX_ID_COUNT; id++) {
        unsigned *top = sfx_sound(id)->loop ? &loop : &shot;

        *top = peaks[id] > *top ? peaks[id] : *top;
    }
    CHECK(loop > 0);
    CHECK(shot + loop <= 127u);
}

/* A fake driver for the rest gate. A ramp plays while `playing`; a call
 * to rest_mid() starts one unless one has ended or `rc` is set. */
static struct {
    int playing, ended, rc, calls, stops;
} drv;

static int fake_rest_mid(void)
{
    drv.calls++;
    if (drv.rc || drv.ended) {
        return drv.rc;
    }
    drv.playing = 1;
    return 0;
}

static int fake_busy(void)
{
    return drv.playing;
}

static void fake_stop(void)
{
    drv.stops++;
    drv.playing = 0;
}

static const sfx_rest_ops_t fake = { fake_rest_mid, fake_busy, fake_stop };

static void gate_start(sfx_rest_t *r)
{
    memset(&drv, 0, sizeof(drv));
    CHECK_EQ(fake_rest_mid(), 0);
    sfx_rest_init(r);
}

/* While the ramp plays nothing passes; once it ends the gate opens. */
TEST(sfx_rest_waits_for_the_ramp)
{
    sfx_rest_t r;

    gate_start(&r);
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    CHECK_EQ(drv.calls, 1);
    drv.playing = 0;
    drv.ended = 1;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 1);
    CHECK_EQ(sfx_rest_ready(&r, &fake), 1);
    CHECK_EQ(drv.calls, 2);
}

/* A cut ramp: the gate starts one more and opens when that one ends. */
TEST(sfx_rest_retries_a_cut_ramp)
{
    sfx_rest_t r;

    gate_start(&r);
    drv.playing = 0;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    CHECK_EQ(drv.playing, 1);
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    drv.playing = 0;
    drv.ended = 1;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 1);
    CHECK_EQ(drv.stops, 0);
}

/* The retry cut too: the gate stops the third ramp it started and stays shut. */
TEST(sfx_rest_shuts_after_one_retry)
{
    sfx_rest_t r;

    gate_start(&r);
    drv.playing = 0;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    drv.playing = 0;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    CHECK_EQ(drv.stops, 1);
    CHECK_EQ(drv.playing, 0);
    drv.ended = 1;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    CHECK_EQ(drv.calls, 3);
}

/* A refused retry counts as the retry; a second refusal shuts the gate. */
TEST(sfx_rest_refused_retry)
{
    sfx_rest_t r;

    gate_start(&r);
    drv.playing = 0;
    drv.rc = -3;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
    CHECK_EQ(r.shut, 1);
    CHECK_EQ(drv.stops, 0);
    drv.rc = 0;
    drv.ended = 1;
    CHECK_EQ(sfx_rest_ready(&r, &fake), 0);
}
