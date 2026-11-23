/*
 * The rules that run the service table. Pure: the table and the state
 * come from the caller.
 */

#include <stddef.h>

#include "uos_service.h"

static unsigned cap(unsigned n)
{
    return n > UOS_SVC_MAX ? UOS_SVC_MAX : n;
}

void uos_svc_state_init(uos_svc_state_t *st)
{
    unsigned i;

    for (i = 0; i < UOS_SVC_MAX; i++) {
        st->rc[i] = UOS_SVC_NOT_STARTED;
    }
    st->down = 0;
}

unsigned uos_svc_init_all(uos_svc_state_t *st, const uos_service_t *const *tab, unsigned n,
                          uos_svc_init_report_fn report, void *ctx)
{
    unsigned i, failed = 0;

    n = cap(n);
    for (i = 0; i < n; i++) {
        int rc = tab[i]->init != NULL ? tab[i]->init() : 0;

        /* A positive result would read as "not started": count it as a failure. */
        if (rc > 0) {
            rc = -rc;
        }
        st->rc[i] = rc;
        if (rc != 0) {
            failed++;
        }
        if (report != NULL) {
            report(tab[i], rc, ctx);
        }
    }
    return failed;
}

void uos_svc_tick_all(const uos_svc_state_t *st, const uos_service_t *const *tab, unsigned n,
                      uint32_t now)
{
    unsigned i;

    n = cap(n);
    if (st->down) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (st->rc[i] == 0 && tab[i]->tick != NULL) {
            tab[i]->tick(now);
        }
    }
}

unsigned uos_svc_shutdown_all(uos_svc_state_t *st, const uos_service_t *const *tab, unsigned n,
                              int why, uos_svc_down_report_fn report, void *ctx)
{
    unsigned i, told = 0;

    if (st->down) {
        return 0;
    }
    /* Set first: a fault inside a shutdown reaches the panic path, which
     * must not run the shutdowns again. */
    st->down = 1;
    for (i = cap(n); i-- > 0;) {
        if (st->rc[i] != 0) {
            continue;
        }
        if (report != NULL) {
            report(tab[i], why, ctx);
        }
        if (tab[i]->shutdown != NULL) {
            tab[i]->shutdown(why);
        }
        told++;
    }
    return told;
}
