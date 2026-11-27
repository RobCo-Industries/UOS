/*
 * The pure rules of the sound service, src/sfx/sfxrules.c: which table
 * each sound id plays, and which of the two key sounds a key picks.
 * No hardware access. The host tests link this file.
 */
#ifndef UOS_SFXRULES_H
#define UOS_SFXRULES_H

#include <stddef.h>
#include <stdint.h>

/* The sounds, in the order of src/sfx/sfx_models.json. */
enum {
    UOS_SFX_ID_KEY_A,
    UOS_SFX_ID_KEY_B,
    UOS_SFX_ID_CHAR_SCROLL,
    UOS_SFX_ID_PASS_GOOD,
    UOS_SFX_ID_PASS_BAD,
    UOS_SFX_ID_FAN,
    UOS_SFX_ID_COUNT
};

/* One generated table. */
typedef struct {
    const uint8_t *data;
    size_t len;
    int loop;                   /* 1 for a loop, 0 for a one-shot */
} sfx_sound_t;

/* The table of sound `id`, or NULL for an unknown id. */
const sfx_sound_t *sfx_sound(unsigned id);

/*
 * The key pick. Each key steps the generator
 * rnd = rnd * 1103515245 + 12345, from seed 1, and bit 16 of the new
 * value picks the sound: 0 for key_a, 1 for key_b. A strict alternation
 * would tick like a clock.
 */
typedef struct {
    uint32_t rnd;
} sfx_pick_t;

void sfx_pick_init(sfx_pick_t *p);

/* UOS_SFX_ID_KEY_A or UOS_SFX_ID_KEY_B. */
unsigned sfx_pick_key(sfx_pick_t *p);

#endif
