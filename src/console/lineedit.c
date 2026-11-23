/*
 * The line editor (pure). See lineedit.h.
 *
 * The window: when the line and the cursor cell fit in `width` columns,
 * nothing scrolls. Otherwise the cursor stays from column 1 (column 0
 * holds '<' once the line is cut on the left) to column width - 2 (the
 * last column holds '>' while text is cut on the right), and the window
 * never shows more blank cells after the end than the cursor cell needs.
 */

#include "lineedit.h"

#define KEY_CTRL_A  LE_CTRL('A')
#define KEY_CTRL_B  LE_CTRL('B')
#define KEY_CTRL_C  LE_CTRL('C')
#define KEY_CTRL_D  LE_CTRL('D')
#define KEY_CTRL_E  LE_CTRL('E')
#define KEY_CTRL_F  LE_CTRL('F')
#define KEY_CTRL_K  LE_CTRL('K')
#define KEY_CTRL_N  LE_CTRL('N')
#define KEY_CTRL_O  LE_CTRL('O')
#define KEY_CTRL_P  LE_CTRL('P')

/* Internal operations. */
enum {
    OP_NONE = 0,
    OP_INSERT,
    OP_ACCEPT,
    OP_CANCEL,
    OP_BACKSPACE,
    OP_DELETE,
    OP_LEFT,
    OP_RIGHT,
    OP_HOME,
    OP_END,
    OP_OLDER,
    OP_NEWER,
    OP_KILL,
    OP_MODE,
};

static size_t str_len(const char *s)
{
    size_t n = 0;

    while (s[n] != 0) {
        n++;
    }
    return n;
}

static void copy_bytes(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* ---- History ---- */

void le_hist_init(le_history_t *h)
{
    unsigned i;

    if (h == NULL) {
        return;
    }
    for (i = 0; i < LE_HIST; i++) {
        h->line[i][0] = 0;
    }
    h->head = 0;
    h->count = 0;
}

unsigned le_hist_count(const le_history_t *h)
{
    return h == NULL ? 0 : h->count;
}

const char *le_hist_get(const le_history_t *h, unsigned i)
{
    if (h == NULL || i >= h->count) {
        return NULL;
    }
    return h->line[(h->head + LE_HIST - 1u - i) % LE_HIST];
}

int le_hist_add(le_history_t *h, const char *text, size_t len)
{
    const char *newest;
    char *slot;

    if (h == NULL || text == NULL || len == 0 || len > LE_MAX) {
        return 0;
    }
    newest = le_hist_get(h, 0);
    if (newest != NULL && str_len(newest) == len) {
        size_t i;

        for (i = 0; i < len && newest[i] == text[i]; i++) {
        }
        if (i == len) {
            return 0;
        }
    }
    slot = h->line[h->head];
    copy_bytes(slot, text, len);
    slot[len] = 0;
    h->head = (uint8_t)((h->head + 1u) % LE_HIST);
    if (h->count < LE_HIST) {
        h->count++;
    }
    return 1;
}

/* ---- The window ---- */

static void fix_scroll(le_t *e)
{
    unsigned w = e->width;
    unsigned max_scroll;

    if ((unsigned)e->len + 1u <= w) {
        e->scroll = 0;
        return;
    }
    if (e->scroll > 0 && e->cur < e->scroll + 1u) {
        e->scroll = (uint16_t)(e->cur > 0 ? e->cur - 1u : 0u);
    }
    if (e->cur > e->scroll + w - 2u) {
        e->scroll = (uint16_t)(e->cur - (w - 2u));
    }
    max_scroll = (unsigned)e->len + 2u - w;
    if (e->scroll > max_scroll) {
        e->scroll = (uint16_t)max_scroll;
    }
}

unsigned le_cursor_col(const le_t *e)
{
    return e == NULL ? 0 : (unsigned)(e->cur - e->scroll);
}

unsigned le_len(const le_t *e)
{
    return e == NULL ? 0 : e->len;
}

int le_state(const le_t *e)
{
    return e == NULL ? LE_ERR_ARG : e->state;
}

int le_display(const le_t *e, char *out, size_t size, unsigned *col)
{
    unsigned c;
    unsigned w;

    if (e == NULL || out == NULL || size < (size_t)e->width + 1u) {
        return LE_ERR_ARG;
    }
    w = e->width;
    for (c = 0; c < w; c++) {
        unsigned pos = e->scroll + c;

        if (c == 0 && e->scroll > 0) {
            out[c] = '<';
        } else if (c == w - 1u && e->len > e->scroll + w - 1u) {
            out[c] = '>';
        } else if (pos < e->len) {
            out[c] = (e->flags & LE_F_MASK) ? '*' : e->buf[pos];
        } else {
            out[c] = ' ';
        }
    }
    out[w] = 0;
    if (col != NULL) {
        *col = le_cursor_col(e);
    }
    return (int)w;
}

/* ---- Editing ---- */

int le_init(le_t *e, char *buf, size_t size, unsigned max, unsigned flags,
            le_history_t *hist, unsigned width)
{
    if (e == NULL || buf == NULL || max == 0 || max > LE_MAX || size < (size_t)max + 1u ||
        width < LE_MIN_WIDTH || width > LE_MAX_WIDTH || (flags & ~(LE_F_MASK | LE_F_NOHIST))) {
        return LE_ERR_ARG;
    }
    e->buf = buf;
    e->buf[0] = 0;
    e->max = (uint16_t)max;
    e->len = 0;
    e->cur = 0;
    e->scroll = 0;
    e->width = (uint8_t)width;
    e->flags = (uint8_t)flags;
    e->overwrite = 0;
    e->state = LE_EDITING;
    e->hpos = -1;
    e->hist = hist;
    e->saved[0] = 0;
    e->saved_len = 0;
    return 0;
}

static int op_of(const input_event_t *ev, unsigned *ch)
{
    if (ev->type == INPUT_EV_CHAR) {
        unsigned c = ev->code;

        if (c >= 0x20 && c <= 0x7e) {
            *ch = c;
            return OP_INSERT;
        }
        switch (c) {
        case KEY_CTRL_A: return OP_HOME;
        case KEY_CTRL_B: return OP_LEFT;
        case KEY_CTRL_C: return OP_CANCEL;
        case KEY_CTRL_D: return OP_DELETE;
        case KEY_CTRL_E: return OP_END;
        case KEY_CTRL_F: return OP_RIGHT;
        case KEY_CTRL_K: return OP_KILL;
        case KEY_CTRL_N: return OP_NEWER;
        case KEY_CTRL_O: return OP_MODE;
        case KEY_CTRL_P: return OP_OLDER;
        default:         return OP_NONE;
        }
    }
    if (ev->type != INPUT_EV_KEY || ev->value == INPUT_RELEASE) {
        return OP_NONE;
    }
    if (ev->value == INPUT_REPEAT) {
        switch (ev->code) {
        case INPUT_KEY_LEFT:  return OP_LEFT;
        case INPUT_KEY_RIGHT: return OP_RIGHT;
        case INPUT_KEY_UP:    return OP_OLDER;
        case INPUT_KEY_DOWN:  return OP_NEWER;
        case INPUT_KEY_BACK:  return OP_BACKSPACE;
        case INPUT_KEY_F3:    return OP_DELETE;
        default:              return OP_NONE;
        }
    }
    if (ev->value != INPUT_PRESS) {
        return OP_NONE;
    }
    switch (ev->code) {
    case INPUT_KEY_SELECT: return OP_ACCEPT;
    case INPUT_KEY_BACK:   return OP_BACKSPACE;
    case INPUT_KEY_LEFT:   return OP_LEFT;
    case INPUT_KEY_RIGHT:  return OP_RIGHT;
    case INPUT_KEY_UP:     return OP_OLDER;
    case INPUT_KEY_DOWN:   return OP_NEWER;
    case INPUT_KEY_F1:     return OP_HOME;
    case INPUT_KEY_F2:     return OP_END;
    case INPUT_KEY_F3:     return OP_DELETE;
    default:               return OP_NONE;
    }
}

static int recall_allowed(const le_t *e)
{
    return e->hist != NULL && !(e->flags & (LE_F_MASK | LE_F_NOHIST));
}

/* Puts `text` in the buffer, cut at max, cursor at its end. */
static void load_line(le_t *e, const char *text, size_t n)
{
    if (n > e->max) {
        n = e->max;
    }
    copy_bytes(e->buf, text, n);
    e->buf[n] = 0;
    e->len = (uint16_t)n;
    e->cur = (uint16_t)n;
}

static void remove_at(le_t *e, unsigned pos)
{
    unsigned i;

    for (i = pos; i < e->len; i++) {
        e->buf[i] = e->buf[i + 1u];
    }
    e->len--;
}

/* Applies one operation. Returns the redraw kind before the scroll check;
 * *from gets the first changed character position. */
static int apply(le_t *e, int op, unsigned ch, unsigned *from, int *refused)
{
    unsigned i;

    *from = e->cur;
    switch (op) {
    case OP_INSERT:
        if (e->overwrite && e->cur < e->len) {
            e->buf[e->cur] = (char)ch;
            e->cur++;
            return LE_RD_FROM;
        }
        if (e->len >= e->max) {
            *refused = 1;
            return LE_RD_NONE;
        }
        for (i = e->len + 1u; i > e->cur; i--) {
            e->buf[i] = e->buf[i - 1u];
        }
        e->buf[e->cur] = (char)ch;
        e->len++;
        e->cur++;
        return LE_RD_FROM;
    case OP_BACKSPACE:
        if (e->cur == 0) {
            *refused = 1;
            return LE_RD_NONE;
        }
        e->cur--;
        remove_at(e, e->cur);
        *from = e->cur;
        return LE_RD_FROM;
    case OP_DELETE:
        if (e->cur >= e->len) {
            *refused = 1;
            return LE_RD_NONE;
        }
        remove_at(e, e->cur);
        return LE_RD_FROM;
    case OP_KILL:
        if (e->cur >= e->len) {
            return LE_RD_NONE;
        }
        e->len = e->cur;
        e->buf[e->len] = 0;
        return LE_RD_FROM;
    case OP_LEFT:
        if (e->cur == 0) {
            *refused = 1;
            return LE_RD_NONE;
        }
        e->cur--;
        return LE_RD_CURSOR;
    case OP_RIGHT:
        if (e->cur >= e->len) {
            *refused = 1;
            return LE_RD_NONE;
        }
        e->cur++;
        return LE_RD_CURSOR;
    case OP_HOME:
        if (e->cur == 0) {
            return LE_RD_NONE;
        }
        e->cur = 0;
        return LE_RD_CURSOR;
    case OP_END:
        if (e->cur == e->len) {
            return LE_RD_NONE;
        }
        e->cur = e->len;
        return LE_RD_CURSOR;
    case OP_MODE:
        e->overwrite = (uint8_t)!e->overwrite;
        return LE_RD_NONE;
    case OP_OLDER:
        if (!recall_allowed(e) || (unsigned)(e->hpos + 1) >= le_hist_count(e->hist)) {
            *refused = 1;
            return LE_RD_NONE;
        }
        if (e->hpos < 0) {
            copy_bytes(e->saved, e->buf, (size_t)e->len + 1u);
            e->saved_len = e->len;
        }
        e->hpos++;
        {
            const char *h = le_hist_get(e->hist, (unsigned)e->hpos);

            load_line(e, h, str_len(h));
        }
        return LE_RD_ALL;
    case OP_NEWER:
        if (!recall_allowed(e) || e->hpos < 0) {
            *refused = 1;
            return LE_RD_NONE;
        }
        e->hpos--;
        if (e->hpos < 0) {
            load_line(e, e->saved, e->saved_len);
        } else {
            const char *h = le_hist_get(e->hist, (unsigned)e->hpos);

            load_line(e, h, str_len(h));
        }
        return LE_RD_ALL;
    default:
        return LE_RD_NONE;
    }
}

int le_event(le_t *e, const input_event_t *ev, le_redraw_t *rd)
{
    le_redraw_t dummy;
    unsigned ch = 0;
    unsigned from;
    uint16_t old_scroll;
    int refused = 0;
    int what;
    int op;

    if (rd == NULL) {
        rd = &dummy;
    }
    rd->what = LE_RD_NONE;
    rd->from = 0;
    rd->refused = 0;
    if (e == NULL || ev == NULL) {
        return LE_ERR_ARG;
    }
    if (e->state != LE_EDITING) {
        return e->state;
    }
    op = op_of(ev, &ch);
    if (op == OP_ACCEPT) {
        e->state = LE_ACCEPT;
        if (!(e->flags & (LE_F_MASK | LE_F_NOHIST))) {
            (void)le_hist_add(e->hist, e->buf, e->len);
        }
        return e->state;
    }
    if (op == OP_CANCEL) {
        e->state = LE_CANCEL;
        return e->state;
    }
    old_scroll = e->scroll;
    what = apply(e, op, ch, &from, &refused);
    fix_scroll(e);
    if (what != LE_RD_NONE && e->scroll != old_scroll) {
        what = LE_RD_ALL;
    }
    rd->what = (uint8_t)what;
    rd->refused = (uint8_t)refused;
    if (what == LE_RD_FROM) {
        rd->from = (uint8_t)(from > e->scroll ? from - e->scroll : 0u);
    }
    return LE_EDITING;
}

int le_key(le_t *e, unsigned key, le_redraw_t *rd)
{
    input_event_t ev = { INPUT_EV_KEY, INPUT_SRC_SERIAL, (uint16_t)key, INPUT_PRESS, 0 };

    return le_event(e, &ev, rd);
}

int le_char(le_t *e, unsigned c, le_redraw_t *rd)
{
    input_event_t ev = { INPUT_EV_CHAR, INPUT_SRC_SERIAL, (uint16_t)c, 0, 0 };

    return le_event(e, &ev, rd);
}
