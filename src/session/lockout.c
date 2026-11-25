#include "lockout.h"

lk_err_t lk_init(lk_t *lk, unsigned retries, unsigned lockout_s)
{
    if (retries < LK_RETRIES_MIN || retries > LK_RETRIES_MAX) {
        return LK_E_RETRIES;
    }
    if (lockout_s > LK_LOCKOUT_MAX_S) {
        return LK_E_LOCKOUT;
    }
    lk->retries = (uint8_t)retries;
    lk->lockout_ms = (uint32_t)lockout_s * 1000u;
    lk->fails = 0;
    lk->locked = 0;
    lk->locked_at = 0;
    lk->lock_ms = 0;
    return LK_OK;
}

int lk_locked(lk_t *lk, uint32_t now)
{
    if (lk->locked && (uint32_t)(now - lk->locked_at) >= lk->lock_ms) {
        lk->locked = 0;
        lk->fails = 0;
    }
    return lk->locked;
}

int lk_fail(lk_t *lk, uint32_t now)
{
    if (lk_locked(lk, now)) {
        return 1;
    }
    lk->fails++;
    if (lk->fails >= lk->retries) {
        lk->fails = 0;
        if (lk->lockout_ms > 0) {
            lk->locked = 1;
            lk->locked_at = now;
            lk->lock_ms = lk->lockout_ms;
        }
    }
    return lk->locked;
}

int lk_lock(lk_t *lk, uint32_t now, unsigned seconds)
{
    uint32_t ms;

    if (seconds > LK_LOCKOUT_MAX_S) {
        seconds = LK_LOCKOUT_MAX_S;
    }
    ms = (uint32_t)seconds * 1000u;
    if (lk_locked(lk, now) && lk->lock_ms - (uint32_t)(now - lk->locked_at) >= ms) {
        return 1;
    }
    if (ms == 0) {
        return lk->locked;
    }
    lk->locked = 1;
    lk->locked_at = now;
    lk->lock_ms = ms;
    lk->fails = 0;
    return 1;
}

void lk_success(lk_t *lk)
{
    lk->fails = 0;
}

uint32_t lk_remaining_s(lk_t *lk, uint32_t now)
{
    uint32_t left;

    if (!lk_locked(lk, now)) {
        return 0;
    }
    left = lk->lock_ms - (uint32_t)(now - lk->locked_at);
    return (left + 999u) / 1000u;
}

unsigned lk_tries_left(const lk_t *lk)
{
    return lk->locked ? 0u : (unsigned)(lk->retries - lk->fails);
}

const char *lk_strerror(lk_err_t err)
{
    switch (err) {
    case LK_OK:
        return "ok";
    case LK_E_RETRIES:
        return "retries must be 1 to 9";
    case LK_E_LOCKOUT:
        return "lockout must be 0 to 3600 seconds";
    default:
        return "unknown lockout error";
    }
}
