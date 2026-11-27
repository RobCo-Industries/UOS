/*
 * The console service's decisions (pure). See conview.h.
 */

#include "conview.h"
#include "lineedit.h"

static const char HINTS[] = "  UP/DN line  LT/RT page  F3/F4 ends  BACK quit";

unsigned cv_input_row(unsigned want, unsigned first, unsigned count)
{
    if (count > 0 && want >= first && first > 0) {
        return first - 1u;
    }
    return want;
}

int cv_opens_osk(const input_event_t *ev, int osk_open)
{
    if (ev == NULL || osk_open || ev->type != INPUT_EV_KEY || ev->value == INPUT_RELEASE) {
        return 0;
    }
    if (ev->source == INPUT_SRC_GPIO) {
        return 1;
    }
    return ev->code == CV_OSK_KEY && ev->value == INPUT_PRESS;
}

int cv_alt_key(const input_event_t *ev, int alt, unsigned len, int osk_open)
{
    if (ev == NULL || !alt || len != 0 || osk_open || ev->type != INPUT_EV_KEY ||
        ev->value != INPUT_PRESS) {
        return 0;
    }
    if (ev->source == INPUT_SRC_GPIO) {
        return ev->code == CV_ALT_BUTTON;
    }
    return ev->code == CV_ALT_KEY;
}

unsigned cv_line_width(size_t prompt_len, unsigned cols)
{
    unsigned w;

    if (prompt_len > CV_PROMPT_MAX) {
        prompt_len = CV_PROMPT_MAX;
    }
    w = cols > prompt_len + 1u ? cols - 1u - (unsigned)prompt_len : 0u;
    if (w < LE_MIN_WIDTH) {
        w = LE_MIN_WIDTH;
    }
    if (w > LE_MAX_WIDTH) {
        w = LE_MAX_WIDTH;
    }
    return w;
}

int cv_pager_status(const pg_t *p, char *out, size_t size)
{
    char st[48];
    size_t n = 0;
    size_t i;
    int r;

    if (p == NULL || out == NULL || size < CV_STATUS_BUF) {
        return CV_ERR_ARG;
    }
    r = pg_status(p, st, sizeof(st));
    if (r < 0) {
        return CV_ERR_ARG;
    }
    for (i = 0; st[i] != 0 && n < CV_STATUS_MAX; i++) {
        out[n++] = st[i];
    }
    for (i = 0; HINTS[i] != 0 && n < CV_STATUS_MAX; i++) {
        out[n++] = HINTS[i];
    }
    out[n] = 0;
    return (int)n;
}

void cv_printable(char *dst, const char *src, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];

        if (c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c <= 0x7e)) {
            dst[i] = (char)c;
        } else {
            dst[i] = '.';
        }
    }
}

int cv_quote(char *out, size_t size, const char *text, size_t len, size_t limit)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    size_t i;
    int cut = 0;

    if (out == NULL || size < 3 || (text == NULL && len > 0)) {
        return CV_ERR_ARG;
    }
    out[n++] = '"';
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        int esc = c <= 0x20 || c >= 0x7f || c == '"' || c == '%';
        size_t need = esc ? 3u : 1u;

        /* Room for the character, the closing quote and the NUL. */
        if (i >= limit || n + need + 2u > size) {
            cut = 1;
            break;
        }
        if (esc) {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15u];
        } else {
            out[n++] = (char)c;
        }
    }
    out[n++] = '"';
    out[n] = 0;
    return cut;
}

int cv_parse_flags(const char *word, unsigned *flags)
{
    unsigned f = 0;
    size_t i;

    if (word == NULL || flags == NULL || word[0] == 0) {
        return CV_ERR_ARG;
    }
    if (word[0] == '-' && word[1] == 0) {
        *flags = 0;
        return 0;
    }
    for (i = 0; word[i] != 0; i++) {
        unsigned bit;

        if (word[i] == 'm') {
            bit = LE_F_MASK;
        } else if (word[i] == 'h') {
            bit = LE_F_NOHIST;
        } else {
            return CV_ERR_ARG;
        }
        if (f & bit) {
            return CV_ERR_ARG;
        }
        f |= bit;
    }
    *flags = f;
    return 0;
}

int cv_parse_count(const char *word, unsigned max, unsigned *out)
{
    unsigned v = 0;
    size_t i;

    if (word == NULL || out == NULL || word[0] == 0) {
        return CV_ERR_ARG;
    }
    for (i = 0; word[i] != 0; i++) {
        if (word[i] < '0' || word[i] > '9' || i >= 6) {
            return CV_ERR_ARG;
        }
        v = v * 10u + (unsigned)(word[i] - '0');
    }
    if (v == 0 || v > max) {
        return CV_ERR_ARG;
    }
    *out = v;
    return 0;
}
