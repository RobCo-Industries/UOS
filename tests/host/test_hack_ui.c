#include <string.h>

#include <robco/input.h>

#include "hackui.h"
#include "vfs.h"

#include "rc_test.h"

/* A fixed game: 4 words of 5 letters at known cells. */
static hk_game_t g;

static void make_game(void)
{
    static const char *words[] = { "ALPHA", "BRAVO", "CHARM", "DELTA" };
    hk_pick_t p;
    hk_config_t c = { 4, 60, 100 };
    hk_rng_t r;
    unsigned i;

    memset(&p, 0, sizeof(p));
    for (i = 0; i < 4; i++) {
        p.word[i] = words[i];
    }
    p.count = 4;
    p.len = 5;
    p.password = 2;
    hk_rng_seed(&r, 1234);
    CHECK_EQ(hk_game_init(&g, &p, &c, &r), HK_OK);
}

TEST(cell_positions)
{
    unsigned row = 0, col = 0;

    CHECK_EQ(hg_cell_pos(0, &row, &col), HK_OK);
    CHECK_EQ(row, HG_ROW_BOARD);
    CHECK_EQ(col, HG_COL_CELLS0);
    CHECK_EQ(hg_cell_pos(11, &row, &col), HK_OK);
    CHECK_EQ(col, HG_COL_CELLS0 + 11);
    /* col * 192 + row * 12 + x: a run wraps to the next row of its column. */
    CHECK_EQ(hg_cell_pos(12, &row, &col), HK_OK);
    CHECK_EQ(row, HG_ROW_BOARD + 1);
    CHECK_EQ(col, HG_COL_CELLS0);
    CHECK_EQ(hg_cell_pos(191, &row, &col), HK_OK);
    CHECK_EQ(row, HG_ROW_BOARD + 15);
    CHECK_EQ(col, HG_COL_CELLS0 + 11);
    CHECK_EQ(hg_cell_pos(192, &row, &col), HK_OK);
    CHECK_EQ(row, HG_ROW_BOARD);
    CHECK_EQ(col, HG_COL_CELLS1);
    CHECK_EQ(hg_cell_pos(383, &row, &col), HK_OK);
    CHECK_EQ(row, HG_ROW_BOARD + 15);
    CHECK_EQ(col, HG_COL_CELLS1 + 11);
    CHECK_EQ(hg_cell_pos(384, &row, &col), HK_ERR_ARG);
    /* The rows fit 80 columns and do not reach the cursor row. */
    CHECK(HG_COL_HISTORY + HK_LINE_SIZE - 1 <= 80);
    CHECK(HG_ROW_BOARD + HK_ROWS < HG_ROW_CURSOR);
}

TEST(board_rows_match_cells)
{
    char line[HG_LINE_SIZE];
    char addr[HK_ADDR_TEXT_SIZE];
    unsigned r, i, row, col;

    make_game();
    for (r = 0; r < HK_ROWS; r++) {
        CHECK(hg_board_line(&g, r, line, sizeof(line)) > 0);
        (void)hk_format_address(hk_board_address(&g.board, 0, r), addr, sizeof(addr));
        CHECK(strncmp(line, addr, 6) == 0);
        (void)hk_format_address(hk_board_address(&g.board, 1, r), addr, sizeof(addr));
        CHECK(strncmp(line + HG_COL_CELLS1 - 7, addr, 6) == 0);
    }
    /* Every cell sits where hg_cell_pos() says. */
    for (i = 0; i < HK_BOARD_CHARS; i++) {
        CHECK_EQ(hg_cell_pos(i, &row, &col), HK_OK);
        CHECK(hg_board_line(&g, row - HG_ROW_BOARD, line, sizeof(line)) > (int)col);
        CHECK_EQ(line[col], g.board.cell[i]);
    }
    CHECK_EQ(hg_board_line(&g, HK_ROWS, line, sizeof(line)), -1);
    CHECK_EQ(hg_board_line(&g, 0, line, 40), -1);
    CHECK_EQ(hg_board_cells(NULL, 0, line, sizeof(line)), -1);
}

TEST(history_sits_at_the_bottom)
{
    char line[HG_LINE_SIZE];
    unsigned start = 0, len = 0;

    make_game();
    CHECK_EQ(hg_board_line(&g, 15, line, sizeof(line)), (int)strlen(line));
    CHECK(strlen(line) < HG_COL_HISTORY);
    /* A wrong guess: three lines, the newest on row 15. */
    g.cursor = g.board.word_pos[0];
    CHECK_EQ(hk_highlight(&g, &start, &len), HK_HL_WORD);
    CHECK_EQ(hk_select(&g, 0), HK_EV_DENIED);
    CHECK(hg_board_line(&g, 13, line, sizeof(line)) > 0);
    CHECK_STR(line + HG_COL_HISTORY, "ALPHA");
    CHECK(hg_board_line(&g, 14, line, sizeof(line)) > 0);
    CHECK_STR(line + HG_COL_HISTORY, "Entry denied.");
    CHECK(hg_board_line(&g, 15, line, sizeof(line)) > 0);
    CHECK_STR(line + HG_COL_HISTORY, "Likeness=0");
    CHECK(hg_board_line(&g, 12, line, sizeof(line)) > 0);
    CHECK(strlen(line) < HG_COL_HISTORY);
}

TEST(cursor_and_attempts_lines)
{
    char line[HG_LINE_SIZE];

    make_game();
    g.cursor = g.board.word_pos[1];
    CHECK_EQ(hg_cursor_line(&g, line, sizeof(line)), 7);
    CHECK_STR(line, "> BRAVO");
    CHECK_EQ(hg_attempts_line(4, line, sizeof(line)), (int)strlen("ATTEMPTS LEFT: 4  # # # #"));
    CHECK_STR(line, "ATTEMPTS LEFT: 4  # # # #");
    CHECK_EQ(hg_attempts_line(0, line, sizeof(line)), (int)strlen("ATTEMPTS LEFT: 0 "));
    CHECK_EQ(hg_attempts_line(10, line, sizeof(line)), -1);
    CHECK(hg_title_line(line, sizeof(line)) > 0 && strlen(line) < 80);
    CHECK(hg_keys_line(line, sizeof(line)) > 0 && strlen(line) < 80);
    CHECK_EQ(hg_target_line("GUEST", line, sizeof(line)), 21);
    CHECK_STR(line, "TARGET ACCOUNT: GUEST");
}

TEST(keys)
{
    hk_dir_t d = HK_LEFT;

    CHECK_EQ(hg_key(INPUT_KEY_UP, INPUT_PRESS, &d), HG_K_MOVE);
    CHECK_EQ(d, HK_UP);
    CHECK_EQ(hg_key(INPUT_KEY_DOWN, INPUT_REPEAT, &d), HG_K_MOVE);
    CHECK_EQ(d, HK_DOWN);
    CHECK_EQ(hg_key(INPUT_KEY_LEFT, INPUT_PRESS, &d), HG_K_MOVE);
    CHECK_EQ(d, HK_LEFT);
    CHECK_EQ(hg_key(INPUT_KEY_RIGHT, INPUT_PRESS, &d), HG_K_MOVE);
    CHECK_EQ(d, HK_RIGHT);
    CHECK_EQ(hg_key(INPUT_KEY_RIGHT, INPUT_RELEASE, &d), HG_K_NONE);
    CHECK_EQ(hg_key(INPUT_KEY_SELECT, INPUT_PRESS, &d), HG_K_SELECT);
    CHECK_EQ(hg_key(INPUT_KEY_SELECT, INPUT_REPEAT, &d), HG_K_NONE);
    CHECK_EQ(hg_key(INPUT_KEY_BACK, INPUT_PRESS, &d), HG_K_BACK);
    CHECK_EQ(hg_key(INPUT_KEY_BACK, INPUT_REPEAT, &d), HG_K_NONE);
    CHECK_EQ(hg_key(INPUT_KEY_F2, INPUT_PRESS, &d), HG_K_NONE);
}

TEST(config_ranges)
{
    hk_config_t c = { 0, 0, 0 };

    CHECK_EQ(hg_config(4, 60, 100, &c), HK_OK);
    CHECK_EQ(c.attempts, 4);
    CHECK_EQ(c.lockout_s, 60);
    CHECK_EQ(c.ticks_per_s, 100u);
    CHECK_EQ(hg_config(0, 60, 100, &c), HK_ERR_ARG);
    CHECK_EQ(hg_config(10, 60, 100, &c), HK_ERR_ARG);
    CHECK_EQ(hg_config(4, 3601, 100, &c), HK_ERR_ARG);
    CHECK_EQ(hg_config(4, 60, 0, &c), HK_ERR_ARG);
    CHECK_EQ(c.attempts, 4);
    CHECK_EQ(hg_config(9, 0, HK_TPS_MAX, &c), HK_OK);
    CHECK_EQ(hg_config(1, 3600, 1, NULL), HK_ERR_ARG);
}

TEST(words_path)
{
    char p[24];

    CHECK_EQ(hg_words_path(7, p, sizeof(p)), 18);
    CHECK_STR(p, "/UOS/WORDS/W07.TXT");
    CHECK_EQ(hg_words_path(12, p, sizeof(p)), 18);
    CHECK_STR(p, "/UOS/WORDS/W12.TXT");
    CHECK_EQ(hg_words_path(3, p, sizeof(p)), -1);
    CHECK_EQ(hg_words_path(13, p, sizeof(p)), -1);
}

TEST(screen_states)
{
    CHECK_EQ(hg_next(HG_S_OFF, HG_E_START_OK), HG_S_PLAYING);
    CHECK_EQ(hg_next(HG_S_OFF, HG_E_START_FAIL), HG_S_ERROR);
    CHECK_EQ(hg_next(HG_S_ERROR, HG_E_BACK), HG_S_OFF);
    CHECK_EQ(hg_next(HG_S_PLAYING, HG_E_WIN), HG_S_WON);
    CHECK_EQ(hg_next(HG_S_PLAYING, HG_E_LOSE), HG_S_LOST);
    CHECK_EQ(hg_next(HG_S_PLAYING, HG_E_BACK), HG_S_OFF);
    /* The result stands: BACK during the pause does nothing. */
    CHECK_EQ(hg_next(HG_S_WON, HG_E_BACK), HG_S_WON);
    CHECK_EQ(hg_next(HG_S_LOST, HG_E_BACK), HG_S_LOST);
    CHECK_EQ(hg_next(HG_S_WON, HG_E_PAUSE_END), HG_S_OFF);
    CHECK_EQ(hg_next(HG_S_LOST, HG_E_PAUSE_END), HG_S_OFF);
    CHECK_EQ(hg_next(HG_S_ERROR, HG_E_WIN), HG_S_ERROR);
    CHECK_EQ(hg_next(HG_S_OFF, HG_E_WIN), HG_S_OFF);
    CHECK_STR(hg_state_name(HG_S_LOST), "lost");
    CHECK_STR(hg_state_name(HG_S_NUM), "unknown");
    CHECK_STR(hg_event_name(HK_EV_DUD), "dud");
    CHECK_STR(hg_event_name((hk_event_t)9), "unknown");
}

TEST(start_results)
{
    char t[HG_LINE_SIZE];

    CHECK_EQ(hg_start_result(VFS_OK, HK_OK), HG_START_OK);
    CHECK_EQ(hg_start_result(VFS_OK, HK_ERR_CHAR), HG_START_BADLIST);
    CHECK_EQ(hg_start_result(VFS_OK, HK_ERR_TOO_FEW), HG_START_BADLIST);
    CHECK_EQ(hg_start_result(VFS_E_NOTFOUND, HK_OK), HG_START_NOLIST);
    CHECK_EQ(hg_start_result(VFS_E_DIRNF, HK_OK), HG_START_NOLIST);
    CHECK_EQ(hg_start_result(VFS_E_NOCARD, HK_OK), HG_START_NOCARD);
    CHECK_EQ(hg_start_result(VFS_E_NOTREE, HK_OK), HG_START_NOCARD);
    CHECK_EQ(hg_start_result(VFS_E_NOMEM, HK_OK), HG_START_NOMEM);
    CHECK_EQ(hg_start_result(VFS_E_TOOBIG, HK_OK), HG_START_BADLIST);
    CHECK_EQ(hg_start_result(VFS_E_READ, HK_OK), HG_START_BADLIST);
    CHECK(hg_start_text(HG_START_NOLIST, "/UOS/WORDS/W07.TXT", t, sizeof(t)) > 0);
    CHECK_STR(t, "WORD LIST /UOS/WORDS/W07.TXT IS MISSING.");
    CHECK(hg_start_text(HG_START_BADLIST, "/UOS/WORDS/W07.TXT", t, sizeof(t)) > 0);
    CHECK_STR(t, "WORD LIST /UOS/WORDS/W07.TXT IS DAMAGED.");
    CHECK_EQ(hg_start_text(HG_START_OK, NULL, t, sizeof(t)), 0);
    CHECK_EQ(hg_start_text(HG_START_NUM, NULL, t, sizeof(t)), -1);
    CHECK_STR(hg_start_name(HG_START_NOMEM), "nomem");
}
