/* Calls posted by maintenance commands, carried by the input queue. Pure. */

#include <robco/klibc.h>

#include "uos_portcall.h"

void uos_pc_init(uos_pc_t *t)
{
    k_memset(t, 0, sizeof(*t));
}

int uos_pc_make(uos_pc_t *t, uos_call_fn fn, void *arg, input_event_t *in)
{
    unsigned i;

    for (i = 0; i < UOS_PC_LEN; i++) {
        if (!(t->used & (1u << i))) {
            break;
        }
    }
    if (i == UOS_PC_LEN) {
        return UOS_EVQ_ERR_FULL;
    }
    t->used |= 1u << i;
    t->fn[i] = fn;
    t->arg[i] = arg;
    k_memset(in, 0, sizeof(*in));
    in->type = UOS_INPUT_EV_CALL;
    in->source = INPUT_SRC_SCRIPT;
    in->code = (uint16_t)i;
    return 0;
}

void uos_pc_cancel(uos_pc_t *t, const input_event_t *in)
{
    if (in->type == UOS_INPUT_EV_CALL && in->code < UOS_PC_LEN) {
        t->used &= ~(1u << in->code);
    }
}

int uos_pc_convert(uos_pc_t *t, const input_event_t *in, uos_event_t *out)
{
    k_memset(out, 0, sizeof(*out));
    if (in->type == UOS_INPUT_EV_CALL) {
        if (in->code >= UOS_PC_LEN || !(t->used & (1u << in->code))) {
            return 0;
        }
        t->used &= ~(1u << in->code);
        out->type = UOS_EV_CALL;
        out->fn = t->fn[in->code];
        out->arg = t->arg[in->code];
        out->value = 1;
        return 1;
    }
    if (in->type == INPUT_EV_KEY && in->value != INPUT_RELEASE) {
        out->type = UOS_EV_KEY;
    } else if (in->type == INPUT_EV_CHAR) {
        out->type = UOS_EV_CHAR;
    } else {
        return 0;
    }
    out->source = in->source;
    out->code = in->code;
    out->value = in->value;
    return 1;
}
