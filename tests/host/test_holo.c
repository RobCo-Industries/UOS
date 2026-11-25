/* Data holotapes: every validation error, the limits, the accessors, the reader model and its layout. */

#include <stdio.h>
#include <string.h>

#include <robco/rimg.h>

#include "holo/holofmt.h"
#include "rc_test.h"

/* Room for the largest tape plus slack for "one over" cases. */
static uint8_t img[HF_TAPE_MAX + 4096];
static uint32_t img_size;
static hf_tape_t tape;

static void wr16(uint32_t off, uint32_t v)
{
    img[off] = (uint8_t)v;
    img[off + 1] = (uint8_t)(v >> 8);
}

static void wr32(uint32_t off, uint32_t v)
{
    wr16(off, v & 0xffff);
    wr16(off + 2, v >> 16);
}

/* File offsets of the header fields and the archive. */
#define H(x)        (RIMG_HDR_OFFSET + (x))
#define OFF_FSIZE   H(0x14)
#define OFF_MSIZE   H(0x18)
#define OFF_CRC     H(0x1c)
#define PL          HF_PAYLOAD_OFFSET
#define ENT(i)      (PL + HF_ARC_HDR_SIZE + (i) * HF_ENTRY_SIZE)
#define ENT_OFF(i)  (ENT(i) + HF_NAME_FIELD)
#define ENT_LEN(i)  (ENT(i) + HF_NAME_FIELD + 4)

static void set_size(uint32_t size)
{
    img_size = size;
    wr32(OFF_FSIZE, size);
    wr32(OFF_MSIZE, size);
}

static void reseal(void)
{
    wr32(OFF_CRC, rimg_crc(img, img_size));
}

/*
 * Builds a tape with `n` entries named "Entry 1".., each body `len` bytes of
 * 'a' to 'z', placed in order right after the table. Seals it.
 */
static void build_n(uint32_t n, uint32_t len)
{
    uint32_t i, j, body;

    memset(img, 0, sizeof(img));
    wr32(0, HF_STUB_WORD);
    memcpy(img + RIMG_HDR_OFFSET, RIMG_MAGIC_ROBCO78, 8);
    wr16(H(0x08), 1);
    wr16(H(0x0a), 64);
    wr32(H(0x0c), RIMG_FLAG_META);
    wr32(H(0x10), 0);
    wr32(H(0x20), 1u << 16);
    wr32(H(0x28), HF_SUBTYPE_DATA);
    memcpy(img + H(0x2c), "TEST", 4);
    strcpy((char *)img + RIMG_META_OFFSET, "Test tape");
    memcpy(img + PL, "HTXT", 4);
    wr16(PL + 4, 1);
    wr16(PL + 6, n);
    body = HF_ARC_HDR_SIZE + n * HF_ENTRY_SIZE;
    for (i = 0; i < n; i++) {
        snprintf((char *)img + ENT(i), HF_NAME_FIELD, "Entry %u", (unsigned)(i + 1));
        wr32(ENT_OFF(i), body);
        wr32(ENT_LEN(i), len);
        for (j = 0; j < len; j++) {
            img[PL + body + j] = (uint8_t)('a' + (i + j) % 26);
        }
        body += len;
    }
    set_size(PL + body);
    reseal();
}

static int open_img(void)
{
    return hf_open(&tape, img, img_size);
}

/* Changes a field after the build and reseals, so only that field is wrong. */
#define BAD32(off, v, code)             \
    do {                                \
        build_n(3, 10);                 \
        wr32((off), (v));               \
        reseal();                       \
        CHECK_EQ(open_img(), (code));   \
    } while (0)

TEST(valid_tape_and_accessors)
{
    const char *text;
    uint32_t len;

    build_n(3, 10);
    CHECK_EQ(open_img(), HF_OK);
    CHECK_STR(hf_title(&tape), "Test tape");
    CHECK_EQ(hf_count(&tape), 3);
    CHECK_STR(hf_entry_name(&tape, 0), "Entry 1");
    CHECK_STR(hf_entry_name(&tape, 2), "Entry 3");
    CHECK(hf_entry_name(&tape, 3) == NULL);
    CHECK_EQ(hf_entry_text(&tape, 1, &text, &len), HF_OK);
    CHECK_EQ(len, 10);
    CHECK_MEM(text, "bcdefghijk", 10);
    CHECK_EQ(hf_entry_text(&tape, 3, &text, &len), HF_E_INDEX);
    CHECK_EQ(hf_entry_text(&tape, 0, NULL, &len), HF_E_ARG);
    CHECK_EQ(hf_count(NULL), 0);
    CHECK(hf_title(NULL) == NULL);
}

TEST(null_arguments)
{
    build_n(1, 1);
    CHECK_EQ(hf_open(NULL, img, img_size), HF_E_ARG);
    CHECK_EQ(hf_open(&tape, NULL, img_size), HF_E_ARG);
}

TEST(rimg_steps)
{
    build_n(3, 10);
    CHECK_EQ(hf_open(&tape, img, 0x5f), HF_E_IO);
    CHECK_EQ(hf_open(&tape, img, HF_TAPE_MAX + 1), HF_E_TOOBIG);

    build_n(3, 10);
    img[RIMG_HDR_OFFSET] = 'X';
    CHECK_EQ(open_img(), HF_E_MAGIC);
    build_n(3, 10);
    memcpy(img + RIMG_HDR_OFFSET, RIMG_MAGIC_BOOT, 8);
    CHECK_EQ(open_img(), HF_E_MAGIC);

    build_n(3, 10);
    wr16(H(0x08), 2);
    CHECK_EQ(open_img(), HF_E_VERSION);
    build_n(3, 10);
    wr32(H(0x0c), 0);
    CHECK_EQ(open_img(), HF_E_VERSION);
    build_n(3, 10);
    wr32(H(0x3c), 1);
    CHECK_EQ(open_img(), HF_E_VERSION);

    /* file_size differs from the bytes the caller read. */
    build_n(3, 10);
    CHECK_EQ(hf_open(&tape, img, img_size - 1), HF_E_SIZE);
    CHECK_EQ(hf_open(&tape, img, img_size + 1), HF_E_SIZE);
    /* A header too small for META. */
    build_n(3, 10);
    CHECK_EQ(hf_open(&tape, img, 0x100), HF_E_SIZE);

    BAD32(H(0x28), 0, HF_E_SUBTYPE);
    BAD32(H(0x28), 1, HF_E_SUBTYPE);
    BAD32(H(0x28), 3, HF_E_SUBTYPE);
    BAD32(H(0x10), 0x00800000, HF_E_BOUNDS);
    BAD32(H(0x10), 4, HF_E_BOUNDS);
    build_n(3, 10);
    wr32(OFF_MSIZE, img_size + 4);
    reseal();
    CHECK_EQ(open_img(), HF_E_BOUNDS);
    BAD32(H(0x20), 2u << 16, HF_E_ABI);
    BAD32(H(0x20), 0, HF_E_ABI);
    /* The minor is not checked. */
    BAD32(H(0x20), (1u << 16) | 7, HF_OK);

    build_n(3, 10);
    img[img_size - 1] ^= 1;
    CHECK_EQ(open_img(), HF_E_CRC);
    build_n(3, 10);
    img[1] = 0x55;  /* the stub is covered by the CRC too */
    CHECK_EQ(open_img(), HF_E_CRC);
}

TEST(meta_title)
{
    build_n(1, 4);
    memset(img + RIMG_META_OFFSET, 0, HF_TITLE_FIELD);
    reseal();
    CHECK_EQ(open_img(), HF_E_TITLE);

    build_n(1, 4);
    memset(img + RIMG_META_OFFSET, 'T', HF_TITLE_MAX);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
    CHECK_EQ(strlen(hf_title(&tape)), HF_TITLE_MAX);

    build_n(1, 4);
    memset(img + RIMG_META_OFFSET, 'T', HF_TITLE_FIELD);
    reseal();
    CHECK_EQ(open_img(), HF_E_TITLE);

    build_n(1, 4);
    img[RIMG_META_OFFSET + 2] = '\t';
    reseal();
    CHECK_EQ(open_img(), HF_E_TITLE);

    build_n(1, 4);
    img[RIMG_META_OFFSET + 40] = 'x';  /* junk after the NUL */
    reseal();
    CHECK_EQ(open_img(), HF_E_TITLE);

    build_n(1, 4);
    img[RIMG_META_OFFSET] = (char)0x80;
    reseal();
    CHECK_EQ(open_img(), HF_E_TITLE);

    /* The icon is not checked. */
    build_n(1, 4);
    memset(img + RIMG_META_OFFSET + HF_TITLE_FIELD, 0xff, 128);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
}

TEST(archive_header)
{
    build_n(1, 0);
    set_size(PL + 7);
    reseal();
    CHECK_EQ(open_img(), HF_E_ARCSHORT);
    set_size(PL);
    reseal();
    CHECK_EQ(open_img(), HF_E_ARCSHORT);

    build_n(2, 5);
    img[PL + 3] = 'X';
    reseal();
    CHECK_EQ(open_img(), HF_E_ARCMAGIC);

    build_n(2, 5);
    wr16(PL + 4, 2);
    reseal();
    CHECK_EQ(open_img(), HF_E_ARCVER);
    build_n(2, 5);
    wr16(PL + 4, 0);
    reseal();
    CHECK_EQ(open_img(), HF_E_ARCVER);

    build_n(2, 5);
    wr16(PL + 6, 0);
    reseal();
    CHECK_EQ(open_img(), HF_E_COUNT);
    build_n(2, 5);
    wr16(PL + 6, 33);
    reseal();
    CHECK_EQ(open_img(), HF_E_COUNT);
    build_n(2, 5);
    wr16(PL + 6, 0xffff);
    reseal();
    CHECK_EQ(open_img(), HF_E_COUNT);

    /* Two entries declared, room for one table row only. */
    build_n(1, 0);
    wr16(PL + 6, 2);
    set_size(PL + HF_ARC_HDR_SIZE + 2 * HF_ENTRY_SIZE - 1);
    reseal();
    CHECK_EQ(open_img(), HF_E_TABLE);
    /* Exactly room for the table, empty bodies at the end. */
    build_n(2, 0);
    CHECK_EQ(img_size, PL + HF_ARC_HDR_SIZE + 2 * HF_ENTRY_SIZE);
    CHECK_EQ(open_img(), HF_OK);
}

TEST(entry_names)
{
    build_n(2, 5);
    memset(img + ENT(1), 0, HF_NAME_FIELD);
    reseal();
    CHECK_EQ(open_img(), HF_E_NAME);

    build_n(2, 5);
    memset(img + ENT(0), 'N', HF_NAME_MAX);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
    CHECK_EQ(strlen(hf_entry_name(&tape, 0)), HF_NAME_MAX);

    build_n(2, 5);
    memset(img + ENT(0), 'N', HF_NAME_FIELD);
    reseal();
    CHECK_EQ(open_img(), HF_E_NAME);

    build_n(2, 5);
    img[ENT(1) + 1] = 0x7f;
    reseal();
    CHECK_EQ(open_img(), HF_E_NAME);

    build_n(2, 5);
    img[ENT(1) + 31] = 'z';
    reseal();
    CHECK_EQ(open_img(), HF_E_NAME);
}

TEST(entry_ranges)
{
    uint32_t table_end = HF_ARC_HDR_SIZE + 3 * HF_ENTRY_SIZE;
    uint32_t plen;

    BAD32(ENT_LEN(1), HF_ENTRY_TEXT_MAX + 1, HF_E_LENGTH);
    BAD32(ENT_LEN(1), 0xffffffff, HF_E_LENGTH);
    BAD32(ENT_OFF(0), table_end - 1, HF_E_RANGE);
    BAD32(ENT_OFF(0), 0, HF_E_RANGE);
    BAD32(ENT_OFF(2), 0xffffffff, HF_E_RANGE);
    BAD32(ENT_OFF(2), table_end + 21, HF_E_RANGE);  /* ends one byte past */

    build_n(3, 10);
    plen = img_size - PL;
    /* Edges that are fine: the last body ends at the payload end, an empty
     * body sits at the very end. */
    CHECK_EQ(open_img(), HF_OK);
    wr32(ENT_OFF(2), plen);
    wr32(ENT_LEN(2), 0);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
    wr32(ENT_OFF(2), plen + 1);
    reseal();
    CHECK_EQ(open_img(), HF_E_RANGE);
    /* Gaps between bodies are allowed. */
    build_n(3, 10);
    wr32(ENT_LEN(0), 5);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
}

TEST(overlap)
{
    uint32_t table_end = HF_ARC_HDR_SIZE + 3 * HF_ENTRY_SIZE;

    BAD32(ENT_OFF(1), table_end + 9, HF_E_OVERLAP);   /* one shared byte */
    BAD32(ENT_OFF(2), table_end, HF_E_OVERLAP);       /* first and last */
    BAD32(ENT_OFF(1), table_end + 10, HF_OK);         /* adjacent is fine */

    /* Same body twice. */
    build_n(3, 10);
    wr32(ENT_OFF(1), table_end);
    reseal();
    CHECK_EQ(open_img(), HF_E_OVERLAP);
    /* One body inside another. */
    build_n(3, 10);
    wr32(ENT_LEN(0), 30);
    wr32(ENT_OFF(1), table_end + 12);
    wr32(ENT_LEN(1), 2);
    reseal();
    CHECK_EQ(open_img(), HF_E_OVERLAP);
    /* A later entry's last byte under an earlier entry's only byte. */
    build_n(2, 10);
    wr32(ENT_OFF(0), HF_ARC_HDR_SIZE + 2 * HF_ENTRY_SIZE + 19);
    wr32(ENT_LEN(0), 1);
    reseal();
    CHECK_EQ(open_img(), HF_E_OVERLAP);
    wr32(ENT_OFF(0), HF_ARC_HDR_SIZE + 2 * HF_ENTRY_SIZE + 10);
    reseal();
    CHECK_EQ(open_img(), HF_E_OVERLAP);
    /* Empty bodies overlap nothing. */
    build_n(3, 10);
    wr32(ENT_OFF(1), table_end + 3);
    wr32(ENT_LEN(1), 0);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
}

TEST(limits)
{
    const char *text;
    uint32_t len;

    build_n(HF_ENTRIES_MAX, HF_ENTRY_TEXT_MAX);
    CHECK_EQ(img_size, HF_TAPE_MAX);
    CHECK_EQ(open_img(), HF_OK);
    CHECK_EQ(hf_count(&tape), 32);
    CHECK_STR(hf_entry_name(&tape, 31), "Entry 32");
    CHECK_EQ(hf_entry_text(&tape, 31, &text, &len), HF_OK);
    CHECK_EQ(len, HF_ENTRY_TEXT_MAX);

    build_n(HF_ENTRIES_MAX + 1, 1);
    CHECK_EQ(open_img(), HF_E_COUNT);

    build_n(1, HF_ENTRY_TEXT_MAX + 1);
    CHECK_EQ(open_img(), HF_E_LENGTH);
    build_n(1, HF_ENTRY_TEXT_MAX);
    CHECK_EQ(open_img(), HF_OK);

    /* A file one byte over the limit is refused before anything else. */
    build_n(HF_ENTRIES_MAX, HF_ENTRY_TEXT_MAX);
    set_size(HF_TAPE_MAX + 1);
    reseal();
    CHECK_EQ(open_img(), HF_E_TOOBIG);
    CHECK_EQ(HF_TAPE_MAX, 2098728);
}

TEST(body_bytes_are_not_errors)
{
    build_n(1, 6);
    memcpy(img + PL + HF_ARC_HDR_SIZE + HF_ENTRY_SIZE, "a\0\x01\xff\r\n", 6);
    reseal();
    CHECK_EQ(open_img(), HF_OK);
    CHECK_EQ(hf_display_char('a'), 'a');
    CHECK_EQ(hf_display_char(' '), ' ');
    CHECK_EQ(hf_display_char('~'), '~');
    CHECK_EQ(hf_display_char('\t'), '\t');
    CHECK_EQ(hf_display_char('\r'), '\r');
    CHECK_EQ(hf_display_char('\n'), '\n');
    CHECK_EQ(hf_display_char(0), '.');
    CHECK_EQ(hf_display_char(0x1b), '.');
    CHECK_EQ(hf_display_char(0x7f), '.');
    CHECK_EQ(hf_display_char(0x80), '.');
    CHECK_EQ(hf_display_char(0xff), '.');
}

TEST(error_text)
{
    int i;

    for (i = 0; i < HF_E_COUNT_OF; i++) {
        CHECK(strcmp(hf_err_name(i), "unknown") != 0);
        CHECK(strlen(hf_strerror(i)) <= HF_REASON_MAX);
        CHECK(strlen(hf_strerror(i)) > 0);
    }
    CHECK_STR(hf_err_name(HF_E_CRC), "crc");
    CHECK_STR(hf_err_name(HF_E_OVERLAP), "overlap");
    CHECK_STR(hf_strerror(HF_E_CRC), "tape is damaged");
    CHECK_STR(hf_err_name(-1), "unknown");
    CHECK_STR(hf_err_name(HF_E_COUNT_OF), "unknown");
    CHECK_STR(hf_strerror(99), "unknown error");
}

/* ---- Reader model ---- */

static hf_tape_info_t tapes[3] = {
    { "LOG.HTP", "Pump house log", HF_OK },
    { "BAD.HTP", "", HF_E_CRC },
    { "PROC.HTP", "Operating procedures", HF_OK },
};
static hf_reader_t rd;
static hf_action_t act;
static char lines[HF_ROWS][HF_LINE_SIZE];

static int ev(hf_event_t e)
{
    return hf_reader_event(&rd, e, &act);
}

static const char *rule78 =
    "------------------------------------------------------------------------------";

TEST(reader_tape_list_layout)
{
    uint32_t i;

    CHECK_EQ(hf_reader_init(&rd, tapes, 3), HF_OK);
    CHECK_EQ(hf_layout(&rd, lines, HF_ROWS), HF_OK);
    CHECK_STR(lines[0], "UOS HOLOTAPE READER");
    CHECK_STR(lines[1], "Holotapes in SYS$HOLO: 3");
    CHECK_STR(lines[2], rule78);
    CHECK_STR(lines[3], "> LOG.HTP      Pump house log");
    CHECK_STR(lines[4], "  BAD.HTP      DAMAGED: tape is damaged");
    CHECK_STR(lines[5], "  PROC.HTP     Operating procedures");
    for (i = 6; i < 28; i++) {
        CHECK_STR(lines[i], "");
    }
    CHECK_STR(lines[28], rule78);
    CHECK_STR(lines[29], "UP/DOWN move   ENTER open tape   BACK leave reader");
    CHECK_EQ(hf_layout(&rd, lines, HF_ROWS - 1), HF_E_BUF);
}

TEST(reader_moves_and_wraps)
{
    hf_reader_init(&rd, tapes, 3);
    CHECK_EQ(ev(HF_EV_UP), HF_OK);
    CHECK_EQ(act.kind, HF_ACT_NONE);
    CHECK_EQ(rd.sel_tape, 2);
    ev(HF_EV_DOWN);
    CHECK_EQ(rd.sel_tape, 0);
    ev(HF_EV_DOWN);
    CHECK_EQ(rd.sel_tape, 1);
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[3], "  LOG.HTP      Pump house log");
    CHECK_STR(lines[4], "> BAD.HTP      DAMAGED: tape is damaged");
    /* A damaged tape cannot be opened. */
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_REFUSED);
    CHECK_EQ(act.tape, 1);
    CHECK_EQ(rd.screen, HF_SCR_TAPES);
    ev(HF_EV_BACK);
    CHECK_EQ(act.kind, HF_ACT_EXIT);
    CHECK_EQ(hf_reader_event(&rd, (hf_event_t)9, &act), HF_E_ARG);
}

TEST(reader_open_read_back)
{
    uint32_t i;

    build_n(3, 10);
    hf_reader_init(&rd, tapes, 3);
    ev(HF_EV_UP);
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_LOAD);
    CHECK_EQ(act.tape, 2);
    CHECK_EQ(hf_reader_loaded(&rd, img, img_size), HF_OK);
    CHECK_EQ(rd.screen, HF_SCR_ENTRIES);

    CHECK_EQ(hf_layout(&rd, lines, HF_ROWS), HF_OK);
    CHECK_STR(lines[0], "UOS HOLOTAPE READER");
    CHECK_STR(lines[1], "Tape: Test tape");
    CHECK_STR(lines[3], "> 01  Entry 1");
    CHECK_STR(lines[4], "  02  Entry 2");
    CHECK_STR(lines[5], "  03  Entry 3");
    CHECK_STR(lines[6], "");
    CHECK_STR(lines[29], "UP/DOWN move   ENTER read entry   BACK tape list");

    ev(HF_EV_UP);
    CHECK_EQ(rd.sel_entry, 2);
    ev(HF_EV_DOWN);
    ev(HF_EV_DOWN);
    CHECK_EQ(rd.sel_entry, 1);
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_SHOW);
    CHECK_STR(act.title, "Entry 2");
    CHECK_EQ(act.len, 10);
    CHECK_MEM(act.text, "bcdefghijk", 10);
    CHECK_EQ(rd.screen, HF_SCR_READING);
    CHECK_EQ(hf_layout(&rd, lines, HF_ROWS), HF_E_STATE);
    /* Only BACK leaves the pager screen. */
    ev(HF_EV_DOWN);
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_NONE);
    CHECK_EQ(rd.screen, HF_SCR_READING);
    ev(HF_EV_BACK);
    CHECK_EQ(act.kind, HF_ACT_NONE);
    CHECK_EQ(rd.screen, HF_SCR_ENTRIES);
    CHECK_EQ(rd.sel_entry, 1);
    ev(HF_EV_BACK);
    CHECK_EQ(act.kind, HF_ACT_CLOSE);
    CHECK_EQ(act.tape, 2);
    CHECK_EQ(rd.screen, HF_SCR_TAPES);
    CHECK_EQ(rd.sel_tape, 2);
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[5], "> PROC.HTP     Operating procedures");
    for (i = 0; i < HF_ROWS; i++) {
        CHECK(strlen(lines[i]) <= HF_LINE_MAX);
    }
}

TEST(reader_load_failure_stays)
{
    build_n(3, 10);
    img[img_size - 1] ^= 1;
    hf_reader_init(&rd, tapes, 3);
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_LOAD);
    CHECK_EQ(hf_reader_loaded(&rd, img, img_size), HF_E_CRC);
    CHECK_EQ(rd.screen, HF_SCR_TAPES);
    CHECK_EQ(hf_reader_loaded(NULL, img, img_size), HF_E_ARG);
}

TEST(reader_empty_list)
{
    CHECK_EQ(hf_reader_init(&rd, NULL, 0), HF_OK);
    ev(HF_EV_DOWN);
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_NONE);
    CHECK_EQ(hf_reader_loaded(&rd, img, img_size), HF_E_STATE);
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[1], "Holotapes in SYS$HOLO: 0");
    CHECK_STR(lines[3], "  No holotapes found in SYS$HOLO.");
    CHECK_STR(lines[4], "");
    CHECK_EQ(hf_reader_init(&rd, NULL, 1), HF_E_ARG);
    CHECK_EQ(hf_reader_init(&rd, tapes, HF_TAPES_MAX + 1), HF_E_ARG);
}

TEST(reader_scrolls_32_entries)
{
    uint32_t i;

    build_n(HF_ENTRIES_MAX, 2);
    hf_reader_init(&rd, tapes, 3);
    ev(HF_EV_ENTER);
    CHECK_EQ(hf_reader_loaded(&rd, img, img_size), HF_OK);
    ev(HF_EV_UP);               /* wraps to entry 32 */
    CHECK_EQ(rd.sel_entry, 31);
    CHECK_EQ(rd.top_entry, 7);
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[3], "  08  Entry 8");
    CHECK_STR(lines[27], "> 32  Entry 32");
    ev(HF_EV_DOWN);             /* wraps to entry 1 */
    CHECK_EQ(rd.top_entry, 0);
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[3], "> 01  Entry 1");
    CHECK_STR(lines[27], "  25  Entry 25");
    for (i = 0; i < 24; i++) {
        ev(HF_EV_DOWN);
    }
    CHECK_EQ(rd.top_entry, 0);
    ev(HF_EV_DOWN);
    CHECK_EQ(rd.sel_entry, 25);
    CHECK_EQ(rd.top_entry, 1);
}

TEST(reader_long_title_fits)
{
    static hf_tape_info_t one[1];

    memset(one, 0, sizeof(one));
    strcpy(one[0].file, "ABCDEFGH.HTP");
    memset(one[0].title, 'W', HF_TITLE_MAX);
    hf_reader_init(&rd, one, 1);
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_EQ(strlen(lines[3]), HF_LINE_MAX);
    CHECK_EQ(memcmp(lines[3], "> ABCDEFGH.HTP W", 16), 0);
    one[0].err = HF_E_OVERLAP;
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[3], "> ABCDEFGH.HTP DAMAGED: two entries share text");
    one[0].err = HF_E_UNCHECKED;
    hf_layout(&rd, lines, HF_ROWS);
    CHECK_STR(lines[3], "> ABCDEFGH.HTP NOT CHECKED: select it to try again");
}

/* A tape the scan could not read is not damaged: ENTER asks to load it. */
TEST(reader_unchecked_tape_loads)
{
    static hf_tape_info_t one[1];

    memset(one, 0, sizeof(one));
    strcpy(one[0].file, "LATER.HTP");
    one[0].err = HF_E_UNCHECKED;
    hf_reader_init(&rd, one, 1);
    ev(HF_EV_ENTER);
    CHECK_EQ(act.kind, HF_ACT_LOAD);
    CHECK_EQ(act.tape, 0);
    CHECK_STR(hf_err_name(HF_E_UNCHECKED), "unchecked");
    CHECK_STR(hf_strerror(HF_E_UNCHECKED), "tape could not be checked");
}
