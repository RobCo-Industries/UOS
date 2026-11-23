/* Host tests for src/hack/hack.c. Run from the repository root. */
#include "hack.h"

#include "rc_test.h"

#define CARD_DIR "card/UOS/WORDS/"

static char g_file[16][8192];
static hk_list_t g_list;
static hk_game_t g_game;

/* Loads card/UOS/WORDS/Wnn.TXT into a static buffer and parses it. */
static size_t load_list(unsigned n, hk_list_t *list)
{
    char path[64];
    FILE *f;
    size_t len;
    int err;

    snprintf(path, sizeof(path), CARD_DIR "W%02u.TXT", n);
    f = fopen(path, "rb");
    if (f == NULL) {
        RC_FAIL("cannot open %s (run from the repository root)", path);
        return 0;
    }
    len = fread(g_file[n], 1, sizeof(g_file[n]), f);
    fclose(f);
    CHECK(len < sizeof(g_file[n]));
    err = hk_list_parse(g_file[n], len, n, list);
    CHECK_EQ(err, HK_OK);
    return list->count;
}

static int in_junk(char c)
{
    return c != '\0' && strchr(HK_JUNK_CHARS, c) != NULL;
}

/* ---- errors, generator, difficulty ---- */

TEST(strerror_every_code)
{
    CHECK_STR(hk_strerror(HK_OK), "ok");
    CHECK_STR(hk_strerror(HK_ERR_ARG), "bad argument");
    CHECK_STR(hk_strerror(HK_ERR_LENGTH), "word of the wrong length");
    CHECK_STR(hk_strerror(HK_ERR_CHAR), "character outside A to Z");
    CHECK_STR(hk_strerror(HK_ERR_DUP), "duplicate word");
    CHECK_STR(hk_strerror(HK_ERR_TOO_MANY), "too many words");
    CHECK_STR(hk_strerror(HK_ERR_TOO_FEW), "too few words");
    CHECK_STR(hk_strerror(HK_ERR_STATE), "not allowed in this state");
    CHECK_STR(hk_strerror(HK_ERR_COUNT), "unknown error");
    CHECK_STR(hk_strerror(-1), "unknown error");
}

TEST(rng_xorshift32)
{
    hk_rng_t r, z;
    unsigned i;

    hk_rng_seed(&r, 1);
    CHECK_EQ(hk_rng_next(&r), 270369u);
    CHECK_EQ(hk_rng_next(&r), 67634689u);
    hk_rng_seed(&z, 0);
    CHECK_EQ(z.state, 0x9E3779B9u);
    CHECK(hk_rng_next(&z) != 0);
    CHECK_EQ(hk_rng_below(&r, 0), 0);
    for (i = 0; i < 10000; i++) {
        CHECK(hk_rng_below(&r, 7) < 7);
        CHECK_EQ(hk_rng_below(&r, 1), 0);
    }
}

TEST(difficulty_table)
{
    hk_difficulty_t d;
    hk_rng_t r;
    unsigned i, seen4 = 0, seen5 = 0;

    CHECK_EQ(hk_difficulty_parse("novice", &d), HK_OK);
    CHECK_EQ(d, HK_NOVICE);
    CHECK_EQ(hk_difficulty_parse("Advanced", &d), HK_OK);
    CHECK_EQ(d, HK_ADVANCED);
    CHECK_EQ(hk_difficulty_parse("EXPERT", &d), HK_OK);
    CHECK_EQ(d, HK_EXPERT);
    CHECK_EQ(hk_difficulty_parse("master", &d), HK_OK);
    CHECK_EQ(d, HK_MASTER);
    CHECK_EQ(hk_difficulty_parse("mast", &d), HK_ERR_ARG);
    CHECK_EQ(hk_difficulty_parse("masters", &d), HK_ERR_ARG);
    CHECK_EQ(hk_difficulty_parse("", &d), HK_ERR_ARG);
    CHECK_EQ(hk_difficulty_parse(rc_launder(NULL), &d), HK_ERR_ARG);
    CHECK_EQ(hk_difficulty_parse("novice", rc_launder(NULL)), HK_ERR_ARG);
    CHECK_STR(hk_difficulty_name(HK_EXPERT), "expert");
    CHECK(hk_difficulty_name(HK_DIFF_COUNT) == NULL);
    CHECK(hk_difficulty_info(HK_DIFF_COUNT) == NULL);
    CHECK_EQ(hk_difficulty_info(HK_NOVICE)->words, 8);
    CHECK_EQ(hk_difficulty_info(HK_ADVANCED)->words, 12);
    CHECK_EQ(hk_difficulty_info(HK_EXPERT)->words, 14);
    CHECK_EQ(hk_difficulty_info(HK_MASTER)->words, 16);
    CHECK_EQ(hk_difficulty_info(HK_ADVANCED)->min_len, 6);
    CHECK_EQ(hk_difficulty_info(HK_ADVANCED)->max_len, 8);
    CHECK_EQ(hk_difficulty_info(HK_MASTER)->max_len, 12);
    hk_rng_seed(&r, 5);
    for (i = 0; i < 200; i++) {
        unsigned l = hk_pick_length(HK_NOVICE, &r);

        CHECK(l == 4 || l == 5);
        seen4 += l == 4;
        seen5 += l == 5;
    }
    CHECK(seen4 > 0 && seen5 > 0);
    CHECK_EQ(hk_pick_length(HK_DIFF_COUNT, &r), 0);
    CHECK_EQ(hk_pick_length(HK_NOVICE, rc_launder(NULL)), 0);
}

/* ---- likeness ---- */

TEST(likeness)
{
    CHECK_EQ(hk_likeness("CART", "CART", 4), 4);
    CHECK_EQ(hk_likeness("CART", "CARD", 4), 3);
    CHECK_EQ(hk_likeness("CART", "TRAC", 4), 0);
    CHECK_EQ(hk_likeness("ABCDEFGHIJKL", "ABCDEFGHIJKX", 12), 11);
    CHECK_EQ(hk_likeness("ABCD", "ABCX", 3), 3);
    CHECK_EQ(hk_likeness("ABCD", "ABCD", 0), 0);
    CHECK_EQ(hk_likeness(rc_launder(NULL), "ABCD", 4), 0);
}

/* ---- word lists ---- */

TEST(list_parse_formats)
{
    static const char lf[] = "ABCD\nEFGH\n\nIJKL";
    static const char crlf[] = "ABCD\r\nEFGH\r\n\r\nIJKL\r\n";

    CHECK_EQ(hk_list_parse(lf, sizeof(lf) - 1, 4, &g_list), HK_OK);
    CHECK_EQ(g_list.count, 3);
    CHECK(memcmp(g_list.word[2], "IJKL", 4) == 0);
    CHECK_EQ(hk_list_parse(crlf, sizeof(crlf) - 1, 4, &g_list), HK_OK);
    CHECK_EQ(g_list.count, 3);
    CHECK_EQ(g_list.err_line, 0);
    CHECK_EQ(hk_list_parse("", 0, 4, &g_list), HK_OK);
    CHECK_EQ(g_list.count, 0);
    CHECK_EQ(hk_list_parse(rc_launder(NULL), 0, 4, &g_list), HK_OK);
}

TEST(list_parse_errors)
{
    static const char len[] = "ABCD\nABCDE\n";
    static const char low[] = "ABCD\nabcd\n";
    static const char sp[] = "ABCD\nAB D\n";
    static const char cr[] = "AB\rD\n";
    static const char dup[] = "ABCD\nEFGH\nABCD\n";

    CHECK_EQ(hk_list_parse(len, sizeof(len) - 1, 4, &g_list), HK_ERR_LENGTH);
    CHECK_EQ(g_list.err_line, 2);
    CHECK_EQ(hk_list_parse(low, sizeof(low) - 1, 4, &g_list), HK_ERR_CHAR);
    CHECK_EQ(g_list.err_line, 2);
    CHECK_EQ(hk_list_parse(sp, sizeof(sp) - 1, 4, &g_list), HK_ERR_CHAR);
    CHECK_EQ(hk_list_parse(cr, sizeof(cr) - 1, 4, &g_list), HK_ERR_CHAR);
    CHECK_EQ(g_list.err_line, 1);
    CHECK_EQ(hk_list_parse(dup, sizeof(dup) - 1, 4, &g_list), HK_ERR_DUP);
    CHECK_EQ(g_list.err_line, 3);
    CHECK_EQ(hk_list_parse("ABC\n", 4, 3, &g_list), HK_ERR_ARG);
    CHECK_EQ(hk_list_parse("ABCDEFGHIJKLM\n", 14, 13, &g_list), HK_ERR_ARG);
    CHECK_EQ(hk_list_parse("ABCD", 4, 4, rc_launder(NULL)), HK_ERR_ARG);
    CHECK_EQ(hk_list_parse(rc_launder(NULL), 4, 4, &g_list), HK_ERR_ARG);
}

/* Builds n distinct 4-letter words, base 26, one per line. */
static size_t make_words(char *buf, unsigned n)
{
    unsigned i;
    size_t p = 0;

    for (i = 0; i < n; i++) {
        buf[p++] = (char)('A' + i / (26 * 26 * 26) % 26);
        buf[p++] = (char)('A' + i / (26 * 26) % 26);
        buf[p++] = (char)('A' + i / 26 % 26);
        buf[p++] = (char)('A' + i % 26);
        buf[p++] = '\n';
    }
    return p;
}

TEST(list_parse_limit)
{
    size_t len = make_words(g_file[0], HK_LIST_MAX);

    CHECK_EQ(hk_list_parse(g_file[0], len, 4, &g_list), HK_OK);
    CHECK_EQ(g_list.count, HK_LIST_MAX);
    len = make_words(g_file[0], HK_LIST_MAX + 1);
    CHECK_EQ(hk_list_parse(g_file[0], len, 4, &g_list), HK_ERR_TOO_MANY);
    CHECK_EQ(g_list.err_line, HK_LIST_MAX + 1);
}

TEST(card_lists_parse)
{
    unsigned n;

    for (n = HK_WORD_MIN; n <= HK_WORD_MAX; n++) {
        CHECK(load_list(n, &g_list) >= HK_LIST_MIN_FILE);
    }
}

/* ---- candidate selection ---- */

static int cmp_desc(const void *a, const void *b)
{
    return (int)*(const unsigned *)b - (int)*(const unsigned *)a;
}

/* Checks one pick; returns 1 when the spread rule holds. */
static void check_pick(const hk_list_t *list, const hk_pick_t *p, unsigned n)
{
    static unsigned like[HK_LIST_MAX];
    const char *pw = p->word[p->password];
    unsigned i, j, m = 0, b, k = n - 1, near = 0, far = 0;

    CHECK_EQ(p->count, n);
    CHECK_EQ(p->len, list->len);
    for (i = 0; i < p->count; i++) {
        for (j = i + 1; j < p->count; j++) {
            CHECK(memcmp(p->word[i], p->word[j], p->len) != 0);
        }
    }
    for (i = 0; i < list->count; i++) {
        if (memcmp(list->word[i], pw, list->len) != 0) {
            like[m++] = hk_likeness(list->word[i], pw, list->len);
        }
    }
    CHECK_EQ(m, list->count - 1u);
    if (!p->spread_ok) {
        return;
    }
    qsort(like, m, sizeof(like[0]), cmp_desc);
    b = m / 3;
    for (i = 0; i < p->count; i++) {
        unsigned l;

        if (i == p->password) {
            continue;
        }
        l = hk_likeness(p->word[i], pw, p->len);
        CHECK(l < p->len);
        near += l >= like[b - 1];
        far += l <= like[2 * b];
    }
    CHECK(near >= (k + 2) / 3);
    CHECK(far >= k / 3);
}

TEST(pick_every_difficulty)
{
    unsigned d, seed;

    for (d = 0; d < HK_DIFF_COUNT; d++) {
        const hk_diff_info_t *info = hk_difficulty_info((hk_difficulty_t)d);
        unsigned len;

        for (len = info->min_len; len <= info->max_len; len++) {
            load_list(len, &g_list);
            for (seed = 1; seed <= 300; seed++) {
                hk_rng_t r;
                hk_pick_t p;

                hk_rng_seed(&r, seed);
                CHECK_EQ(hk_pick(&g_list, (hk_difficulty_t)d, &r, &p), HK_OK);
                CHECK_EQ(p.spread_ok, 1);
                check_pick(&g_list, &p, info->words);
            }
        }
    }
}

TEST(pick_fallback_and_errors)
{
    hk_rng_t r;
    hk_pick_t p;
    size_t len;
    unsigned seed;

    /* 9 words for novice: m = 8, band 2 < quota 3, so the fallback runs. */
    len = make_words(g_file[0], 9);
    CHECK_EQ(hk_list_parse(g_file[0], len, 4, &g_list), HK_OK);
    for (seed = 1; seed < 50; seed++) {
        hk_rng_seed(&r, seed);
        CHECK_EQ(hk_pick(&g_list, HK_NOVICE, &r, &p), HK_OK);
        CHECK_EQ(p.spread_ok, 0);
        check_pick(&g_list, &p, 8);
    }
    /* 10 words: band 3 meets the quota. */
    len = make_words(g_file[0], 10);
    CHECK_EQ(hk_list_parse(g_file[0], len, 4, &g_list), HK_OK);
    hk_rng_seed(&r, 3);
    CHECK_EQ(hk_pick(&g_list, HK_NOVICE, &r, &p), HK_OK);
    CHECK_EQ(p.spread_ok, 1);
    check_pick(&g_list, &p, 8);
    /* Exactly the count works; one fewer fails. */
    len = make_words(g_file[0], 8);
    CHECK_EQ(hk_list_parse(g_file[0], len, 4, &g_list), HK_OK);
    CHECK_EQ(hk_pick(&g_list, HK_NOVICE, &r, &p), HK_OK);
    check_pick(&g_list, &p, 8);
    len = make_words(g_file[0], 7);
    CHECK_EQ(hk_list_parse(g_file[0], len, 4, &g_list), HK_OK);
    CHECK_EQ(hk_pick(&g_list, HK_NOVICE, &r, &p), HK_ERR_TOO_FEW);
    CHECK_EQ(hk_pick(&g_list, HK_ADVANCED, &r, &p), HK_ERR_LENGTH);
    CHECK_EQ(hk_pick(&g_list, HK_DIFF_COUNT, &r, &p), HK_ERR_ARG);
    CHECK_EQ(hk_pick(rc_launder(NULL), HK_NOVICE, &r, &p), HK_ERR_ARG);
}

/* ---- board ---- */

static void make_pick(hk_difficulty_t d, uint32_t seed, hk_pick_t *p)
{
    hk_rng_t r;
    unsigned len;

    hk_rng_seed(&r, seed);
    len = hk_pick_length(d, &r);
    load_list(len, &g_list);
    CHECK_EQ(hk_pick(&g_list, d, &r, p), HK_OK);
}

static unsigned count_pairs(const hk_board_t *b)
{
    static const char op[] = "([{<", cl[] = ")]}>";
    unsigned i, j, n = 0;

    for (i = 0; i < HK_BOARD_CHARS; i++) {
        const char *o = strchr(op, b->cell[i]);
        int close = -1;

        if (b->cell[i] == '\0' || o == NULL) {
            CHECK_EQ(b->pair_close[i], -1);
            continue;
        }
        for (j = i + 1; j < HK_BOARD_CHARS && j % HK_ROW_CHARS != 0; j++) {
            if (b->cell[j] >= 'A' && b->cell[j] <= 'Z') {
                break;
            }
            if (b->cell[j] == cl[o - op]) {
                close = (int)j;
                break;
            }
        }
        CHECK_EQ(b->pair_close[i], close);
        n += close >= 0;
    }
    return n;
}

static void check_board(const hk_board_t *b, const hk_pick_t *p)
{
    static uint8_t owned[HK_BOARD_CHARS];
    unsigned w, i, c;

    memset(owned, 0, sizeof(owned));
    CHECK_EQ(b->count, p->count);
    CHECK(b->start_addr >= 0x1000u);
    CHECK((unsigned)b->start_addr + HK_BOARD_CHARS - 1 <= 0xFFFFu);
    for (w = 0; w < b->count; w++) {
        unsigned s = b->word_pos[w], e = s + b->len - 1;

        CHECK_EQ(s / HK_COL_CHARS, e / HK_COL_CHARS);
        CHECK(memcmp(&b->cell[s], p->word[w], b->len) == 0);
        if (s % HK_COL_CHARS != 0) {
            CHECK(in_junk(b->cell[s - 1]));
        }
        if ((e + 1) % HK_COL_CHARS != 0) {
            CHECK(in_junk(b->cell[e + 1]));
        }
        for (i = s; i <= e; i++) {
            CHECK_EQ(owned[i], 0);
            owned[i] = 1;
            CHECK_EQ(hk_board_word_at(b, i), (int)w);
        }
    }
    for (i = 0; i < HK_BOARD_CHARS; i++) {
        if (!owned[i]) {
            CHECK(in_junk(b->cell[i]));
            CHECK_EQ(hk_board_word_at(b, i), -1);
        }
    }
    /* Each word appears exactly once, read along its column. */
    for (w = 0; w < b->count; w++) {
        unsigned hits = 0;

        for (c = 0; c < HK_COLS; c++) {
            for (i = 0; i + b->len <= HK_COL_CHARS; i++) {
                hits += memcmp(&b->cell[c * HK_COL_CHARS + i], p->word[w], b->len) == 0;
            }
        }
        CHECK_EQ(hits, 1);
    }
    CHECK(count_pairs(b) >= HK_MIN_PAIRS);
    CHECK_EQ(hk_board_pair_count(b), count_pairs(b));
}

TEST(board_invariants_1000_seeds)
{
    static hk_board_t b;
    unsigned d, seed;

    for (d = 0; d < HK_DIFF_COUNT; d++) {
        for (seed = 1; seed <= 1000; seed++) {
            hk_pick_t p;
            hk_rng_t r;

            make_pick((hk_difficulty_t)d, seed, &p);
            hk_rng_seed(&r, seed * 7919u);
            CHECK_EQ(hk_board_build(&b, &p, &r), HK_OK);
            check_board(&b, &p);
        }
    }
}

TEST(board_addresses)
{
    static hk_board_t b;
    hk_pick_t p;
    hk_rng_t r;
    char buf[HK_ADDR_TEXT_SIZE];

    make_pick(HK_NOVICE, 11, &p);
    hk_rng_seed(&r, 11);
    CHECK_EQ(hk_board_build(&b, &p, &r), HK_OK);
    CHECK_EQ(hk_board_address(&b, 0, 0), b.start_addr);
    CHECK_EQ(hk_board_address(&b, 0, 1), b.start_addr + 12u);
    CHECK_EQ(hk_board_address(&b, 1, 0), b.start_addr + 192u);
    CHECK_EQ(hk_board_address(&b, 1, 15), b.start_addr + 372u);
    CHECK_EQ(hk_board_address(&b, 2, 0), 0);
    CHECK_EQ(hk_board_address(&b, 0, 16), 0);
    CHECK_EQ(hk_format_address(0xF964, buf, sizeof(buf)), HK_OK);
    CHECK_STR(buf, "0xF964");
    CHECK_EQ(hk_format_address(0x1A0B, buf, sizeof(buf)), HK_OK);
    CHECK_STR(buf, "0x1A0B");
    CHECK_EQ(hk_format_address(0x1A0B, buf, 6), HK_ERR_ARG);
    CHECK_EQ(hk_format_address(0x1A0B, rc_launder(NULL), 7), HK_ERR_ARG);
}

TEST(board_bad_args)
{
    static hk_board_t b;
    hk_pick_t p;
    hk_rng_t r;

    make_pick(HK_NOVICE, 2, &p);
    hk_rng_seed(&r, 2);
    p.count = 0;
    CHECK_EQ(hk_board_build(&b, &p, &r), HK_ERR_ARG);
    p.count = HK_WORDS_MAX + 1;
    CHECK_EQ(hk_board_build(&b, &p, &r), HK_ERR_ARG);
    p.count = 8;
    p.len = 3;
    CHECK_EQ(hk_board_build(&b, &p, &r), HK_ERR_ARG);
    p.len = 13;
    CHECK_EQ(hk_board_build(&b, &p, &r), HK_ERR_ARG);
    p.len = 4;
    p.password = 8;
    CHECK_EQ(hk_board_build(&b, &p, &r), HK_ERR_ARG);
    CHECK_EQ(hk_board_build(&b, &p, rc_launder(NULL)), HK_ERR_ARG);
}

/* ---- game helpers ---- */

static const hk_config_t g_cfg = { 4, 60, 1000 };

static void start_game(hk_difficulty_t d, uint32_t seed, const hk_config_t *cfg)
{
    hk_pick_t p;
    hk_rng_t r;

    make_pick(d, seed, &p);
    hk_rng_seed(&r, seed);
    CHECK_EQ(hk_game_init(&g_game, &p, cfg, &r), HK_OK);
}

/* Moves the cursor to cell i with the public moves only. */
static void go_to(hk_game_t *g, unsigned i)
{
    unsigned k, col = i / HK_COL_CHARS, row = i % HK_COL_CHARS / HK_ROW_CHARS;
    unsigned x = i % HK_ROW_CHARS;

    for (k = 0; k < 40; k++) {
        hk_move(g, HK_LEFT);
        hk_move(g, HK_UP);
    }
    for (k = 0; k < col * HK_ROW_CHARS + x; k++) {
        hk_move(g, HK_RIGHT);
    }
    for (k = 0; k < row; k++) {
        hk_move(g, HK_DOWN);
    }
    CHECK_EQ(g->cursor, i);
}

static int first_wrong(const hk_game_t *g, int skip)
{
    unsigned w;

    for (w = 0; w < g->board.count; w++) {
        if (w != g->password && !g->dud[w] && (int)w != skip) {
            return (int)w;
        }
    }
    return -1;
}

static int next_opener(const hk_game_t *g, unsigned from)
{
    unsigned i;

    for (i = from; i < HK_BOARD_CHARS; i++) {
        if (g->board.pair_close[i] >= 0 && !g->used[i]) {
            return (int)i;
        }
    }
    return -1;
}

static void check_lines(const hk_game_t *g, const char *const *lines, unsigned n)
{
    unsigned i;

    CHECK_EQ(hk_history_count(g), n);
    for (i = 0; i < n; i++) {
        CHECK_STR(hk_history_line(g, i), lines[i]);
    }
    CHECK(hk_history_line(g, n) == NULL);
}

/* ---- cursor ---- */

TEST(cursor_moves)
{
    unsigned k, i;
    static uint8_t seen[HK_BOARD_CHARS];

    start_game(HK_NOVICE, 9, &g_cfg);
    CHECK_EQ(g_game.cursor, 0);
    hk_move(&g_game, HK_LEFT);
    CHECK_EQ(g_game.cursor, 0);
    hk_move(&g_game, HK_UP);
    CHECK_EQ(g_game.cursor, 0);
    for (k = 0; k < 11; k++) {
        hk_move(&g_game, HK_RIGHT);
    }
    CHECK_EQ(g_game.cursor, 11);
    hk_move(&g_game, HK_RIGHT);
    CHECK_EQ(g_game.cursor, 192);
    hk_move(&g_game, HK_LEFT);
    CHECK_EQ(g_game.cursor, 11);
    hk_move(&g_game, HK_RIGHT);
    for (k = 0; k < 11; k++) {
        hk_move(&g_game, HK_RIGHT);
    }
    CHECK_EQ(g_game.cursor, 203);
    hk_move(&g_game, HK_RIGHT);
    CHECK_EQ(g_game.cursor, 203);
    for (k = 0; k < 15; k++) {
        hk_move(&g_game, HK_DOWN);
    }
    CHECK_EQ(g_game.cursor, 383);
    hk_move(&g_game, HK_DOWN);
    CHECK_EQ(g_game.cursor, 383);
    hk_move(&g_game, (hk_dir_t)9);
    CHECK_EQ(g_game.cursor, 383);
    /* Sweep: every row, left to right across both columns. */
    memset(seen, 0, sizeof(seen));
    go_to(&g_game, 0);
    for (i = 0; i < HK_ROWS; i++) {
        for (k = 0; k < 2 * HK_ROW_CHARS; k++) {
            seen[g_game.cursor]++;
            hk_move(&g_game, HK_RIGHT);
        }
        for (k = 0; k < 2 * HK_ROW_CHARS; k++) {
            hk_move(&g_game, HK_LEFT);
        }
        CHECK_EQ(g_game.cursor % HK_COL_CHARS, i * HK_ROW_CHARS);
        CHECK_EQ(g_game.cursor / HK_COL_CHARS, 0);
        hk_move(&g_game, HK_DOWN);
    }
    for (i = 0; i < HK_BOARD_CHARS; i++) {
        CHECK_EQ(seen[i], 1);
    }
    hk_move(rc_launder(NULL), HK_LEFT);
}

TEST(highlight)
{
    unsigned s, n, w;
    int o;

    start_game(HK_ADVANCED, 21, &g_cfg);
    for (w = 0; w < g_game.board.count; w++) {
        go_to(&g_game, g_game.board.word_pos[w] + g_game.board.len - 1u);
        CHECK_EQ(hk_highlight(&g_game, &s, &n), HK_HL_WORD);
        CHECK_EQ(s, g_game.board.word_pos[w]);
        CHECK_EQ(n, g_game.board.len);
    }
    o = next_opener(&g_game, 0);
    CHECK(o >= 0);
    go_to(&g_game, (unsigned)o);
    CHECK_EQ(hk_highlight(&g_game, &s, &n), HK_HL_PAIR);
    CHECK_EQ(s, (unsigned)o);
    CHECK_EQ(n, (unsigned)g_game.board.pair_close[o] - (unsigned)o + 1);
    go_to(&g_game, (unsigned)g_game.board.pair_close[o]);
    CHECK_EQ(hk_highlight(&g_game, &s, &n), HK_HL_CHAR);
    CHECK_EQ(s, (unsigned)g_game.board.pair_close[o]);
    CHECK_EQ(n, 1);
    CHECK_EQ(hk_highlight(rc_launder(NULL), &s, &n), HK_HL_CHAR);
}

/* ---- scripted games ---- */

TEST(game_win)
{
    char pw[HK_WORD_MAX + 1];
    const char *lines[2];
    uint32_t secs = 99;

    start_game(HK_NOVICE, 31, &g_cfg);
    memcpy(pw, &g_game.board.cell[g_game.board.word_pos[g_game.password]], g_game.board.len);
    pw[g_game.board.len] = '\0';
    go_to(&g_game, g_game.board.word_pos[g_game.password] + 1u);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_WIN);
    lines[0] = pw;
    lines[1] = "Exact match.";
    check_lines(&g_game, lines, 2);
    CHECK_EQ(hk_result(&g_game, 0, &secs), HK_WON);
    CHECK_EQ(secs, 0);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_NONE);
    hk_cancel(&g_game);
    CHECK_EQ(hk_result(&g_game, 0, NULL), HK_WON);
}

TEST(game_lockout_and_lines)
{
    hk_config_t cfg = { 2, 60, 1000 };
    char w0[HK_WORD_MAX + 1], w1[HK_WORD_MAX + 1], l0[16], l1[16];
    const char *lines[6];
    const char *pw;
    int a, b;
    uint32_t secs;

    start_game(HK_ADVANCED, 41, &cfg);
    pw = &g_game.board.cell[g_game.board.word_pos[g_game.password]];
    a = first_wrong(&g_game, -1);
    b = first_wrong(&g_game, a);
    memcpy(w0, &g_game.board.cell[g_game.board.word_pos[a]], g_game.board.len);
    w0[g_game.board.len] = '\0';
    memcpy(w1, &g_game.board.cell[g_game.board.word_pos[b]], g_game.board.len);
    w1[g_game.board.len] = '\0';
    snprintf(l0, sizeof(l0), "Likeness=%u", hk_likeness(w0, pw, g_game.board.len));
    snprintf(l1, sizeof(l1), "Likeness=%u", hk_likeness(w1, pw, g_game.board.len));
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, 100), HK_EV_DENIED);
    CHECK_EQ(g_game.attempts, 1);
    CHECK_EQ(hk_result(&g_game, 100, &secs), HK_PLAYING);
    go_to(&g_game, g_game.board.word_pos[b]);
    CHECK_EQ(hk_select(&g_game, 5000), HK_EV_LOCKOUT);
    lines[0] = w0;
    lines[1] = "Entry denied.";
    lines[2] = l0;
    lines[3] = w1;
    lines[4] = "Entry denied.";
    lines[5] = l1;
    check_lines(&g_game, lines, 6);
    CHECK_EQ(hk_result(&g_game, 5000, &secs), HK_LOCKED);
    CHECK_EQ(secs, 60);
    CHECK_EQ(hk_result(&g_game, 5001, &secs), HK_LOCKED);
    CHECK_EQ(secs, 60);
    CHECK_EQ(hk_result(&g_game, 5000 + 59000, &secs), HK_LOCKED);
    CHECK_EQ(secs, 1);
    CHECK_EQ(hk_result(&g_game, 5000 + 60000, &secs), HK_LOCKED);
    CHECK_EQ(secs, 0);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_NONE);
    hk_cancel(&g_game);
    CHECK_EQ(hk_result(&g_game, 0, NULL), HK_LOCKED);
}

TEST(lockout_timer_wraps)
{
    hk_config_t cfg = { 1, 60, 1000 };
    uint32_t t0 = 0xFFFFFF00u, secs;
    int a;

    start_game(HK_NOVICE, 51, &cfg);
    a = first_wrong(&g_game, -1);
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, t0), HK_EV_LOCKOUT);
    CHECK_EQ(hk_result(&g_game, t0 + 30000u, &secs), HK_LOCKED);
    CHECK_EQ(secs, 30);
    CHECK_EQ(hk_result(&g_game, t0 + 59001u, &secs), HK_LOCKED);
    CHECK_EQ(secs, 1);
    CHECK_EQ(hk_result(&g_game, t0 + 60000u, &secs), HK_LOCKED);
    CHECK_EQ(secs, 0);
    /* Longest lockout at the fastest tick: no overflow. */
    cfg.lockout_s = HK_LOCKOUT_S_MAX;
    cfg.ticks_per_s = HK_TPS_MAX;
    start_game(HK_NOVICE, 51, &cfg);
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, t0), HK_EV_LOCKOUT);
    CHECK_EQ(hk_result(&g_game, t0, &secs), HK_LOCKED);
    CHECK_EQ(secs, 3600);
    /* lockout_s = 0: over at once. */
    cfg.lockout_s = 0;
    cfg.ticks_per_s = 1000;
    start_game(HK_NOVICE, 51, &cfg);
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, t0), HK_EV_LOCKOUT);
    CHECK_EQ(hk_result(&g_game, t0, &secs), HK_LOCKED);
    CHECK_EQ(secs, 0);
}

/* Finds a seed whose first bracket gives the wanted event. */
static uint32_t seed_for(hk_event_t want)
{
    uint32_t seed;

    for (seed = 1; seed < 1000; seed++) {
        hk_game_t *g = &g_game;
        int o;

        start_game(HK_NOVICE, seed, &g_cfg);
        o = next_opener(g, 0);
        go_to(g, (unsigned)o);
        if (hk_select(g, 0) == want) {
            return seed;
        }
    }
    RC_FAIL("no seed gives event %d", (int)want);
    return 1;
}

TEST(bracket_dud_and_reset)
{
    char pair[16];
    const char *lines[6];
    uint32_t seed;
    int o, w, a;
    unsigned dudw = 0, k, n;

    /* Dud removal. */
    seed = seed_for(HK_EV_DUD);
    start_game(HK_NOVICE, seed, &g_cfg);
    a = first_wrong(&g_game, -1);
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_DENIED);
    CHECK_EQ(g_game.attempts, 3);
    o = next_opener(&g_game, 0);
    n = (unsigned)g_game.board.pair_close[o] - (unsigned)o + 1;
    memcpy(pair, &g_game.board.cell[o], n);
    pair[n] = '\0';
    go_to(&g_game, (unsigned)o);
    /* A guess does not use the generator, so the bracket still removes a dud. */
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_DUD);
    CHECK_EQ(g_game.attempts, 3);
    lines[3] = pair;
    lines[4] = "Dud removed.";
    CHECK_EQ(hk_history_count(&g_game), 5);
    CHECK_STR(hk_history_line(&g_game, 3), lines[3]);
    CHECK_STR(hk_history_line(&g_game, 4), lines[4]);

    /* A fresh game with the found seed: the first bracket removes a dud. */
    start_game(HK_NOVICE, seed, &g_cfg);
    o = next_opener(&g_game, 0);
    n = (unsigned)g_game.board.pair_close[o] - (unsigned)o + 1;
    memcpy(pair, &g_game.board.cell[o], n);
    pair[n] = '\0';
    go_to(&g_game, (unsigned)o);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_DUD);
    lines[0] = pair;
    lines[1] = "Dud removed.";
    check_lines(&g_game, lines, 2);
    for (w = 0; w < g_game.board.count; w++) {
        if (g_game.dud[w]) {
            dudw = (unsigned)w;
            CHECK((unsigned)w != g_game.password);
            for (k = 0; k < g_game.board.len; k++) {
                CHECK_EQ(g_game.board.cell[g_game.board.word_pos[w] + k], '.');
            }
        }
    }
    go_to(&g_game, g_game.board.word_pos[dudw]);
    CHECK_EQ(hk_highlight(&g_game, NULL, NULL), HK_HL_CHAR);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_NONE);
    CHECK_EQ(g_game.attempts, 4);
    /* The same bracket a second time does nothing. */
    go_to(&g_game, (unsigned)o);
    CHECK_EQ(hk_highlight(&g_game, NULL, NULL), HK_HL_CHAR);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_NONE);
    CHECK_EQ(hk_history_count(&g_game), 2);
    CHECK_EQ(g_game.board.cell[o], pair[0]);

    /* Tries reset, forced by the seed. */
    seed = seed_for(HK_EV_RESET);
    start_game(HK_NOVICE, seed, &g_cfg);
    a = first_wrong(&g_game, -1);
    o = next_opener(&g_game, 0);
    /* Spend attempts on guesses first: they do not touch the generator. */
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_DENIED);
    go_to(&g_game, g_game.board.word_pos[a]);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_DENIED);
    CHECK_EQ(g_game.attempts, 2);
    go_to(&g_game, (unsigned)o);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_RESET);
    CHECK_EQ(g_game.attempts, 4);
    CHECK_STR(hk_history_line(&g_game, hk_history_count(&g_game) - 1), "Tries reset.");
}

TEST(brackets_never_remove_password)
{
    uint32_t seed;

    for (seed = 1; seed <= 200; seed++) {
        int o;
        unsigned duds = 0, w, resets = 0;

        start_game(HK_EXPERT, seed, &g_cfg);
        while ((o = next_opener(&g_game, 0)) >= 0) {
            hk_event_t ev;

            go_to(&g_game, (unsigned)o);
            ev = hk_select(&g_game, 0);
            CHECK(ev == HK_EV_DUD || ev == HK_EV_RESET);
            resets += ev == HK_EV_RESET;
            duds += ev == HK_EV_DUD;
        }
        CHECK(!g_game.dud[g_game.password]);
        CHECK(memcmp(&g_game.board.cell[g_game.board.word_pos[g_game.password]], ".", 1) != 0);
        for (w = 0, o = 0; w < g_game.board.count; w++) {
            o += g_game.dud[w];
        }
        CHECK_EQ((unsigned)o, duds);
        CHECK(duds <= g_game.board.count - 1u);
        (void)resets;
    }
}

TEST(all_duds_gone_then_reset)
{
    unsigned w;
    int o;

    start_game(HK_NOVICE, 61, &g_cfg);
    for (w = 0; w < g_game.board.count; w++) {
        if (w != g_game.password) {
            g_game.dud[w] = 1;
        }
    }
    o = next_opener(&g_game, 0);
    go_to(&g_game, (unsigned)o);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_RESET);
}

TEST(history_ring_and_two_digit_likeness)
{
    static const char *const words[2] = { "AAAAAAAAAAAB", "AAAAAAAAAAAA" };
    hk_config_t cfg = { 9, 60, 1000 };
    hk_pick_t p;
    hk_rng_t r;
    unsigned i;

    memset(&p, 0, sizeof(p));
    p.word[0] = words[0];
    p.word[1] = words[1];
    p.count = 2;
    p.len = 12;
    p.password = 1;
    hk_rng_seed(&r, 3);
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_OK);
    for (i = 0; i < 6; i++) {
        go_to(&g_game, g_game.board.word_pos[0]);
        CHECK_EQ(hk_select(&g_game, 0), HK_EV_DENIED);
    }
    /* 18 lines pushed, 16 kept: the oldest two are gone. */
    CHECK_EQ(hk_history_count(&g_game), HK_HISTORY_LINES);
    CHECK_STR(hk_history_line(&g_game, 0), "Likeness=11");
    CHECK_STR(hk_history_line(&g_game, 1), "AAAAAAAAAAAB");
    CHECK_STR(hk_history_line(&g_game, 2), "Entry denied.");
    CHECK_STR(hk_history_line(&g_game, 15), "Likeness=11");
    CHECK(hk_history_line(&g_game, 16) == NULL);
    CHECK(hk_history_line(rc_launder(NULL), 0) == NULL);
    CHECK_EQ(hk_history_count(rc_launder(NULL)), 0);
}

TEST(cancel_and_init_errors)
{
    hk_config_t cfg = g_cfg;
    hk_pick_t p;
    hk_rng_t r;

    start_game(HK_NOVICE, 71, &g_cfg);
    hk_cancel(&g_game);
    CHECK_EQ(hk_result(&g_game, 0, NULL), HK_CANCELLED);
    CHECK_EQ(hk_select(&g_game, 0), HK_EV_NONE);
    hk_cancel(rc_launder(NULL));
    CHECK_EQ(hk_result(rc_launder(NULL), 0, NULL), HK_CANCELLED);

    make_pick(HK_NOVICE, 71, &p);
    hk_rng_seed(&r, 71);
    cfg.attempts = 0;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_ERR_ARG);
    cfg.attempts = 10;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_ERR_ARG);
    cfg.attempts = 9;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_OK);
    cfg.lockout_s = 3601;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_ERR_ARG);
    cfg.lockout_s = 3600;
    cfg.ticks_per_s = 0;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_ERR_ARG);
    cfg.ticks_per_s = HK_TPS_MAX + 1;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_ERR_ARG);
    cfg.ticks_per_s = HK_TPS_MAX;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_OK);
    p.len = 2;
    CHECK_EQ(hk_game_init(&g_game, &p, &cfg, &r), HK_ERR_ARG);
    CHECK_EQ(hk_game_init(rc_launder(NULL), &p, &cfg, &r), HK_ERR_ARG);
}

TEST(game_does_not_touch_caller_rng)
{
    hk_pick_t p;
    hk_rng_t r;

    make_pick(HK_NOVICE, 81, &p);
    hk_rng_seed(&r, 81);
    CHECK_EQ(hk_game_init(&g_game, &p, &g_cfg, &r), HK_OK);
    CHECK_EQ(r.state, 81u);
}
