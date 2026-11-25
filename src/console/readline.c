/*
 * The "read a line" screen: the line editor and the on-screen keyboard
 * on the kernel's screen stack. Decisions live in lineedit.c, conview.c
 * and osk.c; this file draws and routes events.
 *
 * The screen sits on top of the caller's screen and paints only its own
 * rows: the input row, and the keyboard's rows while it is open. It is a
 * partial screen. The library console keeps no saved layers, so when the
 * keyboard closes this screen calls uos_screen_expose(): the stack then
 * redraws the screens below, bottom-up, before this one.
 */

#include <robco/console.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "conint.h"
#include "conview.h"
#include "osk.h"
#include "uos.h"

static struct {
    int active;
    le_t le;
    char buf[LE_MAX + 1];
    char prompt[CV_PROMPT_MAX + 1];
    unsigned plen;
    unsigned want_row;          /* the row the read started on */
    unsigned row;               /* the row drawn now */
    unsigned flags;
    osk_t osk;
    con_line_fn cb;
    void *ctx;
} rl;

static void place_cursor(void)
{
    if (uos.fb_ok) {
        console_set_cursor(rl.plen + le_cursor_col(&rl.le), rl.row);
    }
}

/* The input row from window column `from`, or the whole row with the prompt. */
static void paint_line(int whole, unsigned from)
{
    char disp[LE_MAX_WIDTH + 1];
    int w = le_display(&rl.le, disp, sizeof(disp), NULL);

    if (w < 0) {
        return;
    }
    if (whole) {
        con_put(0, rl.row, rl.prompt, rl.plen, CONSOLE_ATTR_NORMAL, 0);
        from = 0;
    }
    con_put(rl.plen + from, rl.row, disp + from, (size_t)w - from, CONSOLE_ATTR_NORMAL, 1);
    place_cursor();
}

/* The line on the PL011, as le_serial() says: a person on a serial
 * terminal sees what they type. Masked lines show '*'. The keyboard's
 * rows are never echoed. */
static void echo(unsigned what)
{
    char out[LE_SERIAL_SIZE];
    int n = le_serial(&rl.le, rl.prompt, what, out, sizeof(out));

    if (n > 0) {
        uart_puts(out);
        uos_serial_open_line(1);
    }
}

static void paint_osk(void)
{
    char text[OSK_ROW_BUF];
    unsigned first = 0, count = 0, r, i;

    if (osk_screen_rows(&rl.osk, con_rows(), &first, &count) != 0) {
        count = 0;
    }
    rl.row = cv_input_row(rl.want_row, first, count);
    for (r = 0; r < count; r++) {
        if (osk_row_text(&rl.osk, r, text, sizeof(text)) == OSK_TEXT_COLS) {
            con_put(0, first + r, text, OSK_TEXT_COLS, CONSOLE_ATTR_NORMAL, 1);
        }
    }
    for (i = 0; i < OSK_KEYS && count > 0; i++) {
        osk_cell_t cell;

        if (osk_key_cell(&rl.osk, i, &cell) == 0 && cell.highlighted &&
            osk_row_text(&rl.osk, cell.y, text, sizeof(text)) == OSK_TEXT_COLS) {
            con_put(cell.x, first + cell.y, text + cell.x, cell.w, CONSOLE_ATTR_INVERSE, 0);
        }
    }
}

static void rl_draw(uos_screen_t *s)
{
    (void)s;
    paint_osk();
    paint_line(1, 0);
    echo(LE_RD_ALL);
    if (uos.fb_ok) {
        console_cursor_show(1);
    }
}

static int feed(const input_event_t *ev)
{
    le_redraw_t rd;
    int r = le_event(&rl.le, ev, &rd);

    if (r != LE_EDITING) {
        return r;
    }
    echo(rd.what);
    switch (rd.what) {
    case LE_RD_CURSOR:
        place_cursor();
        break;
    case LE_RD_FROM:
        paint_line(0, rd.from);
        break;
    case LE_RD_ALL:
        paint_line(1, 0);
        break;
    default:
        break;
    }
    return r;
}

static uos_screen_t rl_screen;

/* LE_ACCEPT, LE_CANCEL, or RL_ALT for the alt key. */
#define RL_ALT  100

static void finish(int r)
{
    con_line_fn cb = rl.cb;
    void *ctx = rl.ctx;
    int result = r == LE_ACCEPT ? CON_OK : r == RL_ALT ? CON_ALT : CON_CANCEL;

    if (result != CON_OK) {
        rl.buf[0] = 0;
    }
    osk_close(&rl.osk);
    if (uos.fb_ok) {
        console_cursor_show(0);
    }
    /* The serial line ends with the read. */
    uos_serial_break();
    rl.active = 0;
    (void)uos_screen_pop(&rl_screen);
    if (cb != NULL) {
        cb(result, rl.buf, (unsigned)k_strlen(rl.buf), ctx);
    }
    /* A new read started from the callback owns the buffer now. */
    if (!rl.active) {
        k_memset(rl.buf, 0, sizeof(rl.buf));
    }
}

static int rl_event(uos_screen_t *s, const uos_event_t *ev)
{
    input_event_t in;
    input_event_t out;
    int was_open = osk_is_open(&rl.osk);
    int r = LE_EDITING;

    (void)s;
    if (ev->type != UOS_EV_KEY && ev->type != UOS_EV_CHAR) {
        return 0;
    }
    in.type = ev->type == UOS_EV_KEY ? INPUT_EV_KEY : INPUT_EV_CHAR;
    in.source = ev->source;
    in.code = ev->code;
    in.value = ev->value;
    in.t_ms = 0;

    if (cv_alt_key(&in, (rl.flags & CON_RL_ALTKEY) != 0, le_len(&rl.le), was_open)) {
        finish(RL_ALT);
        return 1;
    }
    if (cv_opens_osk(&in, was_open)) {
        (void)con_osk_open();
        return 1;
    }
    if (was_open) {
        if (osk_handle(&rl.osk, &in) == OSK_IGNORED) {
            r = feed(&in);
        }
        while (r == LE_EDITING && osk_pop(&rl.osk, &out)) {
            r = feed(&out);
        }
    } else {
        r = feed(&in);
    }
    if (r != LE_EDITING) {
        if (was_open) {
            UOS_MARK("osk state=closed");
        }
        finish(r);
        return 1;
    }
    if (was_open && !osk_is_open(&rl.osk)) {
        UOS_MARK("osk state=closed");
        uos_screen_expose();
    } else if (was_open) {
        paint_osk();
        paint_line(1, 0);
    }
    return 1;
}

/*
 * Popped by another caller, such as a logout that clears the stack: the
 * read ends with no callback and its text is wiped. finish() clears
 * rl.active before its own pop, so this does nothing then.
 */
static void rl_leave(uos_screen_t *s)
{
    (void)s;
    if (!rl.active) {
        return;
    }
    osk_close(&rl.osk);
    if (uos.fb_ok) {
        console_cursor_show(0);
    }
    uos_serial_break();
    rl.active = 0;
    rl.cb = NULL;
    rl.ctx = NULL;
    k_memset(rl.buf, 0, sizeof(rl.buf));
    UOS_MARK("line result=abort");
}

static uos_screen_t rl_screen = { "readline", NULL, rl_leave, rl_event, rl_draw, NULL, 0 };

int con_line_active(void)
{
    return rl.active;
}

int con_line_on_top(void)
{
    return rl.active && uos_screen_on_top(&rl_screen);
}

int con_line_abort(void)
{
    if (!rl.active) {
        return CON_ERR_BUSY;
    }
    if (!uos_screen_on_top(&rl_screen)) {
        return CON_ERR_COVERED;
    }
    /* rl_leave() ends the read with no callback. */
    (void)uos_screen_pop(&rl_screen);
    return 0;
}

int con_osk_open(void)
{
    if (!rl.active) {
        return CON_ERR_BUSY;
    }
    if (!uos_screen_on_top(&rl_screen)) {
        return CON_ERR_COVERED;
    }
    if (!osk_is_open(&rl.osk)) {
        osk_open(&rl.osk, (rl.flags & CON_RL_MASK) != 0);
        UOS_MARK("osk state=open password=%d", osk_password(&rl.osk));
        paint_osk();
        paint_line(1, 0);
    }
    return 0;
}

int con_osk_close(void)
{
    if (!rl.active) {
        return CON_ERR_BUSY;
    }
    if (!uos_screen_on_top(&rl_screen)) {
        return CON_ERR_COVERED;
    }
    if (osk_is_open(&rl.osk)) {
        osk_close(&rl.osk);
        UOS_MARK("osk state=closed");
        uos_screen_expose();
    }
    return 0;
}

void con_osk_state(int *open, unsigned *layer, unsigned *row, unsigned *col,
                   unsigned *line_row)
{
    unsigned r = 0, c = 0;

    osk_highlight(&rl.osk, &r, &c);
    *open = rl.active && osk_is_open(&rl.osk);
    *layer = osk_layer(&rl.osk);
    *row = r;
    *col = c;
    *line_row = rl.row;
}

int con_read_line(const char *prompt, unsigned flags, unsigned max, con_line_fn cb, void *ctx)
{
    unsigned col = 0, row = 0, last;
    size_t n;

    if (!con_up) {
        return CON_ERR_DOWN;
    }
    if (rl.active) {
        return CON_ERR_BUSY;
    }
    if (prompt == NULL) {
        prompt = "";
    }
    n = k_strlen(prompt);
    if (n > CV_PROMPT_MAX) {
        n = CV_PROMPT_MAX;
    }
    k_memcpy(rl.prompt, prompt, n);
    /* A prompt that does not end in a blank gets one. */
    if (n > 0 && prompt[n - 1] != ' ' && n < CV_PROMPT_MAX) {
        rl.prompt[n++] = ' ';
    }
    rl.prompt[n] = 0;
    if (le_init(&rl.le, rl.buf, sizeof(rl.buf), max, flags & (CON_RL_MASK | CON_RL_NOHIST),
                &con_history, cv_line_width(n, con_cols())) != 0) {
        return CON_ERR_ARG;
    }
    rl.plen = (unsigned)n;
    rl.flags = flags;
    osk_init(&rl.osk);
    rl.cb = cb;
    rl.ctx = ctx;

    if (uos.fb_ok) {
        console_get_cursor(&col, &row);
        if (col != 0) {
            row++;
        }
        last = con_text_rows() - 1u;
        while (row > last) {
            console_set_cursor(0, con_rows() - 1u);
            console_putc('\n');
            row--;
            con_clear_rows(last, 1);
            con_status_redraw();
        }
    }
    rl.want_row = row;
    rl.row = row;
    if (uos_screen_push(&rl_screen) != 0) {
        return CON_ERR_SCREEN;
    }
    rl.active = 1;
    return 0;
}
