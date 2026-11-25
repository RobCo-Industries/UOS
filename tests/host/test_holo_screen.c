#include <string.h>

#include "holoscan.h"
#include "vfs.h"

#include "rc_test.h"

TEST(scan_results)
{
    CHECK_EQ(hs_scan_result(VFS_OK, 3), HS_SCAN_LIST);
    CHECK_EQ(hs_scan_result(VFS_OK, 0), HS_SCAN_EMPTY);
    CHECK_EQ(hs_scan_result(VFS_E_NOFILES, 0), HS_SCAN_EMPTY);
    CHECK_EQ(hs_scan_result(VFS_E_NOTFOUND, 0), HS_SCAN_EMPTY);
    CHECK_EQ(hs_scan_result(VFS_E_NOCARD, 0), HS_SCAN_NOCARD);
    CHECK_EQ(hs_scan_result(VFS_E_NOTREE, 0), HS_SCAN_NOFOLDER);
    CHECK_EQ(hs_scan_result(VFS_E_DIRNF, 0), HS_SCAN_NOFOLDER);
    CHECK_EQ(hs_scan_result(VFS_E_READ, 0), HS_SCAN_ERROR);
    CHECK_STR(hs_scan_name(HS_SCAN_NOFOLDER), "nofolder");
    CHECK_STR(hs_scan_name(HS_SCAN_NUM), "unknown");
}

TEST(load_results)
{
    CHECK_EQ(hs_load_result(VFS_OK, HF_OK), HS_LOAD_OK);
    CHECK_EQ(hs_load_result(VFS_OK, HF_E_CRC), HS_LOAD_DAMAGED);
    CHECK_EQ(hs_load_result(VFS_E_NOMEM, HF_OK), HS_LOAD_NOMEM);
    CHECK_EQ(hs_load_result(VFS_E_NOCARD, HF_OK), HS_LOAD_NOCARD);
    CHECK_EQ(hs_load_result(VFS_E_NOTREE, HF_OK), HS_LOAD_NOCARD);
    CHECK_EQ(hs_load_result(VFS_E_NOTFOUND, HF_OK), HS_LOAD_GONE);
    CHECK_EQ(hs_load_result(VFS_E_TOOBIG, HF_OK), HS_LOAD_DAMAGED);
    CHECK_EQ(hs_load_result(VFS_E_READ, HF_OK), HS_LOAD_RETRY);
    CHECK_EQ(hs_load_result(VFS_E_BUSY, HF_OK), HS_LOAD_RETRY);
    CHECK_EQ(hs_read_err(VFS_E_TOOBIG), HF_E_TOOBIG);
    /* Only the size says something about the tape: every other read error
     * leaves it unchecked, never damaged. */
    CHECK_EQ(hs_read_err(VFS_E_NOMEM), HF_E_UNCHECKED);
    CHECK_EQ(hs_read_err(VFS_E_READ), HF_E_UNCHECKED);
    CHECK_EQ(hs_read_err(VFS_E_BUSY), HF_E_UNCHECKED);
    CHECK_EQ(hs_read_err(VFS_E_NOCARD), HF_E_UNCHECKED);
    CHECK_EQ(hs_read_err(VFS_E_NOTFOUND), HF_E_UNCHECKED);
    CHECK_EQ(hs_read_err(VFS_E_NOPRIV), HF_E_UNCHECKED);
    CHECK_STR(hs_load_name(HS_LOAD_RETRY), "retry");
    CHECK(strlen(hs_load_text(HS_LOAD_RETRY)) <= 72);
    CHECK_STR(hs_load_name(HS_LOAD_NOMEM), "nomem");
    CHECK_STR(hs_load_text(HS_LOAD_OK), "");
    CHECK_STR(hs_load_text(HS_LOAD_NOMEM), "NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.");
    CHECK(strlen(hs_load_text(HS_LOAD_NOCARD)) <= 72);
}

static char lines[HF_ROWS][HF_LINE_SIZE];

static void layout(hf_reader_t *r, const hf_tape_info_t *t, uint32_t n)
{
    CHECK_EQ(hf_reader_init(r, t, n), HF_OK);
    CHECK_EQ(hf_layout(r, lines, HF_ROWS), HF_OK);
}

TEST(apply_notices)
{
    hf_reader_t r;
    hf_tape_info_t t[2];

    memset(t, 0, sizeof(t));
    strcpy(t[0].file, "A.HTP");
    strcpy(t[0].title, "First");
    strcpy(t[1].file, "B.HTP");
    t[1].err = HF_E_CRC;
    layout(&r, t, 0);
    hs_apply(lines, 1, HS_SCAN_NOCARD, 0, 0, "");
    CHECK_STR(lines[3], "  No card. Insert a UOS card to read holotapes.");
    layout(&r, t, 0);
    hs_apply(lines, 1, HS_SCAN_NOFOLDER, 0, 0, NULL);
    CHECK_STR(lines[3], "  This card has no holotape folder, SYS$HOLO.");
    layout(&r, t, 0);
    hs_apply(lines, 1, HS_SCAN_EMPTY, 0, 0, NULL);
    CHECK_STR(lines[3], "  No holotapes found in SYS$HOLO.");
    /* More tapes than the list holds. */
    layout(&r, t, 2);
    hs_apply(lines, 1, HS_SCAN_LIST, 32, 40, NULL);
    CHECK_STR(lines[1], "Holotapes in SYS$HOLO: 40, first 32 shown");
    CHECK(strncmp(lines[3], "> A.HTP", 7) == 0);
    CHECK(strstr(lines[4], "DAMAGED: ") != NULL);
    /* A message replaces the bottom rule; no message keeps it. */
    CHECK_EQ(lines[28][0], '-');
    layout(&r, t, 2);
    hs_apply(lines, 1, HS_SCAN_LIST, 2, 2, "NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.");
    CHECK_STR(lines[28], "** NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.");
    CHECK_STR(lines[1], "Holotapes in SYS$HOLO: 2");
}

TEST(loading_text)
{
    char b[HF_LINE_SIZE];

    hs_loading_text("TECHLOG.HTP", b, sizeof(b));
    CHECK_STR(b, "LOADING TECHLOG.HTP ...");
    hs_loading_text(NULL, b, sizeof(b));
    CHECK_STR(b, "LOADING  ...");
}
