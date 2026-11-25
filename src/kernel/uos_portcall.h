/*
 * Calls posted by maintenance commands. Pure: src/kernel/portcall.c.
 *
 * Keys and port commands come in on one line, in one order. Keys go into
 * the input layer's queue. A call posted by a port command goes into the
 * same queue, as an input event of type UOS_INPUT_EV_CALL whose code
 * names a slot of this table. The loop turns input events into kernel
 * events in queue order, so a call runs after every key received before
 * its command and before every key received after it.
 */
#ifndef UOS_PORTCALL_H
#define UOS_PORTCALL_H

#include <stdint.h>

#include <robco/input.h>

#include "uos_event.h"

/* An input event type the library does not use: a port call. */
#define UOS_INPUT_EV_CALL   0x40u
/* Slots: one per input queue entry, so a full queue runs out first. */
#define UOS_PC_LEN          INPUT_QUEUE_LEN

typedef struct {
    uos_call_fn fn[UOS_PC_LEN];
    void *arg[UOS_PC_LEN];
    uint32_t used;      /* bit n: slot n holds a call not taken yet */
} uos_pc_t;

void uos_pc_init(uos_pc_t *t);

/*
 * Stores fn(arg) in a free slot and fills *in with the input event that
 * carries it. Returns 0, or UOS_EVQ_ERR_FULL when no slot is free.
 */
int uos_pc_make(uos_pc_t *t, uos_call_fn fn, void *arg, input_event_t *in);

/* Frees the slot of an event made by uos_pc_make() that was not queued. */
void uos_pc_cancel(uos_pc_t *t, const input_event_t *in);

/*
 * Turns an input event into a kernel event: a key press or repeat, a
 * character, or a port call, which frees its slot. A port call gets
 * value 1, so the loop counts it as pending input until it runs.
 * Returns 1, or 0 for an event the loop drops: a release, a dial turn,
 * a call with a free or bad slot.
 */
int uos_pc_convert(uos_pc_t *t, const input_event_t *in, uos_event_t *out);

#endif
