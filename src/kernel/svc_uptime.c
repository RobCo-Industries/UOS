/*
 * The uptime service: the smallest real service, proof of the table.
 * Its init records the start tick and adds the `uptime` maintenance
 * command. Its tick records the last tick the loop gave it, so the tests
 * see that services tick.
 */

#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"

static uint32_t start_tick;
static uint32_t last_tick;   /* the `now` of the last tick() call */

/* `uptime`: "uptime s=<seconds since boot> ticks=<ticks> start=<tick of init>
 * last=<tick of the last tick() call>". */
static int cmd_uptime(int argc, char **argv)
{
    uint32_t now = uos_ticks();

    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    uos_maint_reply("uptime s=%u ticks=%u start=%u last=%u", (unsigned)(now / UOS_TICK_HZ),
                    (unsigned)now, (unsigned)start_tick, (unsigned)last_tick);
    return 0;
}

static int uptime_init(void)
{
    start_tick = uos_ticks();
    return uos_maint_register("uptime", cmd_uptime, "seconds since boot");
}

static void uptime_tick(uint32_t now)
{
    last_tick = now;
}

static void uptime_shutdown(int why)
{
    (void)why;
}

const uos_service_t uos_svc_uptime = { "uptime", uptime_init, uptime_tick, uptime_shutdown };
