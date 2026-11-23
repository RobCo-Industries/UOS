/*
 * The vfs service: checks the card state at init and adds the `fs`
 * maintenance command. Every `fs` sub-command that touches the card is
 * posted to the loop and reports with an "ev=fs" marker. See vfs.h and
 * docs/ARCHITECTURE.md.
 */
#include <robco/alloc.h>
#include <robco/crc32.h>
#include <robco/irq.h>
#include <robco/klibc.h>

#include "uos.h"
#include "uos_maint.h"
#include "uos_service.h"
#include "vfs.h"
#include "vfsrules.h"

/* Limits of the maintenance commands. */
#define FS_LS_DEFAULT  32u          /* entries `fs ls` prints when no max is given */
#define FS_LS_MAX      128u
#define FS_CAT_DEFAULT 65536u       /* bytes `fs cat` reads when no max is given */
#define FS_READ_MAX    (4u << 20)   /* largest max of `fs cat` and file of `fs crc` */
#define FS_LINE_SHOW   60u          /* characters of the first line `fs cat` shows */
#define FS_ARG_MAX     (UOS_MAINT_LINE_MAX + 1)

enum { OP_LS = 1, OP_CAT, OP_CRC, OP_WRITE, OP_NEW, OP_DEL, OP_REN, OP_COPY, OP_MKDIR,
       OP_FREE, OP_STAT, OP_PATTERN };

static const char *const op_names[] = {
    "", "ls", "cat", "crc", "write", "new", "del", "ren", "copy", "mkdir", "free", "stat",
    "pattern",
};

/* The test context: the maintenance port acts as this user. */
static vp_ctx_t ctx;
static uint32_t privs = VP_PRIV_SYSTEM;

/* One request at a time. */
static struct {
    int op;
    int busy;
    char a[FS_ARG_MAX];
    char b[FS_ARG_MAX];
    uint32_t n;
} req;

static vfs_iter_t iter;
static vfs_copy_t copy;
static unsigned copy_files, copy_steps;
static uint32_t copy_max_ticks;
static vfs_entry_t ls_entries[FS_LS_MAX];

static void done(int rc, const char *extra)
{
    UOS_MARK("fs op=%s rc=%d err=%s%s%s", op_names[req.op], rc, vfs_strerror(rc),
             extra[0] != '\0' ? " " : "", extra);
    req.busy = 0;
}

static int parse_u32(const char *s, uint32_t max, uint32_t *out)
{
    uint32_t n = 0;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    for (; *s >= '0' && *s <= '9'; s++) {
        if (n > (max - (uint32_t)(*s - '0')) / 10u) {
            return -1;
        }
        n = n * 10u + (uint32_t)(*s - '0');
    }
    if (*s != '\0') {
        return -1;
    }
    *out = n;
    return 0;
}

/* ---- Posted work ------------------------------------------------------------- */

static void run_ls(void)
{
    char date[VFS_DATE_LEN + 1];
    char extra[64];
    size_t count = 0, total = 0, i;
    int rc = vfs_list(&ctx, privs, req.a, ls_entries, req.n, &count, &total);

    for (i = 0; i < count; i++) {
        const vfs_entry_t *e = &ls_entries[i];

        (void)vfs_fmt_datetime(e->date, e->time, date, sizeof(date));
        date[11] = '\0';
        UOS_MARK("fs op=ls entry=%s.%s;1 size=%u date=%s time=%s dir=%u", e->name, e->type,
                 (unsigned)e->size, date, date + 12, (unsigned)e->is_dir);
    }
    (void)k_snprintf(extra, sizeof(extra), "count=%u total=%u more=%u", (unsigned)count,
                     (unsigned)total, total > count ? 1u : 0u);
    done(rc, extra);
}

static void run_cat(int crc)
{
    char line[FS_LINE_SHOW + 1];
    char extra[FS_LINE_SHOW + 48];
    char *buf = NULL;
    size_t len = 0, i;
    int rc = vfs_read_all(&ctx, privs, req.a, "TXT", crc ? FS_READ_MAX : req.n, &buf, &len);

    if (rc != VFS_OK) {
        done(rc, "");
        return;
    }
    if (crc) {
        (void)k_snprintf(extra, sizeof(extra), "len=%u crc=0x%08x", (unsigned)len,
                         (unsigned)crc32(buf, len));
    } else {
        for (i = 0; i < len && i < FS_LINE_SHOW && buf[i] != '\n' && buf[i] != '\r'; i++) {
            char ch = buf[i];

            line[i] = ch == ' ' ? '_' : (ch > ' ' && ch < 127) ? ch : '?';
        }
        line[i] = '\0';
        (void)k_snprintf(extra, sizeof(extra), "len=%u line=%s", (unsigned)len,
                         i > 0 ? line : "-");
    }
    vfs_free(buf);
    done(rc, extra);
}

static void run_write(unsigned flags)
{
    size_t n = k_strlen(req.b);

    req.b[n] = '\n';
    done(vfs_write_all(&ctx, privs, req.a, NULL, req.b, n + 1, flags), "");
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds: a file of n bytes, byte i = (i * 31 + 7) mod 256. */
static void run_pattern(void)
{
    uint8_t *p = NULL;
    uint32_t i;
    int rc;

    if (req.n > 0) {
        uint32_t s = irq_save();

        p = k_malloc(req.n);
        irq_restore(s);
        if (p == NULL) {
            done(VFS_E_NOMEM, "");
            return;
        }
        for (i = 0; i < req.n; i++) {
            p[i] = (uint8_t)(i * 31u + 7u);
        }
    }
    rc = vfs_write_all(&ctx, privs, req.a, NULL, p, req.n, 0);
    vfs_free(p);
    done(rc, "");
}
#endif

/* DELETE and RENAME: every match in one pass, one marker per file. */
static void run_each(void)
{
    char extra[48];
    const char *spec;
    unsigned files = 0;
    int rc = vfs_iter_begin(&iter, &ctx, privs, req.a,
                            req.op == OP_DEL ? VP_OP_DELETE : VP_OP_WRITE);

    while (rc == VFS_OK && (spec = vfs_iter_next(&iter)) != NULL) {
        rc = req.op == OP_DEL ? vfs_delete(&ctx, privs, spec)
                              : vfs_rename(&ctx, privs, spec, req.b);
        if (rc == VFS_OK) {
            files++;
            UOS_MARK("fs op=%s file=%s", op_names[req.op], spec);
        }
    }
    (void)k_snprintf(extra, sizeof(extra), "files=%u skipped=%u", files,
                     (unsigned)iter.skipped);
    vfs_iter_end(&iter);
    done(rc, extra);
}

/* COPY: one vfs_copy_step() per loop pass, then the next match. */
static void copy_next(void *arg);

static void copy_finish(int rc)
{
    char extra[80];

    (void)k_snprintf(extra, sizeof(extra), "files=%u skipped=%u steps=%u maxticks=%u",
                     copy_files, (unsigned)iter.skipped, copy_steps, (unsigned)copy_max_ticks);
    vfs_iter_end(&iter);
    done(rc, extra);
}

static void copy_step(void *arg)
{
    uint32_t t0 = uos_ticks(), dt;
    int rc = vfs_copy_step(&copy);

    (void)arg;
    dt = uos_ticks() - t0;
    copy_steps++;
    if (dt > copy_max_ticks) {
        copy_max_ticks = dt;
    }
    if (rc == 1) {
        if (uos_post(copy_step, NULL) != 0) {
            vfs_copy_abort(&copy);
            copy_finish(VFS_E_BUSY);
        }
        return;
    }
    if (rc != VFS_OK) {
        copy_finish(rc);
        return;
    }
    copy_files++;
    copy_next(NULL);
}

static void copy_next(void *arg)
{
    const char *spec = vfs_iter_next(&iter);
    int rc;

    (void)arg;
    if (spec == NULL) {
        copy_finish(VFS_OK);
        return;
    }
    rc = vfs_copy_begin(&copy, &ctx, privs, spec, req.b, (unsigned)req.n);
    if (rc != VFS_OK) {
        copy_finish(rc);
        return;
    }
    UOS_MARK("fs op=copy file=%s", spec);
    if (uos_post(copy_step, NULL) != 0) {
        vfs_copy_abort(&copy);
        copy_finish(VFS_E_BUSY);
    }
}

static void run_copy(void)
{
    int rc = vfs_iter_begin(&iter, &ctx, privs, req.a, VP_OP_READ);

    copy_files = 0;
    copy_steps = 0;
    copy_max_ticks = 0;
    if (rc != VFS_OK) {
        copy_finish(rc);
        return;
    }
    copy_next(NULL);
}

static void run_stat(void)
{
    char date[VFS_DATE_LEN + 1];
    char extra[96];
    vfs_entry_t e;
    int ex = vfs_exists(&ctx, privs, req.a);
    int rc = vfs_stat(&ctx, privs, req.a, &e);

    if (rc != VFS_OK) {
        (void)k_snprintf(extra, sizeof(extra), "exists=%d", ex);
        done(rc, extra);
        return;
    }
    (void)vfs_fmt_datetime(e.date, e.time, date, sizeof(date));
    date[11] = '\0';
    (void)k_snprintf(extra, sizeof(extra), "exists=%d name=%s.%s;1 size=%u date=%s time=%s dir=%u",
                     ex, e.name, e.type, (unsigned)e.size, date, date + 12, (unsigned)e.is_dir);
    done(rc, extra);
}

static void run_free(void)
{
    char extra[64];
    uint64_t total = 0, free_bytes = 0;
    int rc = vfs_free_space(&total, &free_bytes);

    (void)k_snprintf(extra, sizeof(extra), "total=%u free=%u", (unsigned)total,
                     (unsigned)free_bytes);
    done(rc, extra);
}

static void run(void *arg)
{
    (void)arg;
    switch (req.op) {
    case OP_LS:    run_ls(); break;
    case OP_CAT:   run_cat(0); break;
    case OP_CRC:   run_cat(1); break;
    case OP_WRITE: run_write(0); break;
    case OP_NEW:   run_write(VFS_W_NEW); break;
    case OP_DEL:
    case OP_REN:   run_each(); break;
    case OP_COPY:  run_copy(); break;
    case OP_MKDIR: done(vfs_mkdir(&ctx, privs, req.a), ""); break;
    case OP_FREE:  run_free(); break;
    case OP_STAT:  run_stat(); break;
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    case OP_PATTERN: run_pattern(); break;
#endif
    default:       done(VFS_E_ARG, ""); break;
    }
}

/* ---- The maintenance command ---------------------------------------------------- */

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds: a fake device service with DOOR1 and LAMP2. */
static char fake_last[32];

static int fake_find(const char *name)
{
    return k_strcmp(name, "DOOR1") == 0 || k_strcmp(name, "LAMP2") == 0;
}

static int fake_read(const char *name, char *buf, size_t size)
{
    if (!fake_find(name)) {
        return VFS_E_NODEV;
    }
    return k_snprintf(buf, size, "%s FAKE %s", name, fake_last[0] ? fake_last : "IDLE");
}

static int fake_write(const char *name, const char *text, uint32_t privs)
{
    size_t i;

    (void)privs;
    if (!fake_find(name)) {
        return VFS_E_NODEV;
    }
    for (i = 0; i + 1 < sizeof(fake_last) && text[i] != '\0' && text[i] != '\n'; i++) {
        fake_last[i] = text[i] == ' ' ? '_' : text[i];
    }
    fake_last[i] = '\0';
    return VFS_OK;
}

static int fake_list(unsigned index, char *name, size_t size)
{
    static const char *const names[] = { "LAMP2", "DOOR1" };

    if (index >= 2u) {
        return VFS_E_NODEV;
    }
    return k_snprintf(name, size, "%s", names[index]);
}

static const vfs_dev_hooks_t fake_hooks = { fake_read, fake_write, fake_list };
#endif

static int post(int op, const char *a, const char *b, uint32_t n)
{
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("unknown command");
    }
    if (req.busy) {
        return uos_maint_fail("busy");
    }
    req.op = op;
    req.n = n;
    (void)k_snprintf(req.a, sizeof(req.a), "%s", a != NULL ? a : "");
    (void)k_snprintf(req.b, sizeof(req.b), "%s", b != NULL ? b : "");
    req.busy = 1;
    if (uos_post(run, NULL) != 0) {
        req.busy = 0;
        return uos_maint_fail("full");
    }
    return 0;
}

static int cmd_fs(int argc, char **argv)
{
    const char *sub = argc > 0 ? argv[0] : "";
    char shown[VP_DISPLAY_MAX];
    uint32_t n = 0;
    int rc;

    if (k_strcmp(sub, "ls") == 0 && (argc == 2 || argc == 3)) {
        n = FS_LS_DEFAULT;
        if (argc == 3 && (parse_u32(argv[2], FS_LS_MAX, &n) != 0 || n == 0)) {
            return uos_maint_fail("max 1 to 128");
        }
        return post(OP_LS, argv[1], NULL, n);
    }
    if ((k_strcmp(sub, "ls") == 0 || k_strcmp(sub, "free") == 0) && argc == 1) {
        return post(sub[0] == 'l' ? OP_LS : OP_FREE, "", NULL, FS_LS_DEFAULT);
    }
    if (k_strcmp(sub, "cat") == 0 && (argc == 2 || argc == 3)) {
        n = FS_CAT_DEFAULT;
        if (argc == 3 && parse_u32(argv[2], FS_READ_MAX, &n) != 0) {
            return uos_maint_fail("max 0 to 4194304");
        }
        return post(OP_CAT, argv[1], NULL, n);
    }
    if (argc == 2 && k_strcmp(sub, "crc") == 0) {
        return post(OP_CRC, argv[1], NULL, 0);
    }
    if (argc == 2 && k_strcmp(sub, "del") == 0) {
        return post(OP_DEL, argv[1], NULL, 0);
    }
    if (argc == 2 && k_strcmp(sub, "mkdir") == 0) {
        return post(OP_MKDIR, argv[1], NULL, 0);
    }
    if (argc == 2 && k_strcmp(sub, "stat") == 0) {
        return post(OP_STAT, argv[1], NULL, 0);
    }
    if (argc == 3 && k_strcmp(sub, "ren") == 0) {
        return post(OP_REN, argv[1], argv[2], 0);
    }
    if (k_strcmp(sub, "copy") == 0 &&
        (argc == 3 || (argc == 4 && k_strcmp(argv[3], "new") == 0))) {
        return post(OP_COPY, argv[1], argv[2], argc == 4 ? VFS_W_NEW : 0);
    }
    if (argc >= 3 && (k_strcmp(sub, "write") == 0 || k_strcmp(sub, "new") == 0)) {
        return post(sub[0] == 'w' ? OP_WRITE : OP_NEW, argv[1],
                    uos_mt_rejoin(argv[2], argv[argc - 1]), 0);
    }
    if (argc == 2 && k_strcmp(sub, "default") == 0) {
        rc = vp_set_default(&ctx, argv[1]);
        if (rc != VP_OK) {
            return uos_maint_fail("bad default");
        }
        (void)vp_show_default(&ctx, shown, sizeof(shown));
        uos_maint_reply("default %s", shown);
        return 0;
    }
    if (argc == 3 && k_strcmp(sub, "who") == 0) {
        if (parse_u32(argv[2], 0xffffffffu, &n) != 0) {
            return uos_maint_fail("privs is a number");
        }
        vp_ctx_init(&ctx, k_strcmp(argv[1], "-") == 0 ? NULL : argv[1]);
        privs = n;
        (void)vp_show_default(&ctx, shown, sizeof(shown));
        uos_maint_reply("who home=%s privs=%u default=%s", ctx.home[0] ? ctx.home : "-",
                        (unsigned)privs, shown);
        return 0;
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    if (argc == 3 && k_strcmp(sub, "pattern") == 0) {
        if (parse_u32(argv[2], FS_READ_MAX, &n) != 0) {
            return uos_maint_fail("bytes 0 to 4194304");
        }
        return post(OP_PATTERN, argv[1], NULL, n);
    }
    if (argc == 2 && k_strcmp(sub, "fakedev") == 0) {
        if (k_strcmp(argv[1], "on") == 0) {
            vfs_dev_register(&fake_hooks);
        } else if (k_strcmp(argv[1], "off") == 0) {
            vfs_dev_register(NULL);
        } else {
            return uos_maint_fail("fakedev on|off");
        }
        uos_maint_reply("fakedev %s", argv[1]);
        return 0;
    }
#endif
    return uos_maint_fail("usage: fs ls|cat|crc|write|new|del|ren|copy|mkdir|stat|free|default|who");
}

/* ---- The service ---------------------------------------------------------------- */

/* The card state comes from the kernel's mount step: vfs_ready() reads it
 * on every call, so init cannot fail on a missing card. */
static int vfs_init(void)
{
    vp_ctx_init(&ctx, NULL);
    privs = VP_PRIV_SYSTEM;
    return uos_maint_register("fs", cmd_fs, "file system: fs ls|cat|crc|write|new|del|ren|copy|"
                             "mkdir|stat|free|default|who");
}

const uos_service_t uos_svc_vfs = { "vfs", vfs_init, NULL, NULL };
