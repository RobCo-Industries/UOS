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
