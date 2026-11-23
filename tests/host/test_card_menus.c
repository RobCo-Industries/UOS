/* Host test of the shipped card: every device entry in a menu under
 * card/UOS/HOME names a device of card/UOS/DEVICES.CFG and an action
 * word the device module accepts for that device's type. Runs from the
 * repository root. */

#include <dirent.h>

#include "rc_test.h"

#include "dev.h"
#include "tui.h"

static dv_set_t g_set;
static dv_cfg_result_t g_res;
static tui_menu_t g_menu;

static size_t read_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        printf("  cannot open %s\n", path);
        return (size_t)-1;
    }
    n = fread(buf, 1, size, f);
    fclose(f);
    return n;
}

static int load_devices(void)
{
    static char buf[8192];
    size_t n = read_file("card/UOS/DEVICES.CFG", buf, sizeof(buf));

    CHECK(n != (size_t)-1 && n < sizeof(buf));
    if (n == (size_t)-1) {
        return -1;
    }
    CHECK_EQ(dv_cfg_parse_pins(buf, n, DV_REFUSED_UOS, &g_set, &g_res), DV_OK);
    CHECK_EQ(g_res.err_count, 0);
    dv_reset(&g_set);
    return 0;
}

/* Checks one device entry. Returns 1 when it is good. */
static int check_entry(const char *path, const tui_entry_t *e)
{
    char name[TUI_TARGET_MAX + 1];
    const char *word;
    char *sp;
    dv_action_t act;
    dv_op_t ops[DV_CMD_OPS];
    dv_set_t copy;
    int idx;
    int rc;

    snprintf(name, sizeof(name), "%s", e->target);
    sp = strchr(name, ' ');
    if (sp == NULL) {
        printf("  %s entry %u: no action word in \"%s\"\n", path, e->num, e->target);
        return 0;
    }
    *sp = '\0';
    word = sp + 1;
    idx = dv_find(&g_set, name);
    if (idx < 0) {
        printf("  %s entry %u: no device %s in DEVICES.CFG\n", path, e->num, name);
        return 0;
    }
    act = dv_action_parse(word);
    if (act == DV_ACT_NONE) {
        printf("  %s entry %u: %s is not an action word\n", path, e->num, word);
        return 0;
    }
    /* A SYSTEM user passes every privilege check: only the type can refuse. */
    copy = g_set;
    rc = dv_command(&copy, (unsigned)idx, act, DV_PRIV_SYSTEM, 0, ops, DV_CMD_OPS);
    if (rc < 0) {
        printf("  %s entry %u: %s %s: %s\n", path, e->num, name, word, dv_strerror(rc));
        return 0;
    }
    return 1;
}

TEST(card_menu_devices_exist_and_take_their_words)
{
    static char buf[8192];
    static const char *const homes[] = { "ADMIN", "OPER", "GUEST" };
    unsigned h;
    unsigned files = 0;
    unsigned devices = 0;

    if (load_devices() != 0) {
        return;
    }
    for (h = 0; h < sizeof(homes) / sizeof(homes[0]); h++) {
        char dir[128];
        DIR *d;
        struct dirent *de;

        snprintf(dir, sizeof(dir), "card/UOS/HOME/%s", homes[h]);
        d = opendir(dir);
        CHECK(d != NULL);
        if (d == NULL) {
            continue;
        }
        while ((de = readdir(d)) != NULL) {
            size_t len = strlen(de->d_name);
            char path[512];
            size_t n;
            unsigned i;

            if (len < 5 || strcmp(de->d_name + len - 4, ".CFG") != 0) {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
            n = read_file(path, buf, sizeof(buf));
            CHECK(n != (size_t)-1 && n < sizeof(buf));
            if (n == (size_t)-1) {
                continue;
            }
            files++;
            CHECK_EQ(tui_cfg_parse(buf, n, TUI_PRIV_SYSTEM, &g_menu, NULL), TUI_OK);
            CHECK_EQ(g_menu.errors, 0);
            for (i = 0; i < g_menu.count; i++) {
                if (g_menu.entry[i].type != TUI_T_DEVICE) {
                    continue;
                }
                devices++;
                CHECK(check_entry(path, &g_menu.entry[i]));
            }
        }
        closedir(d);
    }
    /* TERMINAL.CFG in each home, CONTROL.CFG and MAINT.CFG. */
    CHECK(files >= 5);
    CHECK(devices >= 6);
}
