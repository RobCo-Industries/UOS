#ifndef UOS_LOCKOUT_H
#define UOS_LOCKOUT_H

#include <stdint.h>

/*
 * Login failure state machine. Ticks are milliseconds in a uint32_t that
 * wraps every 2^32 ms, about 49.7 days. All tick arithmetic is the
 * difference now - start in uint32_t, so it survives the wrap as long as
 * the caller polls (any lk_ call) at least once per wrap period while a
 * lockout is running. The login screen polls on every tick, so it does.
 */
/*
 * The terminal has one lockout state, shared by the password login and
 * the password game. Every event and its effect, `fails` being the count
 * and `lock` the running lock:
 *
 *   wrong password          lk_fail: fails + 1; at retries, lock lockout_s, fails 0
 *   unknown user            lk_fail, as a wrong password
 *   right password, account
 *     disabled              lk_fail, as a wrong password: the screen never
 *                           tells the password was right
 *   right password          lk_session_start: fails 0
 *   game won                lk_session_start: fails 0
 *   game started            nothing
 *   game left with BACK     nothing, whatever the wrong guesses
 *   game lost               lk_lock [hack] lockout_s: lock, fails 0; with 0 s
 *                           nothing
 *   game refused            nothing; while locked it is refused as `locked`
 *   logout, idle timeout    nothing: fails is kept; no lock can run in a
 *                           session, since a session cannot start while locked
 *   lock runs out           lk_locked: lock ends, fails 0
 *   input while locked      lk_fail ignored, lock unchanged
 *   reboot                  lk_init: fails 0, no lock (RAM only)
 */
#define LK_RETRIES_MIN      1
#define LK_RETRIES_MAX      9
#define LK_RETRIES_DEFAULT  3
#define LK_LOCKOUT_MAX_S    3600
#define LK_LOCKOUT_DEFAULT_S 30

typedef enum lk_err {
    LK_OK = 0,
    LK_E_RETRIES,           /* retries outside 1 to 9 */
    LK_E_LOCKOUT,           /* lockout_s over 3600 */
    LK_E_NUM
} lk_err_t;

typedef struct lk {
    uint32_t lockout_ms;
    uint32_t locked_at;     /* tick when the lock started */
    uint32_t lock_ms;       /* length of the running lock: lockout_ms, or lk_lock()'s */
    uint8_t retries;
    uint8_t fails;          /* failures since the last success or unlock */
    uint8_t locked;
} lk_t;

/* Sets the limits and clears the state. On an error the state is unchanged. */
lk_err_t lk_init(lk_t *lk, unsigned retries, unsigned lockout_s);
/*
 * Records one failed login at `now`. Returns 1 if the login is now locked.
 * A failure while locked is ignored and returns 1. With lockout_s = 0 the
 * count resets when it reaches retries and nothing locks.
 */
int lk_fail(lk_t *lk, uint32_t now);
/*
 * Locks at `now` for `seconds`, whatever the count: the password game's
 * lockout shares the login's lock. 0 seconds does not lock. While a lock
 * runs, the one that ends later wins. Returns 1 if locked. Seconds over
 * LK_LOCKOUT_MAX_S are LK_LOCKOUT_MAX_S.
 */
int lk_lock(lk_t *lk, uint32_t now, unsigned seconds);
/*
 * A session starts: a right password, a won password game, or the
 * test-only `sess login`. Clears the count. sess_begin() calls it, so
 * every way into a session goes through it.
 */
void lk_session_start(lk_t *lk);
/* 1 while locked at `now`. Ends an expired lock and clears the count. */
int lk_locked(lk_t *lk, uint32_t now);
/* Whole seconds left, rounded up; 0 when not locked. Ends an expired lock. */
uint32_t lk_remaining_s(lk_t *lk, uint32_t now);
/* Failures left before the lock; 0 while locked. */
unsigned lk_tries_left(const lk_t *lk);
const char *lk_strerror(lk_err_t err);

#endif
