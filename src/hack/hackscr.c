/*
 * The password game screen and the `hack` maintenance command. Thin: the
 * game is hack.c, the screen's own rules are hackui.c, the session rules
 * are the session service's.
 *
 * The word list is a system file: /UOS/WORDS is SYSTEM-only in the access
 * table, and the game runs at the login with nobody logged in, so it is
 * read with vfs_sys_read_all(). The board is drawn on the screen only:
 * the PL011 gets the markers and the result lines, never the board.
 *
 * Maintenance command:
 *   hack op=start             posts hack_screen_start()
 *   hack move <dir> [n]    posts n moves (1 to 64): up, down, left, right
 *   hack select            posts SELECT
 *   hack back              posts BACK
 *   hack state             state= cursor= hl= hlstart= hllen= attempts= result= len=
 *                          words= pending=, then "row r=<n> text=<cells>" for the 16
 *                          board rows and "hist i=<n> text=" for each history line
 * Test builds only (UOS_TEST_HOOKS=1):
 *   hack seed <n>          the next games use seed n in place of the RNG
 * Markers (none names the password before the game ends):
 *   hack op=rng result=<hw|fallback> rc=
 *   hack op=start result=<ok|refused|nocard|nolist|badlist|nomem|config> why= len= words=
 *        attempts= difficulty= lockout_s= seed=<hw|fixed>
 *   hack op=guess word=<WORD> result=<denied|win|lockout> likeness= attempts=
 *   hack op=bracket result=<dud|reset> attempts=
 *   hack op=end result=<won|lost|left>
 */

#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/rng.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "con.h"
#include "hackscr.h"
#include "hackui.h"
#include "login.h"
#include "sfx.h"
#include "sess.h"
#include "sh.h"
#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"
#include "uos_text.h"
#include "vfs.h"

_Static_assert(UOS_DIFF_NOVICE == HK_NOVICE && UOS_DIFF_ADVANCED == HK_ADVANCED &&
               UOS_DIFF_EXPERT == HK_EXPERT && UOS_DIFF_MASTER == HK_MASTER,
               "[hack] difficulty values must match hack.h");

/* A list holds at most 512 words of 12 letters and a CR LF each. */
#define WORDS_FILE_MAX      (HK_LIST_MAX * (HK_WORD_MAX + 2) + 64)
#define PAUSE_TICKS         UOS_TICK_HZ     /* the result shows 1 s */
#define MOVES_MAX           64u

static hk_list_t list;
static hk_game_t game;

static struct {
    hg_state_t st;
    char msg[HG_LINE_SIZE];
    char path[24];
    hk_pick_t pick;
    int rng_hw;
    hack_done_fn done;
    void *ctx;
    int how;
    int on_top;                 /* the login prompt was the top at the start */
    uos_timer_id_t pause;
    int waited;                 /* a covered game owes its end: see hack_uncover() */
    uint16_t closes;            /* times the screen closed: tags posted calls */
    unsigned posted;
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    int fixed;
    uint32_t seed;
#endif
} hg;

static uos_screen_t hack_screen;

/* ---- Drawing ---- */

static void put_row(unsigned row, const char *text, unsigned attr)
{
    unsigned n = (unsigned)k_strlen(text);
    unsigned cols;

    if (!uos.fb_ok || row >= con_text_rows()) {
        return;
    }
    cols = console_cols();
    if (n > cols - 1) {
        n = cols - 1;
    }
    console_set_cursor(0, row);
    console_set_attr(attr);
    console_write(text, n);
    console_set_attr(CONSOLE_ATTR_NORMAL);
    for (; n < cols - 1; n++) {
        console_putc(' ');
    }
}

static void hack_draw(uos_screen_t *s)
{
    char line[HG_LINE_SIZE];
    unsigned i, start = 0, len = 0, row, col;

    (void)s;
    if (!uos.fb_ok) {
        return;
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_clear();
    (void)hg_title_line(line, sizeof(line));
    put_row(HG_ROW_TITLE, line, CONSOLE_ATTR_BRIGHT);
    (void)hg_target_line(uos_settings()->hack_user, line, sizeof(line));
    put_row(HG_ROW_TARGET, line, CONSOLE_ATTR_NORMAL);
    if (hg.st != HG_S_ERROR) {
        (void)hg_attempts_line(game.attempts, line, sizeof(line));
        put_row(HG_ROW_ATTEMPTS, line, CONSOLE_ATTR_NORMAL);
        for (i = 0; i < HK_ROWS; i++) {
            if (hg_board_line(&game, i, line, sizeof(line)) >= 0) {
                put_row(HG_ROW_BOARD + i, line, CONSOLE_ATTR_NORMAL);
            }
        }
        if (hg.st == HG_S_PLAYING) {
            (void)hk_highlight(&game, &start, &len);
            for (i = 0; i < len && start + i < HK_BOARD_CHARS; i++) {
                if (hg_cell_pos(start + i, &row, &col) == HK_OK) {
                    console_set_cursor(col, row);
                    console_set_attr(CONSOLE_ATTR_INVERSE);
                    console_write(&game.board.cell[start + i], 1);
                }
            }
            console_set_attr(CONSOLE_ATTR_NORMAL);
            (void)hg_cursor_line(&game, line, sizeof(line));
            put_row(HG_ROW_CURSOR, line, CONSOLE_ATTR_NORMAL);
        }
    }
    put_row(HG_ROW_MSG, hg.msg, CONSOLE_ATTR_BRIGHT);
    (void)hg_keys_line(line, sizeof(line));
    put_row(HG_ROW_KEYS, line, CONSOLE_ATTR_DIM);
    con_status_redraw();
}

/* One line on the PL011 only: the screen draws its own rows. */
static void serial_line(const char *text)
{
    uos_serial_break();
    uart_puts(text);
    uart_putc('\n');
}

static void say(const char *text)
{
    k_snprintf(hg.msg, sizeof(hg.msg), "%s", text);
    if (text[0] != '\0') {
        serial_line(text);
    }
    uos_screen_dirty(&hack_screen);
}

/* ---- Start ---- */

static uint32_t seed_now(int *fixed)
{
    uint32_t seed = 0;

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (hg.fixed) {
        *fixed = 1;
        return hg.seed;
    }
#endif
    *fixed = 0;
    if (!hg.rng_hw || rng_u32(&seed) != 0) {
        /* No generator: the time since boot, in microseconds. */
        seed = (uint32_t)timer_now_us() ^ (uos_ticks() << 16);
    }
    return seed;
}

static int start_game(void)
{
    const uos_settings_t *cfg = uos_settings();
    hk_difficulty_t diff = (hk_difficulty_t)cfg->hack_difficulty;
    hk_config_t hc;
    hk_rng_t rng;
    hg_start_t res;
    char *buf = NULL;
    size_t blen = 0;
    unsigned len;
    int fixed, rc, hk = HK_OK, refused;

    refused = lg_hack_where((lg_hack_t)sess_hack_check(), hg.on_top);
    if (refused != LG_HACK_OK) {
        UOS_MARK("hack op=start result=refused why=%s", lg_hack_name((lg_hack_t)refused));
        /* hack_enter() prints every start failure on the serial line, once. */
        k_snprintf(hg.msg, sizeof(hg.msg), "%s", lg_hack_text((lg_hack_t)refused));
        return -1;
    }
    if (hg_config(cfg->hack_attempts, cfg->hack_lockout_s, UOS_TICK_HZ, &hc) != HK_OK) {
        UOS_MARK("hack op=start result=%s", hg_start_name(HG_START_CONFIG));
        (void)hg_start_text(HG_START_CONFIG, NULL, hg.msg, sizeof(hg.msg));
        return -1;
    }
    hk_rng_seed(&rng, seed_now(&fixed));
    len = hk_pick_length(diff, &rng);
    (void)hg_words_path(len, hg.path, sizeof(hg.path));
    rc = vfs_sys_read_all(hg.path, WORDS_FILE_MAX, &buf, &blen);
    if (rc == VFS_OK) {
        hk = hk_list_parse(buf, blen, len, &list);
        if (hk == HK_OK) {
            hk = hk_pick(&list, diff, &rng, &hg.pick);
        }
        if (hk == HK_OK) {
            hk = hk_game_init(&game, &hg.pick, &hc, &rng);
        }
    }
    /* The board holds its own copy of the letters: the list's buffer goes. */
    vfs_free(buf);
    k_memset(&hg.pick, 0, sizeof(hg.pick));
    res = hg_start_result(rc, hk);
    UOS_MARK("hack op=start result=%s why=%s len=%u words=%u attempts=%u difficulty=%s "
             "lockout_s=%u seed=%s line=%u",
             hg_start_name(res), rc != VFS_OK ? vfs_strerror(rc) : hk_strerror(hk), len,
             res == HG_START_OK ? (unsigned)game.board.count : 0u, (unsigned)hc.attempts,
             hk_difficulty_name(diff), (unsigned)hc.lockout_s, fixed ? "fixed" : "hw",
             (unsigned)list.err_line);
    if (res != HG_START_OK) {
        (void)hg_start_text(res, hg.path, hg.msg, sizeof(hg.msg));
        k_memset(&game, 0, sizeof(game));
        return -1;
    }
    return 0;
}

/* ---- Play ---- */

static void pause_end(void *arg)
{
    hg_state_t was = hg.st;

    (void)arg;
    hg.pause = UOS_TIMER_NONE;
    /* A covered game ends once it is the top again: the screen over it, a
     * holotape reader for one, stays as the person left it. The game owes
     * the end, and hack_uncover() runs it when the game is uncovered. */
    if (!uos_screen_on_top(&hack_screen)) {
        if (!hg.waited) {
            hg.waited = 1;
            UOS_MARK("hack op=wait why=covered");
        }
        return;
    }
    hg.waited = 0;
    hg.st = hg_next(hg.st, HG_E_PAUSE_END);
    if (was == HG_S_WON) {
        hg.how = HACK_DONE_WON;
        /* Replaces the stack: this screen leaves on the way. */
        if (sess_hack_begin() != LG_HACK_OK) {
            hg.how = HACK_DONE_LEFT;
            if (uos_screen_on_top(&hack_screen)) {
                (void)uos_screen_pop(&hack_screen);
            }
        }
    } else if (was == HG_S_LOST) {
        hg.how = HACK_DONE_LOST;
        if (!sess_hack_lock()) {
            hg.how = HACK_DONE_LEFT;
            if (uos_screen_on_top(&hack_screen)) {
                (void)uos_screen_pop(&hack_screen);
            }
        }
    }
}

static void pause_start(void)
{
    if (uos_timer_start(PAUSE_TICKS, 0, pause_end, NULL, &hg.pause) != 0) {
        hg.pause = UOS_TIMER_NONE;
        pause_end(NULL);
    }
}

static void do_select(void)
{
    char word[HK_WORD_MAX + 1];
    unsigned start = 0, len = 0, like = 0;
    hk_hl_t hl = hk_highlight(&game, &start, &len);
    hk_event_t ev;

    word[0] = '\0';
    if (hl == HK_HL_WORD && len <= HK_WORD_MAX) {
        k_memcpy(word, &game.board.cell[start], len);
        word[len] = '\0';
        like = hk_likeness(word, &game.board.cell[game.board.word_pos[game.password]], len);
    }
    ev = hk_select(&game, uos_ticks());
    switch (ev) {
    case HK_EV_DENIED:
    case HK_EV_WIN:
    case HK_EV_LOCKOUT:
        UOS_MARK("hack op=guess word=%s result=%s likeness=%u attempts=%u", word, hg_event_name(ev),
                 like, (unsigned)game.attempts);
        break;
    case HK_EV_DUD:
    case HK_EV_RESET:
        UOS_MARK("hack op=bracket result=%s attempts=%u", hg_event_name(ev),
                 (unsigned)game.attempts);
        break;
    default:
        return;
    }
    /* One event, one sound: the result, or a key click for a bracket. */
    uos_sfx_hack(ev);
    if (ev == HK_EV_WIN) {
        hg.st = hg_next(hg.st, HG_E_WIN);
        UOS_MARK("hack op=end result=won");
        say("PASSWORD ACCEPTED. OPENING A SESSION.");
        pause_start();
    } else if (ev == HK_EV_LOCKOUT) {
        hg.st = hg_next(hg.st, HG_E_LOSE);
        UOS_MARK("hack op=end result=lost");
        say("NO ATTEMPT LEFT. THE TERMINAL LOCKS.");
        pause_start();
    }
    uos_screen_invalidate();
}

static void do_key(hg_key_t k, hk_dir_t dir)
{
    if (k == HG_K_BACK && (hg.st == HG_S_PLAYING || hg.st == HG_S_ERROR)) {
        if (hg.st == HG_S_PLAYING) {
            hk_cancel(&game);
        }
        hg.how = HACK_DONE_LEFT;
        (void)uos_screen_pop(&hack_screen);
        return;
    }
    if (hg.st != HG_S_PLAYING) {
        return;
    }
    if (k == HG_K_MOVE) {
        uint16_t was = game.cursor;

        hk_move(&game, dir);
        if (game.cursor != was) {
            uos_sfx_scroll();
        }
        uos_screen_invalidate();
    } else if (k == HG_K_SELECT) {
        do_select();
    }
}

/* ---- The screen ---- */

static void hack_enter(uos_screen_t *s)
{
    (void)s;
    hg.msg[0] = '\0';
    hg.how = HACK_DONE_LEFT;
    hg.st = hg_next(HG_S_OFF, start_game() == 0 ? HG_E_START_OK : HG_E_START_FAIL);
    /* start_game() only fills hg.msg: the reason goes out here, once. */
    if (hg.msg[0] != '\0') {
        serial_line(hg.msg);
    }
    uos_screen_invalidate();
}

static void hack_leave(uos_screen_t *s)
{
    hack_done_fn done = hg.done;
    void *ctx = hg.ctx;

    (void)s;
    if (hg.pause != UOS_TIMER_NONE) {
        (void)uos_timer_cancel(hg.pause);
        hg.pause = UOS_TIMER_NONE;
    }
    if (hg.how == HACK_DONE_LEFT) {
        UOS_MARK("hack op=end result=left");
    }
    hg.st = HG_S_OFF;
    hg.waited = 0;
    hg.closes++;
    hg.done = NULL;
    /* Nothing of the board stays in memory once the game is over. */
    k_memset(&game, 0, sizeof(game));
    k_memset(&list, 0, sizeof(list));
    if (done != NULL) {
        done(hg.how, ctx);
    }
}

static int hack_event(uos_screen_t *s, const uos_event_t *ev)
{
    hk_dir_t dir = HK_UP;

    (void)s;
    if (ev->type == UOS_EV_KEY) {
        do_key(hg_key(ev->code, ev->value, &dir), dir);
    }
    return ev->type == UOS_EV_KEY || ev->type == UOS_EV_CHAR;
}

/* On top again: the end that waited while the game was covered runs now.
 * pause_end() clears `waited` before it ends the game, so it runs once. */
static void hack_uncover(uos_screen_t *s)
{
    (void)s;
    if (hg.waited && hg.pause == UOS_TIMER_NONE) {
        pause_end(NULL);
    }
}

static uos_screen_t hack_screen = {
    .name = "hack",
    .uncover = hack_uncover,
    .enter = hack_enter,
    .leave = hack_leave,
    .event = hack_event,
    .draw = hack_draw,
    .full = 1,
};

int hack_screen_start(hack_done_fn done, void *ctx)
{
    if (hg.st != HG_S_OFF || uos_screen_on_top(&hack_screen)) {
        return HACK_ERR_BUSY;
    }
    hg.done = done;
    hg.ctx = ctx;
    /* Read before the push: once pushed, the game is the top. */
    hg.on_top = sess_prompt_on_top();
    if (uos_screen_push(&hack_screen) != 0) {
        hg.done = NULL;
        return HACK_ERR_SCREEN;
    }
    return 0;
}

int hack_screen_active(void)
{
    return hg.st != HG_S_OFF;
}

/* ---- The login's entry ---- */

static void back_to_login(int how, void *ctx)
{
    (void)ctx;
    if (how == HACK_DONE_LEFT) {
        sess_resume();
    }
}

static int from_login(void)
{
    return hack_screen_start(back_to_login, NULL) == 0 ? 0 : -1;
}

/* ---- The maintenance command ---- */

/* Over the login prompt the command acts as F2. Over anything else it
 * acts as a program's start: the screen opens, refuses with "covered" or
 * "busy", and BACK closes it alone, so the screen under it stays as it
 * was. */
static void post_start(void *arg)
{
    (void)arg;
    hg.posted--;
    if (hack_screen_start(sess_prompt_on_top() ? back_to_login : NULL, NULL) != 0) {
        UOS_MARK("hack result=busy");
    }
}

/* Bits 16 and up of a posted argument: the screen's close count when the
 * command came. A call meant for a game that has ended since, or that finds
 * no game, does nothing and says so. */
static int stale(uintptr_t v)
{
    hg.posted--;
    if ((uint16_t)(v >> 16) != hg.closes || hg.st == HG_S_OFF) {
        UOS_MARK("hack result=gone");
        return 1;
    }
    /* A covered game takes no posted key: BACK would pop the screen on top. */
    if (!uos_screen_on_top(&hack_screen)) {
        UOS_MARK("hack result=covered");
        return 1;
    }
    return 0;
}

/* The direction and the count travel in the argument: two moves posted
 * back to back each keep their own. */
static void post_move(void *arg)
{
    uintptr_t v = (uintptr_t)arg;
    unsigned i;

    if (stale(v)) {
        return;
    }
    for (i = 0; i < (v & 0xffu); i++) {
        do_key(HG_K_MOVE, (hk_dir_t)((v >> 8) & 0xffu));
    }
}

static void post_select(void *arg)
{
    if (!stale((uintptr_t)arg)) {
        do_key(HG_K_SELECT, HK_UP);
    }
}

static void post_back(void *arg)
{
    if (!stale((uintptr_t)arg)) {
        do_key(HG_K_BACK, HK_UP);
    }
}

static int post_arg(uos_call_fn fn, void *arg)
{
    if (uos_post(fn, arg) != 0) {
        return uos_maint_fail("full");
    }
    hg.posted++;
    return 0;
}

/* Tags the call with the screen it is meant for. */
static int post(uos_call_fn fn, uintptr_t v)
{
    return post_arg(fn, (void *)(((uintptr_t)hg.closes << 16) | v));
}

static void reply_state(void)
{
    static const char *const hl_names[] = { "char", "word", "pair" };
    char line[HG_LINE_SIZE];
    char word[HG_LINE_SIZE * 3];
    unsigned start = 0, len = 0, i, n;
    hk_hl_t hl = HK_HL_CHAR;
    uint32_t secs = 0;
    hk_state_t res = HK_PLAYING;
    int live = hg.st == HG_S_PLAYING || hg.st == HG_S_WON || hg.st == HG_S_LOST;

    if (live) {
        hl = hk_highlight(&game, &start, &len);
        res = hk_result(&game, uos_ticks(), &secs);
    }
    uos_maint_reply("hack state=%s cursor=%u hl=%s hlstart=%u hllen=%u attempts=%u result=%s "
                    "len=%u words=%u pairs=%u pending=%u",
                    hg_state_name(hg.st), live ? (unsigned)game.cursor : 0u, hl_names[hl], start,
                    len, live ? (unsigned)game.attempts : 0u,
                    !live ? "none" : res == HK_WON ? "won" : res == HK_LOCKED ? "locked" :
                    res == HK_CANCELLED ? "cancelled" : "playing",
                    live ? (unsigned)game.board.len : 0u, live ? (unsigned)game.board.count : 0u,
                    live ? hk_board_pair_count(&game.board) : 0u,
                    uos_input_pending() + hg.posted);
    if (!live) {
        return;
    }
    for (i = 0; i < HK_ROWS; i++) {
        if (hg_board_cells(&game, i, line, sizeof(line)) >= 0) {
            uos_quote_word(line, word, sizeof(word));
            uos_maint_reply("row r=%u text=%s", i, word);
        }
    }
    n = hk_history_count(&game);
    for (i = 0; i < n; i++) {
        uos_quote_word(hk_history_line(&game, i), word, sizeof(word));
        uos_maint_reply("hist i=%u text=%s", i, word);
    }
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds only: the nm test proves the shipped image has none of this. */
static void __attribute__((noinline)) uos_test_hack_seed_set(void *arg)
{
    hg.posted--;
    hg.seed = (uint32_t)(uintptr_t)arg;
    hg.fixed = 1;
}

/* Posted, as every command that acts: the seed applies after the input
 * received before it. */
static int __attribute__((noinline)) uos_test_hack_seed(char *arg)
{
    uint32_t n;

    if (uos_dec_u32(arg, 0xffffffffu, &n) != 0) {
        return uos_maint_fail("usage hack seed <n>");
    }
    return post_arg(uos_test_hack_seed_set, (void *)(uintptr_t)n);
}
#endif

static int parse_dir(const char *s, hk_dir_t *dir)
{
    static const char *const names[] = { "left", "right", "up", "down" };
    unsigned i;

    for (i = 0; i < 4; i++) {
        if (k_strcmp(s, names[i]) == 0) {
            *dir = (hk_dir_t)i;
            return 0;
        }
    }
    return -1;
}

static int cmd_hack(int argc, char **argv)
{
    uint32_t n = 1;
    hk_dir_t dir = HK_UP;

    if (argc == 1 && k_strcmp(argv[0], "state") == 0) {
        reply_state();
        return 0;
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (argc == 1 && k_strcmp(argv[0], "start") == 0) {
        return post_arg(post_start, NULL);
    }
    if (argc == 1 && k_strcmp(argv[0], "select") == 0) {
        return post(post_select, 0);
    }
    if (argc == 1 && k_strcmp(argv[0], "back") == 0) {
        return post(post_back, 0);
    }
    if ((argc == 2 || argc == 3) && k_strcmp(argv[0], "move") == 0) {
        if (parse_dir(argv[1], &dir) != 0 ||
            (argc == 3 && (uos_dec_u32(argv[2], MOVES_MAX, &n) != 0 || n == 0))) {
            return uos_maint_fail("usage hack move up|down|left|right [n]");
        }
        return post(post_move, ((uintptr_t)dir << 8) | n);
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (argc == 2 && k_strcmp(argv[0], "seed") == 0) {
        return uos_test_hack_seed(argv[1]);
    }
#endif
    return uos_maint_fail("usage hack start|move|select|back|state");
}

/* ---- The service ---- */

/* ---- RUN HACK ---- */

static int prog_posted;

static void push_program(int live)
{
    prog_posted = 0;
    if (!live) {
        return;
    }
    UOS_MARK("hack op=program rc=%d", hack_screen_start(NULL, NULL));
}

/*
 * RUN HACK. As RUN TERMINAL, the run ends at once and the push is posted
 * after it. The shell runs only in a session, so the game shows the
 * session's refusal, busy, and BACK brings the prompt back.
 */
static int prog_start(const char *args, sh_prog_done_fn done, void *tag)
{
    (void)args;
    if (hack_screen_active() || prog_posted || sh_program_post(push_program) != 0) {
        return 1;
    }
    prog_posted = 1;
    done(tag, 0, NULL);
    return 0;
}

static int hack_svc_init(void)
{
    int rc = rng_init();

    hg.rng_hw = rc == 0;
    hg.pause = UOS_TIMER_NONE;
    UOS_MARK("hack op=rng result=%s rc=%d", hg.rng_hw ? "hw" : "fallback", rc);
    (void)sess_set_hack_entry(from_login);
    rc = uos_maint_register("hack", cmd_hack, "the password game: start move select back state");
    if (rc != 0) {
        return rc;
    }
    return sh_program_register("HACK", prog_start);
}

const uos_service_t uos_svc_hack = { "hack", hack_svc_init, NULL, NULL };
