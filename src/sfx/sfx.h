/*
 * The sound service, src/sfx/sfx.c. It opens the audio path when UOS.CFG
 * says `[audio] sound = on`, and plays the tables of sfx_table.c on it.
 */
#ifndef UOS_SFX_H
#define UOS_SFX_H

/* Prints the audio trace records with `[audio] trace = on`, and drops
 * them with `off`. The loop calls it on every pass. Does nothing while
 * the sound is off. Main context only. */
void uos_sfx_poll(void);

/* Plays sound `id`, a UOS_SFX_ID_* of sfxrules.h, at the table's rate,
 * in place of what plays. Never blocks. Does nothing while the sound is
 * off; a play the driver refuses is dropped. Main context only. */
void uos_sfx(unsigned id);

/* Plays key_a or key_b, by the pick of sfxrules.h. Main context only. */
void uos_sfx_key(void);

/* Plays char_scroll for a cursor move, at most once per 30 ms by the
 * scroll limit of sfxrules.h. Main context only. */
void uos_sfx_scroll(void);

/* Plays the one sound of a SELECT in the password game, from its
 * hk_event_t, by sfx_hack_sound(). Main context only. */
void uos_sfx_hack(int ev);

/* Stops what plays. Does nothing while the sound is off. Any context. */
void uos_sfx_stop(void);

#endif
