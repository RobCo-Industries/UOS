/*
 * The holotape reader screen and the `holo` maintenance command. Thin:
 * the model is holoread.c, the screen's own rules are holoscan.c.
 *
 * Reads use vfs_list() and vfs_read_all() with the session's context and
 * privileges: SYS$HOLO is readable by every user in the access table, so
 * a later rule that hides a tape applies here too. With nobody logged in
 * the reader reads with no privilege and no home, as the least user.
 *
 * Maintenance command:
 *   holo open          posts holo_screen_start()
 *   holo list          one line per tape: i= file= err= title=
 *   holo pick <n>      moves the selection to item n of the shown list, then ENTER
 *   holo state         state= scan= tapes= total= sel= entry= open= msg= pending=
 * Test builds only (UOS_TEST_HOOKS=1):
 *   holo hog           takes the heap until no tape fits, or gives it back
 * Markers:
 *   holo op=open, holo op=exit
 *   holo op=tape i= file= result=<ok|hf error> bytes= us=      one per scanned tape
 *   holo op=scan result=<list|empty|nocard|nofolder|error> total= kept= damaged= rc=
 *   holo op=load file= result=<ok|nomem|nocard|gone|damaged> why= bytes= us= ticks=
 *   holo op=show tape= entry= bytes=
 *   holo op=close file=
 */

#include <robco/alloc.h>
#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/timer.h>

#include "con.h"
#include "holoscan.h"
#include "holoscr.h"
#include "sess.h"
#include "uos.h"
#include "uos_maint.h"
#include "uos_text.h"
#include "uos_service.h"
#include "vfs.h"

#define HOLO_SPEC       "SYS$HOLO:*.HTP"

typedef enum {
    ST_CLOSED = 0,
    ST_SCAN,
    ST_LIST,            /* the model's tape or entry list */
    ST_LOADING,
    ST_READING,
} holo_st_t;

static const char *const st_names[] = { "closed", "scan", "list", "loading", "reading" };

static struct {
    holo_st_t st;
    hs_scan_t scan;
    int scan_rc;
    uint32_t total;             /* tapes the listing matched */
    uint32_t kept;              /* tapes in the list */
    uint32_t next;              /* the scan's next tape */
    vfs_entry_t ent[HF_TAPES_MAX];
    hf_tape_info_t tapes[HF_TAPES_MAX];
    hf_reader_t r;
    char *buf;                  /* the open tape, from the heap */
    uint32_t open;
    char msg[HF_LINE_SIZE];
    holo_done_fn done;
    void *ctx;
    unsigned posted;            /* maintenance calls not yet run */
    uint32_t pick;
} hs;

static uos_screen_t holo_screen;

/* ---- Reads ---- */

static void user_ctx(vp_ctx_t *ctx, uint32_t *privs)
{
    if (sess_vp_ctx(ctx) != 0) {
        vp_ctx_init(ctx, NULL);
    }
    *privs = sess_privs();
}

static int read_tape(const char *file, char **buf, size_t *len, uint32_t *us)
{
    char spec[24];
    vp_ctx_t ctx;
    uint32_t privs;
    uint64_t t0 = timer_now_us();
    int rc;

    user_ctx(&ctx, &privs);
    k_snprintf(spec, sizeof(spec), "SYS$HOLO:%s", file);
    rc = vfs_read_all(&ctx, privs, spec, NULL, HF_TAPE_MAX, buf, len);
    *us = (uint32_t)(timer_now_us() - t0);
    return rc;
}

static void entry_file(const vfs_entry_t *e, char out[HF_FILE_MAX + 1])
{
    k_snprintf(out, HF_FILE_MAX + 1, "%.8s.%.3s", e->name, e->type);
}

/* ---- Drawing ---- */

static void put_row(unsigned row, const char *text, unsigned attr)
{
    unsigned n = (unsigned)k_strlen(text);
    unsigned cols;

    if (!uos.fb_ok || row >= con_text_rows()) {
        return;
    }
    cols = console_cols();
    if (n > cols - 1) {
        n = cols - 1;
    }
    console_set_cursor(0, row);
    console_set_attr(attr);
    console_write(text, n);
    console_set_attr(CONSOLE_ATTR_NORMAL);
    for (; n < cols - 1; n++) {
        console_putc(' ');
    }
}

static void holo_draw(uos_screen_t *s)
{
    static char lines[HF_ROWS][HF_LINE_SIZE];
    unsigned i;

    (void)s;
    if (!uos.fb_ok) {
        return;
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_clear();
    if (hs.st == ST_SCAN) {
        put_row(0, "UOS HOLOTAPE READER", CONSOLE_ATTR_BRIGHT);
        put_row(3, "  Scanning SYS$HOLO ...", CONSOLE_ATTR_NORMAL);
        con_status_redraw();
        return;
    }
    if (hf_layout(&hs.r, lines, HF_ROWS) != HF_OK) {
        con_status_redraw();
        return;
    }
    hs_apply(lines, hs.r.screen == HF_SCR_TAPES, hs.scan, hs.kept, hs.total, hs.msg);
    for (i = 0; i < HF_ROWS; i++) {
        put_row(i, lines[i], i == 0 ? CONSOLE_ATTR_BRIGHT :
                             lines[i][0] == '>' ? CONSOLE_ATTR_INVERSE : CONSOLE_ATTR_NORMAL);
    }
    con_status_redraw();
}

/* ---- Scan ---- */

static void later(uos_call_fn fn)
{
    uos_timer_id_t id;

    if (uos_post(fn, NULL) != 0 && uos_timer_start(1, 0, fn, NULL, &id) != 0) {
        uos_set_error("holo");
    }
}

static void scan_done(void)
{
    uint32_t i, damaged = 0;

    for (i = 0; i < hs.kept; i++) {
        damaged += hs.tapes[i].err != HF_OK;
    }
    (void)hf_reader_init(&hs.r, hs.tapes, hs.kept);
    hs.st = ST_LIST;
    UOS_MARK("holo op=scan result=%s total=%u kept=%u damaged=%u rc=%d", hs_scan_name(hs.scan),
             (unsigned)hs.total, (unsigned)hs.kept, (unsigned)damaged, hs.scan_rc);
    uos_screen_invalidate();
}

static void scan_step(void *arg)
{
    hf_tape_info_t *ti;
    hf_tape_t t;
    char *buf = NULL;
    size_t len = 0;
    uint32_t us = 0;
    int rc;

    (void)arg;
    if (hs.st != ST_SCAN) {
        return;
    }
    if (hs.next >= hs.kept) {
        scan_done();
        return;
    }
    ti = &hs.tapes[hs.next];
    entry_file(&hs.ent[hs.next], ti->file);
    ti->title[0] = '\0';
    rc = read_tape(ti->file, &buf, &len, &us);
    if (rc == VFS_OK) {
        ti->err = hf_open(&t, buf, (uint32_t)len);
        if (ti->err == HF_OK) {
            k_snprintf(ti->title, sizeof(ti->title), "%s", hf_title(&t));
        }
    } else {
        ti->err = hs_read_err(rc);
    }
    vfs_free(buf);
    UOS_MARK("holo op=tape i=%u file=%s result=%s bytes=%u us=%u rc=%d", (unsigned)hs.next,
             ti->file, hf_err_name(ti->err), (unsigned)len, (unsigned)us, rc);
    hs.next++;
    later(scan_step);
}

static void scan_begin(void)
{
    vp_ctx_t ctx;
    uint32_t privs;
    size_t count = 0, total = 0;

    user_ctx(&ctx, &privs);
    hs.scan_rc = vfs_list(&ctx, privs, HOLO_SPEC, hs.ent, HF_TAPES_MAX, &count, &total);
    hs.scan = hs_scan_result(hs.scan_rc, total);
    if (hs.scan != HS_SCAN_LIST) {
        count = total = 0;
    }
    hs.kept = (uint32_t)count;
    hs.total = (uint32_t)total;
    hs.next = 0;
    hs.st = ST_SCAN;
    uos_screen_invalidate();
    later(scan_step);
}

/* ---- The model's actions ---- */

static void close_tape(void)
{
    if (hs.buf != NULL) {
        UOS_MARK("holo op=close file=%s", hs.tapes[hs.open].file);
        vfs_free(hs.buf);
        hs.buf = NULL;
    }
}

static void act(const hf_action_t *a);

static void pager_done(int result, void *ctx)
{
    hf_action_t a;

    (void)result;
    (void)ctx;
    if (hs.st != ST_READING) {
        return;
    }
    hs.st = ST_LIST;
    (void)hf_reader_event(&hs.r, HF_EV_BACK, &a);
    act(&a);
    uos_screen_invalidate();
}

static void load_step(void *arg)
{
    size_t len = 0;
    uint32_t us = 0, t0 = uos_ticks();
    hs_load_t res;
    char *buf = NULL;
    int rc, hrc = HF_OK;
    const char *file = hs.tapes[hs.open].file;

    (void)arg;
    if (hs.st != ST_LOADING) {
        return;
    }
    rc = read_tape(file, &buf, &len, &us);
    if (rc == VFS_OK) {
        hrc = hf_reader_loaded(&hs.r, buf, (uint32_t)len);
    } else {
        hrc = hs_read_err(rc);
    }
    res = hs_load_result(rc, hrc);
    UOS_MARK("holo op=load file=%s result=%s why=%s bytes=%u us=%u ticks=%u", file,
             hs_load_name(res), rc == VFS_OK ? hf_err_name(hrc) : vfs_strerror(rc),
             (unsigned)len, (unsigned)us, (unsigned)(uos_ticks() - t0));
    hs.st = ST_LIST;
    k_snprintf(hs.msg, sizeof(hs.msg), "%s", hs_load_text(res));
    if (res == HS_LOAD_OK) {
        hs.buf = buf;
    } else {
        vfs_free(buf);
        if (res == HS_LOAD_DAMAGED) {
            hs.tapes[hs.open].err = hrc;
        }
    }
    uos_screen_invalidate();
}

static void act(const hf_action_t *a)
{
    int rc;

    switch (a->kind) {
    case HF_ACT_LOAD:
        hs.open = a->tape;
        hs.st = ST_LOADING;
        hs_loading_text(hs.tapes[a->tape].file, hs.msg, sizeof(hs.msg));
        uos_screen_invalidate();
        /* The draw runs before the next pass: the loading line shows
         * while the read holds the loop. */
        later(load_step);
        break;
    case HF_ACT_REFUSED:
        k_snprintf(hs.msg, sizeof(hs.msg), "%s", hs_load_text(HS_LOAD_DAMAGED));
        UOS_MARK("holo op=load file=%s result=refused", hs.tapes[a->tape].file);
        break;
    case HF_ACT_SHOW:
        rc = con_page_text(a->title, a->text, a->len, pager_done, NULL);
        UOS_MARK("holo op=show tape=%s entry=%u bytes=%u rc=%d", hs.tapes[hs.open].file,
                 (unsigned)hs.r.sel_entry, (unsigned)a->len, rc);
        if (rc == 0) {
            hs.st = ST_READING;
        } else {
            hf_action_t b;

            (void)hf_reader_event(&hs.r, HF_EV_BACK, &b);
            k_snprintf(hs.msg, sizeof(hs.msg), "%s", hs_load_text(HS_LOAD_NOMEM));
        }
        break;
    case HF_ACT_CLOSE:
        close_tape();
        break;
    case HF_ACT_EXIT:
        (void)uos_screen_pop();
        break;
    default:
        break;
    }
}

static void model_event(hf_event_t ev)
{
    hf_action_t a;

    if (hs.st != ST_LIST) {
        return;
    }
    hs.msg[0] = '\0';
    if (hf_reader_event(&hs.r, ev, &a) == HF_OK) {
        act(&a);
    }
    if (hs.st != ST_CLOSED) {
        uos_screen_invalidate();
    }
}

/* ---- The screen ---- */

static void holo_enter(uos_screen_t *s)
{
    (void)s;
    UOS_MARK("holo op=open");
    scan_begin();
}

static void holo_leave(uos_screen_t *s)
{
    holo_done_fn done = hs.done;
    void *ctx = hs.ctx;

    (void)s;
    close_tape();
    hs.st = ST_CLOSED;
    hs.done = NULL;
    hs.msg[0] = '\0';
    UOS_MARK("holo op=exit");
    if (done != NULL) {
        done(ctx);
    }
}

static int holo_event(uos_screen_t *s, const uos_event_t *ev)
{
    (void)s;
    if (ev->type == UOS_EV_KEY && ev->value != INPUT_RELEASE) {
        switch (ev->code) {
        case INPUT_KEY_UP:
            model_event(HF_EV_UP);
            break;
        case INPUT_KEY_DOWN:
            model_event(HF_EV_DOWN);
            break;
        case INPUT_KEY_SELECT:
        case INPUT_KEY_RIGHT:
            model_event(HF_EV_ENTER);
            break;
        case INPUT_KEY_BACK:
        case INPUT_KEY_LEFT:
            model_event(HF_EV_BACK);
            break;
        default:
            break;
        }
    }
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

static uos_screen_t holo_screen = {
    .name = "holo",
    .enter = holo_enter,
    .leave = holo_leave,
    .event = holo_event,
    .draw = holo_draw,
};

int holo_screen_start(holo_done_fn done, void *ctx)
{
    if (hs.st != ST_CLOSED) {
        return HOLO_ERR_BUSY;
    }
    hs.done = done;
    hs.ctx = ctx;
    hs.msg[0] = '\0';
    if (uos_screen_push(&holo_screen) != 0) {
        hs.done = NULL;
        return HOLO_ERR_SCREEN;
    }
    return 0;
}

int holo_screen_active(void)
{
    return hs.st != ST_CLOSED;
}

/* ---- The maintenance command ---- */

static void post_open(void *arg)
{
    int rc;

    (void)arg;
    hs.posted--;
    rc = holo_screen_start(NULL, NULL);
    if (rc != 0) {
        UOS_MARK("holo result=%s", rc == HOLO_ERR_BUSY ? "busy" : "screen");
    }
}

static void post_pick(void *arg)
{
    uint32_t want = hs.pick, *sel, n;

    (void)arg;
    hs.posted--;
    if (hs.st != ST_LIST) {
        UOS_MARK("holo result=busy");
        return;
    }
    sel = hs.r.screen == HF_SCR_TAPES ? &hs.r.sel_tape : &hs.r.sel_entry;
    n = hs.r.screen == HF_SCR_TAPES ? hs.r.ntapes : hs.r.tape.count;
    if (want >= n) {
        UOS_MARK("holo result=range");
        return;
    }
    while (*sel != want) {
        model_event(*sel < want ? HF_EV_DOWN : HF_EV_UP);
    }
    model_event(HF_EV_ENTER);
}

static int post(uos_call_fn fn)
{
    if (uos_post(fn, NULL) != 0) {
        return uos_maint_fail("full");
    }
    hs.posted++;
    return 0;
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds only: the nm test proves the shipped image has none of this.
 * Each block holds the next one's address in its first word. */
static void *hog_list;

static void __attribute__((noinline)) uos_test_holo_hog(void)
{
    k_heap_stats_t st;
    void *p;
    size_t n;

    if (hog_list != NULL) {
        while (hog_list != NULL) {
            p = hog_list;
            hog_list = *(void **)p;
            k_free(p);
        }
        uos_maint_reply("hog freed");
        return;
    }
    for (;;) {
        k_heap_stats(&st);
        if (st.largest_free < HF_TAPE_MAX) {
            break;
        }
        n = st.largest_free - HF_TAPE_MAX / 2;
        p = k_malloc(n);
        if (p == NULL) {
            break;
        }
        *(void **)p = hog_list;
        hog_list = p;
    }
    uos_maint_reply("hog largest_free=%u", (unsigned)st.largest_free);
}
#endif

static int cmd_holo(int argc, char **argv)
{
    char word[HF_TITLE_MAX * 3 + 1];
    uint32_t i;

    if (argc == 1 && k_strcmp(argv[0], "state") == 0) {
        uos_maint_reply("holo state=%s scan=%s screen=%s tapes=%u total=%u sel=%u entry=%u "
                        "open=%s msg=%s pending=%u",
                        st_names[hs.st], hs_scan_name(hs.scan),
                        hs.st == ST_CLOSED || hs.st == ST_SCAN ? "none" :
                        hs.r.screen == HF_SCR_TAPES ? "tapes" :
                        hs.r.screen == HF_SCR_ENTRIES ? "entries" : "reading",
                        (unsigned)hs.kept, (unsigned)hs.total, (unsigned)hs.r.sel_tape,
                        (unsigned)hs.r.sel_entry, hs.buf != NULL ? hs.tapes[hs.open].file : "none",
                        hs.msg[0] != '\0' ? "yes" : "none",
                        input_pending() + hs.posted +
                            (hs.st == ST_SCAN || hs.st == ST_LOADING ? 1u : 0u));
        return 0;
    }
    if (argc == 1 && k_strcmp(argv[0], "list") == 0) {
        for (i = 0; hs.st != ST_CLOSED && hs.st != ST_SCAN && i < hs.kept; i++) {
            uos_quote_word(hs.tapes[i].title, word, sizeof(word));
            uos_maint_reply("tape i=%u file=%s err=%s title=%s", (unsigned)i, hs.tapes[i].file,
                            hf_err_name(hs.tapes[i].err), word[0] != '\0' ? word : "-");
        }
        return 0;
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (argc == 1 && k_strcmp(argv[0], "hog") == 0) {
        uos_test_holo_hog();
        return 0;
    }
#endif
    if (argc == 1 && k_strcmp(argv[0], "open") == 0) {
        return post(post_open);
    }
    if (argc == 2 && k_strcmp(argv[0], "pick") == 0) {
        if (uos_dec_u32(argv[1], HF_TAPES_MAX, &hs.pick) != 0) {
            return uos_maint_fail("usage holo pick <n>");
        }
        return post(post_pick);
    }
    return uos_maint_fail("usage holo open|list|pick <n>|state");
}

static int holo_svc_init(void)
{
    return uos_maint_register("holo", cmd_holo, "the holotape reader: open list pick state");
}

static void holo_svc_shutdown(int why)
{
    (void)why;
}

const uos_service_t uos_svc_holo = { "holo", holo_svc_init, NULL, holo_svc_shutdown };
