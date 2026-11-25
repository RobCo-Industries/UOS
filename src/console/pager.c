/*
 * The pager screen: a full-screen view of a text with the pure pager of
 * src/tui/pager.c. Row 0 is the title, rows 1 to 28 the body, row 29 the
 * status row. The text is copied into the kernel heap with every byte
 * outside printable ASCII as '.', and the pager's index too; both are
 * freed when the person leaves.
 */

#include <robco/alloc.h>
#include <robco/console.h>
#include <robco/klibc.h>

#include "conint.h"
#include "conview.h"
#include "tui.h"
#include "uos.h"

#define TITLE_MAX   78u

static struct {
    int active;
    pg_t pg;
    char *text;
    uint32_t *marks;
    char title[TITLE_MAX + 1];
    con_done_fn cb;
    void *ctx;
} pv;

static void pv_draw(uos_screen_t *s)
{
    char line[PG_MAX_WIDTH + 1];
    char st[CV_STATUS_BUF];
    uint32_t first, count, i;
    unsigned rows = con_rows();
    int n;

    (void)s;
    if (!uos.fb_ok) {
        return;
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_clear();
    con_put(1, 0, pv.title, k_strlen(pv.title), CONSOLE_ATTR_BRIGHT, 0);
    pg_visible(&pv.pg, &first, &count);
    if (pg_lines(&pv.pg) == 0) {
        con_put(1, 1, "(no text)", 9, CONSOLE_ATTR_DIM, 0);
    }
    for (i = 0; i < count; i++) {
        n = pg_line(&pv.pg, first + i, line, sizeof(line));
        if (n > 0) {
            con_put(1, 1 + i, line, (size_t)n, CONSOLE_ATTR_NORMAL, 0);
        }
    }
    n = cv_pager_status(&pv.pg, st, sizeof(st));
    if (n > 0) {
        con_put(0, rows - 1u, st, (size_t)n, CONSOLE_ATTR_INVERSE, 1);
    }
    console_set_cursor(0, rows - 1u);
}

static void release(void)
{
    k_free(pv.text);
    k_free(pv.marks);
    pv.text = NULL;
    pv.marks = NULL;
    pv.active = 0;
}

static void report(void)
{
    uint32_t first, count;

    pg_visible(&pv.pg, &first, &count);
    UOS_MARK("page top=%u shown=%u lines=%u", (unsigned)pv.pg.top, (unsigned)count,
             (unsigned)pg_lines(&pv.pg));
}

static int pv_event(uos_screen_t *s, const uos_event_t *ev)
{
    con_done_fn cb;
    void *ctx;
    unsigned key;
    uint32_t top;

    (void)s;
    if (ev->type == UOS_EV_KEY) {
        key = ev->code;
    } else if (ev->type == UOS_EV_CHAR) {
        key = TUI_CHAR(ev->code);
    } else {
        return 0;
    }
    top = pv.pg.top;
    if (pg_key(&pv.pg, key) == PG_EXIT) {
        cb = pv.cb;
        ctx = pv.ctx;
        release();
        (void)uos_screen_pop();
        if (cb != NULL) {
            cb(CON_OK, ctx);
        }
        return 1;
    }
    if (pv.pg.top != top) {
        report();
        uos_screen_invalidate();
    }
    return 1;
}

/* Popped by another caller: the text is freed and nobody is called back. */
static void pv_leave(uos_screen_t *s)
{
    (void)s;
    if (pv.active) {
        release();
        UOS_MARK("page result=abort");
    }
}

static uos_screen_t pv_screen = { "pager", NULL, pv_leave, pv_event, pv_draw, NULL };

int con_page_active(void)
{
    return pv.active;
}

int con_page_text(const char *title, const char *text, size_t len, con_done_fn cb, void *ctx)
{
    uint32_t cap;
    size_t n;

    if (!con_up) {
        return CON_ERR_DOWN;
    }
    if (pv.active) {
        return CON_ERR_BUSY;
    }
    if ((text == NULL && len > 0) || len > CON_PAGE_MAX) {
        return CON_ERR_ARG;
    }
    cap = PG_MARKS_FOR(len);
    pv.marks = k_malloc(cap * sizeof(uint32_t));
    pv.text = k_malloc(len > 0 ? len : 1u);
    if (pv.marks == NULL || pv.text == NULL) {
        release();
        return CON_ERR_MEM;
    }
    cv_printable(pv.text, text, len);
    if (pg_init(&pv.pg, pv.text, len, TUI_TEXT_COLS, con_rows() - 2u, pv.marks, cap) != PG_OK) {
        release();
        return CON_ERR_ARG;
    }
    if (title == NULL) {
        title = "";
    }
    n = k_strlen(title);
    if (n > TITLE_MAX) {
        n = TITLE_MAX;
    }
    k_memcpy(pv.title, title, n);
    pv.title[n] = 0;
    pv.cb = cb;
    pv.ctx = ctx;
    if (uos_screen_push(&pv_screen) != 0) {
        release();
        return CON_ERR_SCREEN;
    }
    pv.active = 1;
    report();
    return 0;
}
