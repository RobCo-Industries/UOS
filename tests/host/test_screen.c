/* Host tests of src/kernel/screen.c: the screen stack rules. */

#include "rc_test.h"

#include "uos_screen.h"

static char log_buf[256];

static void note(const char *what, const uos_screen_t *s)
{
    size_t n = strlen(log_buf);

    snprintf(log_buf + n, sizeof(log_buf) - n, "%s:%s ", what, s->name);
}

static void enter(uos_screen_t *s) { note("enter", s); }
static void leave(uos_screen_t *s) { note("leave", s); }
static void draw(uos_screen_t *s) { note("draw", s); }

/* ctx: 1 uses every event, 0 passes it down. */
static int event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)ev;
    note("event", s);
    return s->ctx != NULL;
}

#define SCREEN(var, nm, uses) \
    uos_screen_t var = { nm, enter, leave, event, draw, (void *)(uses), 0 }

TEST(push_enters_and_draws_the_top)
{
    uos_sstack_t ss;
    SCREEN(a, "a", 1);
    SCREEN(b, "b", 1);

    uos_ss_init(&ss);
    log_buf[0] = '\0';
    CHECK_EQ(uos_ss_push(&ss, &a), 0);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_EQ(uos_ss_draw(&ss), 0);
    CHECK_EQ(uos_ss_push(&ss, &b), 0);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_STR(log_buf, "enter:a draw:a enter:b draw:b ");
    CHECK(uos_ss_top(&ss) == &b);
}

TEST(pop_leaves_and_redraws_the_screen_below)
{
    uos_sstack_t ss;
    SCREEN(a, "a", 1);
    SCREEN(b, "b", 1);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &a);
    uos_ss_push(&ss, &b);
    uos_ss_draw(&ss);
    log_buf[0] = '\0';
    CHECK_EQ(uos_ss_pop(&ss), 0);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_STR(log_buf, "leave:b draw:a ");
    CHECK_EQ(uos_ss_pop(&ss), UOS_SCREEN_ERR_LAST);
    CHECK(uos_ss_top(&ss) == &a);
}

TEST(replace_leaves_then_enters)
{
    uos_sstack_t ss;
    SCREEN(a, "a", 1);
    SCREEN(b, "b", 1);

    uos_ss_init(&ss);
    log_buf[0] = '\0';
    CHECK_EQ(uos_ss_replace(&ss, &a), 0);
    CHECK_EQ(uos_ss_replace(&ss, &b), 0);
    CHECK_EQ(ss.depth, 1u);
    CHECK_STR(log_buf, "enter:a leave:a enter:b ");
    CHECK_EQ(uos_ss_replace(&ss, &b), UOS_SCREEN_ERR_ARG);
}

TEST(events_pass_down_until_used)
{
    uos_sstack_t ss;
    uos_event_t ev = { .type = UOS_EV_KEY };
    SCREEN(a, "a", 1);
    SCREEN(b, "b", 0);
    SCREEN(c, "c", 0);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &a);
    uos_ss_push(&ss, &b);
    uos_ss_push(&ss, &c);
    log_buf[0] = '\0';
    CHECK_EQ(uos_ss_event(&ss, &ev), 1);
    CHECK_STR(log_buf, "event:c event:b event:a ");
    a.ctx = NULL;
    log_buf[0] = '\0';
    CHECK_EQ(uos_ss_event(&ss, &ev), 0);
    CHECK_STR(log_buf, "event:c event:b event:a ");
}

static uos_sstack_t *popper_ss;

static int pop_self(uos_screen_t *s, const uos_event_t *ev)
{
    (void)ev;
    note("event", s);
    uos_ss_pop(popper_ss);
    return 0;
}

TEST(a_screen_may_pop_itself_in_its_handler)
{
    uos_sstack_t ss;
    uos_event_t ev = { .type = UOS_EV_KEY };
    SCREEN(a, "a", 1);
    uos_screen_t b = { "b", NULL, leave, pop_self, NULL, NULL, 0 };

    uos_ss_init(&ss);
    popper_ss = &ss;
    uos_ss_push(&ss, &a);
    uos_ss_push(&ss, &b);
    log_buf[0] = '\0';
    /* The stack changed: the event is spent, the screen below does not get it. */
    CHECK_EQ(uos_ss_event(&ss, &ev), 0);
    CHECK_STR(log_buf, "event:b leave:b ");
    CHECK(uos_ss_top(&ss) == &a);
}

TEST(depth_limit_and_bad_arguments)
{
    uos_sstack_t ss;
    uos_screen_t s[UOS_SCREEN_DEPTH + 1];
    unsigned i;

    memset(s, 0, sizeof(s));
    uos_ss_init(&ss);
    for (i = 0; i < UOS_SCREEN_DEPTH; i++) {
        s[i].name = "s";
        CHECK_EQ(uos_ss_push(&ss, &s[i]), 0);
    }
    CHECK_EQ(uos_ss_push(&ss, &s[UOS_SCREEN_DEPTH]), UOS_SCREEN_ERR_FULL);
    CHECK_EQ(uos_ss_push(&ss, &s[0]), UOS_SCREEN_ERR_ARG);
    CHECK_EQ(uos_ss_push(&ss, NULL), UOS_SCREEN_ERR_ARG);
    CHECK_EQ(uos_ss_push(NULL, &s[0]), UOS_SCREEN_ERR_ARG);
    CHECK_EQ(uos_ss_replace(&ss, &s[0]), UOS_SCREEN_ERR_ARG);
    CHECK_EQ(uos_ss_pop(NULL), UOS_SCREEN_ERR_ARG);
    /* NULL callbacks are allowed. */
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_EQ(uos_ss_event(&ss, &(uos_event_t){ .type = UOS_EV_KEY }), 0);
}

TEST(empty_stack)
{
    uos_sstack_t ss;

    uos_ss_init(&ss);
    CHECK(uos_ss_top(&ss) == NULL);
    CHECK_EQ(uos_ss_pop(&ss), UOS_SCREEN_ERR_LAST);
    CHECK_EQ(uos_ss_draw(&ss), 0);
    uos_ss_invalidate(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 0);
    CHECK(uos_ss_top(NULL) == NULL);
}

TEST(invalidate_draws_again)
{
    uos_sstack_t ss;
    SCREEN(a, "a", 1);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &a);
    uos_ss_draw(&ss);
    log_buf[0] = '\0';
    uos_ss_invalidate(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_STR(log_buf, "draw:a ");
}

#define FULL(var, nm) \
    uos_screen_t var = { nm, enter, leave, event, draw, (void *)1, 1 }

/* login (full), read line, reader (full), refusal (full), pause line */
TEST(pop_redraws_from_the_highest_full_screen_up)
{
    uos_sstack_t ss;
    FULL(login, "login");
    SCREEN(line, "line", 1);
    FULL(reader, "reader");
    FULL(game, "game");
    SCREEN(pause, "pause", 1);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &login);
    uos_ss_push(&ss, &line);
    uos_ss_push(&ss, &reader);
    uos_ss_push(&ss, &game);
    uos_ss_push(&ss, &pause);
    uos_ss_draw(&ss);
    log_buf[0] = '\0';
    /* The pause line closes: the game it sat on repaints, the game only. */
    uos_ss_pop(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    uos_ss_pop(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_STR(log_buf, "leave:pause draw:game leave:game draw:reader ");
    log_buf[0] = '\0';
    /* The reader closes over the read line: the login first, then the line. */
    uos_ss_pop(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 2);
    CHECK_STR(log_buf, "leave:reader draw:login draw:line ");
}

TEST(push_and_invalidate_draw_the_top_only)
{
    uos_sstack_t ss;
    FULL(login, "login");
    SCREEN(line, "line", 1);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &login);
    uos_ss_push(&ss, &line);
    log_buf[0] = '\0';
    CHECK_EQ(uos_ss_draw(&ss), 1);
    uos_ss_invalidate(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 1);
    CHECK_STR(log_buf, "draw:line draw:line ");
}

TEST(no_full_screen_redraws_from_the_bottom)
{
    uos_sstack_t ss;
    SCREEN(a, "a", 1);
    SCREEN(b, "b", 1);
    SCREEN(c, "c", 1);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &a);
    uos_ss_push(&ss, &b);
    uos_ss_push(&ss, &c);
    uos_ss_draw(&ss);
    log_buf[0] = '\0';
    uos_ss_pop(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 2);
    CHECK_STR(log_buf, "leave:c draw:a draw:b ");
}

TEST(expose_and_replace_redraw_the_visible_screens)
{
    uos_sstack_t ss;
    FULL(login, "login");
    SCREEN(line, "line", 1);
    SCREEN(other, "other", 1);

    uos_ss_init(&ss);
    uos_ss_push(&ss, &login);
    uos_ss_push(&ss, &line);
    uos_ss_draw(&ss);
    log_buf[0] = '\0';
    uos_ss_expose(&ss);
    CHECK_EQ(uos_ss_draw(&ss), 2);
    CHECK_EQ(uos_ss_draw(&ss), 0);
    uos_ss_replace(&ss, &other);
    CHECK_EQ(uos_ss_draw(&ss), 2);
    CHECK_STR(log_buf, "draw:login draw:line leave:line enter:other draw:login draw:other ");
}
