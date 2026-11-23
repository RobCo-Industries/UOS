#include "rc_test.h"

#include "osk.h"

static input_event_t key(unsigned code, int value)
{
    input_event_t e;

    memset(&e, 0, sizeof(e));
    e.type = INPUT_EV_KEY;
    e.source = INPUT_SRC_GPIO;
    e.code = (uint16_t)code;
    e.value = value;
    e.t_ms = 1234;
    return e;
}

static int press(osk_t *k, unsigned code)
{
    input_event_t e = key(code, INPUT_PRESS);

    return osk_handle(k, &e);
}

static int rep(osk_t *k, unsigned code)
{
    input_event_t e = key(code, INPUT_REPEAT);

    return osk_handle(k, &e);
}

static void go(osk_t *k, unsigned row, unsigned col)
{
    k->row = (uint8_t)row;
    k->col = (uint8_t)col;
}

static void drain(osk_t *k)
{
    input_event_t e;

    while (osk_pop(k, &e)) {
    }
}

static unsigned row_width(unsigned row)
{
    return row == OSK_FUNC_ROW ? OSK_FUNC_KEYS : OSK_CHAR_COLS;
}

static unsigned key_index(unsigned row, unsigned col)
{
    return row * OSK_CHAR_COLS + col;
}

TEST(osk_init_state)
{
    osk_t k;
    unsigned r, c;

    osk_init(&k);
    CHECK_EQ(osk_is_open(&k), 0);
    CHECK_EQ(osk_password(&k), 0);
    CHECK_EQ(osk_layer(&k), 0u);
    CHECK_EQ(osk_pending(&k), 0u);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(r, 0u);
    CHECK_EQ(c, 0u);
}

TEST(osk_closed_ignores_keys)
{
    osk_t k;

    osk_init(&k);
    CHECK_EQ(press(&k, INPUT_KEY_RIGHT), OSK_IGNORED);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_IGNORED);
    CHECK_EQ(osk_pending(&k), 0u);
}

TEST(osk_open_resets)
{
    osk_t k;
    unsigned r, c;

    osk_init(&k);
    osk_open(&k, 0);
    press(&k, INPUT_KEY_DOWN);
    go(&k, OSK_FUNC_ROW, OSK_FN_SHIFT);
    press(&k, INPUT_KEY_SELECT);
    go(&k, 0, 0);
    press(&k, INPUT_KEY_SELECT);
    CHECK_EQ(osk_layer(&k), 1u);
    CHECK_EQ(osk_pending(&k), 1u);
    osk_open(&k, 1);
    CHECK_EQ(osk_is_open(&k), 1);
    CHECK_EQ(osk_password(&k), 1);
    CHECK_EQ(osk_layer(&k), 0u);
    CHECK_EQ(osk_pending(&k), 0u);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(r, 0u);
    CHECK_EQ(c, 0u);
    osk_open(&k, 0);
    CHECK_EQ(osk_password(&k), 0);
}

TEST(osk_password_flag_kept_while_typing)
{
    osk_t k;
    input_event_t e;

    osk_init(&k);
    osk_open(&k, 7);
    CHECK_EQ(osk_password(&k), 1);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    CHECK_EQ(osk_pop(&k, &e), 1);
    CHECK_EQ(e.code, 'A');
    CHECK_EQ(osk_password(&k), 1);
}

TEST(osk_char_and_dial_events_pass_through)
{
    osk_t k;
    input_event_t e;

    osk_init(&k);
    osk_open(&k, 0);
    memset(&e, 0, sizeof(e));
    e.type = INPUT_EV_CHAR;
    e.code = 'x';
    CHECK_EQ(osk_handle(&k, &e), OSK_IGNORED);
    e.type = INPUT_EV_DIAL;
    e.value = 1;
    CHECK_EQ(osk_handle(&k, &e), OSK_IGNORED);
    CHECK_EQ(press(&k, INPUT_KEY_F1), OSK_IGNORED);
    CHECK_EQ(press(&k, INPUT_KEY_DIAL_PUSH), OSK_IGNORED);
    CHECK_EQ(osk_is_open(&k), 1);
}

TEST(osk_bad_args)
{
    osk_t k;
    input_event_t e = key(INPUT_KEY_UP, INPUT_PRESS);

    osk_init(&k);
    CHECK_EQ(osk_handle(rc_launder(NULL), &e), OSK_ERR_ARG);
    CHECK_EQ(osk_handle(&k, rc_launder(NULL)), OSK_ERR_ARG);
}

TEST(osk_release_consumed_does_nothing)
{
    osk_t k;
    input_event_t e = key(INPUT_KEY_SELECT, INPUT_RELEASE);
    unsigned r, c;

    osk_init(&k);
    osk_open(&k, 0);
    CHECK_EQ(osk_handle(&k, &e), OSK_CONSUMED);
    e.code = INPUT_KEY_RIGHT;
    CHECK_EQ(osk_handle(&k, &e), OSK_CONSUMED);
    e.code = INPUT_KEY_BACK;
    CHECK_EQ(osk_handle(&k, &e), OSK_CONSUMED);
    CHECK_EQ(osk_is_open(&k), 1);
    CHECK_EQ(osk_pending(&k), 0u);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(c, 0u);
}

/* Breadth-first search over the grid with the model's own moves. */
TEST(osk_every_key_reachable_from_every_key)
{
    static const unsigned moves[4] = {
        INPUT_KEY_UP, INPUT_KEY_DOWN, INPUT_KEY_LEFT, INPUT_KEY_RIGHT,
    };
    unsigned layer, start, bad = 0;

    for (layer = 0; layer < OSK_LAYERS; layer++) {
        for (start = 0; start < OSK_KEYS; start++) {
            uint8_t seen[OSK_KEYS];
            unsigned queue[OSK_KEYS];
            unsigned qh = 0, qt = 0, found = 0, m;

            memset(seen, 0, sizeof(seen));
            seen[start] = 1;
            queue[qt++] = start;
            while (qh < qt) {
                unsigned cur = queue[qh++];

                found++;
                for (m = 0; m < 4; m++) {
                    osk_t k;
                    unsigned r, c, idx;

                    osk_init(&k);
                    osk_open(&k, 0);
                    k.layer = (uint8_t)layer;
                    if (cur < OSK_CHAR_ROWS * OSK_CHAR_COLS) {
                        go(&k, cur / OSK_CHAR_COLS, cur % OSK_CHAR_COLS);
                    } else {
                        go(&k, OSK_FUNC_ROW, cur - OSK_CHAR_ROWS * OSK_CHAR_COLS);
                    }
                    press(&k, moves[m]);
                    osk_highlight(&k, &r, &c);
                    if (c >= row_width(r)) {
                        bad++;
                        continue;
                    }
                    idx = key_index(r, c);
                    if (!seen[idx]) {
                        seen[idx] = 1;
                        queue[qt++] = idx;
                    }
                }
            }
            if (found != OSK_KEYS) {
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0u);
}

TEST(osk_every_printable_typed)
{
    unsigned c, layer, row, col;

    for (c = 0x20; c <= 0x7e; c++) {
        int hit = 0;

        for (layer = 0; layer < OSK_LAYERS; layer++) {
            for (row = 0; row < OSK_CHAR_ROWS; row++) {
                for (col = 0; col < OSK_CHAR_COLS; col++) {
                    if (osk_layout_char(layer, row, col) == c) {
                        osk_t k;
                        input_event_t e;

                        osk_init(&k);
                        osk_open(&k, 0);
                        if (layer == 1) {
                            go(&k, OSK_FUNC_ROW, OSK_FN_SHIFT);
                            press(&k, INPUT_KEY_SELECT);
                        }
                        go(&k, row, col);
                        if (press(&k, INPUT_KEY_SELECT) == OSK_CONSUMED &&
                            osk_pop(&k, &e) == 1 && e.type == INPUT_EV_CHAR &&
                            e.code == c && e.source == OSK_SRC && e.value == 0) {
                            hit = 1;
                        }
                    }
                }
            }
        }
        if (!hit) {
            RC_FAIL("character 0x%02x not typed", c);
        }
    }
}

TEST(osk_layout_layer0_holds_command_set)
{
    const char *want = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./:$_-* [];%=(),\"";
    unsigned i, row, col;

    /* Every layer-0 command character except '"' is on layer 0. */
    for (i = 0; want[i] != '\0'; i++) {
        int hit = 0;

        for (row = 0; row < OSK_CHAR_ROWS; row++) {
            for (col = 0; col < OSK_CHAR_COLS; col++) {
                if (osk_layout_char(0, row, col) == (uint8_t)want[i]) {
                    hit = 1;
                }
            }
        }
        CHECK_EQ(hit, want[i] != '"');
    }
    CHECK_EQ(osk_layout_char(OSK_LAYERS, 0, 0), 0);
    CHECK_EQ(osk_layout_char(0, OSK_CHAR_ROWS, 0), 0);
    CHECK_EQ(osk_layout_char(0, 0, OSK_CHAR_COLS), 0);
}

TEST(osk_wrap_left_right_every_row)
{
    osk_t k;
    unsigned row, r, c;

    osk_init(&k);
    osk_open(&k, 0);
    for (row = 0; row < OSK_ROWS; row++) {
        go(&k, row, 0);
        press(&k, INPUT_KEY_LEFT);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, row);
        CHECK_EQ(c, row_width(row) - 1u);
        press(&k, INPUT_KEY_RIGHT);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, row);
        CHECK_EQ(c, 0u);
    }
}

TEST(osk_wrap_up_down_every_column)
{
    /* Column of each character key on the function row and back. */
    static const unsigned to_fn[OSK_CHAR_COLS] = { 0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5 };
    static const unsigned to_col[OSK_FUNC_KEYS] = { 1, 3, 5, 7, 9, 11 };
    osk_t k;
    unsigned col, f, r, c;

    osk_init(&k);
    osk_open(&k, 0);
    for (col = 0; col < OSK_CHAR_COLS; col++) {
        go(&k, 0, col);
        press(&k, INPUT_KEY_UP);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, OSK_FUNC_ROW);
        CHECK_EQ(c, to_fn[col]);
        go(&k, OSK_CHAR_ROWS - 1u, col);
        press(&k, INPUT_KEY_DOWN);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, OSK_FUNC_ROW);
        CHECK_EQ(c, to_fn[col]);
        go(&k, 1, col);
        press(&k, INPUT_KEY_UP);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, 0u);
        CHECK_EQ(c, col);
        press(&k, INPUT_KEY_DOWN);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, 1u);
        CHECK_EQ(c, col);
    }
    for (f = 0; f < OSK_FUNC_KEYS; f++) {
        go(&k, OSK_FUNC_ROW, f);
        press(&k, INPUT_KEY_DOWN);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, 0u);
        CHECK_EQ(c, to_col[f]);
        go(&k, OSK_FUNC_ROW, f);
        press(&k, INPUT_KEY_UP);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, OSK_CHAR_ROWS - 1u);
        CHECK_EQ(c, to_col[f]);
        /* Down then up returns to the same function key. */
        press(&k, INPUT_KEY_DOWN);
        osk_highlight(&k, &r, &c);
        CHECK_EQ(r, OSK_FUNC_ROW);
        CHECK_EQ(c, f);
    }
}

TEST(osk_full_cycle_vertical)
{
    osk_t k;
    unsigned i, r, c;

    osk_init(&k);
    osk_open(&k, 0);
    go(&k, 0, 1);
    for (i = 0; i < OSK_ROWS; i++) {
        press(&k, INPUT_KEY_DOWN);
    }
    osk_highlight(&k, &r, &c);
    CHECK_EQ(r, 0u);
    CHECK_EQ(c, 1u);
}

static void emitted_key(osk_t *k, unsigned code)
{
    input_event_t e;

    CHECK_EQ(osk_pending(k), 2u);
    CHECK_EQ(osk_pop(k, &e), 1);
    CHECK_EQ(e.type, INPUT_EV_KEY);
    CHECK_EQ(e.code, code);
    CHECK_EQ(e.value, INPUT_PRESS);
    CHECK_EQ(e.source, OSK_SRC);
    CHECK_EQ(e.t_ms, 1234u);
    CHECK_EQ(osk_pop(k, &e), 1);
    CHECK_EQ(e.type, INPUT_EV_KEY);
    CHECK_EQ(e.code, code);
    CHECK_EQ(e.value, INPUT_RELEASE);
    CHECK_EQ(osk_pop(k, &e), 0);
}

TEST(osk_function_keys_emit)
{
    osk_t k;

    osk_init(&k);
    osk_open(&k, 0);
    go(&k, OSK_FUNC_ROW, OSK_FN_BACKSPACE);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    emitted_key(&k, INPUT_KEY_BACK);
    go(&k, OSK_FUNC_ROW, OSK_FN_LEFT);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    emitted_key(&k, INPUT_KEY_LEFT);
    go(&k, OSK_FUNC_ROW, OSK_FN_RIGHT);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    emitted_key(&k, INPUT_KEY_RIGHT);
    CHECK_EQ(osk_is_open(&k), 1);
    go(&k, OSK_FUNC_ROW, OSK_FN_ENTER);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    emitted_key(&k, INPUT_KEY_SELECT);
    CHECK_EQ(osk_is_open(&k), 0);
}

TEST(osk_close_key_and_back)
{
    osk_t k;

    osk_init(&k);
    osk_open(&k, 0);
    go(&k, OSK_FUNC_ROW, OSK_FN_CLOSE);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    CHECK_EQ(osk_is_open(&k), 0);
    CHECK_EQ(osk_pending(&k), 0u);
    osk_open(&k, 0);
    CHECK_EQ(press(&k, INPUT_KEY_BACK), OSK_CONSUMED);
    CHECK_EQ(osk_is_open(&k), 0);
    CHECK_EQ(osk_pending(&k), 0u);
    osk_open(&k, 0);
    CHECK_EQ(rep(&k, INPUT_KEY_BACK), OSK_CONSUMED);
    CHECK_EQ(osk_is_open(&k), 1);
    osk_close(&k);
    CHECK_EQ(osk_is_open(&k), 0);
}

TEST(osk_close_keeps_queued_events)
{
    osk_t k;
    input_event_t e;

    osk_init(&k);
    osk_open(&k, 0);
    press(&k, INPUT_KEY_SELECT);
    press(&k, INPUT_KEY_BACK);
    CHECK_EQ(osk_is_open(&k), 0);
    CHECK_EQ(osk_pop(&k, &e), 1);
    CHECK_EQ(e.code, 'A');
}

TEST(osk_shift_cycles_layers_and_keeps_position)
{
    osk_t k;
    unsigned row, col, r, c;

    osk_init(&k);
    osk_open(&k, 0);
    go(&k, OSK_FUNC_ROW, OSK_FN_SHIFT);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    CHECK_EQ(osk_layer(&k), 1u);
    CHECK_EQ(osk_pending(&k), 0u);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(r, OSK_FUNC_ROW);
    CHECK_EQ(c, OSK_FN_SHIFT);
    press(&k, INPUT_KEY_SELECT);
    CHECK_EQ(osk_layer(&k), 0u);
    /* Every layer has a key at every position, so the position stays. */
    for (row = 0; row < OSK_CHAR_ROWS; row++) {
        for (col = 0; col < OSK_CHAR_COLS; col++) {
            CHECK(osk_layout_char(1, row, col) != 0);
            CHECK(osk_layout_char(0, row, col) != 0);
        }
    }
    go(&k, 2, 7);
    press(&k, INPUT_KEY_DOWN);
    press(&k, INPUT_KEY_DOWN);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(r, OSK_FUNC_ROW);
    press(&k, INPUT_KEY_SELECT); /* key 3 is RIGHT, not SHIFT */
    CHECK_EQ(osk_layer(&k), 0u);
    drain(&k);
}

TEST(osk_shift_then_type_lower)
{
    osk_t k;
    input_event_t e;

    osk_init(&k);
    osk_open(&k, 0);
    press(&k, INPUT_KEY_UP); /* to SHIFT */
    press(&k, INPUT_KEY_SELECT);
    press(&k, INPUT_KEY_DOWN); /* back to column 1 */
    press(&k, INPUT_KEY_SELECT);
    CHECK_EQ(osk_pop(&k, &e), 1);
    CHECK_EQ(e.code, 'b');
}

TEST(osk_repeat_arrows_move)
{
    osk_t k;
    unsigned i, r, c;

    osk_init(&k);
    osk_open(&k, 0);
    press(&k, INPUT_KEY_RIGHT);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(rep(&k, INPUT_KEY_RIGHT), OSK_CONSUMED);
    }
    osk_highlight(&k, &r, &c);
    CHECK_EQ(c, 6u);
    rep(&k, INPUT_KEY_DOWN);
    rep(&k, INPUT_KEY_UP);
    rep(&k, INPUT_KEY_UP);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(r, OSK_FUNC_ROW);
    CHECK_EQ(c, 3u);
    rep(&k, INPUT_KEY_LEFT);
    osk_highlight(&k, &r, &c);
    CHECK_EQ(c, 2u);
}

TEST(osk_repeat_select_rules)
{
    osk_t k;
    unsigned f;

    osk_init(&k);
    osk_open(&k, 0);
    /* Characters: no repeat. */
    CHECK_EQ(rep(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    CHECK_EQ(osk_pending(&k), 0u);
    /* BACKSPACE, LEFT, RIGHT repeat. */
    go(&k, OSK_FUNC_ROW, OSK_FN_BACKSPACE);
    rep(&k, INPUT_KEY_SELECT);
    emitted_key(&k, INPUT_KEY_BACK);
    go(&k, OSK_FUNC_ROW, OSK_FN_LEFT);
    rep(&k, INPUT_KEY_SELECT);
    emitted_key(&k, INPUT_KEY_LEFT);
    go(&k, OSK_FUNC_ROW, OSK_FN_RIGHT);
    rep(&k, INPUT_KEY_SELECT);
    emitted_key(&k, INPUT_KEY_RIGHT);
    /* ENTER, SHIFT, CLOSE: no repeat. */
    for (f = 0; f < OSK_FUNC_KEYS; f++) {
        if (f == OSK_FN_BACKSPACE || f == OSK_FN_LEFT || f == OSK_FN_RIGHT) {
            continue;
        }
        go(&k, OSK_FUNC_ROW, f);
        CHECK_EQ(rep(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
        CHECK_EQ(osk_pending(&k), 0u);
        CHECK_EQ(osk_is_open(&k), 1);
        CHECK_EQ(osk_layer(&k), 0u);
    }
}

TEST(osk_queue_limit_chars)
{
    osk_t k;
    input_event_t e;
    unsigned i;

    osk_init(&k);
    osk_open(&k, 0);
    for (i = 0; i < OSK_OUT_MAX; i++) {
        CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    }
    CHECK_EQ(osk_pending(&k), OSK_OUT_MAX);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_ERR_FULL);
    CHECK_EQ(osk_pending(&k), OSK_OUT_MAX);
    CHECK_EQ(osk_pop(&k, &e), 1);
    CHECK_EQ(press(&k, INPUT_KEY_RIGHT), OSK_CONSUMED);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    for (i = 0; i < OSK_OUT_MAX - 1u; i++) {
        CHECK_EQ(osk_pop(&k, &e), 1);
        CHECK_EQ(e.code, 'A');
    }
    CHECK_EQ(osk_pop(&k, &e), 1);
    CHECK_EQ(e.code, 'B');
    CHECK_EQ(osk_pop(&k, &e), 0);
}

TEST(osk_queue_limit_keys_atomic)
{
    osk_t k;
    input_event_t e;
    unsigned i;

    osk_init(&k);
    osk_open(&k, 0);
    for (i = 0; i < OSK_OUT_MAX - 1u; i++) {
        press(&k, INPUT_KEY_SELECT);
    }
    /* One slot left: a key needs two, so nothing is emitted. */
    go(&k, OSK_FUNC_ROW, OSK_FN_BACKSPACE);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_ERR_FULL);
    CHECK_EQ(osk_pending(&k), OSK_OUT_MAX - 1u);
    go(&k, OSK_FUNC_ROW, OSK_FN_ENTER);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_ERR_FULL);
    CHECK_EQ(osk_is_open(&k), 1);
    CHECK_EQ(osk_pop(&k, &e), 1);
    CHECK_EQ(press(&k, INPUT_KEY_SELECT), OSK_CONSUMED);
    CHECK_EQ(osk_is_open(&k), 0);
    CHECK_EQ(osk_pending(&k), OSK_OUT_MAX);
}

static const char *const golden_layer0[OSK_ROWS] = {
    "   A     B     C     D     E     F     G     H     I     J     K     L     M    ",
    "   N     O     P     Q     R     S     T     U     V     W     X     Y     Z    ",
    "   0     1     2     3     4     5     6     7     8     9     .     /     :    ",
    "   $     _     -     *    SPC    [     ]     ;     %     =     (     )     ,    ",
    "     SHIFT        BKSP         LEFT         RIGHT        ENTER        CLOSE     ",
};

static const char *const golden_layer1[OSK_ROWS] = {
    "   a     b     c     d     e     f     g     h     i     j     k     l     m    ",
    "   n     o     p     q     r     s     t     u     v     w     x     y     z    ",
    "   !     \"     #     &     '     +     <     >     ?     @     \\     ^     `    ",
    "   {     |     }     ~     .     ,     :     ;     /     _     -     =    SPC   ",
    "     SHIFT        BKSP         LEFT         RIGHT        ENTER        CLOSE     ",
};

TEST(osk_rows_golden)
{
    osk_t k;
    char buf[OSK_ROW_BUF];
    unsigned row;

    osk_init(&k);
    osk_open(&k, 0);
    for (row = 0; row < OSK_ROWS; row++) {
        CHECK_EQ(osk_row_text(&k, row, buf, sizeof(buf)), (int)OSK_TEXT_COLS);
        CHECK_STR(buf, golden_layer0[row]);
    }
    k.layer = 1;
    for (row = 0; row < OSK_ROWS; row++) {
        CHECK_EQ(osk_row_text(&k, row, buf, sizeof(buf)), (int)OSK_TEXT_COLS);
        CHECK_STR(buf, golden_layer1[row]);
    }
}

TEST(osk_row_text_errors)
{
    osk_t k;
    char buf[OSK_ROW_BUF + 1];

    osk_init(&k);
    memset(buf, 'Z', sizeof(buf));
    CHECK_EQ(osk_row_text(&k, 0, buf, OSK_ROW_BUF - 1u), OSK_ERR_SPACE);
    CHECK_EQ(buf[0], 'Z');
    CHECK_EQ(osk_row_text(&k, OSK_ROWS, buf, sizeof(buf)), OSK_ERR_ARG);
    CHECK_EQ(osk_row_text(&k, 0, rc_launder(NULL), sizeof(buf)), OSK_ERR_ARG);
    CHECK_EQ(osk_row_text(&k, 0, buf, OSK_ROW_BUF), (int)OSK_TEXT_COLS);
    CHECK_EQ(buf[OSK_TEXT_COLS], '\0');
    CHECK_EQ(buf[OSK_ROW_BUF], 'Z');
}

TEST(osk_cells_match_rows)
{
    osk_t k;
    osk_cell_t cell;
    char buf[OSK_ROW_BUF];
    unsigned i, lit = 0;

    osk_init(&k);
    osk_open(&k, 0);
    go(&k, 2, 4);
    for (i = 0; i < OSK_KEYS; i++) {
        CHECK_EQ(osk_key_cell(&k, i, &cell), 0);
        CHECK(cell.x + cell.w <= OSK_TEXT_COLS);
        CHECK(cell.y < OSK_ROWS);
        osk_row_text(&k, cell.y, buf, sizeof(buf));
        if (!cell.is_func && cell.ch != ' ') {
            /* Single character in the cell's centre, extra space to the right. */
            CHECK_EQ(buf[cell.x + (cell.w - 1u) / 2u], (char)cell.ch);
        }
        lit += cell.highlighted;
        if (cell.highlighted) {
            CHECK_EQ(cell.y, 2u);
            CHECK_EQ(cell.x, 1u + 4u * OSK_CHAR_W);
            CHECK_EQ(cell.ch, '4');
        }
    }
    CHECK_EQ(lit, 1u);
    CHECK_EQ(osk_key_cell(&k, 52, &cell), 0);
    CHECK_EQ(cell.is_func, 1);
    CHECK_EQ(cell.fn, OSK_FN_SHIFT);
    CHECK_EQ(cell.x, 1u);
    CHECK_EQ(cell.w, OSK_FUNC_W);
    CHECK_EQ(cell.y, OSK_FUNC_ROW);
    CHECK_EQ(osk_key_cell(&k, 57, &cell), 0);
    CHECK_EQ(cell.fn, OSK_FN_CLOSE);
    CHECK_EQ(cell.x + cell.w, 79u);
    CHECK_EQ(osk_key_cell(&k, OSK_KEYS, &cell), OSK_ERR_ARG);
    CHECK_EQ(osk_key_cell(&k, 0, rc_launder(NULL)), OSK_ERR_ARG);
}

TEST(osk_screen_rows_cover_bottom)
{
    osk_t k;
    unsigned first = 99, count = 99;

    osk_init(&k);
    CHECK_EQ(osk_screen_rows(&k, 30, &first, &count), 0);
    CHECK_EQ(first, 30u);
    CHECK_EQ(count, 0u);
    osk_open(&k, 0);
    CHECK_EQ(osk_screen_rows(&k, 30, &first, &count), 0);
    CHECK_EQ(first, 25u);
    CHECK_EQ(count, 5u);
    CHECK_EQ(osk_screen_rows(&k, OSK_ROWS, &first, &count), 0);
    CHECK_EQ(first, 0u);
    CHECK_EQ(osk_screen_rows(&k, OSK_ROWS - 1u, &first, &count), OSK_ERR_SCREEN);
    CHECK_EQ(osk_screen_rows(&k, 30, rc_launder(NULL), &count), OSK_ERR_ARG);
    CHECK_EQ(osk_screen_rows(&k, 30, &first, rc_launder(NULL)), OSK_ERR_ARG);
}

TEST(osk_strerror_text)
{
    CHECK_STR(osk_strerror(OSK_IGNORED), "event not for the keyboard");
    CHECK_STR(osk_strerror(OSK_CONSUMED), "event used by the keyboard");
    CHECK_STR(osk_strerror(OSK_ERR_FULL), "keyboard output queue full");
    CHECK_STR(osk_strerror(OSK_ERR_ARG), "bad argument");
    CHECK_STR(osk_strerror(OSK_ERR_SPACE), "buffer too small");
    CHECK_STR(osk_strerror(OSK_ERR_SCREEN), "screen too small for the keyboard");
    CHECK_STR(osk_strerror(-99), "unknown keyboard code");
}
