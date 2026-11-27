/*
 * The terminal screen model. Pure: keys in, actions and a layout out.
 * The caller loads every file the model asks for.
 */

#include "tui.h"

#include <robco/klibc.h>

/* Copies at most max characters, control bytes as '?'. */
static void copy_line(char *dst, const char *src, unsigned max)
{
    unsigned n = 0;

    if (src != 0) {
        for (; src[n] && n < max; n++) {
            unsigned char ch = (unsigned char)src[n];

            dst[n] = (ch < 0x20 || ch > 0x7e) ? '?' : (char)ch;
        }
    }
    dst[n] = 0;
}

static void set_msg(tui_t *t, const char *s)
{
    copy_line(t->msg, s, TUI_MSG_MAX);
}

static tui_level_t *cur(tui_t *t)
{
    return t->depth ? &t->level[t->depth - 1] : 0;
}

static const tui_level_t *ccur(const tui_t *t)
{
    return t->depth ? &t->level[t->depth - 1] : 0;
}

int tui_init(tui_t *t, uint32_t user_privs, const char *sysname, const char *root)
{
    if (t == 0 || root == 0 || !tui_is_83(root)) {
        return TUI_E_ARG;
    }
    k_memset(t, 0, sizeof(*t));
    t->privs = user_privs;
    copy_line(t->sysname, (sysname && sysname[0]) ? sysname : "UOS", TUI_SYSNAME_MAX);
    copy_line(t->name, root, TUI_FILE_MAX);
    t->state = TUI_ST_WAIT;
    t->pending = TUI_ACT_LOAD_MENU;
    return TUI_ACT_LOAD_MENU;
}

unsigned tui_rows(const tui_t *t)
{
    const tui_level_t *l = ccur(t);
    unsigned nhead = l ? l->nhead : 0;

    return TUI_ROW_LAST_ENTRY - (TUI_ROW_HEAD + nhead + 1) + 1;
}

static void fix_scroll(tui_t *t)
{
    tui_level_t *l = cur(t);
    unsigned rows = tui_rows(t);
    unsigned count;

    if (l == 0) {
        return;
    }
    count = l->menu.count;
    if (l->sel >= count) {
        l->sel = count ? (uint8_t)(count - 1) : 0;
    }
    if (l->sel < l->top) {
        l->top = l->sel;
    }
    if (l->sel >= l->top + rows) {
        l->top = (uint8_t)(l->sel - rows + 1);
    }
    if (count > rows && l->top > count - rows) {
        l->top = (uint8_t)(count - rows);
    }
    if (count <= rows) {
        l->top = 0;
    }
}

static int wait_for(tui_t *t, int act)
{
    t->state = TUI_ST_WAIT;
    t->pending = (uint8_t)act;
    return act;
}

static int choose(tui_t *t)
{
    tui_level_t *l = cur(t);
    const tui_entry_t *e;
    const char *sp;

    if (l == 0 || l->menu.count == 0) {
        return TUI_ACT_NONE;
    }
    e = &l->menu.entry[l->sel];
    t->name[0] = 0;
    t->word[0] = 0;
    t->text[0] = 0;
    switch (e->type) {
    case TUI_T_TEXT:
        copy_line(t->name, e->target, TUI_FILE_MAX);
        return wait_for(t, TUI_ACT_SHOW_TEXT);
    case TUI_T_MENU:
        if (t->depth >= TUI_DEPTH_MAX) {
            set_msg(t, "Menus nest 8 deep at most");
            return TUI_ACT_NONE;
        }
        copy_line(t->name, e->target, TUI_FILE_MAX);
        return wait_for(t, TUI_ACT_LOAD_MENU);
    case TUI_T_COMMAND:
        copy_line(t->text, e->target, TUI_COMMAND_MAX);
        return wait_for(t, TUI_ACT_COMMAND);
    case TUI_T_DEVICE:
        /* The parser stored "NAME WORD", both within their limits. */
        sp = e->target;
        while (*sp && *sp != ' ') {
            sp++;
        }
        copy_line(t->name, e->target, (unsigned)(sp - e->target));
        copy_line(t->word, *sp ? sp + 1 : sp, TUI_DEVWORD_MAX);
        return wait_for(t, TUI_ACT_DEVICE);
    case TUI_T_HOLOTAPE:
        copy_line(t->name, e->target, TUI_FILE_MAX);
        return wait_for(t, TUI_ACT_HOLOTAPE);
    case TUI_T_LOGOUT:
        t->state = TUI_ST_DONE;
        t->pending = TUI_ACT_NONE;
        return TUI_ACT_LOGOUT;
    default:
        return TUI_ACT_NONE;
    }
}

static int is_choose(unsigned key)
{
    return key == INPUT_KEY_SELECT || key == INPUT_KEY_DIAL_PUSH;
}

static int is_back(unsigned key)
{
    return key == INPUT_KEY_BACK || key == INPUT_KEY_LEFT;
}

int tui_key(tui_t *t, unsigned key)
{
    tui_level_t *l;
    unsigned count;

    if (t == 0) {
        return TUI_E_ARG;
    }
    if (t->state == TUI_ST_CONFIRM) {
        if (is_choose(key)) {
            t->state = TUI_ST_DONE;
            return t->closes ? TUI_ACT_CLOSE : TUI_ACT_LOGOUT;
        }
        if (is_back(key)) {
            t->state = TUI_ST_MENU;
        }
        return TUI_ACT_NONE;
    }
    if (t->state != TUI_ST_MENU) {
        return TUI_ACT_NONE;
    }
    t->msg[0] = 0;
    l = cur(t);
    count = l ? l->menu.count : 0;
    if (is_back(key)) {
        if (t->depth > 1) {
            t->depth--;
        } else {
            t->state = TUI_ST_CONFIRM;
        }
        return TUI_ACT_NONE;
    }
    if (count == 0) {
        return TUI_ACT_NONE;
    }
    if (key == INPUT_KEY_UP) {
        l->sel = (uint8_t)(l->sel == 0 ? count - 1 : l->sel - 1u);
    } else if (key == INPUT_KEY_DOWN) {
        l->sel = (uint8_t)(l->sel + 1u >= count ? 0 : l->sel + 1u);
    } else if (key >= TUI_CHAR('1') && key <= TUI_CHAR('9')) {
        unsigned n = key - TUI_CHAR('1');

        if (n < count) {
            l->sel = (uint8_t)n;
        }
    } else if (is_choose(key)) {
        return choose(t);
    }
    fix_scroll(t);
    return TUI_ACT_NONE;
}

int tui_set_close(tui_t *t)
{
    if (t == 0) {
        return TUI_E_ARG;
    }
    t->closes = 1;
    return TUI_OK;
}

unsigned tui_map_key(int is_char, unsigned code, int repeat)
{
    if (is_char) {
        if (code >= '1' && code <= '9') {
            return TUI_CHAR(code);
        }
        return (code == '\r' || code == '\n') ? INPUT_KEY_SELECT : 0;
    }
    if (code == INPUT_KEY_NONE || code >= INPUT_KEY_COUNT) {
        return 0;
    }
    if (repeat && code != INPUT_KEY_UP && code != INPUT_KEY_DOWN) {
        return 0;
    }
    return code;
}

const char *tui_act_name(int act)
{
    static const char *const names[] = { "none", "menu", "header", "text", "command",
                                         "device", "holotape", "logout", "close" };

    return act >= 0 && act <= TUI_ACT_CLOSE ? names[act] : "?";
}

const char *tui_state_name(int state)
{
    static const char *const names[] = { "wait", "menu", "confirm", "done" };

    return state >= 0 && state <= TUI_ST_DONE ? names[state] : "?";
}

int tui_login_choice(int has_file, int read_ok, int parse_rc, unsigned visible,
                     uint32_t privs, int *why)
{
    int w = TUI_WHY_NONE;

    if (has_file) {
        if (!read_ok) {
            w = TUI_WHY_READ;
        } else if (parse_rc != TUI_OK) {
            w = TUI_WHY_SYNTAX;
        } else if (visible == 0) {
            w = TUI_WHY_EMPTY;
        }
    }
    if (why != 0) {
        *why = w;
    }
    if (!has_file) {
        return TUI_OPEN_SHELL;
    }
    if (w != TUI_WHY_NONE && (privs & (UOS_PRIV_SYSTEM | UOS_PRIV_SECURITY)) != 0) {
        return TUI_OPEN_SHELL;
    }
    return TUI_OPEN_MENU;
}

const char *tui_why_name(int why)
{
    static const char *const names[] = { "none", "read", "syntax", "empty" };

    return why >= 0 && why <= TUI_WHY_EMPTY ? names[why] : "?";
}

int tui_event(tui_t *t, const input_event_t *ev)
{
    if (t == 0 || ev == 0) {
        return TUI_E_ARG;
    }
    if (ev->type == INPUT_EV_KEY && ev->value != INPUT_RELEASE) {
        return tui_key(t, ev->code);
    }
    if (ev->type == INPUT_EV_CHAR && ev->code < 0x100) {
        return tui_key(t, TUI_CHAR(ev->code));
    }
    return TUI_ACT_NONE;
}

int tui_give_menu(tui_t *t, const char *text, size_t len)
{
    tui_level_t *l;
    unsigned line = 0;
    char buf[TUI_MSG_MAX + 1];

    if (t == 0) {
        return TUI_E_ARG;
    }
    if (t->state != TUI_ST_WAIT || t->pending != TUI_ACT_LOAD_MENU || t->depth >= TUI_DEPTH_MAX) {
        return TUI_E_STATE;
    }
    l = &t->level[t->depth];
    t->state = TUI_ST_MENU;
    t->pending = TUI_ACT_NONE;
    if (tui_cfg_parse(text, len, t->privs, &l->menu, &line) != TUI_OK) {
        k_snprintf(buf, sizeof(buf), "Bad menu file %s (line %u)", t->name, line);
        set_msg(t, buf);
        return TUI_ACT_NONE;
    }
    copy_line(l->file, t->name, TUI_FILE_MAX);
    l->sel = 0;
    l->top = 0;
    l->nhead = 0;
    t->depth++;
    t->msg[0] = 0;
    if (l->menu.errors) {
        k_snprintf(buf, sizeof(buf), "%s: %u errors, first: %s (line %u)", l->file,
                   (unsigned)l->menu.errors, tui_strerror(l->menu.err[0].code),
                   (unsigned)l->menu.err[0].line);
        set_msg(t, buf);
    }
    /* A root file with no entry for this user says why. BACK logs out. */
    if (t->depth == 1 && l->menu.count == 0) {
        k_snprintf(buf, sizeof(buf), "No menu entry for this user in %s", l->file);
        set_msg(t, buf);
    }
    if (l->menu.header[0]) {
        copy_line(t->name, l->menu.header, TUI_FILE_MAX);
        return wait_for(t, TUI_ACT_LOAD_HEADER);
    }
    return TUI_ACT_NONE;
}

int tui_give_header(tui_t *t, const char *text, size_t len)
{
    tui_level_t *l;
    size_t i = 0;

    if (t == 0 || (text == 0 && len > 0)) {
        return TUI_E_ARG;
    }
    if (t->state != TUI_ST_WAIT || t->pending != TUI_ACT_LOAD_HEADER) {
        return TUI_E_STATE;
    }
    l = cur(t);
    l->nhead = 0;
    while (i < len && l->nhead < TUI_HEADER_LINES) {
        char *out = l->head[l->nhead];
        unsigned col = 0;

        while (i < len && text[i] != '\n' && text[i] != '\r') {
            unsigned char ch = (unsigned char)text[i++];

            if (ch == '\t') {
                do {
                    if (col < TUI_TEXT_COLS) {
                        out[col] = ' ';
                    }
                    col++;
                } while (col % PG_TAB);
            } else if (col < TUI_TEXT_COLS) {
                out[col] = (ch < 0x20 || ch > 0x7e) ? '?' : (char)ch;
                col++;
            } else {
                col++;
            }
        }
        out[col < TUI_TEXT_COLS ? col : TUI_TEXT_COLS] = 0;
        if (i < len && text[i] == '\r') {
            i++;
        }
        if (i < len && text[i] == '\n') {
            i++;
        }
        l->nhead++;
    }
    t->state = TUI_ST_MENU;
    t->pending = TUI_ACT_NONE;
    fix_scroll(t);
    return TUI_ACT_NONE;
}

int tui_give_error(tui_t *t, const char *line)
{
    if (t == 0) {
        return TUI_E_ARG;
    }
    if (t->state != TUI_ST_WAIT) {
        return TUI_E_STATE;
    }
    t->state = TUI_ST_MENU;
    t->pending = TUI_ACT_NONE;
    set_msg(t, line);
    return TUI_ACT_NONE;
}

int tui_done(tui_t *t, const char *line)
{
    if (t == 0) {
        return TUI_E_ARG;
    }
    if (t->state != TUI_ST_WAIT ||
        (t->pending != TUI_ACT_SHOW_TEXT && t->pending != TUI_ACT_COMMAND &&
         t->pending != TUI_ACT_DEVICE && t->pending != TUI_ACT_HOLOTAPE)) {
        return TUI_E_STATE;
    }
    t->state = TUI_ST_MENU;
    t->pending = TUI_ACT_NONE;
    set_msg(t, line);
    return TUI_ACT_NONE;
}

/* ---- Layout ---- */

static int put(tui_line_t *out, unsigned max, unsigned *n, unsigned row,
               unsigned attr, const char *s)
{
    tui_line_t *l;

    if (s[0] == 0) {
        return 0;
    }
    if (*n >= max) {
        return -1;
    }
    l = &out[(*n)++];
    l->row = (uint8_t)row;
    l->col = 1;
    l->attr = (uint8_t)attr;
    copy_line(l->text, s, TUI_TEXT_COLS);
    return 0;
}

static const char *level_name(const tui_level_t *l)
{
    return l->menu.title[0] ? l->menu.title : l->file;
}

/*
 * Emits the breadcrumb "A / B / C" (pass 0 counts, pass 1 writes the
 * characters from `skip` on into buf at `at`).
 */
static unsigned crumb_pass(const tui_t *t, char *buf, unsigned skip, unsigned at)
{
    unsigned pos = 0;
    unsigned i;

    for (i = 0; i < t->depth; i++) {
        const char *s = level_name(&t->level[i]);
        const char *parts[2] = { i ? " / " : "", s };
        unsigned p;

        for (p = 0; p < 2; p++) {
            for (s = parts[p]; *s; s++, pos++) {
                if (buf && pos >= skip) {
                    buf[at++] = *s;
                }
            }
        }
    }
    if (buf) {
        buf[at] = 0;
    }
    return pos;
}

/* buf holds TUI_TEXT_COLS + 1 bytes. A long trail keeps its end after "...". */
static void crumb(const tui_t *t, char *buf)
{
    unsigned n = crumb_pass(t, 0, 0, 0);

    if (n <= TUI_TEXT_COLS) {
        crumb_pass(t, buf, 0, 0);
    } else {
        k_memcpy(buf, "...", 3);
        crumb_pass(t, buf, n - (TUI_TEXT_COLS - 3), 3);
    }
}

static const char *hint(const tui_t *t)
{
    const tui_level_t *l = ccur(t);
    int top = t->depth <= 1;

    switch (t->state) {
    case TUI_ST_WAIT:
        return "Please wait";
    case TUI_ST_CONFIRM:
        return t->closes ? "ENTER: close  BACK: stay" : "ENTER: log out  BACK: stay";
    case TUI_ST_DONE:
        return t->closes ? "Closed" : "Logged out";
    default:
        break;
    }
    if (l == 0 || l->menu.count == 0) {
        return !top ? "BACK: up a level" : t->closes ? "BACK: close" : "BACK: log out";
    }
    return !top ? "UP/DOWN: move  1-9: pick  ENTER: choose  BACK: up a level"
           : t->closes ? "UP/DOWN: move  1-9: pick  ENTER: choose  BACK: close"
                       : "UP/DOWN: move  1-9: pick  ENTER: choose  BACK: log out";
}

int tui_layout(const tui_t *t, tui_line_t *out, unsigned max)
{
    const tui_level_t *l;
    unsigned n = 0;
    unsigned i;
    unsigned row;
    unsigned rows;
    char buf[TUI_TEXT_COLS + 1];
    int rc = 0;

    if (t == 0 || out == 0) {
        return TUI_E_ARG;
    }
    l = ccur(t);
    rc |= put(out, max, &n, TUI_ROW_SYSNAME, TUI_A_NORMAL, t->sysname);
    if (l) {
        rc |= put(out, max, &n, TUI_ROW_TITLE, TUI_A_NORMAL, level_name(l));
    }
    k_memset(buf, '-', TUI_TEXT_COLS);
    buf[TUI_TEXT_COLS] = 0;
    rc |= put(out, max, &n, TUI_ROW_RULE, TUI_A_DIM, buf);
    if (l) {
        for (i = 0; i < l->nhead; i++) {
            rc |= put(out, max, &n, TUI_ROW_HEAD + i, TUI_A_NORMAL, l->head[i]);
        }
        row = TUI_ROW_HEAD + l->nhead + 1;
        rows = tui_rows(t);
        if (l->menu.count == 0 && t->state != TUI_ST_WAIT) {
            rc |= put(out, max, &n, row, TUI_A_DIM, "(no entries)");
        }
        for (i = l->top; i < l->menu.count && i < (unsigned)l->top + rows; i++, row++) {
            int sel = i == l->sel;

            k_snprintf(buf, sizeof(buf), "%s%s", sel ? "> " : "  ", l->menu.entry[i].label);
            rc |= put(out, max, &n, row, sel ? TUI_A_INVERSE : TUI_A_NORMAL, buf);
        }
    }
    if (t->state == TUI_ST_CONFIRM) {
        rc |= put(out, max, &n, TUI_ROW_MSG, TUI_A_NORMAL,
                  t->closes ? "Close the menu? ENTER: yes  BACK: no"
                            : "Log out? ENTER: yes  BACK: no");
    } else {
        rc |= put(out, max, &n, TUI_ROW_MSG, TUI_A_NORMAL, t->msg);
    }
    crumb(t, buf);
    rc |= put(out, max, &n, TUI_ROW_CRUMB, TUI_A_DIM, buf);
    rc |= put(out, max, &n, TUI_ROW_FOOTER, TUI_A_DIM, hint(t));
    return rc ? TUI_E_SPACE : (int)n;
}
