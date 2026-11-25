/* vfs rules: error mapping, dates, work names, wildcard targets, listing filter. */

#include <stdio.h>
#include <string.h>

#include "rc_test.h"
#include "vfsrules.h"

static vp_res_t res_of(const char *home, const char *spec)
{
    vp_ctx_t c;
    vp_res_t r;

    vp_ctx_init(&c, home);
    memset(&r, 0, sizeof(r));
    CHECK_EQ(vp_resolve(&c, spec, NULL, VP_R_WILD_OK | VP_R_EMPTY_OK, &r), VP_OK);
    return r;
}

TEST(strerror_words)
{
    static const char *const words[] = {
        "ok", "nocard", "notree", "badspec", "notfound", "dirnotfound", "isdir", "exists",
        "nopriv", "readonly", "protected", "toobig", "readerr", "writeerr", "full", "nomem",
        "nodev", "nofiles", "busy", "badarg",
    };
    int i;

    CHECK_EQ(VFS_E_COUNT, (int)(sizeof(words) / sizeof(words[0])));
    for (i = 0; i < VFS_E_COUNT; i++) {
        CHECK_STR(vfs_strerror(-i), words[i]);
    }
    CHECK_STR(vfs_strerror(1), "unknown");
    CHECK_STR(vfs_strerror(-VFS_E_COUNT), "unknown");
}

TEST(msg_map)
{
    CHECK_EQ(vfs_msg(VFS_OK), DCL_NORMAL);
    CHECK_EQ(vfs_msg(VFS_E_NOCARD), DCL_SYS_NODEV);
    CHECK_EQ(vfs_msg(VFS_E_NOTREE), DCL_SYS_NODEV);
    CHECK_EQ(vfs_msg(VFS_E_NODEV), DCL_SYS_NODEV);
    CHECK_EQ(vfs_msg(VFS_E_SPEC), DCL_SYS_IVSPEC);
    CHECK_EQ(vfs_msg(VFS_E_ISDIR), DCL_SYS_IVSPEC);
    CHECK_EQ(vfs_msg(VFS_E_ARG), DCL_SYS_IVSPEC);
    CHECK_EQ(vfs_msg(VFS_E_NOTFOUND), DCL_SYS_FNF);
    CHECK_EQ(vfs_msg(VFS_E_DIRNF), DCL_SYS_DNF);
    CHECK_EQ(vfs_msg(VFS_E_EXISTS), DCL_SYS_EXISTS);
    CHECK_EQ(vfs_msg(VFS_E_NOPRIV), DCL_SYS_NOPRIV);
    CHECK_EQ(vfs_msg(VFS_E_READONLY), DCL_SYS_WRITLCK);
    CHECK_EQ(vfs_msg(VFS_E_PROTECTED), DCL_SYS_PROTECT);
    CHECK_EQ(vfs_msg(VFS_E_TOOBIG), DCL_SYS_TOOBIG);
    CHECK_EQ(vfs_msg(VFS_E_READ), DCL_SYS_READERR);
    CHECK_EQ(vfs_msg(VFS_E_NOMEM), DCL_SYS_INSFMEM);
    CHECK_EQ(vfs_msg(VFS_E_WRITE), DCL_SYS_WRITERR);
    CHECK_EQ(vfs_msg(VFS_E_BUSY), DCL_SYS_WRITERR);
    CHECK_EQ(vfs_msg(VFS_E_FULL), DCL_SYS_DISKFULL);
    CHECK_EQ(vfs_msg(VFS_E_NOFILES), DCL_SYS_NOFILES);
    CHECK_EQ(vfs_msg(-99), DCL_SYS_READERR);
}

TEST(from_fs)
{
    CHECK_EQ(vfs_from_fs(FS_OK, 0), VFS_OK);
    CHECK_EQ(vfs_from_fs(42, 1), VFS_OK);
    CHECK_EQ(vfs_from_fs(FS_ERR_NOENT, 0), VFS_E_NOTFOUND);
    CHECK_EQ(vfs_from_fs(FS_ERR_EXIST, 1), VFS_E_EXISTS);
    CHECK_EQ(vfs_from_fs(FS_ERR_ISDIR, 0), VFS_E_ISDIR);
    CHECK_EQ(vfs_from_fs(FS_ERR_NAME, 1), VFS_E_SPEC);
    CHECK_EQ(vfs_from_fs(FS_ERR_NAMETOOLONG, 1), VFS_E_SPEC);
    CHECK_EQ(vfs_from_fs(FS_ERR_INVAL, 1), VFS_E_SPEC);
    CHECK_EQ(vfs_from_fs(FS_ERR_NOSPC, 1), VFS_E_FULL);
    CHECK_EQ(vfs_from_fs(FS_ERR_RO, 1), VFS_E_READONLY);
    CHECK_EQ(vfs_from_fs(FS_ERR_DENIED, 1), VFS_E_WRITE);
    CHECK_EQ(vfs_from_fs(FS_ERR_NOVOL, 0), VFS_E_NOCARD);
    CHECK_EQ(vfs_from_fs(FS_ERR_NOFS, 0), VFS_E_NOCARD);
    CHECK_EQ(vfs_from_fs(FS_ERR_NOTREADY, 0), VFS_E_NOCARD);
    CHECK_EQ(vfs_from_fs(FS_ERR_NOMEM, 0), VFS_E_NOMEM);
    CHECK_EQ(vfs_from_fs(FS_ERR_TOOBIG, 0), VFS_E_TOOBIG);
    CHECK_EQ(vfs_from_fs(FS_ERR_IO, 0), VFS_E_READ);
    CHECK_EQ(vfs_from_fs(FS_ERR_IO, 1), VFS_E_WRITE);
    CHECK_EQ(vfs_from_fs(FS_ERR_TIMEOUT, 0), VFS_E_READ);
    CHECK_EQ(vfs_from_fs(FS_ERR_CORRUPT, 1), VFS_E_WRITE);
}

TEST(from_vp)
{
    CHECK_EQ(vfs_from_vp(VP_OK), VFS_OK);
    CHECK_EQ(vfs_from_vp(VP_E_NOPRIV), VFS_E_NOPRIV);
    CHECK_EQ(vfs_from_vp(VP_E_READONLY), VFS_E_READONLY);
    CHECK_EQ(vfs_from_vp(VP_E_PROTECTED), VFS_E_PROTECTED);
    CHECK_EQ(vfs_from_vp(VP_E_NODEV), VFS_E_NODEV);
    CHECK_EQ(vfs_from_vp(VP_E_BADCHAR), VFS_E_SPEC);
    CHECK_EQ(vfs_from_vp(VP_E_ROOT), VFS_E_SPEC);
    CHECK_EQ(vfs_from_vp(VP_E_NOHOME), VFS_E_SPEC);
}

TEST(datetime)
{
    char b[VFS_DATE_LEN + 1];
    /* 2026-01-01 00:00:00, the library's time without a clock. */
    uint16_t d = (uint16_t)((46u << 9) | (1u << 5) | 1u);

    CHECK_EQ(vfs_fmt_datetime(d, 0, b, sizeof(b)), VFS_OK);
    CHECK_STR(b, "01-JAN-2026 00:00:00");
    CHECK_EQ(vfs_fmt_datetime((uint16_t)((0u << 9) | (12u << 5) | 31u),
                              (uint16_t)((23u << 11) | (59u << 5) | 29u), b, sizeof(b)), VFS_OK);
    CHECK_STR(b, "31-DEC-1980 23:59:58");
    CHECK_EQ(vfs_fmt_datetime((uint16_t)((127u << 9) | (6u << 5) | 9u),
                              (uint16_t)((7u << 11) | (5u << 5) | 3u), b, sizeof(b)), VFS_OK);
    CHECK_STR(b, "09-JUN-2107 07:05:06");
    CHECK_EQ(strlen(b), (size_t)VFS_DATE_LEN);
    /* Bad fields. */
    CHECK_EQ(vfs_fmt_datetime(0, 0, b, sizeof(b)), VFS_OK);
    CHECK_STR(b, "?\?-?\?\?-?\?\?\? ?\?:?\?:?\?");
    CHECK_EQ(vfs_fmt_datetime((uint16_t)((13u << 5) | 1u), 0, b, sizeof(b)), VFS_OK);
    CHECK_EQ(b[0], '?');
    CHECK_EQ(vfs_fmt_datetime(d, (uint16_t)(24u << 11), b, sizeof(b)), VFS_OK);
    CHECK_EQ(b[0], '?');
    CHECK_EQ(vfs_fmt_datetime(d, (uint16_t)(60u << 5), b, sizeof(b)), VFS_OK);
    CHECK_EQ(b[0], '?');
    CHECK_EQ(vfs_fmt_datetime(d, 30u, b, sizeof(b)), VFS_OK);   /* 60 seconds */
    CHECK_EQ(b[0], '?');
    /* Buffer one short. */
    strcpy(b, "x");
    CHECK_EQ(vfs_fmt_datetime(d, 0, b, VFS_DATE_LEN), VFS_E_ARG);
    CHECK_STR(b, "");
    CHECK_EQ(vfs_fmt_datetime(d, 0, NULL, 30), VFS_E_ARG);
}

TEST(work_name)
{
    char b[VP_PATH_MAX + 1];

    CHECK_EQ(vfs_work_name("/UOS/A.TXT", '~', b, sizeof(b)), VFS_OK);
    CHECK_STR(b, "/UOS/~A.TXT");
    CHECK_EQ(vfs_work_name("/UOS/HOME/ADMIN/NOTES.TXT", '#', b, sizeof(b)), VFS_OK);
    CHECK_STR(b, "/UOS/HOME/ADMIN/#NOTES.TXT");
    CHECK_EQ(vfs_work_name("/UOS/HOME/", '~', b, sizeof(b)), VFS_E_ARG);
    CHECK_STR(b, "");
    CHECK_EQ(vfs_work_name("/UOS/A", '!', b, sizeof(b)), VFS_E_ARG);
    /* Exact fit: 6 characters plus the mark plus the terminator. */
    CHECK_EQ(vfs_work_name("/UOS/A", '~', b, 8), VFS_OK);
    CHECK_STR(b, "/UOS/~A");
    CHECK_EQ(vfs_work_name("/UOS/A", '~', b, 7), VFS_E_ARG);
    CHECK_STR(b, "");
}

TEST(wild_target)
{
    char n[VP_NAME_MAX + 1], t[VP_TYPE_MAX + 1];

    CHECK_EQ(vfs_wild_target("A", "TXT", "B", "DAT", 0, n, t), VFS_OK);
    CHECK_STR(n, "B");
    CHECK_STR(t, "DAT");
    CHECK_EQ(vfs_wild_target("A", "TXT", "*", "BAK", 0, n, t), VFS_OK);
    CHECK_STR(n, "A");
    CHECK_STR(t, "BAK");
    CHECK_EQ(vfs_wild_target("A", "TXT", "NEW", "*", 0, n, t), VFS_OK);
    CHECK_STR(n, "NEW");
    CHECK_STR(t, "TXT");
    CHECK_EQ(vfs_wild_target("ABCDEFGH", "XYZ", "", "", 1, n, t), VFS_OK);
    CHECK_STR(n, "ABCDEFGH");
    CHECK_STR(t, "XYZ");
    CHECK_EQ(vfs_wild_target("A", "", "*", "*", 0, n, t), VFS_OK);
    CHECK_STR(t, "");
    CHECK_EQ(vfs_wild_target("A", "TXT", "B*", "TXT", 0, n, t), VFS_E_SPEC);
    CHECK_EQ(vfs_wild_target("A", "TXT", "B", "T%T", 0, n, t), VFS_E_SPEC);
    CHECK_EQ(vfs_wild_target("A", "TXT", "", "TXT", 0, n, t), VFS_E_SPEC);
}

TEST(parent_res)
{
    vp_res_t f = res_of("ADMIN", "[.MAIL]A.TXT"), d;

    CHECK_EQ(vfs_parent_res(&f, &d), VFS_OK);
    CHECK_STR(d.path, "/UOS/HOME/ADMIN/MAIL");
    CHECK_EQ(d.is_dir, 1);
    CHECK_STR(d.name, "");
    f = res_of("ADMIN", "SD0:[000000]UOS.CFG");
    CHECK_EQ(vfs_parent_res(&f, &d), VFS_OK);
    CHECK_STR(d.path, "/UOS");
    f = res_of("ADMIN", "[.MAIL]");
    CHECK_EQ(vfs_parent_res(&f, &d), VFS_OK);
    CHECK_STR(d.path, "/UOS/HOME/ADMIN/MAIL");
    strcpy(f.path, "X");
    f.is_dir = 0;
    CHECK_EQ(vfs_parent_res(&f, &d), VFS_E_SPEC);
}

TEST(entry_res)
{
    vp_res_t d = res_of("ADMIN", "[]"), e;

    CHECK_EQ(vfs_entry_res(&d, "notes.txt", 0, &e), VFS_OK);
    CHECK_STR(e.path, "/UOS/HOME/ADMIN/NOTES.TXT");
    CHECK_STR(e.name, "NOTES");
    CHECK_STR(e.type, "TXT");
    CHECK_EQ(e.depth, 2);
    CHECK_EQ(vfs_entry_res(&d, "README", 0, &e), VFS_OK);
    CHECK_STR(e.path, "/UOS/HOME/ADMIN/README");
    CHECK_EQ(vfs_entry_res(&d, "MAIL", 1, &e), VFS_OK);
    CHECK_STR(e.path, "/UOS/HOME/ADMIN/MAIL");
    CHECK_EQ(e.depth, 3);
    CHECK_STR(e.dir[2], "MAIL");
    CHECK_EQ(e.is_dir, 1);
    CHECK_EQ(vfs_entry_res(&d, "Long name.txt", 0, &e), VFS_E_SPEC);
    CHECK_EQ(vfs_entry_res(&d, "ABCDEFGHI.TXT", 0, &e), VFS_E_SPEC);
    d = res_of("ADMIN", "[A.B.C.D.E.F.G.H]");
    CHECK_EQ(d.depth, 8);
    CHECK_EQ(vfs_entry_res(&d, "X", 1, &e), VFS_E_SPEC);
    CHECK_EQ(vfs_entry_res(&d, "X.TXT", 0, &e), VFS_OK);
}

TEST(list_keep)
{
    vp_res_t d = res_of("GUEST", "SD0:[000000]"), e;
    uint32_t sys = VP_PRIV_SYSTEM;

    /* Work files, odd names and the pattern. */
    CHECK_EQ(vfs_list_keep(&d, "UOS.CFG", 0, 0, "*", "*", "GUEST", sys, VP_OP_LIST, &e), 1);
    CHECK_STR(e.path, "/UOS/UOS.CFG");
    CHECK_EQ(vfs_list_keep(&d, "~UOS.CFG", 0, 0, "*", "*", "GUEST", sys, VP_OP_LIST, &e), 0);
    CHECK_EQ(vfs_list_keep(&d, "#UOS.CFG", 0, 0, "*", "*", "GUEST", sys, VP_OP_LIST, &e), 0);
    CHECK_EQ(vfs_list_keep(&d, "UOS.CFG", 0, 1, "*", "*", "GUEST", sys, VP_OP_LIST, &e), 0);
    CHECK_EQ(vfs_list_keep(&d, "a long name", 0, 0, "*", "*", "GUEST", sys, VP_OP_LIST, &e), 0);
    CHECK_EQ(vfs_list_keep(&d, "UOS.CFG", 0, 0, "*", "TXT", "GUEST", sys, VP_OP_LIST, &e), 0);
    CHECK_EQ(vfs_list_keep(&d, "HOME", 1, 0, "*", "DIR", "GUEST", sys, VP_OP_LIST, &e), 1);
    CHECK_EQ(vfs_list_keep(&d, "HOME", 1, 0, "H%ME", "*", "GUEST", sys, VP_OP_LIST, &e), 1);
    CHECK_EQ(vfs_list_keep(&d, "HOME", 1, 0, "*", "TXT", "GUEST", sys, VP_OP_LIST, &e), 0);
    /* SYSUAF.DAT: SECURITY lists it, a plain user does not, nobody deletes it. */
    CHECK_EQ(vfs_list_keep(&d, "SYSUAF.DAT", 0, 0, "*", "*", "", VP_PRIV_SECURITY,
                           VP_OP_LIST, &e), 1);
    /* A refused match is told apart from no match: callers count it. */
    CHECK_EQ(vfs_list_keep(&d, "SYSUAF.DAT", 0, 0, "*", "*", "", 0, VP_OP_LIST, &e),
             VFS_KEEP_REFUSED);
    CHECK_EQ(vfs_list_keep(&d, "SYSUAF.DAT", 0, 0, "*", "*", "", sys, VP_OP_DELETE, &e),
             VFS_KEEP_REFUSED);
    CHECK_EQ(vfs_list_keep(&d, "SYSUAF.DAT", 0, 0, "X*", "*", "", 0, VP_OP_LIST, &e),
             VFS_KEEP_NO);
    CHECK_EQ(vfs_list_keep(&d, "UOS.CFG", 0, 0, "*", "*", "", sys, VP_OP_DELETE, &e), 1);
    /* Another home through READALL: listed, not deleted. */
    d = res_of("GUEST", "SD0:[HOME.ADMIN]");
    CHECK_EQ(vfs_list_keep(&d, "DUTY.TXT", 0, 0, "*", "*", "GUEST", 0, VP_OP_LIST, &e),
             VFS_KEEP_REFUSED);
    CHECK_EQ(vfs_list_keep(&d, "DUTY.TXT", 0, 0, "*", "*", "GUEST", VP_PRIV_READALL,
                           VP_OP_LIST, &e), 1);
    CHECK_EQ(vfs_list_keep(&d, "DUTY.TXT", 0, 0, "*", "*", "GUEST", VP_PRIV_READALL,
                           VP_OP_DELETE, &e), VFS_KEEP_REFUSED);
}

static vfs_entry_t ent(const char *name, const char *type)
{
    vfs_entry_t e;

    memset(&e, 0, sizeof(e));
    strcpy(e.name, name);
    strcpy(e.type, type);
    return e;
}

TEST(list_insert)
{
    vfs_entry_t a[3], n;
    size_t count = 0;
    static const char *const names[] = { "D", "B", "E", "A", "C", "B" };
    size_t i;

    for (i = 0; i < 6; i++) {
        n = ent(names[i], i == 5 ? "AAA" : "TXT");
        vfs_list_insert(a, &count, 3, &n);
    }
    CHECK_EQ(count, (size_t)3);
    CHECK_STR(a[0].name, "A");
    CHECK_STR(a[1].name, "B");
    CHECK_STR(a[1].type, "AAA");
    CHECK_STR(a[2].name, "B");
    CHECK_STR(a[2].type, "TXT");
    /* Larger than every kept entry: dropped. */
    n = ent("Z", "");
    vfs_list_insert(a, &count, 3, &n);
    CHECK_STR(a[2].name, "B");
    /* max 0 keeps nothing. */
    count = 0;
    vfs_list_insert(a, &count, 0, &n);
    CHECK_EQ(count, (size_t)0);
}

TEST(atomic_pick_follows_recovery_table)
{
    /* target, "~NAME", "#NAME": the library's recovery table, row by row. */
    CHECK_EQ(vfs_atomic_pick(1, 0, 0), 0);
    CHECK_EQ(vfs_atomic_pick(1, 1, 0), 0);
    CHECK_EQ(vfs_atomic_pick(1, 0, 1), 0);
    CHECK_EQ(vfs_atomic_pick(1, 1, 1), 0);
    CHECK_EQ(vfs_atomic_pick(0, 1, 1), '~');
    CHECK_EQ(vfs_atomic_pick(0, 0, 1), '#');
    CHECK_EQ(vfs_atomic_pick(0, 1, 0), 0);    /* a cut first write holds nothing */
    CHECK_EQ(vfs_atomic_pick(0, 0, 0), 0);
}
