/*
 * The password hacking minigame: pure model. See hack.h and
 * docs/modules/hack.md.
 */
#include "hack.h"

#include <robco/klibc.h>

static const char *const hk_err_text[HK_ERR_COUNT] = {
    "ok",
    "bad argument",
    "word of the wrong length",
    "character outside A to Z",
    "duplicate word",
    "too many words",
    "too few words",
    "not allowed in this state",
};

static const hk_diff_info_t hk_diffs[HK_DIFF_COUNT] = {
    { 4, 5, 8 },
    { 6, 8, 12 },
    { 9, 10, 14 },
    { 11, 12, 16 },
};

static const char *const hk_diff_names[HK_DIFF_COUNT] = {
    "novice", "advanced", "expert", "master",
};

static const char hk_junk[] = HK_JUNK_CHARS;
static const char hk_open[] = "([{<";
static const char hk_close[] = ")]}>";

const char *hk_strerror(int err)
{
    if (err < 0 || err >= HK_ERR_COUNT) {
        return "unknown error";
    }
    return hk_err_text[err];
}

/* ---- generator ---- */

void hk_rng_seed(hk_rng_t *r, uint32_t seed)
{
    r->state = seed ? seed : 0x9E3779B9u;
}

uint32_t hk_rng_next(hk_rng_t *r)
{
    uint32_t x = r->state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

uint32_t hk_rng_below(hk_rng_t *r, uint32_t n)
{
    uint64_t v = (uint64_t)hk_rng_next(r) * n;

    return (uint32_t)(v >> 32);
}

/* ---- difficulty ---- */

static char hk_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int hk_difficulty_parse(const char *name, hk_difficulty_t *out)
{
    unsigned d;

    if (name == NULL || out == NULL) {
        return HK_ERR_ARG;
    }
    for (d = 0; d < HK_DIFF_COUNT; d++) {
        const char *a = name;
        const char *b = hk_diff_names[d];

        while (*a != '\0' && hk_lower(*a) == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            *out = (hk_difficulty_t)d;
            return HK_OK;
        }
    }
    return HK_ERR_ARG;
}

const char *hk_difficulty_name(hk_difficulty_t d)
{
    if ((unsigned)d >= HK_DIFF_COUNT) {
        return NULL;
    }
    return hk_diff_names[d];
}

const hk_diff_info_t *hk_difficulty_info(hk_difficulty_t d)
{
    if ((unsigned)d >= HK_DIFF_COUNT) {
        return NULL;
    }
    return &hk_diffs[d];
}

unsigned hk_pick_length(hk_difficulty_t d, hk_rng_t *r)
{
    const hk_diff_info_t *info = hk_difficulty_info(d);

    if (info == NULL || r == NULL) {
        return 0;
    }
    return info->min_len + hk_rng_below(r, (uint32_t)(info->max_len - info->min_len + 1));
}

/* ---- likeness ---- */

unsigned hk_likeness(const char *a, const char *b, unsigned len)
{
    unsigned i, n = 0;

    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (a[i] == b[i]) {
            n++;
        }
    }
    return n;
}

/* ---- word lists ---- */

int hk_list_parse(const char *buf, size_t size, unsigned word_len, hk_list_t *out)
{
    size_t pos = 0;
    uint32_t line = 0;

    if (out == NULL) {
        return HK_ERR_ARG;
    }
    out->count = 0;
    out->len = (uint8_t)word_len;
    out->err_line = 0;
    if ((buf == NULL && size > 0) || word_len < HK_WORD_MIN || word_len > HK_WORD_MAX) {
        return HK_ERR_ARG;
    }
    while (pos < size) {
        size_t start = pos, end, i;
        unsigned k;

        line++;
        while (pos < size && buf[pos] != '\n') {
            pos++;
        }
        end = pos;
        if (pos < size) {
            pos++; /* the LF */
        }
        if (end > start && buf[end - 1] == '\r') {
            end--;
        }
        if (end == start) {
            continue; /* blank line */
        }
        for (i = start; i < end; i++) {
            if (buf[i] < 'A' || buf[i] > 'Z') {
                out->err_line = line;
                return HK_ERR_CHAR;
            }
        }
        if (end - start != word_len) {
            out->err_line = line;
            return HK_ERR_LENGTH;
        }
        for (k = 0; k < out->count; k++) {
            if (k_memcmp(out->word[k], buf + start, word_len) == 0) {
                out->err_line = line;
                return HK_ERR_DUP;
            }
        }
        if (out->count >= HK_LIST_MAX) {
            out->err_line = line;
            return HK_ERR_TOO_MANY;
        }
        out->word[out->count++] = buf + start;
    }
    return HK_OK;
}

/* ---- candidate selection ---- */

static void hk_shuffle16(uint16_t *a, unsigned n, hk_rng_t *r)
{
    unsigned i;

    for (i = n; i > 1; i--) {
        unsigned j = hk_rng_below(r, i);
        uint16_t t = a[i - 1];

        a[i - 1] = a[j];
        a[j] = t;
    }
}

/* Moves k random elements of a[0..n) to its front. */
static void hk_take16(uint16_t *a, unsigned n, unsigned k, hk_rng_t *r)
{
    unsigned i;

    for (i = 0; i < k && i < n; i++) {
        unsigned j = i + hk_rng_below(r, n - i);
        uint16_t t = a[i];

        a[i] = a[j];
        a[j] = t;
    }
}

int hk_pick(hk_list_t *list, hk_difficulty_t d, hk_rng_t *r, hk_pick_t *out)
{
    const hk_diff_info_t *info = hk_difficulty_info(d);
    const char *pw;
    unsigned n, k, m, i, j, b, q1, q2, q3, c = 0;
    uint16_t *s;

    if (list == NULL || r == NULL || out == NULL || info == NULL) {
        return HK_ERR_ARG;
    }
    if (list->len < info->min_len || list->len > info->max_len) {
        return HK_ERR_LENGTH;
    }
    n = info->words;
    if (list->count < n) {
        return HK_ERR_TOO_FEW;
    }
    s = list->scratch;
    i = hk_rng_below(r, list->count);
    pw = list->word[i];
    m = 0;
    for (j = 0; j < list->count; j++) {
        if (j != i) {
            s[m++] = (uint16_t)j;
        }
    }
    /* Random order first, so the stable sort breaks ties at random. */
    hk_shuffle16(s, m, r);
    for (i = 1; i < m; i++) {
        uint16_t v = s[i];
        unsigned lv = hk_likeness(list->word[v], pw, list->len);

        j = i;
        while (j > 0 && hk_likeness(list->word[s[j - 1]], pw, list->len) < lv) {
            s[j] = s[j - 1];
            j--;
        }
        s[j] = v;
    }
    k = n - 1;
    q1 = (k + 2) / 3;
    q2 = (k + 1) / 3;
    q3 = k / 3;
    b = m / 3;
    out->len = list->len;
    out->count = (uint8_t)n;
    if (b >= q1) {
        hk_take16(s, b, q1, r);
        hk_take16(s + b, b, q2, r);
        hk_take16(s + 2 * b, m - 2 * b, q3, r);
        for (i = 0; i < q1; i++) {
            out->word[c++] = list->word[s[i]];
        }
        for (i = 0; i < q2; i++) {
            out->word[c++] = list->word[s[b + i]];
        }
        for (i = 0; i < q3; i++) {
            out->word[c++] = list->word[s[2 * b + i]];
        }
        out->spread_ok = 1;
    } else {
        hk_take16(s, m, k, r);
        for (i = 0; i < k; i++) {
            out->word[c++] = list->word[s[i]];
        }
        out->spread_ok = 0;
    }
    out->word[c] = pw;
    out->password = (uint8_t)c;
    /* Shuffle the set so the password has no fixed place. */
    for (i = n; i > 1; i--) {
        const char *t;

        j = hk_rng_below(r, i);
        t = out->word[i - 1];
        out->word[i - 1] = out->word[j];
        out->word[j] = t;
        if (out->password == i - 1) {
            out->password = (uint8_t)j;
        } else if (out->password == j) {
            out->password = (uint8_t)(i - 1);
        }
    }
    return HK_OK;
}

/* ---- board ---- */

static int hk_is_letter(char c)
{
    return c >= 'A' && c <= 'Z';
}

static int hk_bracket_kind(const char *set, char c)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (set[i] == c) {
            return i;
        }
    }
    return -1;
}

static void hk_find_pairs(hk_board_t *b)
{
    unsigned i;

    for (i = 0; i < HK_BOARD_CHARS; i++) {
        int kind = hk_bracket_kind(hk_open, b->cell[i]);
        unsigned j;

        b->pair_close[i] = -1;
        if (kind < 0) {
            continue;
        }
        for (j = i + 1; j < HK_BOARD_CHARS && j % HK_ROW_CHARS != 0; j++) {
            if (hk_is_letter(b->cell[j])) {
                break;
            }
            if (b->cell[j] == hk_close[kind]) {
                b->pair_close[i] = (int16_t)j;
                break;
            }
        }
    }
}

unsigned hk_board_pair_count(const hk_board_t *b)
{
    unsigned i, n = 0;

    if (b == NULL) {
        return 0;
    }
    for (i = 0; i < HK_BOARD_CHARS; i++) {
        if (b->pair_close[i] >= 0) {
            n++;
        }
    }
    return n;
}

/*
 * A plant spot: two cells on one row, both junk that is not a bracket.
 * Writing an opener and its closer there never breaks an existing pair.
 */
static int hk_plant_ok(const hk_board_t *b, unsigned i)
{
    char c0, c1;

    if (i % HK_ROW_CHARS == HK_ROW_CHARS - 1) {
        return 0;
    }
    c0 = b->cell[i];
    c1 = b->cell[i + 1];
    return !hk_is_letter(c0) && !hk_is_letter(c1) &&
           hk_bracket_kind(hk_open, c0) < 0 && hk_bracket_kind(hk_close, c0) < 0 &&
           hk_bracket_kind(hk_open, c1) < 0 && hk_bracket_kind(hk_close, c1) < 0;
}

unsigned hk_column_free(unsigned w, unsigned len)
{
    return w == 0 ? HK_COL_CHARS : HK_COL_CHARS - w * len - (w - 1);
}

void hk_column_place(unsigned w, unsigned len, const uint8_t *gap, uint16_t *pos)
{
    unsigned i, at = gap[0];

    for (i = 0; i < w; i++) {
        pos[i] = (uint16_t)at;
        at += len + 1 + gap[i + 1];
    }
}

static void hk_place_column(hk_board_t *b, const hk_pick_t *p, unsigned col,
                            unsigned first, unsigned w, hk_rng_t *r)
{
    uint8_t gap[HK_WORDS_MAX + 1];
    uint16_t pos[HK_WORDS_MAX];
    unsigned free_units, i;

    if (w == 0) {
        return;
    }
    k_memset(gap, 0, sizeof(gap));
    free_units = hk_column_free(w, p->len);
    for (i = 0; i < free_units; i++) {
        gap[hk_rng_below(r, w + 1)]++;
    }
    hk_column_place(w, p->len, gap, pos);
    for (i = 0; i < w; i++) {
        b->word_pos[first + i] = (uint16_t)(col * HK_COL_CHARS + pos[i]);
        k_memcpy(&b->cell[b->word_pos[first + i]], p->word[first + i], p->len);
    }
}

int hk_board_build(hk_board_t *b, const hk_pick_t *p, hk_rng_t *r)
{
    unsigned i, left, tries;

    if (b == NULL || p == NULL || r == NULL || p->count == 0 ||
        p->count > HK_WORDS_MAX || p->len < HK_WORD_MIN || p->len > HK_WORD_MAX ||
        p->password >= p->count) {
        return HK_ERR_ARG;
    }
    for (i = 0; i < p->count; i++) {
        if (p->word[i] == NULL) {
            return HK_ERR_ARG;
        }
    }
    b->start_addr = (uint16_t)(HK_ADDR_MIN + hk_rng_below(r, HK_ADDR_SPAN));
    b->count = p->count;
    b->len = p->len;
    for (i = 0; i < HK_BOARD_CHARS; i++) {
        b->cell[i] = hk_junk[hk_rng_below(r, sizeof(hk_junk) - 1)];
    }
    left = (p->count + 1u) / 2u;
    hk_place_column(b, p, 0, 0, left, r);
    hk_place_column(b, p, 1, left, p->count - left, r);
    hk_find_pairs(b);
    for (tries = 0; tries < 64 && hk_board_pair_count(b) < HK_MIN_PAIRS; tries++) {
        unsigned at = hk_rng_below(r, HK_BOARD_CHARS);

        if (hk_plant_ok(b, at)) {
            unsigned kind = hk_rng_below(r, 4);

            b->cell[at] = hk_open[kind];
            b->cell[at + 1] = hk_close[kind];
            hk_find_pairs(b);
        }
    }
    for (i = 0; i < HK_BOARD_CHARS && hk_board_pair_count(b) < HK_MIN_PAIRS; i++) {
        if (hk_plant_ok(b, i)) {
            b->cell[i] = '(';
            b->cell[i + 1] = ')';
            hk_find_pairs(b);
        }
    }
    return HK_OK;
}

uint16_t hk_board_address(const hk_board_t *b, unsigned col, unsigned row)
{
    if (b == NULL || col >= HK_COLS || row >= HK_ROWS) {
        return 0;
    }
    return (uint16_t)(b->start_addr + (col * HK_ROWS + row) * HK_ROW_CHARS);
}

int hk_format_address(uint16_t addr, char *buf, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    if (buf == NULL || size < HK_ADDR_TEXT_SIZE) {
        return HK_ERR_ARG;
    }
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 4; i++) {
        buf[2 + i] = hex[(addr >> (12 - 4 * i)) & 0xF];
    }
    buf[6] = '\0';
    return HK_OK;
}

int hk_board_word_at(const hk_board_t *b, unsigned i)
{
    unsigned w;

    if (b == NULL || i >= HK_BOARD_CHARS) {
        return -1;
    }
    for (w = 0; w < b->count; w++) {
        if (i >= b->word_pos[w] && i < (unsigned)b->word_pos[w] + b->len) {
            return (int)w;
        }
    }
    return -1;
}

/* ---- game ---- */

static void hk_push(hk_game_t *g, const char *text, unsigned len)
{
    unsigned idx;

    if (len > HK_LINE_SIZE - 1) {
        len = HK_LINE_SIZE - 1; /* never happens: lines are at most 12 */
    }
    if (g->hist_count < HK_HISTORY_LINES) {
        idx = (g->hist_head + g->hist_count) % HK_HISTORY_LINES;
        g->hist_count++;
    } else {
        idx = g->hist_head;
        g->hist_head = (uint8_t)((g->hist_head + 1) % HK_HISTORY_LINES);
    }
    k_memcpy(g->history[idx], text, len);
    g->history[idx][len] = '\0';
}

static void hk_push_str(hk_game_t *g, const char *s)
{
    hk_push(g, s, (unsigned)k_strlen(s));
}

int hk_game_init(hk_game_t *g, const hk_pick_t *p, const hk_config_t *cfg, const hk_rng_t *r)
{
    int err;

    if (g == NULL || p == NULL || cfg == NULL || r == NULL ||
        cfg->attempts < HK_ATTEMPTS_MIN || cfg->attempts > HK_ATTEMPTS_MAX ||
        cfg->lockout_s > HK_LOCKOUT_S_MAX ||
        cfg->ticks_per_s == 0 || cfg->ticks_per_s > HK_TPS_MAX) {
        return HK_ERR_ARG;
    }
    k_memset(g, 0, sizeof(*g));
    g->rng = *r;
    err = hk_board_build(&g->board, p, &g->rng);
    if (err != HK_OK) {
        return err;
    }
    g->cfg = *cfg;
    g->state = HK_PLAYING;
    g->attempts = cfg->attempts;
    g->password = p->password;
    return HK_OK;
}

void hk_move(hk_game_t *g, hk_dir_t dir)
{
    unsigned col, row, x;

    if (g == NULL) {
        return;
    }
    col = g->cursor / HK_COL_CHARS;
    row = (g->cursor % HK_COL_CHARS) / HK_ROW_CHARS;
    x = g->cursor % HK_ROW_CHARS;
    switch (dir) {
    case HK_LEFT:
        if (x > 0) {
            x--;
        } else if (col == 1) {
            col = 0;
            x = HK_ROW_CHARS - 1;
        }
        break;
    case HK_RIGHT:
        if (x < HK_ROW_CHARS - 1) {
            x++;
        } else if (col == 0) {
            col = 1;
            x = 0;
        }
        break;
    case HK_UP:
        if (row > 0) {
            row--;
        }
        break;
    case HK_DOWN:
        if (row < HK_ROWS - 1) {
            row++;
        }
        break;
    default:
        break;
    }
    g->cursor = (uint16_t)(col * HK_COL_CHARS + row * HK_ROW_CHARS + x);
}

static int hk_live_word_at(const hk_game_t *g, unsigned i)
{
    int w = hk_board_word_at(&g->board, i);

    if (w >= 0 && g->dud[w]) {
        return -1;
    }
    return w;
}

hk_hl_t hk_highlight(const hk_game_t *g, unsigned *start, unsigned *len)
{
    unsigned s = 0, n = 0;
    hk_hl_t kind = HK_HL_CHAR;
    int w;

    if (g != NULL) {
        unsigned c = g->cursor;

        s = c;
        n = 1;
        w = hk_live_word_at(g, c);
        if (w >= 0) {
            kind = HK_HL_WORD;
            s = g->board.word_pos[w];
            n = g->board.len;
        } else if (g->board.pair_close[c] >= 0 && !g->used[c]) {
            kind = HK_HL_PAIR;
            n = (unsigned)g->board.pair_close[c] - c + 1;
        }
    }
    if (start != NULL) {
        *start = s;
    }
    if (len != NULL) {
        *len = n;
    }
    return kind;
}

static void hk_push_likeness(hk_game_t *g, unsigned n)
{
    char line[HK_LINE_SIZE];
    unsigned i = 0;

    k_memcpy(line, "Likeness=", 9);
    i = 9;
    if (n >= 10) {
        line[i++] = (char)('0' + n / 10);
    }
    line[i++] = (char)('0' + n % 10);
    hk_push(g, line, i);
}

hk_event_t hk_select(hk_game_t *g, uint32_t now)
{
    unsigned c;
    int w;

    if (g == NULL || g->state != HK_PLAYING) {
        return HK_EV_NONE;
    }
    c = g->cursor;
    w = hk_live_word_at(g, c);
    if (w >= 0) {
        const char *word = &g->board.cell[g->board.word_pos[w]];
        const char *pw = &g->board.cell[g->board.word_pos[g->password]];

        hk_push(g, word, g->board.len);
        if ((unsigned)w == g->password) {
            hk_push_str(g, "Exact match.");
            g->state = HK_WON;
            return HK_EV_WIN;
        }
        hk_push_str(g, "Entry denied.");
        hk_push_likeness(g, hk_likeness(word, pw, g->board.len));
        g->attempts--;
        if (g->attempts == 0) {
            g->state = HK_LOCKED;
            g->lock_start = now;
            g->lock_ticks = (uint32_t)g->cfg.lockout_s * g->cfg.ticks_per_s;
            return HK_EV_LOCKOUT;
        }
        return HK_EV_DENIED;
    }
    if (g->board.pair_close[c] >= 0 && !g->used[c]) {
        unsigned duds = 0, k;

        g->used[c] = 1;
        hk_push(g, &g->board.cell[c], (unsigned)g->board.pair_close[c] - c + 1);
        for (k = 0; k < g->board.count; k++) {
            if (k != g->password && !g->dud[k]) {
                duds++;
            }
        }
        if (duds == 0 || hk_rng_below(&g->rng, HK_RESET_CHANCE) == 0) {
            g->attempts = g->cfg.attempts;
            hk_push_str(g, "Tries reset.");
            return HK_EV_RESET;
        }
        k = hk_rng_below(&g->rng, duds);
        for (w = 0; w < g->board.count; w++) {
            if ((unsigned)w == g->password || g->dud[w]) {
                continue;
            }
            if (k-- == 0) {
                g->dud[w] = 1;
                k_memset(&g->board.cell[g->board.word_pos[w]], HK_DUD_CHAR, g->board.len);
                break;
            }
        }
        hk_push_str(g, "Dud removed.");
        return HK_EV_DUD;
    }
    return HK_EV_NONE;
}

void hk_cancel(hk_game_t *g)
{
    if (g != NULL && g->state == HK_PLAYING) {
        g->state = HK_CANCELLED;
    }
}

hk_state_t hk_result(const hk_game_t *g, uint32_t now, uint32_t *secs)
{
    uint32_t left = 0;

    if (g == NULL) {
        return HK_CANCELLED;
    }
    if (g->state == HK_LOCKED) {
        uint32_t elapsed = now - g->lock_start;

        if (elapsed < g->lock_ticks) {
            uint32_t rem = g->lock_ticks - elapsed;

            left = rem / g->cfg.ticks_per_s + (rem % g->cfg.ticks_per_s != 0);
        }
    }
    if (secs != NULL) {
        *secs = left;
    }
    return g->state;
}

unsigned hk_history_count(const hk_game_t *g)
{
    return g == NULL ? 0 : g->hist_count;
}

const char *hk_history_line(const hk_game_t *g, unsigned i)
{
    if (g == NULL || i >= g->hist_count) {
        return NULL;
    }
    return g->history[(g->hist_head + i) % HK_HISTORY_LINES];
}
