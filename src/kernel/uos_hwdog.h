/*
 * The hardware watchdog's rules. Pure: src/kernel/hwdog.c. The device
 * service's glue calls the library's wdog.h from them.
 *
 * Off unless UOS.CFG [watchdog] enabled = on. Started from the device
 * service's first tick, after every service is up. Kicked only from that
 * service's tick, the place that already proves the loop alive for the
 * software watchdog: a hung loop sends no tick, so the board resets. Never
 * kicked from an IRQ. After a shutdown no kick comes: a panic resets the
 * board once the timeout ends; a halt stops the watchdog, so the terminal
 * stays halted.
 */
#ifndef UOS_HWDOG_H
#define UOS_HWDOG_H

#include <stdint.h>

typedef struct {
    uint32_t timeout_ms;    /* given to wdog_start(), 0 while not running */
    uint32_t period;        /* ticks between kicks, at least 1 */
    uint32_t last;          /* tick of the last kick or of the start */
    uint8_t held;           /* no more kicks: after a shutdown */
} uos_hwdog_t;

/* What the shutdown does with a running watchdog. */
#define UOS_HWDOG_LEAVE     0   /* keep it running, no kicks: it resets the board */
#define UOS_HWDOG_STOP      1   /* wdog_stop() */

/*
 * The timeout to pass to wdog_start(): `timeout_ms` when `enabled` and
 * 1000 to 15000, else 0, which means stay off.
 */
uint32_t uos_hwdog_plan(int enabled, unsigned timeout_ms);

/*
 * Records a start at tick `now` with `timeout_ms`, at `tick_hz` ticks per
 * second. Kicks come every quarter of the timeout, at least every tick.
 */
void uos_hwdog_started(uos_hwdog_t *w, uint32_t timeout_ms, uint32_t now, unsigned tick_hz);

/*
 * 1 when the caller, the loop's tick, should call wdog_kick() now: the
 * watchdog runs, no shutdown held it, `loop_alive` is not 0 and a period
 * passed since the last kick (wrap-safe). Else 0.
 */
int uos_hwdog_kick_due(const uos_hwdog_t *w, uint32_t now, int loop_alive);

/* Records a kick at tick `now`. */
void uos_hwdog_kicked(uos_hwdog_t *w, uint32_t now);

/*
 * At shutdown: holds the kicks for good and returns what to do with a
 * running watchdog for `why` (UOS_SHUTDOWN_*): UOS_HWDOG_STOP for a halt,
 * UOS_HWDOG_LEAVE otherwise. A panic then resets the board; a reboot
 * resets it anyway.
 */
int uos_hwdog_shutdown(uos_hwdog_t *w, int why);

#endif
