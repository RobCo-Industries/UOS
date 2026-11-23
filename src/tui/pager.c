/*
 * Pager over a text buffer the caller owns. Pure.
 *
 * Lines are wrapped on the fly. The only index is one offset per
 * PG_MARK_EVERY wrapped lines, in a buffer the caller passes, so finding
 * a line walks at most 63 lines from the nearest mark.
 */

#include "tui.h"

#include <robco/klibc.h>

static int is_ws(char c)
{
    return c == ' ' || c == '\t';
}

/* After a soft break: drops blanks, then one line end if one follows. */
static uint32_t skip_ws(const char *s, uint32_t len, uint32_t i)
{
    while (i < len && is_ws(s[i])) {
        i++;
    }
    if (i < len && s[i] == '\r') {
        i++;
        if (i < len && s[i] == '\n') {
            i++;
        }
    } else if (i < len && s[i] == '\n') {
        i++;
    }
    return i;
}

/* Drops the blanks before a soft break. */
static uint32_t trim(const char *s, uint32_t pos, uint32_t end)
{
    while (end > pos && is_ws(s[end - 1])) {
        end--;
    }
    return end;
}

/*
 * Wraps one line starting at `pos` (< len). Sets *end to the end of its
 * characters and returns where the next line starts. Always moves on.
 */
static uint32_t wrap(const char *s, uint32_t len, uint32_t pos, unsigned width, uint32_t *end)
{
    unsigned col = 0;
    uint32_t i = pos;
    uint32_t brk = 0;
    int have_brk = 0;
    int have_word = 0;

    while (i < len) {
        char c = s[i];
        unsigned w;

        if (c == '\n') {
            *end = i;
            return i + 1;
        }
        if (c == '\r') {
            *end = i;
            return (i + 1 < len && s[i + 1] == '\n') ? i + 2 : i + 1;
        }
        w = c == '\t' ? PG_TAB - col % PG_TAB : 1;
        if (col + w > width) {
            if (is_ws(c) && have_word) {
                *end = trim(s, pos, i);
                return skip_ws(s, len, i);
            }
            if (have_brk) {
                *end = trim(s, pos, brk);
                return skip_ws(s, len, brk);
            }
            *end = i;           /* a word longer than the line: split it */
            return i;
        }
        if (!is_ws(c)) {
            have_word = 1;
        } else if (have_word) {
            brk = i;
            have_brk = 1;
        }
        col += w;
        i++;
    }
    *end = i;
    return i;
}

int pg_init(pg_t *p, const char *text, size_t len, unsigned width, unsigned height,
            uint32_t *marks, uint32_t cap)
{
    uint32_t pos = 0;
    uint32_t n = 0;
    uint32_t end;

    if (p == 0 || (text == 0 && len > 0) || marks == 0 || len > 0xfffffff0u ||
        width < PG_MIN_WIDTH || width > PG_MAX_WIDTH || height < 1 || height > PG_MAX_HEIGHT) {
        return PG_E_ARG;
    }
    k_memset(p, 0, sizeof(*p));
    p->text = text;
    p->len = (uint32_t)len;
    p->width = (uint16_t)width;
    p->height = (uint16_t)height;
    p->marks = marks;
    while (pos < p->len) {
        if (n % PG_MARK_EVERY == 0) {
            if (n / PG_MARK_EVERY >= cap) {
                p->nmarks = 0;
                p->total = 0;
                return PG_E_SPACE;
            }
            marks[n / PG_MARK_EVERY] = pos;
            p->nmarks = n / PG_MARK_EVERY + 1;
        }
        pos = wrap(text, p->len, pos, width, &end);
        n++;
    }
    p->total = n;
    return PG_OK;
}

uint32_t pg_lines(const pg_t *p)
{
    return p ? p->total : 0;
}

uint32_t pg_pages(const pg_t *p)
{
    return p ? (p->total + p->height - 1u) / p->height : 0;
}

uint32_t pg_max_top(const pg_t *p)
{
    return p->total > p->height ? p->total - p->height : 0;
}

void pg_line_down(pg_t *p)
{
    if (p->top < pg_max_top(p)) {
        p->top++;
    }
}

void pg_line_up(pg_t *p)
{
    if (p->top > 0) {
        p->top--;
    }
}

void pg_page_down(pg_t *p)
{
    uint32_t max = pg_max_top(p);

    p->top = max - p->top > p->height ? p->top + p->height : max;
}

void pg_page_up(pg_t *p)
{
    p->top = p->top > p->height ? p->top - p->height : 0;
}

void pg_home(pg_t *p)
{
    p->top = 0;
}

void pg_end(pg_t *p)
{
    p->top = pg_max_top(p);
}

void pg_visible(const pg_t *p, uint32_t *first, uint32_t *count)
{
    uint32_t left = p->total - p->top;

    *first = p->top;
    *count = left < p->height ? left : p->height;
}

int pg_line(const pg_t *p, uint32_t n, char *out, size_t size)
{
    uint32_t pos;
    uint32_t end;
    uint32_t k;
    unsigned col = 0;

    if (p == 0 || out == 0) {
        return PG_E_ARG;
    }
    if (size < (size_t)p->width + 1u) {
        return PG_E_SPACE;
    }
    if (n >= p->total) {
        return PG_E_RANGE;
    }
    pos = p->marks[n / PG_MARK_EVERY];
    for (k = 0; k < n % PG_MARK_EVERY; k++) {
        pos = wrap(p->text, p->len, pos, p->width, &end);
    }
    wrap(p->text, p->len, pos, p->width, &end);
    for (; pos < end; pos++) {
        unsigned char c = (unsigned char)p->text[pos];

        if (c == '\t') {
            do {
                out[col++] = ' ';
            } while (col % PG_TAB && col < p->width);
        } else {
            out[col++] = (c < 0x20 || c > 0x7e) ? '?' : (char)c;
        }
    }
    out[col] = 0;
    return (int)col;
}

int pg_status(const pg_t *p, char *out, size_t size)
{
    uint32_t first;
    uint32_t count;
    int n;

    if (p == 0 || out == 0) {
        return PG_E_ARG;
    }
    pg_visible(p, &first, &count);
    if (p->total == 0) {
        n = k_snprintf(out, size, "line 0 to 0 of 0");
    } else {
        n = k_snprintf(out, size, "line %u to %u of %u", (unsigned)(first + 1u),
                       (unsigned)(first + count), (unsigned)p->total);
    }
    if (n < 0 || (size_t)n >= size) {
        if (size > 0) {
            out[0] = 0;
        }
        return PG_E_SPACE;
    }
    return n;
}

int pg_key(pg_t *p, unsigned key)
{
    if (p == 0) {
        return PG_E_ARG;
    }
    switch (key) {
    case INPUT_KEY_UP:
    case TUI_CHAR('k'):
        pg_line_up(p);
        break;
    case INPUT_KEY_DOWN:
    case TUI_CHAR('j'):
        pg_line_down(p);
        break;
    case INPUT_KEY_LEFT:
    case INPUT_KEY_F1:
    case TUI_CHAR('b'):
        pg_page_up(p);
        break;
    case INPUT_KEY_RIGHT:
    case INPUT_KEY_SELECT:
    case INPUT_KEY_F2:
    case TUI_CHAR(' '):
        pg_page_down(p);
        break;
    case INPUT_KEY_F3:
    case TUI_CHAR('g'):
        pg_home(p);
        break;
    case INPUT_KEY_F4:
    case TUI_CHAR('G'):
        pg_end(p);
        break;
    case INPUT_KEY_BACK:
    case TUI_CHAR('q'):
        return PG_EXIT;
    default:
        break;
    }
    return PG_OK;
}
