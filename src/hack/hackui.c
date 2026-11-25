#include <robco/input.h>
#include <robco/klibc.h>

#include "hackui.h"
#include "vfs.h"

int hg_cell_pos(unsigned i, unsigned *row, unsigned *col)
{
    unsigned c, r, x;

    if (i >= HK_BOARD_CHARS || row == NULL || col == NULL) {
        return HK_ERR_ARG;
    }
    c = i / HK_COL_CHARS;
    r = (i % HK_COL_CHARS) / HK_ROW_CHARS;
    x = i % HK_ROW_CHARS;
    *row = HG_ROW_BOARD + r;
    *col = (c == 0 ? HG_COL_CELLS0 : HG_COL_CELLS1) + x;
    return HK_OK;
}

int hg_board_cells(const hk_game_t *g, unsigned r, char *buf, size_t size)
{
    char a0[HK_ADDR_TEXT_SIZE], a1[HK_ADDR_TEXT_SIZE];
    const char *c0, *c1;

    if (g == NULL || buf == NULL || r >= HK_ROWS || size < HG_COL_CELLS1 + HK_ROW_CHARS + 1) {
        return -1;
    }
    (void)hk_format_address(hk_board_address(&g->board, 0, r), a0, sizeof(a0));
    (void)hk_format_address(hk_board_address(&g->board, 1, r), a1, sizeof(a1));
    c0 = &g->board.cell[r * HK_ROW_CHARS];
    c1 = &g->board.cell[HK_COL_CHARS + r * HK_ROW_CHARS];
    return k_snprintf(buf, size, "%s %.12s  %s %.12s", a0, c0, a1, c1);
}

int hg_board_line(const hk_game_t *g, unsigned r, char *buf, size_t size)
{
    unsigned n, h, first;
    const char *hist = NULL;

    if (size < HG_COL_HISTORY + HK_LINE_SIZE) {
        return -1;
    }
    if (hg_board_cells(g, r, buf, size) < 0) {
        return -1;
    }
    h = hk_history_count(g);
    first = HK_ROWS - h;
    if (r >= first) {
        hist = hk_history_line(g, r - first);
    }
    if (hist == NULL) {
        return (int)k_strlen(buf);
    }
    n = (unsigned)k_strlen(buf);
    while (n < HG_COL_HISTORY) {
        buf[n++] = ' ';
    }
    buf[n] = '\0';
    return (int)n + k_snprintf(buf + n, size - n, "%.15s", hist);
}

int hg_attempts_line(unsigned left, char *buf, size_t size)
{
    int n;
    unsigned i;

    if (buf == NULL || left > HK_ATTEMPTS_MAX) {
        return -1;
    }
    n = k_snprintf(buf, size, "ATTEMPTS LEFT: %u ", left);
    for (i = 0; i < left && n >= 0 && (size_t)n + 2 < size; i++) {
        buf[n++] = ' ';
        buf[n++] = '#';
        buf[n] = '\0';
    }
    return (size_t)n < size ? n : -1;
}

int hg_cursor_line(const hk_game_t *g, char *buf, size_t size)
{
    unsigned start = 0, len = 0, i, n = 2;

    if (g == NULL || buf == NULL || size < 3) {
        return -1;
    }
    (void)hk_highlight(g, &start, &len);
    buf[0] = '>';
    buf[1] = ' ';
    for (i = 0; i < len && start + i < HK_BOARD_CHARS && n + 1 < size; i++) {
        buf[n++] = g->board.cell[start + i];
    }
    buf[n] = '\0';
    return (int)n;
}

int hg_title_line(char *buf, size_t size)
{
    return k_snprintf(buf, size, "%s", "UOS PASSWORD RECOVERY. DIAGNOSTIC MEMORY VIEW.");
}

int hg_target_line(const char *user, char *buf, size_t size)
{
    return k_snprintf(buf, size, "TARGET ACCOUNT: %.15s", user != NULL ? user : "");
}

int hg_keys_line(char *buf, size_t size)
{
    return k_snprintf(buf, size, "%s", "ARROWS MOVE   SELECT TRIES A WORD OR BRACKETS   BACK LEAVES");
}

hg_key_t hg_key(unsigned code, int value, hk_dir_t *dir)
{
    if (value == INPUT_RELEASE) {
        return HG_K_NONE;
    }
    switch (code) {
    case INPUT_KEY_UP:
        *dir = HK_UP;
        return HG_K_MOVE;
    case INPUT_KEY_DOWN:
        *dir = HK_DOWN;
        return HG_K_MOVE;
    case INPUT_KEY_LEFT:
        *dir = HK_LEFT;
        return HG_K_MOVE;
    case INPUT_KEY_RIGHT:
        *dir = HK_RIGHT;
        return HG_K_MOVE;
    case INPUT_KEY_SELECT:
        /* A held SELECT does not guess again and again. */
        return value == INPUT_PRESS ? HG_K_SELECT : HG_K_NONE;
    case INPUT_KEY_BACK:
        return value == INPUT_PRESS ? HG_K_BACK : HG_K_NONE;
    default:
        return HG_K_NONE;
    }
}

int hg_config(unsigned attempts, unsigned lockout_s, uint32_t ticks_per_s, hk_config_t *out)
{
    if (out == NULL || attempts < HK_ATTEMPTS_MIN || attempts > HK_ATTEMPTS_MAX ||
        lockout_s > HK_LOCKOUT_S_MAX || ticks_per_s == 0 || ticks_per_s > HK_TPS_MAX) {
        return HK_ERR_ARG;
    }
    out->attempts = (uint8_t)attempts;
    out->lockout_s = (uint16_t)lockout_s;
    out->ticks_per_s = ticks_per_s;
    return HK_OK;
}

int hg_words_path(unsigned len, char *buf, size_t size)
{
    if (len < HK_WORD_MIN || len > HK_WORD_MAX || buf == NULL) {
        return -1;
    }
    return k_snprintf(buf, size, "/UOS/WORDS/W%02u.TXT", len);
}

hg_state_t hg_next(hg_state_t s, hg_event_t e)
{
    switch (s) {
    case HG_S_OFF:
        if (e == HG_E_START_OK) {
            return HG_S_PLAYING;
        }
        if (e == HG_E_START_FAIL) {
            return HG_S_ERROR;
        }
        break;
    case HG_S_ERROR:
        if (e == HG_E_BACK) {
            return HG_S_OFF;
        }
        break;
    case HG_S_PLAYING:
        if (e == HG_E_WIN) {
            return HG_S_WON;
        }
        if (e == HG_E_LOSE) {
            return HG_S_LOST;
        }
        if (e == HG_E_BACK) {
            return HG_S_OFF;
        }
        break;
    case HG_S_WON:
    case HG_S_LOST:
        /* The pause takes no key: the result stands. */
        if (e == HG_E_PAUSE_END) {
            return HG_S_OFF;
        }
        break;
    default:
        break;
    }
    return s;
}

const char *hg_state_name(hg_state_t s)
{
    static const char *const names[HG_S_NUM] = { "off", "error", "playing", "won", "lost" };

    return (unsigned)s < HG_S_NUM ? names[s] : "unknown";
}

const char *hg_event_name(hk_event_t e)
{
    static const char *const names[] = { "none", "denied", "win", "lockout", "dud", "reset" };

    return (unsigned)e < sizeof(names) / sizeof(names[0]) ? names[e] : "unknown";
}

hg_start_t hg_start_result(int read_rc, int hk_rc)
{
    switch (read_rc) {
    case VFS_OK:
        return hk_rc == HK_OK ? HG_START_OK : HG_START_BADLIST;
    case VFS_E_NOCARD:
    case VFS_E_NOTREE:
        return HG_START_NOCARD;
    case VFS_E_NOTFOUND:
    case VFS_E_DIRNF:
        return HG_START_NOLIST;
    case VFS_E_NOMEM:
        return HG_START_NOMEM;
    default:
        return HG_START_BADLIST;
    }
}

const char *hg_start_name(hg_start_t s)
{
    static const char *const names[HG_START_NUM] = {
        "ok", "nocard", "nolist", "badlist", "nomem", "config",
    };

    return (unsigned)s < HG_START_NUM ? names[s] : "unknown";
}

int hg_start_text(hg_start_t s, const char *path, char *buf, size_t size)
{
    const char *p = path != NULL ? path : "";

    switch (s) {
    case HG_START_OK:
        return k_snprintf(buf, size, "%s", "");
    case HG_START_NOCARD:
        return k_snprintf(buf, size, "%s", "NO UOS CARD: THE PASSWORD GAME HAS NO WORD LIST.");
    case HG_START_NOLIST:
        return k_snprintf(buf, size, "WORD LIST %.24s IS MISSING.", p);
    case HG_START_BADLIST:
        return k_snprintf(buf, size, "WORD LIST %.24s IS DAMAGED.", p);
    case HG_START_NOMEM:
        return k_snprintf(buf, size, "%s", "NOT ENOUGH MEMORY FOR THE PASSWORD GAME.");
    case HG_START_CONFIG:
        return k_snprintf(buf, size, "%s", "THE [hack] SETTINGS ARE REFUSED.");
    default:
        return -1;
    }
}
