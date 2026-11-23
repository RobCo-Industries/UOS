#include "osk.h"

#include "robco/klibc.h"

/* Layout data: OSK_CHAR_ROWS rows of OSK_CHAR_COLS characters per layer. */
static const char osk_chars[OSK_LAYERS][OSK_CHAR_ROWS][OSK_CHAR_COLS + 1] = {
    {
        "ABCDEFGHIJKLM",
        "NOPQRSTUVWXYZ",
        "0123456789./:",
        "$_-* [];%=(),",
    },
    {
        "abcdefghijklm",
        "nopqrstuvwxyz",
        "!\"#&'+<>?@\\^`",
        "{|}~.,:;/_-= ",
    },
};

static const char *const osk_fn_labels[OSK_FUNC_KEYS] = {
    "SHIFT", "BKSP", "LEFT", "RIGHT", "ENTER", "CLOSE",
};

void osk_init(osk_t *k)
{
    k_memset(k, 0, sizeof(*k));
}

void osk_open(osk_t *k, int password)
{
    k->open = 1;
    k->password = password ? 1 : 0;
    k->layer = 0;
    k->row = 0;
    k->col = 0;
    k->head = 0;
    k->count = 0;
}

void osk_close(osk_t *k)
{
    k->open = 0;
}

int osk_is_open(const osk_t *k)
{
    return k->open;
}

int osk_password(const osk_t *k)
{
    return k->password;
}

unsigned osk_layer(const osk_t *k)
{
    return k->layer;
}

void osk_highlight(const osk_t *k, unsigned *row, unsigned *col)
{
    *row = k->row;
    *col = k->col;
}

uint8_t osk_layout_char(unsigned layer, unsigned row, unsigned col)
{
    if (layer >= OSK_LAYERS || row >= OSK_CHAR_ROWS || col >= OSK_CHAR_COLS) {
        return 0;
    }
    return (uint8_t)osk_chars[layer][row][col];
}

/* Function key under character column c: the one holding the cell centre. */
static unsigned col_to_func(unsigned c)
{
    return (c * OSK_CHAR_W + OSK_CHAR_W / 2u) / OSK_FUNC_W;
}

/* Character column under function key f: the one holding the key centre. */
static unsigned func_to_col(unsigned f)
{
    return (f * OSK_FUNC_W + OSK_FUNC_W / 2u) / OSK_CHAR_W;
}

static void move(osk_t *k, unsigned code)
{
    unsigned width = k->row == OSK_FUNC_ROW ? OSK_FUNC_KEYS : OSK_CHAR_COLS;

    switch (code) {
    case INPUT_KEY_LEFT:
        k->col = (uint8_t)(k->col == 0 ? width - 1u : k->col - 1u);
        break;
    case INPUT_KEY_RIGHT:
        k->col = (uint8_t)(k->col + 1u == width ? 0 : k->col + 1u);
        break;
    case INPUT_KEY_UP:
        if (k->row == OSK_FUNC_ROW) {
            k->col = (uint8_t)func_to_col(k->col);
            k->row = OSK_CHAR_ROWS - 1u;
        } else if (k->row == 0) {
            k->col = (uint8_t)col_to_func(k->col);
            k->row = OSK_FUNC_ROW;
        } else {
            k->row--;
        }
        break;
    case INPUT_KEY_DOWN:
        if (k->row == OSK_FUNC_ROW) {
            k->col = (uint8_t)func_to_col(k->col);
            k->row = 0;
        } else if (k->row == OSK_CHAR_ROWS - 1u) {
            k->col = (uint8_t)col_to_func(k->col);
            k->row = OSK_FUNC_ROW;
        } else {
            k->row++;
        }
        break;
    default:
        break;
    }
}

static int emit(osk_t *k, uint8_t type, uint16_t code, int32_t value, uint32_t t_ms)
{
    input_event_t *e = &k->out[(k->head + k->count) % OSK_OUT_MAX];

    e->type = type;
    e->source = OSK_SRC;
    e->code = code;
    e->value = value;
    e->t_ms = t_ms;
    k->count++;
    return 0;
}

/* Emits a press and a release of `key`, or nothing when two slots are not free. */
static int emit_key(osk_t *k, uint16_t key, uint32_t t_ms)
{
    if (OSK_OUT_MAX - k->count < 2u) {
        return OSK_ERR_FULL;
    }
    emit(k, INPUT_EV_KEY, key, INPUT_PRESS, t_ms);
    emit(k, INPUT_EV_KEY, key, INPUT_RELEASE, t_ms);
    return 0;
}

static int select_key(osk_t *k, int repeat, uint32_t t_ms)
{
    int rc;

    if (k->row != OSK_FUNC_ROW) {
        if (repeat) {
            return OSK_CONSUMED;
        }
        if (k->count >= OSK_OUT_MAX) {
            return OSK_ERR_FULL;
        }
        emit(k, INPUT_EV_CHAR, (uint8_t)osk_chars[k->layer][k->row][k->col], 0, t_ms);
        return OSK_CONSUMED;
    }
    switch (k->col) {
    case OSK_FN_BACKSPACE:
        rc = emit_key(k, INPUT_KEY_BACK, t_ms);
        break;
    case OSK_FN_LEFT:
        rc = emit_key(k, INPUT_KEY_LEFT, t_ms);
        break;
    case OSK_FN_RIGHT:
        rc = emit_key(k, INPUT_KEY_RIGHT, t_ms);
        break;
    case OSK_FN_SHIFT:
        if (!repeat) {
            k->layer = (uint8_t)((k->layer + 1u) % OSK_LAYERS);
        }
        return OSK_CONSUMED;
    case OSK_FN_ENTER:
        if (repeat) {
            return OSK_CONSUMED;
        }
        rc = emit_key(k, INPUT_KEY_SELECT, t_ms);
        if (rc == 0) {
            k->open = 0;
        }
        break;
    default: /* OSK_FN_CLOSE */
        if (!repeat) {
            k->open = 0;
        }
        return OSK_CONSUMED;
    }
    return rc < 0 ? rc : OSK_CONSUMED;
}

int osk_handle(osk_t *k, const input_event_t *ev)
{
    int repeat;

    if (k == NULL || ev == NULL) {
        return OSK_ERR_ARG;
    }
    if (!k->open || ev->type != INPUT_EV_KEY) {
        return OSK_IGNORED;
    }
    if (ev->value != INPUT_PRESS && ev->value != INPUT_REPEAT) {
        return OSK_CONSUMED;
    }
    repeat = ev->value == INPUT_REPEAT;
    switch (ev->code) {
    case INPUT_KEY_UP:
    case INPUT_KEY_DOWN:
    case INPUT_KEY_LEFT:
    case INPUT_KEY_RIGHT:
        move(k, ev->code);
        return OSK_CONSUMED;
    case INPUT_KEY_SELECT:
        return select_key(k, repeat, ev->t_ms);
    case INPUT_KEY_BACK:
        if (!repeat) {
            k->open = 0;
        }
        return OSK_CONSUMED;
    default:
        return OSK_IGNORED;
    }
}

int osk_pop(osk_t *k, input_event_t *ev)
{
    if (k->count == 0) {
        return 0;
    }
    *ev = k->out[k->head];
    k->head = (uint8_t)((k->head + 1u) % OSK_OUT_MAX);
    k->count--;
    return 1;
}

unsigned osk_pending(const osk_t *k)
{
    return k->count;
}

/* Writes `label` centred in a cell of width w at dst; extra space goes right. */
static void put_cell(char *dst, unsigned w, const char *label)
{
    size_t n = k_strlen(label);
    unsigned pad = (unsigned)((w - n) / 2u);

    k_memcpy(dst + pad, label, n);
}

int osk_row_text(const osk_t *k, unsigned row, char *buf, size_t size)
{
    unsigned i;

    if (k == NULL || buf == NULL || row >= OSK_ROWS) {
        return OSK_ERR_ARG;
    }
    if (size < OSK_ROW_BUF) {
        return OSK_ERR_SPACE;
    }
    k_memset(buf, ' ', OSK_TEXT_COLS);
    buf[OSK_TEXT_COLS] = '\0';
    if (row == OSK_FUNC_ROW) {
        for (i = 0; i < OSK_FUNC_KEYS; i++) {
            put_cell(buf + OSK_LEFT_MARGIN + i * OSK_FUNC_W, OSK_FUNC_W, osk_fn_labels[i]);
        }
        return (int)OSK_TEXT_COLS;
    }
    for (i = 0; i < OSK_CHAR_COLS; i++) {
        char one[2];
        char c = osk_chars[k->layer][row][i];

        one[0] = c;
        one[1] = '\0';
        put_cell(buf + OSK_LEFT_MARGIN + i * OSK_CHAR_W, OSK_CHAR_W, c == ' ' ? "SPC" : one);
    }
    return (int)OSK_TEXT_COLS;
}

int osk_key_cell(const osk_t *k, unsigned index, osk_cell_t *cell)
{
    unsigned row;
    unsigned col;

    if (k == NULL || cell == NULL || index >= OSK_KEYS) {
        return OSK_ERR_ARG;
    }
    if (index < OSK_CHAR_ROWS * OSK_CHAR_COLS) {
        row = index / OSK_CHAR_COLS;
        col = index % OSK_CHAR_COLS;
        cell->x = (uint8_t)(OSK_LEFT_MARGIN + col * OSK_CHAR_W);
        cell->w = OSK_CHAR_W;
        cell->is_func = 0;
        cell->ch = (uint8_t)osk_chars[k->layer][row][col];
        cell->fn = 0;
    } else {
        row = OSK_FUNC_ROW;
        col = index - OSK_CHAR_ROWS * OSK_CHAR_COLS;
        cell->x = (uint8_t)(OSK_LEFT_MARGIN + col * OSK_FUNC_W);
        cell->w = OSK_FUNC_W;
        cell->is_func = 1;
        cell->ch = 0;
        cell->fn = (uint8_t)col;
    }
    cell->y = (uint8_t)row;
    cell->highlighted = (uint8_t)(k->row == row && k->col == col);
    return 0;
}

int osk_screen_rows(const osk_t *k, unsigned screen_rows, unsigned *first, unsigned *count)
{
    if (k == NULL || first == NULL || count == NULL) {
        return OSK_ERR_ARG;
    }
    if (screen_rows < OSK_ROWS) {
        return OSK_ERR_SCREEN;
    }
    if (!k->open) {
        *first = screen_rows;
        *count = 0;
        return 0;
    }
    *first = screen_rows - OSK_ROWS;
    *count = OSK_ROWS;
    return 0;
}

const char *osk_strerror(int code)
{
    switch (code) {
    case OSK_IGNORED:   return "event not for the keyboard";
    case OSK_CONSUMED:  return "event used by the keyboard";
    case OSK_ERR_FULL:  return "keyboard output queue full";
    case OSK_ERR_ARG:   return "bad argument";
    case OSK_ERR_SPACE: return "buffer too small";
    case OSK_ERR_SCREEN: return "screen too small for the keyboard";
    default:            return "unknown keyboard code";
    }
}
