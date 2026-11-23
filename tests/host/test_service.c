/* Host tests of src/kernel/service.c: the rules that run the service table,
 * with fake services. */

#include "rc_test.h"

#include "uos_service.h"

static char log_buf[512];

static void note(const char *fmt, const char *name, int v)
{
    size_t n = strlen(log_buf);

    snprintf(log_buf + n, sizeof(log_buf) - n, fmt, name, v);
}

#define FAKE(id, result)                                                     \
    static int id##_init(void) { note("init:%s=%d ", #id, result); return result; } \
    static void id##_tick(uint32_t now) { note("tick:%s@%d ", #id, (int)now); }     \
    static void id##_down(int why) { note("down:%s/%d ", #id, why); }               \
    static const uos_service_t id = { #id, id##_init, id##_tick, id##_down }

FAKE(a, 0);
FAKE(b, -5);
FAKE(c, 0);
FAKE(pos, 3);
static const uos_service_t bare = { "bare", NULL, NULL, NULL };

static void init_rep(const uos_service_t *s, int rc, void *ctx)
{
    (void)ctx;
    note("mark:%s=%d ", s->name, rc);
}

static void down_rep(const uos_service_t *s, int why, void *ctx)
{
    (void)ctx;
    note("mark-down:%s/%d ", s->name, why);
}

TEST(init_in_order_and_failure_does_not_stop)
{
    const uos_service_t *const tab[] = { &a, &b, &c };
    uos_svc_state_t st;

    uos_svc_state_init(&st);
    log_buf[0] = '\0';
    CHECK_EQ(uos_svc_init_all(&st, tab, 3, init_rep, NULL), 1u);
    CHECK_STR(log_buf, "init:a=0 mark:a=0 init:b=-5 mark:b=-5 init:c=0 mark:c=0 ");
    CHECK_EQ(st.rc[0], 0);
    CHECK_EQ(st.rc[1], -5);
    CHECK_EQ(st.rc[2], 0);
    CHECK_EQ(st.rc[3], UOS_SVC_NOT_STARTED);
}

TEST(positive_init_result_is_a_failure)
{
    const uos_service_t *const tab[] = { &pos };
    uos_svc_state_t st;

    uos_svc_state_init(&st);
    log_buf[0] = '\0';
    CHECK_EQ(uos_svc_init_all(&st, tab, 1, init_rep, NULL), 1u);
    CHECK_STR(log_buf, "init:pos=3 mark:pos=-3 ");
    CHECK_EQ(st.rc[0], -3);
}

TEST(tick_only_started_services)
{
    const uos_service_t *const tab[] = { &a, &b, &bare, &c };
    uos_svc_state_t st;

    uos_svc_state_init(&st);
    log_buf[0] = '\0';
    uos_svc_tick_all(&st, tab, 4, 7);
    CHECK_STR(log_buf, "");
    uos_svc_init_all(&st, tab, 4, NULL, NULL);
    log_buf[0] = '\0';
    uos_svc_tick_all(&st, tab, 4, 9);
    CHECK_STR(log_buf, "tick:a@9 tick:c@9 ");
}

TEST(shutdown_in_reverse_and_once)
{
    const uos_service_t *const tab[] = { &a, &b, &bare, &c };
    uos_svc_state_t st;

    uos_svc_state_init(&st);
    uos_svc_init_all(&st, tab, 4, NULL, NULL);
    log_buf[0] = '\0';
    CHECK_EQ(uos_svc_shutdown_all(&st, tab, 4, UOS_SHUTDOWN_HALT, down_rep, NULL), 3u);
    CHECK_STR(log_buf, "mark-down:c/1 down:c/1 mark-down:bare/1 mark-down:a/1 down:a/1 ");
    log_buf[0] = '\0';
    CHECK_EQ(uos_svc_shutdown_all(&st, tab, 4, UOS_SHUTDOWN_PANIC, down_rep, NULL), 0u);
    CHECK_STR(log_buf, "");
    /* No ticks after the shutdown. */
    uos_svc_tick_all(&st, tab, 4, 1);
    CHECK_STR(log_buf, "");
}

TEST(shutdown_before_init_tells_nobody)
{
    const uos_service_t *const tab[] = { &a, &c };
    uos_svc_state_t st;

    uos_svc_state_init(&st);
    log_buf[0] = '\0';
    CHECK_EQ(uos_svc_shutdown_all(&st, tab, 2, UOS_SHUTDOWN_PANIC, down_rep, NULL), 0u);
    CHECK_STR(log_buf, "");
    CHECK_EQ(st.down, 1);
}

TEST(table_capped_at_the_limit)
{
    const uos_service_t *tab[UOS_SVC_MAX + 1];
    uos_svc_state_t st;
    unsigned i;

    for (i = 0; i <= UOS_SVC_MAX; i++) {
        tab[i] = &bare;
    }
    uos_svc_state_init(&st);
    CHECK_EQ(uos_svc_init_all(&st, tab, UOS_SVC_MAX + 1, NULL, NULL), 0u);
    CHECK_EQ(uos_svc_shutdown_all(&st, tab, UOS_SVC_MAX + 1, 0, NULL, NULL),
             (unsigned)UOS_SVC_MAX);
}
