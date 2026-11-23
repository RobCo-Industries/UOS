/* vfs path: parse, resolve, format, wildcards, sorting and access rules. */

#include <stdio.h>
#include <string.h>

#include "path.h"
#include "rc_test.h"

#define W VP_R_WILD_OK
#define N VP_R_NEED_NAME
#define E VP_R_EMPTY_OK

/* Context: home ADMIN, default SD0:[HOME.ADMIN]. */
static vp_ctx_t std_ctx(void)
{
    vp_ctx_t c;

    vp_ctx_init(&c, "admin");
    return c;
}

typedef struct {
    const char *spec;
    const char *deftype;
    unsigned flags;
    int rc;
    int tree;
    const char *path;
    int is_dir;
} rcase_t;

static const rcase_t rcases[] = {
    /* plain files in the default directory */
    { "NOTES.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/NOTES.TXT", 0 },
    { "notes.txt", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/NOTES.TXT", 0 },
    { "NoTeS.TxT;1", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/NOTES.TXT", 0 },
    { "A", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A", 0 },
    { "A.", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A", 0 },
    { "A;", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A", 0 },
    { "A;0", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A", 0 },
    { "A;32767", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A", 0 },
    { "A.B;12", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A.B", 0 },
    { "ABCDEFGH.XYZ", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/ABCDEFGH.XYZ", 0 },
    { "$_-09.$_-", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/$_-09.$_-", 0 },
    { "-", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/-", 0 },
    /* default type rule */
    { "README", "TXT", 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/README.TXT", 0 },
    { "README", "hlp", 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/README.HLP", 0 },
    { "README.", "TXT", 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/README", 0 },
    { "README.DOC", "TXT", 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/README.DOC", 0 },
    { "README;3", "TXT", 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/README.TXT", 0 },
    { "[.MAIL]", "TXT", 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 1 },
    /* directories */
    { "[.MAIL]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 1 },
    { "[.MAIL]X.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL/X.TXT", 0 },
    { "[.mail.old]x", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL/OLD/X", 0 },
    { "[]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 1 },
    { "[]X.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/X.TXT", 0 },
    { "[-]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME", 1 },
    { "[-]X.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/X.TXT", 0 },
    { "[-.-]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS", 1 },
    { "[-.-]UOS.CFG", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/UOS.CFG", 0 },
    { "[-.GUEST]A.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/GUEST/A.TXT", 0 },
    { "[-.-.SYS.HELP]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/SYS/HELP", 1 },
    { "[-.-.-]", NULL, 0, VP_E_ROOT, 0, NULL, 0 },
    { "[-.-.-.-.-.-.-.-]", NULL, 0, VP_E_ROOT, 0, NULL, 0 },
    { "[-.-.-.-.-.-.-.-.-]", NULL, 0, VP_E_DEPTH, 0, NULL, 0 },
    { "[HOME.GUEST]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/GUEST", 1 },
    { "[SYS]WELCOME.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/SYS/WELCOME.TXT", 0 },
    { "[000000]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS", 1 },
    { "[000000]SYSUAF.DAT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/SYSUAF.DAT", 0 },
    { "[A.B.C.D.E.F.G.H]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H", 1 },
    { "[A.B.C.D.E.F.G.H]X.Y", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H/X.Y", 0 },
    { "[A.B.C.D.E.F.G.H.I]", NULL, 0, VP_E_DEPTH, 0, NULL, 0 },
    { "[.A.B.C.D.E.F]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A/B/C/D/E/F", 1 },
    { "[.A.B.C.D.E.F.G]", NULL, 0, VP_E_DEPTH, 0, NULL, 0 },
    { "[-.A.B.C.D.E.F.G]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/A/B/C/D/E/F/G", 1 },
    { "[ABCDEFGH]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/ABCDEFGH", 1 },
    { "[ABCDEFGHI]", NULL, 0, VP_E_NAMELEN, 0, NULL, 0 },
    { "[-A]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/-A", 1 },
    /* devices */
    { "SD0:", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 1 },
    { "sd0:[home.admin]a.txt", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A.TXT", 0 },
    { "SD0:[UOS.HOME.ADMIN]NOTES.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/UOS/HOME/ADMIN/NOTES.TXT", 0 },
    { "SD0:[000000]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS", 1 },
    { "SD0:[-]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME", 1 },
    { "SD0:[.MAIL]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 1 },
    { "BOOT:", NULL, 0, VP_OK, VP_TREE_BOOT, "/ROBCO", 1 },
    { "BOOT:UOS.SYS", NULL, 0, VP_OK, VP_TREE_BOOT, "/ROBCO/UOS.SYS", 0 },
    { "BOOT:[.X]A", NULL, 0, VP_OK, VP_TREE_BOOT, "/ROBCO/X/A", 0 },
    { "BOOT:[A]B.C", NULL, 0, VP_OK, VP_TREE_BOOT, "/ROBCO/A/B.C", 0 },
    { "BOOT:[-]", NULL, 0, VP_E_ROOT, 0, NULL, 0 },
    { "DEV:DOOR1", NULL, 0, VP_OK, VP_TREE_DEV, "DOOR1", 0 },
    { "dev:door1;1", NULL, 0, VP_OK, VP_TREE_DEV, "DOOR1", 0 },
    { "DEV:", NULL, 0, VP_OK, VP_TREE_DEV, "", 1 },
    { "DEV:", NULL, N, VP_E_NONAME, 0, NULL, 0 },
    { "DEV:DOOR1", "TXT", 0, VP_OK, VP_TREE_DEV, "DOOR1", 0 },
    { "DEV:[A]DOOR1", NULL, 0, VP_E_DEVDIR, 0, NULL, 0 },
    { "DEV:[]", NULL, 0, VP_E_DEVDIR, 0, NULL, 0 },
    { "DEV:DOOR1.TXT", NULL, 0, VP_E_DEVDIR, 0, NULL, 0 },
    { "DEV:D*", NULL, W, VP_E_DEVDIR, 0, NULL, 0 },
    { "DEV:;1", NULL, 0, VP_E_NONAME, 0, NULL, 0 },
    /* logical names */
    { "SYS$SYSTEM:WELCOME.TXT", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/SYS/WELCOME.TXT", 0 },
    { "sys$system:", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/SYS", 1 },
    { "SYS$HELP:DIR", "HLP", 0, VP_OK, VP_TREE_UOS, "/UOS/SYS/HELP/DIR.HLP", 0 },
    { "SYS$HELP:[-]", NULL, 0, VP_E_ROOT, 0, NULL, 0 },
    { "SYS$HOLO:VAULT.HTP", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOLO/VAULT.HTP", 0 },
    { "SYS$LOGIN:", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 1 },
    { "SYS$LOGIN:[.MAIL]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 1 },
    { "SYS$LOGIN:[MAIL]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 1 },
    { "SYS$LOGIN:[]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 1 },
    { "SYS$LOGIN:[-]", NULL, 0, VP_E_ROOT, 0, NULL, 0 },
    { "SYS$LOGIN:[.A]A.B", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A/A.B", 0 },
    { "SYS$LOGIN:[.A]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A", 1 },
    { "SYS$LOGIN:[.A.-]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "SYS$LOGIN:[A.B.C.D.E.F]", NULL, 0, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/A/B/C/D/E/F", 1 },
    { "SYS$LOGIN:[A.B.C.D.E.F.G]", NULL, 0, VP_E_DEPTH, 0, NULL, 0 },
    { "SYS$NOPE:A", NULL, 0, VP_E_NODEV, 0, NULL, 0 },
    { "SD1:A", NULL, 0, VP_E_NODEV, 0, NULL, 0 },
    { "ABCDEFGHIJKLMNO:A", NULL, 0, VP_E_NODEV, 0, NULL, 0 },
    { "ABCDEFGHIJKLMNOP:A", NULL, 0, VP_E_DEVLEN, 0, NULL, 0 },
    { ":A", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "S-0:A", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "SD*:A", NULL, W, VP_E_WILDDIR, 0, NULL, 0 },
    { "SD%:A", NULL, W, VP_E_WILDDIR, 0, NULL, 0 },
    { "SD0:SD0:A", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    /* names and types */
    { "ABCDEFGHI", NULL, 0, VP_E_NAMELEN, 0, NULL, 0 },
    { "A.ABCD", NULL, 0, VP_E_TYPELEN, 0, NULL, 0 },
    { "ABCDEFGHI.ABCD", NULL, 0, VP_E_NAMELEN, 0, NULL, 0 },
    { "A.B.C", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "..", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "../X", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { ".TXT", NULL, 0, VP_E_NONAME, 0, NULL, 0 },
    { "[.MAIL];1", NULL, 0, VP_E_NONAME, 0, NULL, 0 },
    { "A B", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { " A", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "A/B", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "A\\B", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "/UOS/SYSUAF.DAT", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "\\ROBCO\\UOS.SYS", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "[A/B]", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "[A\\B]", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "[..]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[.]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[A.]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[A..B]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[...]", NULL, 0, VP_E_WILDDIR, 0, NULL, 0 },
    { "[A...]", NULL, 0, VP_E_WILDDIR, 0, NULL, 0 },
    { "[*]", NULL, W, VP_E_WILDDIR, 0, NULL, 0 },
    { "[.M%IL]", NULL, W, VP_E_WILDDIR, 0, NULL, 0 },
    { "[A.-]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[.-]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[-.A.-]", NULL, 0, VP_E_BADDIR, 0, NULL, 0 },
    { "[SD0:A]", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "[A]SD0:B", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "[A]B:", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    { "A<B>", NULL, 0, VP_E_BADCHAR, 0, NULL, 0 },
    /* brackets */
    { "[A", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "A]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "[A]]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "[[A]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "[A][B]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "[A]B[C]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "A.T]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "[", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    { "]", NULL, 0, VP_E_BRACKET, 0, NULL, 0 },
    /* versions */
    { "A;X", NULL, 0, VP_E_BADVER, 0, NULL, 0 },
    { "A;-1", NULL, 0, VP_E_BADVER, 0, NULL, 0 },
    { "A;*", NULL, W, VP_E_BADVER, 0, NULL, 0 },
    { "A;32768", NULL, 0, VP_E_BADVER, 0, NULL, 0 },
    { "A;99999999999999999999", NULL, 0, VP_E_BADVER, 0, NULL, 0 },
    { "A;1;1", NULL, 0, VP_E_BADVER, 0, NULL, 0 },
    { "A;1.TXT", NULL, 0, VP_E_BADVER, 0, NULL, 0 },
    /* wildcards */
    { "*.TXT", NULL, 0, VP_E_WILD, 0, NULL, 0 },
    { "A.%", NULL, 0, VP_E_WILD, 0, NULL, 0 },
    { "*.TXT", NULL, W, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 0 },
    { "[.MAIL]*.*", NULL, W, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 0 },
    { "A%C", "TXT", W, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 0 },
    { "********", NULL, W, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 0 },
    { "*********", NULL, W, VP_E_NAMELEN, 0, NULL, 0 },
    { "*.****", NULL, W, VP_E_TYPELEN, 0, NULL, 0 },
    /* empty and need-name */
    { "", NULL, 0, VP_E_EMPTY, 0, NULL, 0 },
    { "", NULL, E, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN", 1 },
    { "[.MAIL]", NULL, N, VP_E_NONAME, 0, NULL, 0 },
    { "[.MAIL]X", NULL, N, VP_OK, VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL/X", 0 },
};

TEST(resolve_table)
{
    size_t i;
    vp_ctx_t c = std_ctx();

    CHECK(sizeof(rcases) / sizeof(rcases[0]) >= 120);
    for (i = 0; i < sizeof(rcases) / sizeof(rcases[0]); i++) {
        const rcase_t *t = &rcases[i];
        vp_res_t r;
        int rc = vp_resolve(&c, t->spec, t->deftype, t->flags, &r);

        if (rc != t->rc) {
            RC_FAIL("[%zu] \"%s\": rc %d (%s), want %d (%s)", i, t->spec, rc,
                    vp_strerror(rc), t->rc, vp_strerror(t->rc));
            continue;
        }
        if (rc != VP_OK) {
            continue;
        }
        if (r.tree != t->tree || strcmp(r.path, t->path) != 0 || r.is_dir != t->is_dir) {
            RC_FAIL("[%zu] \"%s\": tree %d path \"%s\" dir %d, want %d \"%s\" %d", i, t->spec,
                    r.tree, r.path, r.is_dir, t->tree, t->path, t->is_dir);
        }
    }
}

TEST(parse_fields)
{
    vp_spec_t s;

    CHECK_EQ(vp_parse("SD0:[HOME.ADMIN]notes.txt;7", 0, &s), VP_OK);
    CHECK_STR(s.dev, "SD0");
    CHECK_EQ(s.dir_kind, VP_DIR_ABS);
    CHECK_EQ(s.ndir, 2);
    CHECK_STR(s.dir[0], "HOME");
    CHECK_STR(s.dir[1], "ADMIN");
    CHECK_STR(s.name, "NOTES");
    CHECK_STR(s.type, "TXT");
    CHECK_EQ(s.has_dot, 1);
    CHECK_EQ(s.has_ver, 1);
    CHECK_EQ(s.version, 7);
    CHECK_EQ(s.wild_name, 0);

    CHECK_EQ(vp_parse("[-.-.X]", 0, &s), VP_OK);
    CHECK_EQ(s.dir_kind, VP_DIR_REL);
    CHECK_EQ(s.dir_up, 2);
    CHECK_EQ(s.ndir, 1);
    CHECK_STR(s.dir[0], "X");
    CHECK_STR(s.name, "");
    CHECK_EQ(s.has_dot, 0);

    CHECK_EQ(vp_parse("[]", 0, &s), VP_OK);
    CHECK_EQ(s.dir_kind, VP_DIR_REL);
    CHECK_EQ(s.ndir, 0);
    CHECK_EQ(vp_parse("[000000]", 0, &s), VP_OK);
    CHECK_EQ(s.dir_kind, VP_DIR_ABS);
    CHECK_EQ(s.ndir, 0);
    CHECK_EQ(vp_parse("X", 0, &s), VP_OK);
    CHECK_EQ(s.dir_kind, VP_DIR_NONE);

    CHECK_EQ(vp_parse("A*.%", VP_P_WILD_OK, &s), VP_OK);
    CHECK_EQ(s.wild_name, 1);
    CHECK_EQ(s.wild_type, 1);
    CHECK_EQ(vp_parse("AB.%", VP_P_WILD_OK, &s), VP_OK);
    CHECK_EQ(s.wild_name, 0);
    CHECK_EQ(s.wild_type, 1);

    CHECK_EQ(vp_parse("", 0, &s), VP_E_EMPTY);
    CHECK_EQ(vp_parse(NULL, 0, &s), VP_E_EMPTY);
    CHECK_EQ(vp_parse("", VP_P_EMPTY_OK, &s), VP_OK);
    CHECK_EQ(vp_parse(NULL, VP_P_EMPTY_OK, &s), VP_OK);
}

TEST(spec_length_limit)
{
    char buf[400];
    vp_spec_t s;

    /* VP_SPEC_MAX characters is allowed through length; content then decides. */
    memset(buf, 'A', sizeof(buf));
    buf[VP_SPEC_MAX] = '\0';
    CHECK_EQ(vp_parse(buf, 0, &s), VP_E_NAMELEN);
    buf[VP_SPEC_MAX] = 'A';
    buf[VP_SPEC_MAX + 1] = '\0';
    CHECK_EQ(vp_parse(buf, 0, &s), VP_E_TOOLONG);
    buf[sizeof(buf) - 1] = '\0';
    CHECK_EQ(vp_parse(buf, 0, &s), VP_E_TOOLONG);

    /* A valid specification of exactly VP_SPEC_MAX characters: version padded with zeros. */
    memset(buf, '0', sizeof(buf));
    memcpy(buf, "A;", 2);
    buf[VP_SPEC_MAX - 1] = '1';
    buf[VP_SPEC_MAX] = '\0';
    CHECK_EQ(vp_parse(buf, 0, &s), VP_OK);
    CHECK_EQ(s.version, 1);
}

TEST(context_default)
{
    vp_ctx_t c = std_ctx();
    char out[VP_DISPLAY_MAX];

    CHECK_STR(c.home, "ADMIN");
    CHECK_EQ(vp_show_default(&c, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[HOME.ADMIN]");

    CHECK_EQ(vp_set_default(&c, "[.MAIL]"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[HOME.ADMIN.MAIL]");
    CHECK_EQ(vp_set_default(&c, "[-.-]"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[HOME]");
    CHECK_EQ(vp_set_default(&c, "[-.-]"), VP_E_ROOT);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[HOME]");
    CHECK_EQ(vp_set_default(&c, "[-]"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[000000]");
    CHECK_EQ(vp_set_default(&c, "SYS$HELP:"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[SYS.HELP]");
    CHECK_EQ(vp_set_default(&c, "SYS$LOGIN:"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[HOME.ADMIN]");
    CHECK_EQ(vp_set_default(&c, "NOTES.TXT"), VP_E_NOTDIR);
    CHECK_EQ(vp_set_default(&c, "DEV:"), VP_E_NOTDIR);
    CHECK_EQ(vp_set_default(&c, "DEV:DOOR1"), VP_E_NOTDIR);
    CHECK_EQ(vp_set_default(&c, ""), VP_E_EMPTY);
    CHECK_EQ(vp_set_default(&c, "[*]"), VP_E_WILDDIR);
    CHECK_EQ(vp_set_default(&c, "BOOT:"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "BOOT:[000000]");
    /* relative specs now resolve in BOOT:, SD0: specs go to the tree root */
    {
        vp_res_t r;

        CHECK_EQ(vp_resolve(&c, "UOS.SYS", NULL, 0, &r), VP_OK);
        CHECK_STR(r.path, "/ROBCO/UOS.SYS");
        CHECK_EQ(vp_resolve(&c, "SD0:A.TXT", NULL, 0, &r), VP_OK);
        CHECK_STR(r.path, "/UOS/A.TXT");
    }
    CHECK_EQ(vp_set_default(&c, "SD0:[A.B.C.D.E.F.G.H]"), VP_OK);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[A.B.C.D.E.F.G.H]");
    CHECK_EQ(vp_set_default(&c, "[.I]"), VP_E_DEPTH);

    /* buffer too small: defined error and empty output */
    CHECK_EQ(vp_show_default(&c, out, 21), VP_E_BUF);
    CHECK_STR(out, "");
    CHECK_EQ(vp_show_default(&c, out, 22), VP_OK);
    CHECK_STR(out, "SD0:[A.B.C.D.E.F.G.H]");
}

TEST(context_no_home)
{
    vp_ctx_t c;
    vp_res_t r;
    char out[VP_DISPLAY_MAX];

    vp_ctx_init(&c, NULL);
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[000000]");
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:A", NULL, 0, &r), VP_E_NOHOME);
    vp_ctx_init(&c, "BAD/NAME");
    CHECK_STR(c.home, "");
    vp_ctx_init(&c, "ABCDEFGHI");
    CHECK_STR(c.home, "");
    vp_ctx_init(&c, "ABCDEFGH");
    CHECK_STR(c.home, "ABCDEFGH");
    vp_show_default(&c, out, sizeof(out));
    CHECK_STR(out, "SD0:[HOME.ABCDEFGH]");
}

TEST(format_paths)
{
    char out[VP_DISPLAY_MAX];

    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/HOME/ADMIN/NOTES.TXT", 0, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[HOME.ADMIN]NOTES.TXT;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/home/admin/notes.txt", 0, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[HOME.ADMIN]NOTES.TXT;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/HOME/ADMIN/MAIL", 1, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[HOME.ADMIN]MAIL.DIR;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/HOME", 1, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[000000]HOME.DIR;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/UOS.CFG", 0, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[000000]UOS.CFG;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/README", 0, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[000000]README.;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS", 1, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "SD0:[000000]000000.DIR;1");
    CHECK_EQ(vp_format(VP_TREE_BOOT, "/ROBCO/UOS.SYS", 0, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "BOOT:[000000]UOS.SYS;1");
    CHECK_EQ(vp_format(VP_TREE_DEV, "DOOR1", 0, out, sizeof(out)), VP_OK);
    CHECK_STR(out, "DEV:DOOR1;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H/ABCDEFGH.XYZ", 0, out, sizeof(out)),
             VP_OK);
    CHECK_STR(out, "SD0:[A.B.C.D.E.F.G.H]ABCDEFGH.XYZ;1");

    /* bad paths */
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_STR(out, "");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/ROBCO/UOS.SYS", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOSX/A", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "UOS/A", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/", 1, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS//A", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/../X", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A.B/C", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A.B", 1, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/ABCDEFGHI", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A.ABCD", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A B", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H/I/X", 0, out, sizeof(out)),
             VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_UOS, NULL, 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(7, "/UOS/A", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_DEV, "", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_DEV, "DOOR.1", 0, out, sizeof(out)), VP_E_BADPATH);
    CHECK_EQ(vp_format(VP_TREE_DEV, "ABCDEFGHI", 0, out, sizeof(out)), VP_E_BADPATH);

    /* buffer limits: "SD0:[000000]UOS.CFG;1" is 21 characters */
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/UOS.CFG", 0, out, 21), VP_E_BUF);
    CHECK_STR(out, "");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/UOS.CFG", 0, out, 22), VP_OK);
    CHECK_EQ(vp_format(VP_TREE_DEV, "DOOR1", 0, out, 11), VP_E_BUF);
    CHECK_EQ(vp_format(VP_TREE_DEV, "DOOR1", 0, out, 12), VP_OK);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS", 1, out, 24), VP_E_BUF);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS", 1, out, 25), VP_OK);
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A", 0, out, 0), VP_E_BUF);
}

TEST(round_trips)
{
    static const char *const specs[] = {
        "NOTES.TXT", "README", "README.", "[.MAIL]X.TXT", "[-]X.TXT", "[-.-]UOS.CFG",
        "SD0:[A.B.C.D.E.F.G.H]ABCDEFGH.XYZ", "SYS$SYSTEM:WELCOME.TXT", "SYS$HELP:DIR.HLP",
        "SYS$LOGIN:[.MAIL]M1.TXT", "BOOT:UOS.SYS", "BOOT:[A.B]C", "DEV:DOOR1",
        "[000000]SYSUAF.DAT", "$_-.$_-",
    };
    vp_ctx_t c = std_ctx();
    size_t i;

    for (i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
        vp_res_t r1;
        vp_res_t r2;
        char disp[VP_DISPLAY_MAX];

        CHECK_EQ(vp_resolve(&c, specs[i], "TXT", 0, &r1), VP_OK);
        CHECK_EQ(vp_format(r1.tree, r1.path, 0, disp, sizeof(disp)), VP_OK);
        CHECK_EQ(vp_resolve(&c, disp, "TXT", 0, &r2), VP_OK);
        if (strcmp(r1.path, r2.path) != 0 || r1.tree != r2.tree) {
            RC_FAIL("%s -> %s -> %s -> %s", specs[i], r1.path, disp, r2.path);
        }
    }
    /* "README" with a default type gets it; formatted it says so. */
    {
        vp_res_t r;
        char disp[VP_DISPLAY_MAX];

        vp_resolve(&c, "README", "TXT", 0, &r);
        vp_format(r.tree, r.path, 0, disp, sizeof(disp));
        CHECK_STR(disp, "SD0:[HOME.ADMIN]README.TXT;1");
        vp_resolve(&c, "README.", "TXT", 0, &r);
        vp_format(r.tree, r.path, 0, disp, sizeof(disp));
        CHECK_STR(disp, "SD0:[HOME.ADMIN]README.;1");
    }
    /* directories: resolve, format as NAME.DIR;1 */
    {
        vp_res_t r;
        char disp[VP_DISPLAY_MAX];

        vp_resolve(&c, "SYS$LOGIN:[.MAIL]", NULL, 0, &r);
        CHECK_EQ(vp_format(r.tree, r.path, 1, disp, sizeof(disp)), VP_OK);
        CHECK_STR(disp, "SD0:[HOME.ADMIN]MAIL.DIR;1");
    }
}

TEST(longest_path_fits)
{
    vp_ctx_t c = std_ctx();
    vp_res_t r;
    char disp[VP_DISPLAY_MAX];

    CHECK_EQ(vp_resolve(&c, "BOOT:[ABCDEFGH.ABCDEFGH.ABCDEFGH.ABCDEFGH.ABCDEFGH.ABCDEFGH."
                            "ABCDEFGH.ABCDEFGH]ABCDEFGH.ABC", NULL, 0, &r), VP_OK);
    CHECK_EQ(strlen(r.path), 91);
    CHECK(strlen(r.path) < VP_PATH_MAX);
    CHECK_EQ(vp_format(r.tree, r.path, 0, disp, sizeof(disp)), VP_OK);
    CHECK(strlen(disp) < VP_DISPLAY_MAX);
}

TEST(escape_attempts)
{
    vp_ctx_t c = std_ctx();
    vp_res_t r;
    char big[2048];

    CHECK_EQ(vp_resolve(&c, "[-.-.-]", NULL, 0, &r), VP_E_ROOT);
    CHECK_EQ(vp_resolve(&c, "[-.-.-]X", NULL, 0, &r), VP_E_ROOT);
    CHECK_EQ(vp_resolve(&c, "SD0:[000000]", NULL, 0, &r), VP_OK);
    CHECK_STR(r.path, "/UOS");
    CHECK_EQ(vp_resolve(&c, "[...]", NULL, 0, &r), VP_E_WILDDIR);
    CHECK_EQ(vp_resolve(&c, "[...]X", NULL, W, &r), VP_E_WILDDIR);
    CHECK_EQ(vp_resolve(&c, "..", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "[..]", NULL, 0, &r), VP_E_BADDIR);
    CHECK_EQ(vp_resolve(&c, "../../ROBCO/UOS.SYS", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "..\\ROBCO", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "[.A/..]", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "[SYS$SYSTEM:]", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "[.BOOT:]X", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:[-.-]", NULL, 0, &r), VP_E_ROOT);
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:[-.GUEST]", NULL, 0, &r), VP_E_ROOT);
    CHECK_EQ(vp_resolve(&c, "A\001B", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "A\377B", NULL, 0, &r), VP_E_BADCHAR);
    CHECK_EQ(vp_resolve(&c, "A\nB", NULL, 0, &r), VP_E_BADCHAR);
    memset(big, '[', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK_EQ(vp_resolve(&c, big, NULL, 0, &r), VP_E_TOOLONG);
    memset(big, '-', sizeof(big));
    big[0] = '[';
    big[sizeof(big) - 1] = '\0';
    CHECK_EQ(vp_resolve(&c, big, NULL, 0, &r), VP_E_TOOLONG);
    /* "[-.-....-]" with 200 levels, under the length limit: depth error, no wrap */
    {
        size_t i;
        size_t p = 0;

        big[p++] = '[';
        for (i = 0; i < 100; i++) {
            big[p++] = '-';
            big[p++] = '.';
        }
        big[p - 1] = ']';
        big[p] = '\0';
        CHECK_EQ(vp_resolve(&c, big, NULL, 0, &r), VP_E_DEPTH);
        /* 256 "-": depth counter is 8 bits; must not wrap to OK */
    }
}

TEST(wildcard_matching)
{
    static const struct {
        const char *pat;
        const char *s;
        int want;
    } m[] = {
        { "*", "", 1 },        { "*", "ABC", 1 },      { "", "", 1 },
        { "", "A", 0 },        { "A", "", 0 },         { "A", "a", 1 },
        { "a", "A", 1 },       { "%", "A", 1 },        { "%", "", 0 },
        { "%", "AB", 0 },      { "%%%", "ABC", 1 },    { "%%%", "AB", 0 },
        { "A*", "A", 1 },      { "A*", "ABCDEFGH", 1 }, { "A*", "BA", 0 },
        { "*A", "BA", 1 },     { "*A", "AB", 0 },      { "*A*", "XAX", 1 },
        { "*A*", "XXX", 0 },   { "A*B", "AB", 1 },     { "A*B", "AXXB", 1 },
        { "A*B", "AXXBC", 0 }, { "A%C", "ABC", 1 },    { "A%C", "AC", 0 },
        { "**", "X", 1 },      { "*%", "", 0 },        { "*%", "Z", 1 },
        { "%*", "Z", 1 },      { "*AB*AB", "XABYAB", 1 }, { "*AB*AB", "XABYA", 0 },
        { "W%%", "W04", 1 },   { "W%%", "W1", 0 },     { "NOTES", "NOTES", 1 },
        { "NOTES", "NOTE", 0 }, { "$_-", "$_-", 1 },   { "T*T", "TT", 1 },
    };
    size_t i;

    for (i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        if (vp_match_part(m[i].pat, m[i].s) != m[i].want) {
            RC_FAIL("match \"%s\" vs \"%s\" want %d", m[i].pat, m[i].s, m[i].want);
        }
    }
    CHECK_EQ(vp_match("*", "TXT", "NOTES", "TXT"), 1);
    CHECK_EQ(vp_match("*", "TXT", "NOTES", "DAT"), 0);
    CHECK_EQ(vp_match("*", "", "README", ""), 1);
    CHECK_EQ(vp_match("*", "", "README", "TXT"), 0);
    CHECK_EQ(vp_match("*", "*", "MAIL", "DIR"), 1);
    CHECK_EQ(vp_match("N*", "*", "MAIL", "DIR"), 0);
}

TEST(list_pattern)
{
    vp_spec_t s;
    const char *pn;
    const char *pt;

    vp_parse("", VP_P_EMPTY_OK, &s);
    vp_list_pattern(&s, &pn, &pt);
    CHECK_STR(pn, "*");
    CHECK_STR(pt, "*");
    vp_parse("[.MAIL]", 0, &s);
    vp_list_pattern(&s, &pn, &pt);
    CHECK_STR(pn, "*");
    CHECK_STR(pt, "*");
    vp_parse("A", 0, &s);
    vp_list_pattern(&s, &pn, &pt);
    CHECK_STR(pn, "A");
    CHECK_STR(pt, "*");
    vp_parse("A.", 0, &s);
    vp_list_pattern(&s, &pn, &pt);
    CHECK_STR(pn, "A");
    CHECK_STR(pt, "");
    vp_parse("*.TXT", VP_P_WILD_OK, &s);
    vp_list_pattern(&s, &pn, &pt);
    CHECK_STR(pn, "*");
    CHECK_STR(pt, "TXT");
}

TEST(entries_and_sort)
{
    vp_entry_t e[6];
    vp_entry_t x;

    CHECK_EQ(vp_entry_from_fat("notes.txt", 0, &e[0]), VP_OK);
    CHECK_STR(e[0].name, "NOTES");
    CHECK_STR(e[0].type, "TXT");
    CHECK_EQ(vp_entry_from_fat("MAIL", 1, &e[1]), VP_OK);
    CHECK_STR(e[1].type, "DIR");
    CHECK_EQ(e[1].is_dir, 1);
    CHECK_EQ(vp_entry_from_fat("README", 0, &e[2]), VP_OK);
    CHECK_STR(e[2].type, "");
    CHECK_EQ(vp_entry_from_fat("MAIL.TXT", 0, &e[3]), VP_OK);
    CHECK_EQ(vp_entry_from_fat("A.B", 0, &e[4]), VP_OK);
    CHECK_EQ(vp_entry_from_fat("MAIL.ABC", 0, &e[5]), VP_OK);
    vp_sort_entries(e, 6);
    CHECK_STR(e[0].name, "A");
    CHECK_STR(e[1].name, "MAIL");
    CHECK_STR(e[1].type, "ABC");
    CHECK_STR(e[2].type, "DIR");
    CHECK_STR(e[3].type, "TXT");
    CHECK_STR(e[4].name, "NOTES");
    CHECK_STR(e[5].name, "README");
    vp_sort_entries(e, 0);
    vp_sort_entries(e, 1);
    CHECK(vp_entry_cmp(&e[0], &e[1]) < 0);
    CHECK(vp_entry_cmp(&e[1], &e[0]) > 0);
    CHECK_EQ(vp_entry_cmp(&e[2], &e[2]), 0);

    CHECK_EQ(vp_entry_from_fat("ABCDEFGHI", 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat("A.ABCD", 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat("A.B", 1, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat("", 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat(".X", 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat("A.B.C", 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat("A B", 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat(NULL, 0, &x), VP_E_BADPATH);
    CHECK_EQ(vp_entry_from_fat("ABCDEFGH.XYZ", 0, &x), VP_OK);
}

/* Access table: every area x every privilege profile x every operation. */
enum { P_NONE, P_OPER, P_SEC, P_RALL, P_SYS, NPROF };
static const uint32_t prof_bits[NPROF] = {
    0, VP_PRIV_OPER, VP_PRIV_SECURITY, VP_PRIV_READALL, VP_PRIV_SYSTEM,
};

typedef struct {
    const char *spec;
    /* per profile, per op (read, write, delete, list): expected code */
    int want[NPROF][4];
} acase_t;

#define OK VP_OK
#define NP VP_E_NOPRIV
#define RO VP_E_READONLY
#define PR VP_E_PROTECTED

static const acase_t acases[] = {
    /* SYS$SYSTEM file */
    { "SYS$SYSTEM:WELCOME.TXT",
      { { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* SYS$HELP file */
    { "SYS$HELP:DIR.HLP",
      { { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* SYS$HOLO file */
    { "SYS$HOLO:T.HTP",
      { { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* own home file */
    { "SYS$LOGIN:A.TXT",
      { { OK, OK, OK, OK }, { OK, OK, OK, OK }, { OK, OK, OK, OK }, { OK, OK, OK, OK }, { OK, OK, OK, OK } } },
    /* own home subdirectory */
    { "SYS$LOGIN:[.MAIL]",
      { { OK, OK, OK, OK }, { OK, OK, OK, OK }, { OK, OK, OK, OK }, { OK, OK, OK, OK }, { OK, OK, OK, OK } } },
    /* own home folder itself */
    { "SYS$LOGIN:",
      { { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* another user's file */
    { "SD0:[HOME.GUEST]A.TXT",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* another user's folder */
    { "SD0:[HOME.GUEST]",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* the HOME folder */
    { "SD0:[HOME]",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* a file directly in HOME */
    { "SD0:[HOME]X.TXT",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { OK, NP, NP, OK }, { OK, OK, OK, OK } } },
    /* SYSUAF.DAT */
    { "SD0:[000000]SYSUAF.DAT",
      { { NP, PR, PR, NP }, { NP, PR, PR, NP }, { OK, PR, PR, OK }, { NP, PR, PR, NP }, { OK, PR, PR, OK } } },
    /* other root file */
    { "SD0:[000000]UOS.CFG",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { OK, OK, OK, OK } } },
    /* the root */
    { "SD0:[000000]",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { OK, OK, OK, OK } } },
    /* word lists */
    { "SD0:[WORDS]W04.TXT",
      { { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { NP, NP, NP, NP }, { OK, OK, OK, OK } } },
    /* BOOT: */
    { "BOOT:UOS.SYS",
      { { NP, RO, RO, NP }, { NP, RO, RO, NP }, { NP, RO, RO, NP }, { NP, RO, RO, NP }, { OK, RO, RO, OK } } },
    { "BOOT:",
      { { NP, RO, RO, NP }, { NP, RO, RO, NP }, { NP, RO, RO, NP }, { NP, RO, RO, NP }, { OK, RO, RO, OK } } },
    /* DEV: */
    { "DEV:DOOR1",
      { { OK, OK, NP, OK }, { OK, OK, NP, OK }, { OK, OK, NP, OK }, { OK, OK, NP, OK }, { OK, OK, NP, OK } } },
};

TEST(access_table)
{
    vp_ctx_t c = std_ctx();
    size_t i;
    int p;
    int op;
    int cells = 0;

    for (i = 0; i < sizeof(acases) / sizeof(acases[0]); i++) {
        vp_res_t r;

        CHECK_EQ(vp_resolve(&c, acases[i].spec, NULL, 0, &r), VP_OK);
        for (p = 0; p < NPROF; p++) {
            for (op = 0; op < 4; op++) {
                int got = vp_access(&r, "ADMIN", prof_bits[p], op);

                cells++;
                if (got != acases[i].want[p][op]) {
                    RC_FAIL("%s prof %d op %d: %d (%s), want %d", acases[i].spec, p, op, got,
                            vp_strerror(got), acases[i].want[p][op]);
                }
            }
        }
    }
    CHECK_EQ(cells, 17 * 5 * 4);
}

/* A file directly in HOME has depth 1: vp_access() must not look at
 * dir[1], a slot past the depth. vp_resolve() zeroes it, so give it a
 * stale name the way a reused or hand-built result would hold one. */
TEST(access_file_in_home_ignores_slot_past_depth)
{
    vp_ctx_t c = std_ctx();
    vp_res_t r;

    CHECK_EQ(vp_resolve(&c, "SD0:[HOME]NOTYPE", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 1);
    CHECK_EQ(r.is_dir, 0);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_READ), VP_E_NOPRIV);
    strncpy(r.dir[1], "ADMIN", sizeof(r.dir[1]));
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_READ), VP_E_NOPRIV);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_WRITE), VP_E_NOPRIV);
    CHECK_EQ(vp_access(&r, "ADMIN", VP_PRIV_READALL, VP_OP_READ), VP_OK);
    /* No NUL in the stale slot either. */
    memset(r.dir[1], 'A', sizeof(r.dir[1]));
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_READ), VP_E_NOPRIV);
    CHECK_EQ(vp_resolve(&c, "SD0:[HOME]X.TXT", NULL, 0, &r), VP_OK);
    strncpy(r.dir[1], "ADMIN", sizeof(r.dir[1]));
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_READ), VP_E_NOPRIV);
}

TEST(access_edges)
{
    vp_ctx_t c = std_ctx();
    vp_res_t r;

    /* a user with no home owns nothing */
    vp_resolve(&c, "SD0:[HOME.ADMIN]A.TXT", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "", 0, VP_OP_READ), VP_E_NOPRIV);
    CHECK_EQ(vp_access(&r, NULL, 0, VP_OP_READ), VP_E_NOPRIV);
    CHECK_EQ(vp_access(&r, "GUEST", 0, VP_OP_WRITE), VP_E_NOPRIV);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, 4), VP_E_NOPRIV);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, -1), VP_E_NOPRIV);
    /* SYSUAF.DAT in a subfolder is just a file */
    vp_resolve(&c, "SYS$LOGIN:SYSUAF.DAT", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_WRITE), VP_OK);
    /* SYSUAF with another type is a root file */
    vp_resolve(&c, "SD0:[000000]SYSUAF.TXT", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "ADMIN", VP_PRIV_SECURITY, VP_OP_READ), VP_E_NOPRIV);
    /* a root file named SYS is not the SYS area */
    vp_resolve(&c, "SD0:[000000]SYS", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_READ), VP_E_NOPRIV);
    vp_resolve(&c, "SD0:[000000]SYS.", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_READ), VP_E_NOPRIV);
    /* the SYS folder itself */
    vp_resolve(&c, "SD0:[SYS]", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_LIST), VP_OK);
    CHECK_EQ(vp_access(&r, "ADMIN", 0, VP_OP_DELETE), VP_E_NOPRIV);
    /* all privileges together on SYSUAF.DAT still cannot write */
    vp_resolve(&c, "SD0:[000000]SYSUAF.DAT", NULL, 0, &r);
    CHECK_EQ(vp_access(&r, "ADMIN", 0xffffffffu, VP_OP_WRITE), VP_E_PROTECTED);
}

TEST(error_texts)
{
    int i;

    CHECK_STR(vp_strerror(VP_OK), "success");
    CHECK_STR(vp_strerror(VP_E_EMPTY), "file specification required");
    CHECK_STR(vp_strerror(VP_E_TOOLONG), "file specification too long");
    CHECK_STR(vp_strerror(VP_E_BADCHAR), "invalid character in file specification");
    CHECK_STR(vp_strerror(VP_E_NAMELEN), "file or directory name longer than 8 characters");
    CHECK_STR(vp_strerror(VP_E_TYPELEN), "file type longer than 3 characters");
    CHECK_STR(vp_strerror(VP_E_DEPTH), "too many directory levels");
    CHECK_STR(vp_strerror(VP_E_BRACKET), "unbalanced directory brackets");
    CHECK_STR(vp_strerror(VP_E_BADVER), "invalid version number");
    CHECK_STR(vp_strerror(VP_E_WILDDIR), "wildcard not allowed in device or directory");
    CHECK_STR(vp_strerror(VP_E_WILD), "wildcard not allowed here");
    CHECK_STR(vp_strerror(VP_E_BADDIR), "invalid directory syntax");
    CHECK_STR(vp_strerror(VP_E_DEVLEN), "device name too long");
    CHECK_STR(vp_strerror(VP_E_NODEV), "no such device");
    CHECK_STR(vp_strerror(VP_E_ROOT), "directory above the device root");
    CHECK_STR(vp_strerror(VP_E_NONAME), "file name required");
    CHECK_STR(vp_strerror(VP_E_NOTDIR), "directory required");
    CHECK_STR(vp_strerror(VP_E_DEVDIR), "DEV: takes only a device name");
    CHECK_STR(vp_strerror(VP_E_NOHOME), "no login directory");
    CHECK_STR(vp_strerror(VP_E_BUF), "result too long for the buffer");
    CHECK_STR(vp_strerror(VP_E_BADPATH), "path outside the file system");
    CHECK_STR(vp_strerror(VP_E_NOPRIV), "insufficient privilege");
    CHECK_STR(vp_strerror(VP_E_READONLY), "device is read-only");
    CHECK_STR(vp_strerror(VP_E_PROTECTED), "file changes only through AUTHORIZE or SET PASSWORD");
    CHECK_STR(vp_strerror(-1), "unknown error");
    CHECK_STR(vp_strerror(VP_E_COUNT), "unknown error");
    for (i = 0; i < VP_E_COUNT; i++) {
        CHECK(vp_strerror(i) != NULL && vp_strerror(i)[0] != '\0');
    }
}

/* Depth limit: files and directories, at VP_DEPTH_MAX and one past it. */
TEST(depth_limit)
{
    vp_ctx_t c = std_ctx();
    vp_ctx_t deep;
    vp_ctx_t before;
    vp_spec_t s;
    vp_res_t r;
    vp_res_t r2;
    char disp[VP_DISPLAY_MAX];

    CHECK_EQ(VP_DEPTH_MAX, 8);

    /* parse */
    CHECK_EQ(vp_parse("[A.B.C.D.E.F.G.H]X.TXT", 0, &s), VP_OK);
    CHECK_EQ(s.ndir, 8);
    CHECK_EQ(vp_parse("[A.B.C.D.E.F.G.H.I]X.TXT", 0, &s), VP_E_DEPTH);
    CHECK_EQ(vp_parse("[A.B.C.D.E.F.G.H]", 0, &s), VP_OK);
    CHECK_EQ(vp_parse("[A.B.C.D.E.F.G.H.I]", 0, &s), VP_E_DEPTH);
    CHECK_EQ(vp_parse("[-.-.-.-.-.-.-.-]", 0, &s), VP_OK);
    CHECK_EQ(vp_parse("[-.-.-.-.-.-.-.-.-]", 0, &s), VP_E_DEPTH);

    /* resolve from the root */
    CHECK_EQ(vp_resolve(&c, "SD0:[A.B.C.D.E.F.G.H]X.TXT", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&c, "SD0:[A.B.C.D.E.F.G.H]", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK(r.is_dir);
    CHECK_EQ(vp_resolve(&c, "SD0:[A.B.C.D.E.F.G.H.I]X.TXT", NULL, 0, &r), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&c, "SD0:[A.B.C.D.E.F.G.H.I]", NULL, 0, &r), VP_E_DEPTH);

    /* set default at the limit, then relative forms from there */
    deep = c;
    CHECK_EQ(vp_set_default(&deep, "SD0:[A.B.C.D.E.F.G.H]"), VP_OK);
    CHECK_EQ(deep.depth, 8);
    before = deep;
    CHECK_EQ(vp_set_default(&deep, "[.I]"), VP_E_DEPTH);
    CHECK(memcmp(&before, &deep, sizeof(deep)) == 0);
    CHECK_EQ(vp_set_default(&deep, "SD0:[A.B.C.D.E.F.G.H.I]"), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&deep, "X.TXT", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&deep, "[]", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&deep, "[.I]X.TXT", NULL, 0, &r), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&deep, "[.I]", NULL, 0, &r), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&deep, "[-.I]", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&deep, "[-.I.J]", NULL, 0, &r), VP_E_DEPTH);
    /* from depth 2, a relative spec of 6 fits and 7 does not */
    CHECK_EQ(vp_resolve(&c, "[.A.B.C.D.E.F]", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&c, "[.A.B.C.D.E.F.G]", NULL, 0, &r), VP_E_DEPTH);

    /* logical names count their root folders */
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:[A.B.C.D.E.F]X.TXT", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:[A.B.C.D.E.F]", NULL, 0, &r), VP_OK);
    CHECK_EQ(r.depth, 8);
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:[A.B.C.D.E.F.G]X.TXT", NULL, 0, &r), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&c, "SYS$LOGIN:[A.B.C.D.E.F.G]", NULL, 0, &r), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&c, "SYS$HELP:[A.B.C.D.E.F]", NULL, 0, &r), VP_OK);
    CHECK_EQ(vp_resolve(&c, "SYS$HELP:[A.B.C.D.E.F.G]", NULL, 0, &r), VP_E_DEPTH);
    CHECK_EQ(vp_resolve(&c, "SYS$SYSTEM:[A.B.C.D.E.F.G]", NULL, 0, &r), VP_OK);
    CHECK_EQ(vp_resolve(&c, "SYS$SYSTEM:[A.B.C.D.E.F.G.H]", NULL, 0, &r), VP_E_DEPTH);

    /* format: a file counts its folders, a directory counts itself */
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H/X.TXT", 0, disp, sizeof(disp)), VP_OK);
    CHECK_STR(disp, "SD0:[A.B.C.D.E.F.G.H]X.TXT;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H/I/X.TXT", 0, disp, sizeof(disp)),
             VP_E_BADPATH);
    CHECK_STR(disp, "");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H", 1, disp, sizeof(disp)), VP_OK);
    CHECK_STR(disp, "SD0:[A.B.C.D.E.F.G]H.DIR;1");
    CHECK_EQ(vp_format(VP_TREE_UOS, "/UOS/A/B/C/D/E/F/G/H/I", 1, disp, sizeof(disp)),
             VP_E_BADPATH);
    CHECK_STR(disp, "");
    CHECK_EQ(vp_format(VP_TREE_BOOT, "/ROBCO/A/B/C/D/E/F/G/H/I", 1, disp, sizeof(disp)),
             VP_E_BADPATH);

    /* round trip at the limit: file */
    CHECK_EQ(vp_resolve(&c, "SD0:[A.B.C.D.E.F.G.H]X.TXT", NULL, 0, &r), VP_OK);
    CHECK_EQ(vp_format(r.tree, r.path, 0, disp, sizeof(disp)), VP_OK);
    CHECK_EQ(vp_resolve(&c, disp, NULL, 0, &r2), VP_OK);
    CHECK_STR(r2.path, r.path);
    /* round trip at the limit: directory, shown as H.DIR in its parent */
    CHECK_EQ(vp_resolve(&c, "SD0:[A.B.C.D.E.F.G.H]", NULL, 0, &r), VP_OK);
    CHECK_EQ(vp_format(r.tree, r.path, 1, disp, sizeof(disp)), VP_OK);
    CHECK_EQ(vp_resolve(&c, disp, NULL, 0, &r2), VP_OK);
    CHECK_STR(r2.path, "/UOS/A/B/C/D/E/F/G/H.DIR");
    CHECK_EQ(r2.depth, 7);
    CHECK_EQ(vp_set_default(&deep, "SD0:[A.B.C.D.E.F.G.H]"), VP_OK);
    CHECK_EQ(vp_show_default(&deep, disp, sizeof(disp)), VP_OK);
    CHECK_STR(disp, "SD0:[A.B.C.D.E.F.G.H]");
    CHECK_EQ(vp_resolve(&c, disp, NULL, 0, &r2), VP_OK);
    CHECK_STR(r2.path, r.path);
}
