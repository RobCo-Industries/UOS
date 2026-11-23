/*
 * The service table and the kernel's calls into it. The rules are pure,
 * in service.c. See uos_service.h for how to add a service.
 */

#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/mmu.h>
#include <robco/power.h>
#include <robco/uart.h>

#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"

/* One extern per service. */
extern const uos_service_t uos_svc_uptime;
extern const uos_service_t uos_svc_console;

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds only: a service whose init fails, first in the table, so the
 * tests see the failure policy: the marker, `state`, and the next service
 * still starting. */
#define UOS_TEST_SVC_RC (-5)
static int uos_test_svc_init(void)
{
    return UOS_TEST_SVC_RC;
}
static void uos_test_svc_down(int why)
{
    (void)why;
    UOS_MARK("shutdown name=testfail error=called");
}
static const uos_service_t uos_svc_testfail = { "testfail", uos_test_svc_init, NULL,
                                                uos_test_svc_down };
#endif

/*
 * The services in start order: a service comes after every service it
 * uses. One line each.
 */
static const uos_service_t *const services[] = {
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    &uos_svc_testfail,
#endif
    /* BEGIN SERVICES */
    &uos_svc_uptime,
    &uos_svc_console,
    /* END SERVICES */
};

#define SERVICE_COUNT (sizeof(services) / sizeof(services[0]))

_Static_assert(SERVICE_COUNT <= UOS_SVC_MAX, "too many services: raise UOS_SVC_MAX");

static uos_svc_state_t st = { { [0 ... UOS_SVC_MAX - 1] = UOS_SVC_NOT_STARTED }, 0 };

const uos_service_t *const *uos_services(unsigned *count)
{
    *count = SERVICE_COUNT;
    return services;
}

int uos_service_rc(unsigned i)
{
    return i < UOS_SVC_MAX ? st.rc[i] : UOS_SVC_NOT_STARTED;
}

const char *uos_shutdown_name(int why)
{
    switch (why) {
    case UOS_SHUTDOWN_REBOOT: return "reboot";
    case UOS_SHUTDOWN_HALT:   return "halt";
    case UOS_SHUTDOWN_PANIC:  return "panic";
    default:                  return "other";
    }
}

static void init_report(const uos_service_t *s, int rc, void *ctx)
{
    (void)ctx;
    UOS_MARK("service name=%s rc=%d", s->name, rc);
}

static void down_report(const uos_service_t *s, int why, void *ctx)
{
    (void)ctx;
    UOS_MARK("shutdown name=%s why=%s", s->name, uos_shutdown_name(why));
}

/* `services`: one line per service in start order, with its init result. */
static int cmd_services(int argc, char **argv)
{
    unsigned i;

    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    for (i = 0; i < SERVICE_COUNT; i++) {
        uos_maint_reply("service name=%s rc=%d", services[i]->name, st.rc[i]);
    }
    return 0;
}

void uos_services_init(void)
{
    uos_svc_state_init(&st);
    (void)uos_maint_register("services", cmd_services, "the services and their init results");
    if (uos_svc_init_all(&st, services, SERVICE_COUNT, init_report, NULL) != 0) {
        uos_set_error("service");
    }
}

void uos_services_tick(uint32_t now)
{
    uos_svc_tick_all(&st, services, SERVICE_COUNT, now);
}

void uos_services_shutdown(int why)
{
    (void)uos_svc_shutdown_all(&st, services, SERVICE_COUNT, why, down_report, NULL);
}

void uos_reboot(const char *source)
{
    uos_services_shutdown(UOS_SHUTDOWN_REBOOT);
    UOS_MARK("reboot source=%s", source);
    uart_flush();
    /* Cleans the data cache to memory: the next boot sees every write. */
    mmu_disable();
    power_reboot();
}

void uos_halt(void)
{
    irq_cpu_disable();
    uos_services_shutdown(UOS_SHUTDOWN_HALT);
    uos.phase = UOS_PHASE_HALT;
    UOS_MARK("halt");
    uos_maint_idle();
}
