/*
 * The pure rules of the sound service, src/sfx/sfxrules.c: which table
 * each sound id plays, which of the two key sounds a key picks, how
 * often char_scroll may start and which sound a game result plays.
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

/* Not sounds: what sfx_hack_sound() returns for a key sound and for
 * no sound at all. */
#define UOS_SFX_PICK_KEY  (UOS_SFX_ID_COUNT + 0u)
#define UOS_SFX_NONE      (UOS_SFX_ID_COUNT + 1u)

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

/* The next pick, UOS_SFX_ID_KEY_A or UOS_SFX_ID_KEY_B, without a step.
 * Call sfx_pick_take() once the driver took it, so a dropped key does
 * not use up a pick. */
unsigned sfx_pick_peek(const sfx_pick_t *p);

/* Steps the generator past the pick that sfx_pick_peek() returns. */
void sfx_pick_take(sfx_pick_t *p);

/* sfx_pick_peek() then sfx_pick_take(). */
unsigned sfx_pick_key(sfx_pick_t *p);

/*
 * The rest gate. No effect plays until the library's rest ramp has
 * reached its end: a play that cuts the ramp leaves the rest level at
 * duty 0, and the next sound starts with a click. The library gives no
 * status call, but two calls tell it. audio_busy() is non-zero while the
 * ramp plays. Once the driver is idle, audio_rest_mid() returns 0 and
 * starts nothing when a ramp has ended at its last sample; after a cut it
 * starts a new ramp, so the driver is busy again. The gate tries that
 * once more; if the second ramp fails or is cut too, it stops what it
 * started and stays shut.
 */
typedef struct {
    int (*rest_mid)(void);      /* audio_rest_mid() */
    int (*busy)(void);          /* audio_busy() */
    void (*stop)(void);         /* audio_stop() */
} sfx_rest_ops_t;

typedef struct {
    int ready;                  /* a ramp has reached its end */
    int retry;                  /* 1 while the one retry is left */
    int shut;                   /* the retry failed: no effect plays */
} sfx_rest_t;

/* Call after the service's own audio_rest_mid() returned 0. */
void sfx_rest_init(sfx_rest_t *r);

/* 1 when an effect may play now. Main context only. */
int sfx_rest_ready(sfx_rest_t *r, const sfx_rest_ops_t *ops);

/*
 * The scroll limit. char_scroll starts at most once per
 * SFX_SCROLL_GAP_MS, so a held arrow key does not smear. The time is the
 * kernel's millisecond clock, which wraps at 2^32: only the difference
 * counts. The first move always passes. The gap counts from the last
 * char_scroll the driver took: a dropped one does not start it.
 */
#define SFX_SCROLL_GAP_MS 30u

typedef struct {
    uint32_t last_ms;           /* the last start that played */
    int started;                /* 0 until a start played */
} sfx_gap_t;

void sfx_gap_init(sfx_gap_t *g);

/* 1 when char_scroll may start at `now_ms`. Changes nothing. */
int sfx_gap_open(const sfx_gap_t *g, uint32_t now_ms);

/* Records that char_scroll played at `now_ms`: the next gap starts. */
void sfx_gap_mark(sfx_gap_t *g, uint32_t now_ms);

/* sfx_gap_open(), then sfx_gap_mark() when it is open. */
int sfx_gap_pass(sfx_gap_t *g, uint32_t now_ms);

/*
 * The start rule. The library starts a play at its next tick, and an
 * AUDIO_PRIORITY play asked before that tick replaces the waiting one,
 * which then never sounds. So a key sound asked in the tick of a result
 * sound would erase it. The lane remembers the rank and the tick count
 * of the last play the service asked for. A sound of a lower rank than
 * that play is dropped while it still waits for its tick; a sound of the
 * same or a higher rank replaces it. Once a tick has passed, the play has
 * started, and any new sound replaces it: one voice, the newest wins.
 */
enum {
    SFX_RANK_KEY,               /* key_a, key_b, fan */
    SFX_RANK_SCROLL,            /* char_scroll */
    SFX_RANK_RESULT             /* pass_good, pass_bad */
};

/* The rank of sound `id`. */
unsigned sfx_rank(unsigned id);

typedef struct {
    int set;                    /* a play was asked */
    uint32_t tick;              /* the tick count when it was asked */
    unsigned rank;              /* its rank */
} sfx_lane_t;

void sfx_lane_init(sfx_lane_t *l);

/* 1 when sound `id` may be asked now, at tick count `now_tick`. */
int sfx_lane_may(const sfx_lane_t *l, unsigned id, uint32_t now_tick);

/* Records that the driver took sound `id` at tick count `now_tick`. */
void sfx_lane_took(sfx_lane_t *l, unsigned id, uint32_t now_tick);

/*
 * The sound of a SELECT in the password game, from its hk_event_t:
 * pass_good for a win, pass_bad for a denied word and a lockout,
 * UOS_SFX_PICK_KEY for a dud and a reset, UOS_SFX_NONE for anything
 * else. One event, one sound.
 */
unsigned sfx_hack_sound(int ev);

#endif
