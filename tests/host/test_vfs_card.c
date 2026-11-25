/*
 * vfs.c over the library's FatFs glue on a host file: every entry point
 * after an atomic write cut between its renames, on a writable card and
 * on a read-only one. The image is build/host/vfs_card.img, made here.
 */
/* First: klibc.h declares memcpy() before the host headers turn it into a macro. */
#include "vfs.c"

#include <stdio.h>
#include <string.h>

#include <robco/blk.h>

#include "blk_file.h"
#include "rc_test.h"

#define IMG  "build/host/vfs_card.img"
#define HOME "/UOS/HOME/ADMIN"

uos_state_t uos;

uint32_t irq_save(void)
{
    return 0;
}

void irq_restore(uint32_t state)
{
    (void)state;
}

static blk_dev_t dev;
static vp_ctx_t ctx;
static unsigned char heap[1u << 20];

static void put(const char *path, const char *text)
{
    fs_file_t f;

    CHECK_EQ(fs_open(&f, path, FS_WRITE | FS_CREATE | FS_TRUNC), FS_OK);
    CHECK_EQ(fs_write(&f, text, strlen(text)), (int)strlen(text));
    CHECK_EQ(fs_close(&f), FS_OK);
}

static int there(const char *name)
{
    char path[VP_PATH_MAX];

    snprintf(path, sizeof(path), HOME "/%s", name);
    return fs_stat(path, &st_b) == FS_OK;
}

/* A fresh card. `states` names files to create in HOME, "NAME=text". */
static void card(const char *const *states, int read_only)
{
    static int heap_ready;
    fs_mkfs_opts_t o = { FS_MKFS_FAT, 0 };
    FILE *f;
    char path[VP_PATH_MAX];
    int i;

    if (!heap_ready) {
        k_heap_init(heap, sizeof(heap));
        heap_ready = 1;
    }
    fs_unmount("sd");
    blk_file_close(&dev);
    f = fopen(IMG, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    for (i = 0; i < 2048; i++) {
        static const unsigned char zero[1024];

        fwrite(zero, 1, sizeof(zero), f);
    }
    fclose(f);
    CHECK_EQ(blk_file_open(&dev, IMG, 0), 0);
    CHECK_EQ(fs_mkfs(&dev, &o), FS_OK);
    CHECK_EQ(fs_mount("sd", &dev), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS"), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS/HOME"), FS_OK);
    CHECK_EQ(fs_mkdir(HOME), FS_OK);
    for (i = 0; states[i] != NULL; i++) {
        const char *eq = strchr(states[i], '=');

        snprintf(path, sizeof(path), HOME "/%.*s", (int)(eq - states[i]), states[i]);
        put(path, eq + 1);
    }
    if (read_only) {
        CHECK_EQ(fs_unmount("sd"), FS_OK);
        dev.flags |= BLK_FLAG_READONLY;
        CHECK_EQ(fs_mount("sd", &dev), FS_OK);
    }
    uos.card = UOS_CARD_OK;
    vp_ctx_init(&ctx, "ADMIN");
}

/* Interrupted writes: S1 only "#", S2 only "~", S3 both, T1 only "#". */
static const char *const cut[] = {
    "#S1.TXT=old one\n", "~S2.TXT=cut short", "~S3.TXT=new three\n", "#S3.TXT=old three\n",
    "#T1.TXT=target old\n", "A.TXT=a\n", NULL,
};

static int read_text(const char *spec, char *out, size_t size)
{
    char *buf = NULL;
    size_t len = 0;
    int rc = vfs_read_all(&ctx, 0, spec, NULL, 4096, &buf, &len);

    out[0] = '\0';
    if (rc == VFS_OK) {
        snprintf(out, size, "%.*s", (int)len, buf);
        vfs_free(buf);
    }
    return rc;
}

static void check_listing(void)
{
    vfs_entry_t e[16];
    size_t count = 0, total = 0, i, n = 0;
    char names[16 * 10 + 1] = "";

    CHECK_EQ(vfs_list(&ctx, 0, "*.*", e, 16, &count, &total), VFS_OK);
    for (i = 0; i < count && i < 16; i++) {
        size_t k = strlen(e[i].name);

        memcpy(names + n, e[i].name, k < 9 ? k : 9);
        n += k < 9 ? k : 9;
        names[n++] = ' ';
        names[n] = '\0';
    }
    /* Real names only: the cut first write S2 is gone, no work file shows. */
    CHECK_STR(names, "A S1 S3 T1 ");
    CHECK_EQ(total, 4);
}

static void check_reads(void)
{
    char text[64];
    vfs_entry_t st;
    vfs_iter_t it;

    CHECK_EQ(read_text("S1.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "old one\n");
    CHECK_EQ(read_text("S3.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "new three\n");
    CHECK_EQ(read_text("S2.TXT", text, sizeof(text)), VFS_E_NOTFOUND);
    CHECK_EQ(vfs_exists(&ctx, 0, "S1.TXT"), 1);
    CHECK_EQ(vfs_exists(&ctx, 0, "T1.TXT"), 1);
    CHECK_EQ(vfs_exists(&ctx, 0, "S2.TXT"), 0);
    CHECK_EQ(vfs_stat(&ctx, 0, "S3.TXT", &st), VFS_OK);
    CHECK_STR(st.name, "S3");
    CHECK_EQ(st.size, 10);
    check_listing();
    CHECK_EQ(vfs_iter_begin(&it, &ctx, 0, "S*.TXT", VP_OP_READ), VFS_OK);
    CHECK_EQ(it.count, 2);
    CHECK_STR(vfs_iter_next(&it), "SD0:[HOME.ADMIN]S1.TXT;1");
    CHECK_STR(vfs_iter_next(&it), "SD0:[HOME.ADMIN]S3.TXT;1");
    vfs_iter_end(&it);
    CHECK_EQ(vfs_iter_begin(&it, &ctx, 0, "T1.TXT", VP_OP_READ), VFS_OK);
    vfs_iter_end(&it);
}

TEST(writable_card_recovers_every_entry_point)
{
    card(cut, 0);
    check_reads();
    /* The reads recovered: only real names are left. */
    CHECK(there("S1.TXT") && !there("#S1.TXT"));
    CHECK(there("S3.TXT") && !there("~S3.TXT") && !there("#S3.TXT"));
    CHECK(!there("~S2.TXT"));
}

TEST(writable_copy_recovers_source_and_target)
{
    char text[64];

    card(cut, 0);
    /* The source sits only under "#S1.TXT": the copy finds it. */
    CHECK_EQ(vfs_copy(&ctx, 0, "S1.TXT", "C1.TXT", 0), VFS_OK);
    CHECK_EQ(read_text("C1.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "old one\n");
    CHECK_EQ(vfs_copy(&ctx, 0, "S3.TXT", "C3.TXT", 0), VFS_OK);
    CHECK_EQ(read_text("C3.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "new three\n");
    CHECK_EQ(vfs_copy(&ctx, 0, "S2.TXT", "C2.TXT", 0), VFS_E_NOTFOUND);
    /* The target recovers before the new-only check. */
    CHECK_EQ(vfs_copy(&ctx, 0, "A.TXT", "T1.TXT", VFS_W_NEW), VFS_E_EXISTS);
    CHECK_EQ(read_text("T1.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "target old\n");
}

TEST(writable_writes_see_recovered_names)
{
    static const char *const more[] = {
        "#D1.TXT=d\n", "#R1.TXT=r\n", "#R2.TXT=r2\n", "#M1=file\n", "#W1.TXT=w\n", NULL,
    };
    char text[64];

    card(more, 0);
    CHECK_EQ(vfs_write_all(&ctx, 0, "W1.TXT", NULL, "x", 1, VFS_W_NEW), VFS_E_EXISTS);
    CHECK_EQ(read_text("W1.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "w\n");
    CHECK_EQ(vfs_delete(&ctx, 0, "D1.TXT"), VFS_OK);
    CHECK(!there("D1.TXT") && !there("#D1.TXT"));
    CHECK_EQ(vfs_rename(&ctx, 0, "R1.TXT", "R2.TXT"), VFS_E_EXISTS);
    CHECK_EQ(vfs_rename(&ctx, 0, "R1.TXT", "R3.TXT"), VFS_OK);
    CHECK_EQ(read_text("R3.TXT", text, sizeof(text)), VFS_OK);
    CHECK_STR(text, "r\n");
    CHECK_EQ(vfs_mkdir(&ctx, 0, "[.M1]"), VFS_E_EXISTS);
    CHECK(there("M1") && !there("#M1"));
}

TEST(read_only_card_reads_and_lists_real_names)
{
    card(cut, 1);
    check_reads();
    /* Nothing changed on the card. */
    CHECK(there("#S1.TXT") && !there("S1.TXT"));
    CHECK(there("~S3.TXT") && there("#S3.TXT") && there("~S2.TXT"));
}

TEST(read_only_card_refuses_writes_as_read_only)
{
    card(cut, 1);
    CHECK_EQ(vfs_write_all(&ctx, 0, "S1.TXT", NULL, "x", 1, 0), VFS_E_READONLY);
    CHECK_EQ(vfs_write_all(&ctx, 0, "T1.TXT", NULL, "x", 1, VFS_W_NEW), VFS_E_READONLY);
    CHECK_EQ(vfs_delete(&ctx, 0, "S1.TXT"), VFS_E_READONLY);
    CHECK_EQ(vfs_rename(&ctx, 0, "S1.TXT", "Z.TXT"), VFS_E_READONLY);
    CHECK_EQ(vfs_rename(&ctx, 0, "A.TXT", "T1.TXT"), VFS_E_READONLY);
    CHECK_EQ(vfs_copy(&ctx, 0, "S1.TXT", "C1.TXT", 0), VFS_E_READONLY);
    CHECK_EQ(vfs_copy(&ctx, 0, "A.TXT", "T1.TXT", 0), VFS_E_READONLY);
    CHECK_EQ(vfs_mkdir(&ctx, 0, "[.NEWDIR]"), VFS_E_READONLY);
    /* A clean file too: the card says read-only, not "not found". */
    CHECK_EQ(vfs_write_all(&ctx, 0, "A.TXT", NULL, "x", 1, 0), VFS_E_READONLY);
    CHECK_EQ(vfs_delete(&ctx, 0, "A.TXT"), VFS_E_READONLY);
    CHECK(there("#S1.TXT") && there("A.TXT"));
}

TEST(dev_paths_are_left_alone)
{
    card(cut, 0);
    vfs_dev_register(NULL);
    CHECK_EQ(vfs_exists(&ctx, 0, "DEV:LAMP"), VFS_E_NODEV);
    CHECK(there("#S1.TXT"));
}

/* ---- The maintenance request ---- */

static const char *const none[] = { NULL };

/* A fresh card with a /ROBCO folder, then read-only if asked. */
static void boot_card(int read_only)
{
    card(none, 0);
    CHECK_EQ(fs_mkdir("/ROBCO"), FS_OK);
    if (read_only) {
        CHECK_EQ(fs_unmount("sd"), FS_OK);
        dev.flags |= BLK_FLAG_READONLY;
        CHECK_EQ(fs_mount("sd", &dev), FS_OK);
    }
}

static int req_text(char *out, size_t size)
{
    size_t len = 0;
    int rc = fs_read_file(VFS_MAINT_REQ_PATH, out, size - 1, &len);

    out[rc == FS_OK ? len : 0] = '\0';
    return rc;
}

TEST(maint_request_is_created_with_its_note)
{
    char text[80];
    int made = -1;

    boot_card(0);
    CHECK_EQ(vfs_maint_request("NOTE\r\n", &made), VFS_OK);
    CHECK_EQ(made, 1);
    CHECK_EQ(req_text(text, sizeof(text)), FS_OK);
    CHECK_STR(text, "NOTE\r\n");
    /* A plain name: no work file next to it. */
    CHECK(fs_stat("/ROBCO/~MAINT.REQ", &st_b) != FS_OK);
    CHECK(fs_stat("/ROBCO/#MAINT.REQ", &st_b) != FS_OK);
}

TEST(maint_request_leaves_an_existing_file)
{
    char text[80];
    int made = -1;

    boot_card(0);
    put(VFS_MAINT_REQ_PATH, "OLD");
    CHECK_EQ(vfs_maint_request("NEW", &made), VFS_OK);
    CHECK_EQ(made, 0);
    CHECK_EQ(req_text(text, sizeof(text)), FS_OK);
    CHECK_STR(text, "OLD");
    /* An empty note makes an empty file, a valid request. */
    CHECK_EQ(fs_unlink(VFS_MAINT_REQ_PATH), FS_OK);
    CHECK_EQ(vfs_maint_request(NULL, &made), VFS_OK);
    CHECK_EQ(made, 1);
    CHECK_EQ(req_text(text, sizeof(text)), FS_OK);
    CHECK_STR(text, "");
}

TEST(maint_request_fails_without_a_file)
{
    int made = -1;

    /* No /ROBCO folder. */
    card(none, 0);
    CHECK_EQ(vfs_maint_request("X", &made), VFS_E_DIRNF);
    CHECK_EQ(made, 0);
    /* A folder of that name. */
    boot_card(0);
    CHECK_EQ(fs_mkdir(VFS_MAINT_REQ_PATH), FS_OK);
    CHECK_EQ(vfs_maint_request("X", &made), VFS_E_ISDIR);
    CHECK_EQ(made, 0);
    /* A read-only card. */
    boot_card(1);
    CHECK_EQ(vfs_maint_request("X", &made), VFS_E_READONLY);
    CHECK_EQ(made, 0);
    CHECK(fs_stat(VFS_MAINT_REQ_PATH, &st_b) != FS_OK);
    /* No card. */
    uos.card = UOS_CARD_NONE;
    CHECK_EQ(vfs_maint_request("X", &made), VFS_E_NOCARD);
    uos.card = UOS_CARD_OK;
}

TEST(maint_clear_removes_only_a_file)
{
    int removed = -1;
    int made = 0;

    boot_card(0);
    CHECK_EQ(vfs_maint_clear(&removed), VFS_OK);
    CHECK_EQ(removed, 0);
    CHECK_EQ(vfs_maint_request("", &made), VFS_OK);
    CHECK_EQ(vfs_maint_clear(&removed), VFS_OK);
    CHECK_EQ(removed, 1);
    CHECK(fs_stat(VFS_MAINT_REQ_PATH, &st_b) != FS_OK);
    CHECK_EQ(fs_mkdir(VFS_MAINT_REQ_PATH), FS_OK);
    CHECK_EQ(vfs_maint_clear(&removed), VFS_OK);
    CHECK_EQ(removed, 0);
    CHECK(fs_stat(VFS_MAINT_REQ_PATH, &st_b) == FS_OK && st_b.is_dir);
}
