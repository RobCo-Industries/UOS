/* The hardware watchdog's rules, pure. See uos_hwdog.h. */

#include <stddef.h>

#include "uos_hwdog.h"
#include "uos_service.h"
#include "uos_settings.h"

uint32_t uos_hwdog_plan(int enabled, unsigned timeout_ms)
{
    if (!enabled || timeout_ms < UOS_HWDOG_MS_MIN || timeout_ms > UOS_HWDOG_MS_MAX) {
        return 0;
    }
    return timeout_ms;
}

void uos_hwdog_started(uos_hwdog_t *w, uint32_t timeout_ms, uint32_t now, unsigned tick_hz)
{
    uint32_t ticks;

    if (w == NULL) {
        return;
    }
    ticks = (uint32_t)(((uint64_t)timeout_ms * tick_hz) / 1000u);
    w->timeout_ms = timeout_ms;
    w->period = ticks / 4u != 0 ? ticks / 4u : 1u;
    w->last = now;
    w->held = 0;
}

int uos_hwdog_kick_due(const uos_hwdog_t *w, uint32_t now, int loop_alive)
{
    if (w == NULL || w->timeout_ms == 0 || w->held || !loop_alive) {
        return 0;
    }
    return now - w->last >= w->period;
}

void uos_hwdog_kicked(uos_hwdog_t *w, uint32_t now)
{
    if (w != NULL) {
        w->last = now;
    }
}

int uos_hwdog_shutdown(uos_hwdog_t *w, int why)
{
    if (w != NULL) {
        w->held = 1;
    }
    return why == UOS_SHUTDOWN_HALT ? UOS_HWDOG_STOP : UOS_HWDOG_LEAVE;
}
