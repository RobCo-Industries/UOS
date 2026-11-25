/*
 * The console service: its state, its init and its maintenance commands
 * for tests and operators.
 *
 *   readline <flags> <max> [prompt words]
 *            starts a read on the loop. flags "-" or letters m (masked)
 *            and h (no history); max 1 to 255. When it ends:
 *            ev=line result=<ok|cancel> len=<n> text=<"quoted"|masked>,
 *            the quoted text with blanks, quotes and % as %XX
 *   page <short|long|empty|big>
 *            shows a built-in test text; ev=page result=done when left,
 *            result=cancel when left with Ctrl-C
 *   osk <open|close|state>
 *            the keyboard of the active read
 * Test builds only (UOS_TEST_HOOKS=1):
 *   button <key>
 *            presses and releases a key as a GPIO button would, so the
 *            tests reach the on-screen keyboard path QEMU cannot drive
 */

#include <robco/alloc.h>
#include <robco/input.h>
#include <robco/klibc.h>

#include "conint.h"
#include "conview.h"
#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"

/* Text bytes a line marker shows, so the marker fits its 224-byte buffer. */
#define MARK_TEXT_MAX   160u
#define BIG_LEN         65536u
#define LONG_LINES      120u
#define LONG_BUF        8192u

le_history_t con_history;
int con_up;

/* A command waits for its posted call: refuse a second one meanwhile. */
static int rl_pending;
static unsigned rl_flags;
static unsigned rl_max;
static char rl_prompt[CV_PROMPT_MAX + 1];

static int pg_pending;
static int pg_which;
static char long_text[LONG_BUF];
static size_t long_len;

/* ---- readline ---- */

static void on_line(int result, const char *text, unsigned len, void *ctx)
{
    char q[MARK_TEXT_MAX + 8];
    int cut;

    (void)ctx;
    if (rl_flags & CON_RL_MASK) {
        UOS_MARK("line result=%s len=%u text=masked", result == CON_OK ? "ok" : "cancel", len);
        return;
    }
    cut = cv_quote(q, sizeof(q), text, len, MARK_TEXT_MAX);
    UOS_MARK("line result=%s len=%u text=%s%s", result == CON_OK ? "ok" : "cancel", len, q,
             cut == 1 ? " cut=1" : "");
}

static void start_read(void *arg)
{
    int rc;

    (void)arg;
    rc = con_read_line(NULL, rl_prompt, rl_flags, rl_max, on_line, NULL);
    rl_pending = 0;
    if (rc != 0) {
        UOS_MARK("line result=err rc=%d", rc);
    } else {
        UOS_MARK("line state=reading max=%u flags=%u", rl_max, rl_flags);
    }
}

static int cmd_readline(int argc, char **argv)
{
    char *prompt = "";
    unsigned flags, max;
    size_t n;

    /* Parse into locals: a refused command must not change the flags the
     * active read's on_line() checks for the mask. */
    if (argc < 2) {
        return uos_maint_fail("usage readline <flags> <max> [prompt]");
    }
    if (cv_parse_flags(argv[0], &flags) != 0) {
        return uos_maint_fail("bad flags");
    }
    if (cv_parse_count(argv[1], LE_MAX, &max) != 0) {
        return uos_maint_fail("bad max");
    }
    if (!con_up) {
        return uos_maint_fail("console down");
    }
    if (con_line_active() || rl_pending) {
        return uos_maint_fail("busy");
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (argc > 2) {
        prompt = uos_mt_rejoin(argv[2], argv[argc - 1]);
    }
    n = k_strlen(prompt);
    if (n > CV_PROMPT_MAX) {
        n = CV_PROMPT_MAX;
    }
    k_memcpy(rl_prompt, prompt, n);
    rl_prompt[n] = 0;
    rl_flags = flags;
    rl_max = max;
    if (uos_post(start_read, NULL) != 0) {
        return uos_maint_fail("queue full");
    }
    rl_pending = 1;
    return 0;
}

/* ---- page ---- */

enum { PAGE_SHORT, PAGE_LONG, PAGE_EMPTY, PAGE_BIG };

static const char SHORT_TEXT[] =
    "A short test text for the pager.\n"
    "\tA tab starts this line.\n"
    "Bytes \x01 and \xc8 show as dots.\n";

/* A fixed text of LONG_LINES lines, some long enough to wrap. */
static void make_long(void)
{
    unsigned i;
    int n;

    long_len = 0;
    for (i = 1; i <= LONG_LINES; i++) {
        if (i % 10 == 0) {
            n = k_snprintf(long_text + long_len, LONG_BUF - long_len,
                           "Line %u is long on purpose so that the pager has to break it "
                           "at a blank before the right edge of the screen.\n", i);
        } else {
            n = k_snprintf(long_text + long_len, LONG_BUF - long_len,
                           "Line %u of the long test text.\n", i);
        }
        if (n < 0 || (size_t)n >= LONG_BUF - long_len) {
            break;
        }
        long_len += (size_t)n;
    }
}

static void on_page(int result, void *ctx)
{
    (void)ctx;
    UOS_MARK("page result=%s", result == CON_CANCEL ? "cancel" : "done");
}

static void start_page(void *arg)
{
    char *big = NULL;
    int rc;

    (void)arg;
    pg_pending = 0;
    switch (pg_which) {
    case PAGE_SHORT:
        rc = con_page_text(NULL, "Short test text", SHORT_TEXT, sizeof(SHORT_TEXT) - 1u,
                           on_page, NULL);
        break;
    case PAGE_LONG:
        rc = con_page_text(NULL, "Long test text", long_text, long_len, on_page, NULL);
        break;
    case PAGE_EMPTY:
        rc = con_page_text(NULL, "Empty test text", "", 0, on_page, NULL);
        break;
    default:
        big = k_malloc(BIG_LEN);
        if (big == NULL) {
            rc = CON_ERR_MEM;
            break;
        }
        {
            size_t i;

            /* 64 lines of 1023 letters and a line end each. */
            for (i = 0; i < BIG_LEN; i++) {
                big[i] = (i % 1024u) == 1023u ? '\n' : (char)('a' + (i % 26u));
            }
        }
        rc = con_page_text(NULL, "Generated 64 KiB text", big, BIG_LEN, on_page, NULL);
        k_free(big);
        break;
    }
    if (rc != 0) {
        UOS_MARK("page result=err rc=%d", rc);
    }
}

static int cmd_page(int argc, char **argv)
{
    static const char *const names[] = { "short", "long", "empty", "big" };
    int i;

    if (argc != 1) {
        return uos_maint_fail("usage page <short|long|empty|big>");
    }
    for (i = 0; i < 4; i++) {
        if (k_strcmp(argv[0], names[i]) == 0) {
            break;
        }
    }
    if (i == 4) {
        return uos_maint_fail("unknown text");
    }
    if (!con_up) {
        return uos_maint_fail("console down");
    }
    if (con_page_active() || pg_pending) {
        return uos_maint_fail("busy");
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    pg_which = i;
    if (uos_post(start_page, NULL) != 0) {
        return uos_maint_fail("queue full");
    }
    pg_pending = 1;
    return 0;
}

/* ---- osk ---- */

static void post_osk_open(void *arg)
{
    int rc = con_osk_open();

    (void)arg;
    if (rc != 0) {
        UOS_MARK("osk result=%s", rc == CON_ERR_COVERED ? "covered" : "err");
    }
}

static void post_osk_close(void *arg)
{
    int rc = con_osk_close();

    (void)arg;
    if (rc != 0) {
        UOS_MARK("osk result=%s", rc == CON_ERR_COVERED ? "covered" : "err");
    }
}

static int cmd_osk(int argc, char **argv)
{
    int open;
    unsigned layer, row, col, line_row;

    if (argc != 1) {
        return uos_maint_fail("usage osk <open|close|state>");
    }
    if (k_strcmp(argv[0], "state") == 0) {
        con_osk_state(&open, &layer, &row, &col, &line_row);
        uos_maint_reply("osk open=%d layer=%u row=%u col=%u line=%u reading=%d", open, layer,
                        row, col, line_row, con_line_active());
        return 0;
    }
    if (k_strcmp(argv[0], "open") != 0 && k_strcmp(argv[0], "close") != 0) {
        return uos_maint_fail("usage osk <open|close|state>");
    }
    if (!con_line_active()) {
        return uos_maint_fail("no line");
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (uos_post(argv[0][0] == 'o' ? post_osk_open : post_osk_close, NULL) != 0) {
        return uos_maint_fail("queue full");
    }
    return 0;
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds only: a key from the GPIO source. QEMU cannot drive the Pi's
 * GPIO inputs, so this is how the tests press a button. */
static int cmd_button(int argc, char **argv)
{
    input_event_t ev = { .type = INPUT_EV_KEY, .source = INPUT_SRC_GPIO,
                         .value = INPUT_PRESS };
    const char *name;
    unsigned code;

    if (argc != 1) {
        return uos_maint_fail("usage button <key>");
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    for (code = 1; code < INPUT_KEY_COUNT; code++) {
        name = input_key_name(code);
        if (name != NULL && k_strcmp(name, argv[0]) == 0) {
            break;
        }
    }
    if (code >= INPUT_KEY_COUNT) {
        return uos_maint_fail("unknown key");
    }
    if (!input_room(2)) {
        return uos_maint_fail("full");
    }
    ev.code = (uint16_t)code;
    (void)input_push(&ev);
    ev.value = INPUT_RELEASE;
    (void)input_push(&ev);
    return 0;
}
#endif

/* ---- The service ---- */

static int console_svc_init(void)
{
    int rc;

    le_hist_init(&con_history);
    make_long();
    rc = uos_maint_register("readline", cmd_readline, "reads a line: <flags> <max> [prompt]");
    if (rc == 0) {
        rc = uos_maint_register("page", cmd_page, "pages a test text: short long empty big");
    }
    if (rc == 0) {
        rc = uos_maint_register("osk", cmd_osk, "the keyboard: open close state");
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (rc == 0) {
        rc = uos_maint_register("button", cmd_button, "test: a GPIO button press <key>");
    }
#endif
    con_up = rc == 0;
    return rc;
}

static void console_svc_shutdown(int why)
{
    (void)why;
    con_up = 0;
}

int con_ready(void)
{
    return con_up;
}

const uos_service_t uos_svc_console = { "console", console_svc_init, NULL, console_svc_shutdown };
