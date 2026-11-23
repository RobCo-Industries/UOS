/*
 * Output helpers other services share: a printed line, a system message,
 * clearing rows, the status row and a "press a key" pause.
 */

#include <robco/console.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "conint.h"
#include "uos.h"

#define STATUS_MAX      79u
#define MSG_BUF         192u

static char status[STATUS_MAX + 1];

unsigned con_cols(void)
{
    return uos.fb_ok && console_cols() > 0 ? console_cols() : 80u;
}

unsigned con_rows(void)
{
    return uos.fb_ok && console_rows() > 0 ? console_rows() : 30u;
}

void con_put(unsigned col, unsigned row, const char *text, size_t n, unsigned attr, int fill)
{
    unsigned cols = con_cols();
    unsigned c;

    if (!uos.fb_ok || row >= con_rows() || col >= cols) {
        return;
    }
    if (n > cols - col) {
        n = cols - col;
    }
    console_set_cursor(col, row);
    console_set_attr(attr);
    console_write(text, n);
    console_set_attr(CONSOLE_ATTR_NORMAL);
    if (fill) {
        for (c = col + (unsigned)n; c < cols; c++) {
            console_putc(' ');
        }
    }
}

void con_clear_rows(unsigned first, unsigned count)
{
    unsigned r;

    for (r = first; r < first + count && r < con_rows(); r++) {
        con_put(0, r, "", 0, CONSOLE_ATTR_NORMAL, 1);
    }
}

unsigned con_text_rows(void)
{
    return status[0] != 0 ? con_rows() - 1u : con_rows();
}

void con_status_redraw(void)
{
    if (status[0] != 0) {
        con_put(0, con_rows() - 1u, status, k_strlen(status), CONSOLE_ATTR_INVERSE, 1);
    }
}

void con_status_set(const char *text)
{
    size_t n = text == NULL ? 0 : k_strlen(text);
    unsigned col, row;

    if (n > STATUS_MAX) {
        n = STATUS_MAX;
    }
    k_memcpy(status, text == NULL ? "" : text, n);
    status[n] = 0;
    if (!uos.fb_ok) {
        return;
    }
    console_get_cursor(&col, &row);
    if (n == 0) {
        con_clear_rows(con_rows() - 1u, 1);
    } else {
        con_status_redraw();
    }
    console_set_cursor(col, row);
}

void con_status_clear(void)
{
    con_status_set("");
}

const char *con_status_text(void)
{
    return status;
}

void con_print(const char *text, unsigned attr)
{
    size_t n = text == NULL ? 0 : k_strlen(text);
    unsigned last = con_text_rows() - 1u;
    unsigned col, row;

    uart_puts(text == NULL ? "" : text);
    uart_putc('\n');
    if (!uos.fb_ok) {
        return;
    }
    console_get_cursor(&col, &row);
    if (col != 0) {
        row++;
    }
    while (row > last) {
        /* A new line on the bottom row scrolls everything up one row. */
        console_set_cursor(0, con_rows() - 1u);
        console_putc('\n');
        row--;
    }
    con_put(0, row, text == NULL ? "" : text, n, attr, 1);
    if (row < last) {
        console_set_cursor(0, row + 1u);
    } else {
        console_set_cursor(0, con_rows() - 1u);
        console_putc('\n');
        console_set_cursor(0, last);
        con_clear_rows(last, 1);
    }
    if (status[0] != 0) {
        console_get_cursor(&col, &row);
        con_status_redraw();
        console_set_cursor(col, row);
    }
}

int con_message(dcl_msg_t id, const char *arg, uint32_t num)
{
    char buf[MSG_BUF];
    char *line;
    char *p;
    int n = dcl_format_message(buf, sizeof(buf), id, arg, num);

    if (n < 0) {
        return CON_ERR_ARG;
    }
    line = buf;
    for (p = buf; ; p++) {
        if (*p == '\n' || *p == 0) {
            char end = *p;

            *p = 0;
            con_print(line, CONSOLE_ATTR_NORMAL);
            if (end == 0) {
                break;
            }
            line = p + 1;
        }
    }
    return n;
}

/* ---- The pause screen ---- */

static struct {
    int active;
    con_done_fn cb;
    void *ctx;
    char saved[STATUS_MAX + 1];
    char text[STATUS_MAX + 1];
} pause_st;

static void pause_draw(uos_screen_t *s)
{
    (void)s;
    con_put(0, con_rows() - 1u, pause_st.text, k_strlen(pause_st.text),
            CONSOLE_ATTR_INVERSE, 1);
}

static int pause_event(uos_screen_t *s, const uos_event_t *ev)
{
    con_done_fn cb;
    void *ctx;

    (void)s;
    if (ev->type != UOS_EV_KEY && ev->type != UOS_EV_CHAR) {
        return 0;
    }
    cb = pause_st.cb;
    ctx = pause_st.ctx;
    pause_st.active = 0;
    (void)uos_screen_pop();
    con_status_set(pause_st.saved);
    UOS_MARK("pause done=1");
    if (cb != NULL) {
        cb(CON_OK, ctx);
    }
    return 1;
}

static uos_screen_t pause_screen = { "pause", NULL, NULL, pause_event, pause_draw, NULL };

int con_pause(const char *text, con_done_fn cb, void *ctx)
{
    size_t n;

    if (!con_up) {
        return CON_ERR_DOWN;
    }
    if (pause_st.active) {
        return CON_ERR_BUSY;
    }
    if (text == NULL) {
        text = "Press a key";
    }
    n = k_strlen(text);
    if (n > STATUS_MAX) {
        n = STATUS_MAX;
    }
    k_memcpy(pause_st.text, text, n);
    pause_st.text[n] = 0;
    k_memcpy(pause_st.saved, status, sizeof(status));
    pause_st.cb = cb;
    pause_st.ctx = ctx;
    if (uos_screen_push(&pause_screen) != 0) {
        return CON_ERR_SCREEN;
    }
    pause_st.active = 1;
    return 0;
}
