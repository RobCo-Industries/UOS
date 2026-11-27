/*
 * The sound service, src/sfx/sfx.c. It opens the audio path when UOS.CFG
 * says `[audio] sound = on`. Nothing plays on it yet but the rest ramp.
 */
#ifndef UOS_SFX_H
#define UOS_SFX_H

/* Prints the audio trace records. The loop calls it on every pass. Does
 * nothing while the sound is off. Main context only. */
void uos_sfx_poll(void);

/* Stops what plays. Does nothing while the sound is off. Any context. */
void uos_sfx_stop(void);

#endif
