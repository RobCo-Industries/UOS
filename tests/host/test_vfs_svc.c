/*
 * The `fs` maintenance command of src/vfs/svc.c over vfs.c and the
 * library's FatFs glue on a host file. A fake loop runs the posted calls
 * one at a time, so a test can act between two steps of a request. The
 * image is build/host/vfs_svc.img, made here.
 */
/* First: klibc.h declares memcpy() before the host headers turn it into a macro. */
#include "vfs.c"
#include "svc.c"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <robco/blk.h>

#include "blk_file.h"
#include "rc_test.h"

#define IMG   "build/host/vfs_svc.img"
#define ADMIN "/UOS/HOME/ADMIN"
#define FILE_BYTES (VFS_COPY_CHUNK * 2u + 100u)   /* three steps per file */

uos_state_t uos;

uint32_t irq_save(void)
{
    return 0;
}

void irq_restore(uint32_t state)
{
    (void)state;
}

uint32_t uos_ticks(void)
{
    return 0;
}

/* The markers and the last answer, as text. */
static char marks[16384];
static size_t marks_len;
static char reply[256];

void uos_mark(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = k_vsnprintf(marks + marks_len, sizeof(marks) - marks_len, fmt, ap);
    va_end(ap);
    if (n > 0 && marks_len + (size_t)n + 1u < sizeof(marks)) {
        marks_len += (size_t)n;
        marks[marks_len++] = '\n';
        marks[marks_len] = '\0';
    }
}

void uos_maint_reply(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)k_vsnprintf(reply, sizeof(reply), fmt, ap);
    va_end(ap);
}

int uos_maint_fail(const char *reason)
{
    snprintf(reply, sizeof(reply), "err %s", reason);
    return -1;
}

int uos_maint_register(const char *name, uos_maint_fn fn, const char *help)
{
    (void)name;
    (void)fn;
    (void)help;
    return 0;
}

/* The fake loop: a queue of posted calls. */
#define QMAX 8
static uos_call_fn queue[QMAX];
static unsigned q_head, q_count;

int uos_post(uos_call_fn fn, void *arg)
{
    (void)arg;
    if (q_count == QMAX) {
        return -1;
    }
    queue[(q_head + q_count++) % QMAX] = fn;
    return 0;
}

/* Runs one posted call. 0 when none was waiting. */
static int step(void)
{
    uos_call_fn fn;

    if (q_count == 0) {
        return 0;
    }
    fn = queue[q_head];
    q_head = (q_head + 1u) % QMAX;
    q_count--;
    fn(NULL);
    return 1;
}

/* One `fs` command line, as the maintenance port splits it: the command
 * function sees the words after the command name. */
static int fs_cmd(const char *line)
{
    static char buf[UOS_MAINT_LINE_MAX + 1];
    char *argv[8];
    int argc;

    reply[0] = '\0';
    snprintf(buf, sizeof(buf), "%s", line);
    argc = uos_mt_split(buf, argv, 8);
    return argc < 1 ? -1 : cmd_fs(argc - 1, argv + 1);
}

static blk_dev_t dev;
static unsigned char heap[1u << 20];
static unsigned char data[FILE_BYTES];

static void put(const char *path, const void *bytes, size_t len)
{
    fs_file_t f;

    CHECK_EQ(fs_open(&f, path, FS_WRITE | FS_CREATE | FS_TRUNC), FS_OK);
    CHECK_EQ(fs_write(&f, bytes, len), (int)len);
    CHECK_EQ(fs_close(&f), FS_OK);
}

static int there(const char *path)
{
    fs_stat_t st;

    return fs_stat(path, &st) == FS_OK;
}

/*
 * A fresh card: ADMIN holds SRC with three files, DST, and an OTHER
 * folder with its own DST, so a copy that resolved its target against
 * another default would land there with no error.
 */
static void card(void)
{
    static int heap_ready;
    static const unsigned char zero[1024];
    fs_mkfs_opts_t o = { FS_MKFS_FAT, 0 };
    FILE *f;
    size_t i;

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
    for (i = 0; i < 4096; i++) {
        fwrite(zero, 1, sizeof(zero), f);
    }
    fclose(f);
    CHECK_EQ(blk_file_open(&dev, IMG, 0), 0);
    CHECK_EQ(fs_mkfs(&dev, &o), FS_OK);
    CHECK_EQ(fs_mount("sd", &dev), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS"), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS/HOME"), FS_OK);
    CHECK_EQ(fs_mkdir(ADMIN), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS/HOME/GUEST"), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS/HOME/GUEST/DST"), FS_OK);
    CHECK_EQ(fs_mkdir(ADMIN "/SRC"), FS_OK);
    CHECK_EQ(fs_mkdir(ADMIN "/DST"), FS_OK);
    CHECK_EQ(fs_mkdir(ADMIN "/OTHER"), FS_OK);
    CHECK_EQ(fs_mkdir(ADMIN "/OTHER/DST"), FS_OK);
    for (i = 0; i < sizeof(data); i++) {
        data[i] = (unsigned char)(i * 31u + 7u);
    }
    put(ADMIN "/SRC/F1.BIN", data, sizeof(data));
    put(ADMIN "/SRC/F2.BIN", data, sizeof(data));
    put(ADMIN "/SRC/F3.BIN", data, sizeof(data));
    uos.card = UOS_CARD_OK;
    uos.phase = UOS_PHASE_READY;
    marks_len = 0;
    marks[0] = '\0';
    q_head = q_count = 0;
    req.busy = 0;
    CHECK_EQ(vfs_init(), 0);
    CHECK_EQ(fs_cmd("fs who ADMIN 0"), 0);
}

static void check_landed(void)
{
    CHECK(there(ADMIN "/DST/F1.BIN"));
    CHECK(there(ADMIN "/DST/F2.BIN"));
    CHECK(there(ADMIN "/DST/F3.BIN"));
    CHECK(!there(ADMIN "/OTHER/DST/F1.BIN"));
    CHECK(!there(ADMIN "/OTHER/DST/F2.BIN"));
    CHECK(!there(ADMIN "/OTHER/DST/F3.BIN"));
    CHECK(!there("/UOS/HOME/GUEST/DST/F2.BIN"));
    CHECK(strstr(marks, "fs op=copy rc=0 err=ok files=3 skipped=0") != NULL);
}

/* The context changes between every two steps, behind the commands'
 * back: the request keeps the default and the privileges of its start. */
TEST(copy_keeps_its_snapshot)
{
    int steps = 0, flip = 0;

    card();
    CHECK_EQ(fs_cmd("fs copy [.SRC]*.BIN [.DST]"), 0);
    while (step()) {
        steps++;
        if (flip ^= 1) {
            CHECK_EQ(vp_set_default(&ctx, "[.OTHER]"), VP_OK);
            privs = VP_PRIV_SYSTEM;
        } else {
            vp_ctx_init(&ctx, "GUEST");
            privs = 0;
        }
    }
    CHECK(steps >= 10);
    check_landed();
}

/* The same through the commands: both refuse while the copy runs. */
TEST(default_and_who_refused_while_busy)
{
    char shown[VP_DISPLAY_MAX];
    int refused = 0;

    card();
    CHECK_EQ(fs_cmd("fs copy [.SRC]*.BIN [.DST]"), 0);
    while (step() && req.busy) {
        refused++;
        CHECK_EQ(fs_cmd("fs default [.OTHER]"), -1);
        CHECK_STR(reply, "err busy");
        CHECK_EQ(fs_cmd("fs who GUEST 0"), -1);
        CHECK_STR(reply, "err busy");
    }
    CHECK(refused >= 9);
    check_landed();
    (void)vp_show_default(&ctx, shown, sizeof(shown));
    CHECK_STR(shown, "SD0:[HOME.ADMIN]");
    CHECK_EQ(fs_cmd("fs default [.OTHER]"), 0);
    CHECK_STR(reply, "default SD0:[HOME.ADMIN.OTHER]");
}

/* A request posted but not yet run also keeps the context of its post. */
TEST(posted_request_keeps_its_snapshot)
{
    card();
    CHECK_EQ(fs_cmd("fs mkdir [.NEWDIR]"), 0);
    CHECK_EQ(vp_set_default(&ctx, "[.OTHER]"), VP_OK);
    CHECK(step());
    CHECK(there(ADMIN "/NEWDIR"));
    CHECK(!there(ADMIN "/OTHER/NEWDIR"));
}

/* `fs free` prints the 64-bit values in full. */
TEST(free_prints_full_values)
{
    char want[96];
    uint64_t total = 0, free_bytes = 0;

    card();
    CHECK_EQ(vfs_free_space(&total, &free_bytes), VFS_OK);
    CHECK(total > 0 && free_bytes > 0);
    CHECK_EQ(fs_cmd("fs free"), 0);
    CHECK(step());
    snprintf(want, sizeof(want), "fs op=free rc=0 err=ok total=%llu free=%llu\n",
             (unsigned long long)total, (unsigned long long)free_bytes);
    CHECK_STR(marks, want);
}

/*
 * A volume over 4 GiB: a sparse 6 GiB FAT32 file. A 32-bit cast would
 * print the total and the free count modulo 2^32. Too large for a target
 * test image, so only the host covers it.
 */
TEST(free_over_4_gib)
{
    static const uint64_t size = 6ull << 30;
    fs_mkfs_opts_t o = { FS_MKFS_FAT32, 32768 };
    char want[96];
    uint64_t total = 0, free_bytes = 0;
    FILE *f;

    card();
    fs_unmount("sd");
    blk_file_close(&dev);
    f = fopen("build/host/vfs_svc_big.img", "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    CHECK_EQ(ftruncate(fileno(f), (off_t)size), 0);
    fclose(f);
    CHECK_EQ(blk_file_open(&dev, "build/host/vfs_svc_big.img", 0), 0);
    CHECK_EQ(fs_mkfs(&dev, &o), FS_OK);
    CHECK_EQ(fs_mount("sd", &dev), FS_OK);
    CHECK_EQ(fs_mkdir("/UOS"), FS_OK);
    CHECK_EQ(vfs_free_space(&total, &free_bytes), VFS_OK);
    CHECK(total > (4ull << 30) && free_bytes > (4ull << 30));
    marks_len = 0;
    marks[0] = '\0';
    CHECK_EQ(fs_cmd("fs free"), 0);
    CHECK(step());
    snprintf(want, sizeof(want), "fs op=free rc=0 err=ok total=%llu free=%llu\n",
             (unsigned long long)total, (unsigned long long)free_bytes);
    CHECK_STR(marks, want);
    fs_unmount("sd");
    blk_file_close(&dev);
    remove("build/host/vfs_svc_big.img");
}
