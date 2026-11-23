/*
 * Services: the parts of UOS that start once at boot and stop before a
 * reboot, a halt or after a panic. The table is in services.c. The rules
 * that run it are pure, in service.c, with host tests.
 *
 * Adding a service, two lines in src/kernel/services.c:
 *   1. extern const uos_service_t uos_svc_<name>;   with the other externs
 *   2. &uos_svc_<name>,   in the table, between the BEGIN and END markers,
 *      at the place its start order needs: after every service it uses.
 * The service itself lives in its own file, for example src/<module>/svc.c:
 *
 *     const uos_service_t uos_svc_mail = { "mail", mail_init, NULL, mail_down };
 *
 * Its init may register maintenance commands (uos_maint.h).
 */
#ifndef UOS_SERVICE_H
#define UOS_SERVICE_H

#include <stdint.h>

/* Services in the table, at most. */
#define UOS_SVC_MAX             32

/* The `why` of shutdown(). */
#define UOS_SHUTDOWN_REBOOT     0
#define UOS_SHUTDOWN_HALT       1
#define UOS_SHUTDOWN_PANIC      2

/* The rc of a service whose init has not run. */
#define UOS_SVC_NOT_STARTED     1

typedef struct uos_service {
    const char *name;             /* for markers and `state`: one word */
    /* Called once, in table order, after the card and settings are read,
     * from main context with IRQs on, before the loop runs. 0 or a
     * negative code. NULL counts as 0. */
    int  (*init)(void);
    /* Optional: called from the loop on every TICK event, with the tick
     * count, when init returned 0. Must not block. */
    void (*tick)(uint32_t now);
    /* Optional: called before a reboot, a halt and from the panic path
     * (why says which), in reverse table order, when init returned 0.
     * Runs at most once per boot. Must be safe with interrupts off: no
     * waiting on ticks, timers or the loop. */
    void (*shutdown)(int why);
} uos_service_t;

/* The services in the table, in start order. Read only. */
const uos_service_t *const *uos_services(unsigned *count);
/* Runs every init, with one "ev=service name=<n> rc=<rc>" marker each. A
 * failure does not stop the boot. Main context, once. */
void uos_services_init(void);
/* Ticks the started services. The loop calls it. */
void uos_services_tick(uint32_t now);
/* Shuts the started services down in reverse order, with one
 * "ev=shutdown name=<n> why=<w>" marker each. Once per boot; later calls
 * do nothing. Any context, IRQs off is fine. */
void uos_services_shutdown(int why);
/* The init result of service `i`, or UOS_SVC_NOT_STARTED. */
int uos_service_rc(unsigned i);
/* "reboot", "halt" or "panic". */
const char *uos_shutdown_name(int why);

/* ---- The pure rules ------------------------------------------------------ */

typedef struct {
    int rc[UOS_SVC_MAX];        /* init results, UOS_SVC_NOT_STARTED before */
    int down;                   /* shutdown has run */
} uos_svc_state_t;

/* Called after each init with its result. */
typedef void (*uos_svc_init_report_fn)(const uos_service_t *s, int rc, void *ctx);
/* Called before each shutdown. */
typedef void (*uos_svc_down_report_fn)(const uos_service_t *s, int why, void *ctx);

void uos_svc_state_init(uos_svc_state_t *st);
/* Runs the inits of tab[0..n-1] in order; n is capped at UOS_SVC_MAX.
 * Every service runs, whatever the earlier ones return. Returns the count
 * of failed inits. */
unsigned uos_svc_init_all(uos_svc_state_t *st, const uos_service_t *const *tab, unsigned n,
                          uos_svc_init_report_fn report, void *ctx);
/* Calls tick(now) of each started service that has one, in order. */
void uos_svc_tick_all(const uos_svc_state_t *st, const uos_service_t *const *tab, unsigned n,
                      uint32_t now);
/* Calls shutdown(why) of each started service, last first. Only the first
 * call per state does anything. Returns the number of services told,
 * those without a shutdown function included. */
unsigned uos_svc_shutdown_all(uos_svc_state_t *st, const uos_service_t *const *tab, unsigned n,
                              int why, uos_svc_down_report_fn report, void *ctx);

#endif
