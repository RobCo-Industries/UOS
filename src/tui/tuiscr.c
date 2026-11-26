/*
 * The terminal menu screen, its service and the `menu` maintenance
 * command. Thin: the model is tui.c. Files come from the user's home
 * through the file system with the user's rights; commands run through
 * the shell's executor with the menu as their host screen; devices
 * through the device service; tapes through the holotape reader.
 *
 * Maintenance command:
 *   menu state      menu state=<closed|wait|menu|confirm|done> pending= mode=<session|program>
 *                   level= file= sel= count= hidden= act= top= msg=<yes|none>
 *   menu msg        "msg <text>": the message row as it is, "" for none
 *   menu show       "line r= a=<N|D|I> text=<text as is>" per layout line, top to bottom
 *   menu list       "entry i= type= target=<quoted, cut> label=<text as is>" per
 *                   visible entry of the level
 *   menu pick <n>   moves the selection to entry n (from 0), then ENTER
 *   menu back       BACK, as the key
 * Markers:
 *   tui op=check user= result=<menu|shell> rc=     at each login
 *   tui op=open mode=<session|program> user=
 *   tui op=exit
 *   tui menu=<FILE|none> sel= level= state= msg=   after each change
 *   tui act=<menu|header|text|command|device|holotape|logout|close> target=
 *       result=<ok|fail|bad|busy|...> why=        each action and its result
 *   tui result=<covered|gone|busy|range>           a posted call that could not act
 * Text in markers is quoted as one word: a blank is %20.
 */

#include <robco/console.h>
#include <robco/fb.h>
#include <robco/font.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "con.h"
#include "devs.h"
#include "holofmt.h"
#include "holoscr.h"
#include "sess.h"
#include "sh.h"
#include "tui.h"
#include "tuiscr.h"
#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"
#include "uos_text.h"
#include "vfs.h"

_Static_assert(TUI_A_NORMAL == CONSOLE_ATTR_NORMAL && TUI_A_DIM == CONSOLE_ATTR_DIM &&
               TUI_A_INVERSE == CONSOLE_ATTR_INVERSE, "tui attributes match the console's");

static tui_t tm;                    /* about 37 KB: static, as tui.h asks */

static struct {
    int open;                       /* on the stack */
    int prog;                       /* opened by RUN TERMINAL: close mode */
    int act;                        /* TUI_ACT_* out with another module, 0 for none */
    uint16_t runs;                  /* times the screen opened: tags callbacks */
    unsigned posted;                /* maintenance calls not yet run */
    int cmd_status;                 /* the command's status, for its pager's end */
    char cmd_result[TUI_MSG_MAX + 1];
    /* The last state marked. */
    int marked;
    char m_file[TUI_FILE_MAX + 1];
    uint8_t m_sel, m_depth, m_state;
    char m_msg[TUI_MSG_MAX + 1];
} ms;

static uos_screen_t the_screen;

static void *tag(void)
{
    return (void *)(uintptr_t)ms.runs;
}

static int live(void *t)
{
    return ms.open && (uint16_t)(uintptr_t)t == ms.runs;
}

/* ---- Markers ---- */

static void quote(const char *in, char *out, size_t size)
{
    uos_quote_word(in, out, size);
    if (out[0] == '\0' && size > 4) {
        k_snprintf(out, size, "none");
    }
}

static const tui_level_t *level(void)
{
    return tm.depth ? &tm.level[tm.depth - 1] : NULL;
}

/* One marker when the file, the selection, the level, the state or the
 * message changed. The message also goes once to the PL011, as the other
 * screens print their result lines. */
static void mark_state(void)
{
    const tui_level_t *l = level();
    const char *file = l != NULL ? l->file : "none";
    uint8_t sel = l != NULL ? l->sel : 0;
    char q[100];                    /* a marker line holds 224 bytes: a long message is cut */

    if (ms.marked && k_strcmp(file, ms.m_file) == 0 && sel == ms.m_sel &&
        tm.depth == ms.m_depth && tm.state == ms.m_state && k_strcmp(tm.msg, ms.m_msg) == 0) {
        return;
    }
    if (tm.msg[0] != '\0' && k_strcmp(tm.msg, ms.m_msg) != 0) {
        uos_serial_break();
        uart_puts(tm.msg);
        uart_putc('\n');
    }
    ms.marked = 1;
    k_snprintf(ms.m_file, sizeof(ms.m_file), "%s", file);
    ms.m_sel = sel;
    ms.m_depth = tm.depth;
    ms.m_state = tm.state;
    k_snprintf(ms.m_msg, sizeof(ms.m_msg), "%s", tm.msg);
    quote(tm.msg, q, sizeof(q));
    UOS_MARK("tui menu=%s sel=%u level=%u state=%s msg=%s", file, (unsigned)sel,
             (unsigned)tm.depth, tui_state_name(tm.state), q);
}

static void mark_act(int act, const char *target, const char *result, const char *why)
{
    char qt[60];                    /* cut to keep the marker line whole */
    char qw[100];

    quote(target, qt, sizeof(qt));
    quote(why, qw, sizeof(qw));
    UOS_MARK("tui act=%s target=%s result=%s why=%s", tui_act_name(act), qt, result, qw);
}

static void changed(void)
{
    if (!ms.open) {
        return;
    }
    mark_state();
    uos_screen_dirty(&the_screen);
}

/* ---- Files ---- */

static int user_ctx(vp_ctx_t *ctx)
{
    return sess_vp_ctx(ctx) == 0 ? 0 : VFS_E_ARG;
}

static int read_home(const char *name, char **buf, size_t *len)
{
    vp_ctx_t ctx;

    *buf = NULL;
    *len = 0;
    if (user_ctx(&ctx) != 0) {
        return VFS_E_NOPRIV;
    }
    return vfs_read_all(&ctx, sess_privs(), name, NULL, MENU_FILE_MAX, buf, len);
}

/* The plain line for a file that did not load. */
static void read_fail_text(const char *name, int rc, char *out, size_t size)
{
    if (rc == VFS_E_NOTFOUND) {
        k_snprintf(out, size, "File %s not found", name);
    } else {
        k_snprintf(out, size, "Cannot read %s: %s", name, vfs_strerror(rc));
    }
}

/* ---- Actions ---- */

static int fail(int act, const char *target, const char *result, const char *line)
{
    mark_act(act, target, result, line);
    return tui_give_error(&tm, line);
}

static int load(int act)
{
    char name[TUI_FILE_MAX + 1];
    char line[TUI_MSG_MAX + 1];
    char *buf;
    size_t len;
    unsigned depth = tm.depth;
    int rc;

    k_snprintf(name, sizeof(name), "%s", tm.name);
    rc = read_home(name, &buf, &len);
    if (rc != VFS_OK) {
        read_fail_text(name, rc, line, sizeof(line));
        return fail(act, name, "fail", line);
    }
    if (act == TUI_ACT_LOAD_MENU) {
        rc = tui_give_menu(&tm, buf, len);
        vfs_free(buf);
        if (tm.depth == depth) {
            mark_act(act, name, "bad", tm.msg);
        } else {
            mark_act(act, name, level()->menu.errors ? "errors" : "ok", tm.msg);
        }
        return rc;
    }
    rc = tui_give_header(&tm, buf, len);
    vfs_free(buf);
    mark_act(act, name, "ok", "");
    return rc;
}

static void pager_done(int result, void *ctx)
{
    (void)result;
    if (!live(ctx) || tm.state != TUI_ST_WAIT) {
        return;
    }
    if (ms.act == TUI_ACT_COMMAND) {
        (void)tui_done(&tm, ms.cmd_result);
    } else {
        (void)tui_done(&tm, NULL);
    }
    ms.act = 0;
    changed();
}

static int show_text(void)
{
    char name[TUI_FILE_MAX + 1];
    char line[TUI_MSG_MAX + 1];
    char *buf;
    size_t len;
    int rc;

    k_snprintf(name, sizeof(name), "%s", tm.name);
    rc = read_home(name, &buf, &len);
    if (rc != VFS_OK) {
        read_fail_text(name, rc, line, sizeof(line));
        return fail(TUI_ACT_SHOW_TEXT, name, "fail", line);
    }
    /* The pager copies the text. */
    rc = con_page_text(&the_screen, name, buf, len, pager_done, tag());
    vfs_free(buf);
    if (rc != 0) {
        k_snprintf(line, sizeof(line), "Cannot show %s now", name);
        return fail(TUI_ACT_SHOW_TEXT, name, "busy", line);
    }
    ms.act = TUI_ACT_SHOW_TEXT;
    mark_act(TUI_ACT_SHOW_TEXT, name, "ok", "");
    return TUI_ACT_NONE;
}

static const char *status_word(int status)
{
    switch (status) {
    case SH_OK:   return "ok";
    case SH_WARN: return "warn";
    case SH_FAIL: return "fail";
    default:      return "cancel";
    }
}

/* The command ended. Its output, kept by SH_RUN_CAPTURE, shows in the
 * pager; the final message stays on the message line. Output that is
 * only the final message needs no pager. */
static void cmd_done(int status, const char *result, void *ctx)
{
    size_t len = 0;
    const char *out = sh_output(&len);
    size_t rlen = k_strlen(result);
    int rc;

    if (out != NULL && len == rlen + 1u && k_memcmp(out, result, rlen) == 0) {
        len = 0;
    }

    if (!live(ctx) || tm.state != TUI_ST_WAIT || ms.act != TUI_ACT_COMMAND) {
        return;
    }
    ms.cmd_status = status;
    k_snprintf(ms.cmd_result, sizeof(ms.cmd_result), "%s",
               status == SH_CANCELLED ? "Command cancelled" : result);
    mark_act(TUI_ACT_COMMAND, tm.text, status_word(status), result);
    if (out != NULL && len > 0 && uos_screen_unit_on_top(&the_screen)) {
        rc = con_page_text(&the_screen, tm.text, out, len, pager_done, ctx);
        if (rc == 0) {
            return;
        }
    }
    (void)tui_done(&tm, ms.cmd_result);
    ms.act = 0;
    changed();
}

static int command(void)
{
    char line[TUI_MSG_MAX + 1];
    int rc;

    ms.act = TUI_ACT_COMMAND;
    rc = sh_run_line_on(&the_screen, tm.text, SH_RUN_CAPTURE, cmd_done, tag());
    if (rc == 0) {
        /* cmd_done() may have run already. */
        return TUI_ACT_NONE;
    }
    ms.act = 0;
    switch (rc) {
    case SH_ERR_BUSY:    k_snprintf(line, sizeof(line), "A command is running: try again"); break;
    case SH_ERR_ARG:     k_snprintf(line, sizeof(line), "The command line is empty or too long"); break;
    case SH_ERR_COVERED: k_snprintf(line, sizeof(line), "Commands cannot run now"); break;
    default:             k_snprintf(line, sizeof(line), "Commands are not available"); break;
    }
    return fail(TUI_ACT_COMMAND, tm.text, "refused", line);
}

static int device(void)
{
    char target[TUI_DEVNAME_MAX + TUI_DEVWORD_MAX + 2];
    char line[TUI_MSG_MAX + 1];
    const char *text = NULL;
    int rc;

    k_snprintf(target, sizeof(target), "%s %s", tm.name, tm.word);
    rc = devs_command(tm.name, tm.word, sess_privs(), sess_user(), &text);
    if (rc == 0) {
        k_snprintf(line, sizeof(line), "%s: %s", tm.name, text != NULL ? text : "done");
        mark_act(TUI_ACT_DEVICE, target, "ok", text);
        return tui_done(&tm, line);
    }
    k_snprintf(line, sizeof(line), "%s: %s", tm.name, text != NULL ? text : devs_message(rc));
    mark_act(TUI_ACT_DEVICE, target, "fail", line);
    return tui_done(&tm, line);
}

static void holo_done(void *ctx)
{
    if (!live(ctx) || tm.state != TUI_ST_WAIT || ms.act != TUI_ACT_HOLOTAPE) {
        return;
    }
    (void)tui_done(&tm, NULL);
    ms.act = 0;
    changed();
}

static int holotape(void)
{
    char name[HF_FILE_MAX + 1];
    char spec[32];
    char line[TUI_MSG_MAX + 1];
    vp_ctx_t ctx;
    int rc;

    name[0] = '\0';
    if (tm.name[0] != '\0' && hf_tape_name(tm.name, name) != 0) {
        k_snprintf(line, sizeof(line), "Not a holotape name: %.40s", tm.name);
        return fail(TUI_ACT_HOLOTAPE, tm.name, "badname", line);
    }
    if (name[0] != '\0') {
        k_snprintf(spec, sizeof(spec), "SYS$HOLO:%s", name);
        rc = user_ctx(&ctx) == 0 ? vfs_exists(&ctx, sess_privs(), spec) : VFS_E_NOPRIV;
        if (rc == 0) {
            k_snprintf(line, sizeof(line), "No holotape %s", name);
            return fail(TUI_ACT_HOLOTAPE, name, "missing", line);
        }
        if (rc < 0) {
            k_snprintf(line, sizeof(line), "Cannot read holotapes: %s", vfs_strerror(rc));
            return fail(TUI_ACT_HOLOTAPE, name, "fail", line);
        }
    }
    ms.act = TUI_ACT_HOLOTAPE;
    rc = holo_screen_start_tape(name, holo_done, tag());
    if (rc != 0) {
        ms.act = 0;
        return fail(TUI_ACT_HOLOTAPE, name, "busy", "The holotape reader is busy");
    }
    mark_act(TUI_ACT_HOLOTAPE, name, "ok", "");
    return TUI_ACT_NONE;
}

/* Answers the model's actions until it has none. */
static void serve(int act)
{
    while (ms.open && act != TUI_ACT_NONE) {
        switch (act) {
        case TUI_ACT_LOAD_MENU:
        case TUI_ACT_LOAD_HEADER:
            act = load(act);
            break;
        case TUI_ACT_SHOW_TEXT:
            act = show_text();
            break;
        case TUI_ACT_COMMAND:
            act = command();
            break;
        case TUI_ACT_DEVICE:
            act = device();
            break;
        case TUI_ACT_HOLOTAPE:
            act = holotape();
            break;
        case TUI_ACT_LOGOUT:
            mark_act(act, "", "ok", "");
            mark_state();
            (void)sess_logout(SESS_END_USER);
            return;
        case TUI_ACT_CLOSE:
            mark_act(act, "", "ok", "");
            mark_state();
            (void)uos_screen_pop(&the_screen);
            return;
        default:
            act = TUI_ACT_NONE;
            break;
        }
    }
    changed();
}

/* ---- Drawing ---- */

/* The frame: line primitives, not the font's box glyphs, which leave gaps
 * between cells. A rule under the title, one over the message row, and
 * the two sides between them. */
static void draw_frame(unsigned rows)
{
    const console_scheme_t *sc = console_scheme_info(console_scheme());
    uint32_t colour;
    uint32_t w, y0, y1, x0, x1;

    if (sc == NULL || console_cols() < TUI_COLS || rows <= TUI_ROW_MSG) {
        return;
    }
    colour = fb_rgb(sc->dim.r, sc->dim.g, sc->dim.b);
    w = TUI_COLS * FONT_W;
    x0 = FONT_W / 2u - 1u;
    x1 = w - FONT_W / 2u;
    y0 = TUI_ROW_RULE * FONT_H + FONT_H / 2u;
    y1 = TUI_ROW_MSG * FONT_H - 1u;
    fb_fill_rect(x0, y0, x1 - x0 + 1u, 1, colour);
    fb_fill_rect(x0, y1, x1 - x0 + 1u, 1, colour);
    fb_fill_rect(x0, y0, 1, y1 - y0 + 1u, colour);
    fb_fill_rect(x1, y0, 1, y1 - y0 + 1u, colour);
}

static void menu_draw(uos_screen_t *s)
{
    static tui_line_t lines[TUI_LINES_MAX];
    unsigned rows;
    unsigned cols;
    int n;
    int i;

    (void)s;
    if (!uos.fb_ok) {
        return;
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_clear();
    rows = con_text_rows();
    cols = console_cols();
    n = tui_layout(&tm, lines, TUI_LINES_MAX);
    for (i = 0; i < n; i++) {
        size_t len = k_strlen(lines[i].text);

        /* The rule row is a drawn line. */
        if (lines[i].row == TUI_ROW_RULE || lines[i].row >= rows || lines[i].col >= cols) {
            continue;
        }
        if (len > cols - lines[i].col - 1u) {
            len = cols - lines[i].col - 1u;
        }
        console_set_cursor(lines[i].col, lines[i].row);
        console_set_attr(lines[i].attr);
        console_write(lines[i].text, len);
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    draw_frame(rows);
    con_status_redraw();
}

/* ---- The screen ---- */

static void menu_enter(uos_screen_t *s)
{
    const uos_settings_t *st = uos_settings();
    int act;

    (void)s;
    ms.open = 1;
    ms.act = 0;
    ms.marked = 0;
    ms.runs++;
    UOS_MARK("tui op=open mode=%s user=%s", ms.prog ? "program" : "session", sess_user());
    act = tui_init(&tm, sess_privs(), st != NULL ? st->name : NULL, MENU_ROOT);
    if (ms.prog) {
        (void)tui_set_close(&tm);
    }
    serve(act);
}

static void menu_leave(uos_screen_t *s)
{
    (void)s;
    /* A command still running for the menu takes no step and prints
     * nothing: the shell's next tick ends it. */
    sh_host_gone(&the_screen);
    ms.open = 0;
    ms.act = 0;
    ms.runs++;
    UOS_MARK("tui op=exit");
}

/* On top again: the step or the end of a command the menu's job owes. */
static void menu_uncover(uos_screen_t *s)
{
    (void)s;
    (void)sh_host_uncover(&the_screen);
}

static int menu_event(uos_screen_t *s, const uos_event_t *ev)
{
    unsigned k;

    (void)s;
    if (ev->type != UOS_EV_KEY && ev->type != UOS_EV_CHAR) {
        return 0;
    }
    /* Ctrl-C while a command runs for the menu cancels it. */
    if (ev->type == UOS_EV_CHAR && ev->code == 3u) {
        if (ms.act == TUI_ACT_COMMAND) {
            sh_cancel();
        }
        return 1;
    }
    k = tui_map_key(ev->type == UOS_EV_CHAR, ev->code, ev->value == INPUT_REPEAT);
    if (k != 0) {
        serve(tui_key(&tm, k));
    }
    return 1;
}

static uos_screen_t the_screen = {
    .name = "menu",
    .enter = menu_enter,
    .leave = menu_leave,
    .event = menu_event,
    .draw = menu_draw,
    .uncover = menu_uncover,
    .full = 1,
};

int menu_active(void)
{
    return ms.open;
}

uos_screen_t *menu_screen(void)
{
    return &the_screen;
}

/* ---- The session's screen ---- */

/* At each login: the menu when the home holds TERMINAL.CFG, else the shell. */
static uos_screen_t *menu_next(void)
{
    vp_ctx_t ctx;
    int rc = user_ctx(&ctx) == 0 ? vfs_exists(&ctx, sess_privs(), MENU_ROOT) : VFS_E_NOPRIV;

    UOS_MARK("tui op=check user=%s result=%s rc=%d", sess_user(), rc == 1 ? "menu" : "shell", rc);
    if (rc != 1 || ms.open) {
        return NULL;
    }
    ms.prog = 0;
    return &the_screen;
}

/* ---- RUN TERMINAL ---- */

static void push_program(int live)
{
    int rc;

    ms.posted--;
    if (!live) {
        return;
    }
    if (ms.open) {
        UOS_MARK("tui result=busy");
        return;
    }
    ms.prog = 1;
    rc = uos_screen_push(&the_screen);
    if (rc != 0) {
        ms.prog = 0;
        UOS_MARK("tui result=screen rc=%d", rc);
    }
}

/*
 * RUN TERMINAL. The run ends at once: the shell's job is free again, so
 * the menu's own command entries can run. The push is posted: it comes
 * after the shell ended the RUN job, so the shell owes only its prompt
 * while the menu covers it, and shows it when the menu closes.
 */
static int prog_start(const char *args, sh_prog_done_fn done, void *t)
{
    (void)args;
    if (ms.open) {
        return 1;
    }
    if (sh_program_post(push_program) != 0) {
        return 1;
    }
    ms.posted++;
    done(t, 0, NULL);
    return 0;
}

/* ---- The maintenance command ---- */

static void post_pick(void *arg)
{
    uintptr_t v = (uintptr_t)arg;
    uint32_t want = (uint32_t)(v & 0xffu);
    const tui_level_t *l;
    int back = (v & 0x100u) != 0;

    ms.posted--;
    if ((uint16_t)(v >> 16) != ms.runs || !ms.open) {
        UOS_MARK("tui result=gone");
        return;
    }
    /* A covered menu takes no pick: its action would open over the screen
     * on top. */
    if (!uos_screen_on_top(&the_screen)) {
        UOS_MARK("tui result=covered");
        return;
    }
    if (back) {
        serve(tui_key(&tm, INPUT_KEY_BACK));
        return;
    }
    if (tm.state != TUI_ST_MENU) {
        UOS_MARK("tui result=busy");
        return;
    }
    l = level();
    if (l == NULL || want >= l->menu.count) {
        UOS_MARK("tui result=range");
        return;
    }
    while (level()->sel != want) {
        (void)tui_key(&tm, level()->sel < want ? INPUT_KEY_DOWN : INPUT_KEY_UP);
    }
    serve(tui_key(&tm, INPUT_KEY_SELECT));
}

static int post(uos_call_fn fn, void *arg)
{
    if (uos_post(fn, arg) != 0) {
        return uos_maint_fail("full");
    }
    ms.posted++;
    return 0;
}

static void reply_state(void)
{
    const tui_level_t *l = level();
    uos_maint_reply("menu state=%s pending=%u mode=%s level=%u file=%s sel=%u count=%u "
                    "hidden=%u act=%s top=%d msg=%s",
                    ms.open ? tui_state_name(tm.state) : "closed",
                    uos_input_pending() + ms.posted, ms.prog ? "program" : "session",
                    (unsigned)tm.depth, l != NULL ? l->file : "none",
                    l != NULL ? (unsigned)l->sel : 0u, l != NULL ? (unsigned)l->menu.count : 0u,
                    l != NULL ? (unsigned)l->menu.hidden : 0u,
                    ms.open ? tui_act_name(ms.act) : "none",
                    ms.open && uos_screen_on_top(&the_screen), tm.msg[0] != '\0' ? "yes" : "none");
}

static int cmd_menu(int argc, char **argv)
{
    static tui_line_t lines[TUI_LINES_MAX];
    char q[40];                     /* the reply line holds 122 bytes */
    const tui_level_t *l = level();
    uint32_t n;
    int i;

    if (argc == 1 && k_strcmp(argv[0], "state") == 0) {
        reply_state();
        return 0;
    }
    if (argc == 1 && k_strcmp(argv[0], "msg") == 0) {
        /* As is: a quoted message would not fit the reply line. */
        uos_maint_reply("msg %s", tm.msg);
        return 0;
    }
    if (argc == 1 && k_strcmp(argv[0], "show") == 0) {
        int count = ms.open ? tui_layout(&tm, lines, TUI_LINES_MAX) : 0;

        for (i = 0; i < count; i++) {
            uos_maint_reply("line r=%u a=%c text=%s", (unsigned)lines[i].row,
                            lines[i].attr == TUI_A_INVERSE ? 'I' :
                            lines[i].attr == TUI_A_DIM ? 'D' : 'N', lines[i].text);
        }
        return 0;
    }
    if (argc == 1 && k_strcmp(argv[0], "list") == 0) {
        for (n = 0; ms.open && l != NULL && n < l->menu.count; n++) {
            quote(l->menu.entry[n].target, q, sizeof(q));
            uos_maint_reply("entry i=%u type=%s target=%s label=%s", (unsigned)n,
                            tui_type_name(l->menu.entry[n].type), q, l->menu.entry[n].label);
        }
        return 0;
    }
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    if (argc == 2 && k_strcmp(argv[0], "pick") == 0) {
        if (uos_dec_u32(argv[1], TUI_MAX_ENTRIES - 1u, &n) != 0) {
            return uos_maint_fail("usage menu pick <n>");
        }
        return post(post_pick, (void *)(((uintptr_t)ms.runs << 16) | n));
    }
    if (argc == 1 && k_strcmp(argv[0], "back") == 0) {
        return post(post_pick, (void *)(((uintptr_t)ms.runs << 16) | 0x100u));
    }
    return uos_maint_fail("usage menu state|msg|show|list|pick <n>|back");
}

/* ---- The service ---- */

static int menu_svc_init(void)
{
    int rc = uos_maint_register("menu", cmd_menu,
                                "the terminal menu: state msg show list pick back");

    if (rc != 0) {
        return rc;
    }
    rc = sh_program_register("TERMINAL", prog_start);
    if (rc != 0) {
        return rc;
    }
    return sh_session_screen_register(menu_next);
}

static void menu_svc_shutdown(int why)
{
    (void)why;
}

const uos_service_t uos_svc_menu = { "menu", menu_svc_init, NULL, menu_svc_shutdown };
