/* Data holotape validation and accessors. Pure: works on a tape in memory. */

#include <stddef.h>
#include <stdint.h>

#include <robco/klibc.h>
#include <robco/rimg.h>

#include "holofmt.h"

static const char *const err_names[HF_E_COUNT_OF] = {
    "ok", "arg", "toobig", "io", "magic", "version", "size", "subtype", "bounds",
    "abi", "crc", "title", "arcshort", "arcmagic", "arcver", "count", "table",
    "name", "length", "range", "overlap", "index", "buf", "state", "unchecked",
};

static const char *const err_text[HF_E_COUNT_OF] = {
    "no error",
    "bad argument",
    "tape file is too large",
    "tape file is too short",
    "not a holotape",
    "unsupported tape header",
    "tape size does not match the file",
    "not a data tape",
    "bad load address or memory size",
    "unsupported tape version",
    "tape is damaged",
    "tape title is missing or not plain text",
    "tape holds no archive",
    "archive magic is not HTXT",
    "unsupported archive version",
    "entry count is not 1 to 32",
    "entry table runs past the tape end",
    "entry name is missing or not plain text",
    "entry is longer than 64 KiB",
    "entry text lies outside the tape",
    "two entries share text",
    "no such entry",
    "buffer too small",
    "not valid on this screen",
    "tape could not be checked",
};

const char *hf_err_name(int code)
{
    if (code < 0 || code >= HF_E_COUNT_OF) {
        return "unknown";
    }
    return err_names[code];
}

const char *hf_strerror(int code)
{
    if (code < 0 || code >= HF_E_COUNT_OF) {
        return "unknown error";
    }
    return err_text[code];
}

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int printable(uint8_t c)
{
    return c >= 0x20 && c <= 0x7e;
}

/* 1 to `max` printable characters, then NUL to the end of the field. */
static int text_field_ok(const uint8_t *f, uint32_t field, uint32_t max)
{
    uint32_t i = 0;

    while (i < field && f[i] != 0) {
        if (!printable(f[i])) {
            return 0;
        }
        i++;
    }
    if (i == 0 || i > max) {
        return 0;
    }
    for (; i < field; i++) {
        if (f[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int map_rimg(int rc)
{
    switch (rc) {
    case RIMG_E_IO:
        return HF_E_IO;
    case RIMG_E_MAGIC:
        return HF_E_MAGIC;
    case RIMG_E_VERSION:
        return HF_E_VERSION;
    case RIMG_E_SIZE:
        return HF_E_SIZE;
    default:
        return HF_OK; /* steps 4 and 5 are checked here for data tapes */
    }
}

static const uint8_t *entry_at(const uint8_t *payload, uint32_t i)
{
    return payload + HF_ARC_HDR_SIZE + i * HF_ENTRY_SIZE;
}

int hf_open(hf_tape_t *t, const void *image, uint32_t size)
{
    const rimg_expect_t expect = { RIMG_MAGIC_ROBCO78, 0, 0, 0, size };
    const uint8_t *img = image;
    const rimg_header_t *h;
    const uint8_t *pl;
    uint32_t plen, count, table_end, i, j;
    int rc;

    if (t == NULL || image == NULL) {
        return HF_E_ARG;
    }
    if (size > HF_TAPE_MAX) {
        return HF_E_TOOBIG;
    }
    /* rimg steps 1 to 3 from the library. Data tapes have no slot, so the
     * library always stops them at step 4: steps 4 and 5 follow here. */
    rc = map_rimg(rimg_check_header(img, size, &expect));
    if (rc != HF_OK) {
        return rc;
    }
    h = (const rimg_header_t *)(const void *)(img + RIMG_HDR_OFFSET);
    if (rd32((const uint8_t *)&h->subtype) != HF_SUBTYPE_DATA) {
        return HF_E_SUBTYPE;
    }
    if (rd32((const uint8_t *)&h->load_addr) != HF_LOAD_ADDR ||
        rd32((const uint8_t *)&h->mem_size) != size) {
        return HF_E_BOUNDS;
    }
    if ((rd32((const uint8_t *)&h->abi_version) >> 16) != HF_ABI_MAJOR) {
        return HF_E_ABI;
    }
    if (rimg_crc(img, size) != rd32((const uint8_t *)&h->crc32)) {
        return HF_E_CRC;
    }
    if (!text_field_ok(img + RIMG_META_OFFSET, HF_TITLE_FIELD, HF_TITLE_MAX)) {
        return HF_E_TITLE;
    }

    /* The archive. Step 3 guarantees size >= HF_PAYLOAD_OFFSET. */
    pl = img + HF_PAYLOAD_OFFSET;
    plen = size - HF_PAYLOAD_OFFSET;
    if (plen < HF_ARC_HDR_SIZE) {
        return HF_E_ARCSHORT;
    }
    if (k_memcmp(pl, HF_ARC_MAGIC, 4) != 0) {
        return HF_E_ARCMAGIC;
    }
    if (rd16(pl + 4) != HF_ARC_VERSION) {
        return HF_E_ARCVER;
    }
    count = rd16(pl + 6);
    if (count < 1 || count > HF_ENTRIES_MAX) {
        return HF_E_COUNT;
    }
    table_end = HF_ARC_HDR_SIZE + count * HF_ENTRY_SIZE;
    if (table_end > plen) {
        return HF_E_TABLE;
    }
    for (i = 0; i < count; i++) {
        const uint8_t *e = entry_at(pl, i);
        uint32_t off = rd32(e + HF_NAME_FIELD);
        uint32_t len = rd32(e + HF_NAME_FIELD + 4);

        if (!text_field_ok(e, HF_NAME_FIELD, HF_NAME_MAX)) {
            return HF_E_NAME;
        }
        if (len > HF_ENTRY_TEXT_MAX) {
            return HF_E_LENGTH;
        }
        if (off < table_end || off > plen || len > plen - off) {
            return HF_E_RANGE;
        }
    }
    for (i = 0; i < count; i++) {
        uint32_t ao = rd32(entry_at(pl, i) + HF_NAME_FIELD);
        uint32_t al = rd32(entry_at(pl, i) + HF_NAME_FIELD + 4);

        for (j = i + 1; j < count; j++) {
            uint32_t bo = rd32(entry_at(pl, j) + HF_NAME_FIELD);
            uint32_t bl = rd32(entry_at(pl, j) + HF_NAME_FIELD + 4);

            if (al != 0 && bl != 0 && ao < bo + bl && bo < ao + al) {
                return HF_E_OVERLAP;
            }
        }
    }

    t->image = img;
    t->size = size;
    t->payload = pl;
    t->payload_len = plen;
    t->count = count;
    return HF_OK;
}

const char *hf_title(const hf_tape_t *t)
{
    if (t == NULL || t->image == NULL) {
        return NULL;
    }
    return (const char *)(t->image + RIMG_META_OFFSET);
}

uint32_t hf_count(const hf_tape_t *t)
{
    return t == NULL ? 0 : t->count;
}

const char *hf_entry_name(const hf_tape_t *t, uint32_t idx)
{
    if (t == NULL || t->payload == NULL || idx >= t->count) {
        return NULL;
    }
    return (const char *)entry_at(t->payload, idx);
}

int hf_entry_text(const hf_tape_t *t, uint32_t idx, const char **text, uint32_t *len)
{
    const uint8_t *e;

    if (t == NULL || t->payload == NULL || text == NULL || len == NULL) {
        return HF_E_ARG;
    }
    if (idx >= t->count) {
        return HF_E_INDEX;
    }
    e = entry_at(t->payload, idx);
    *text = (const char *)(t->payload + rd32(e + HF_NAME_FIELD));
    *len = rd32(e + HF_NAME_FIELD + 4);
    return HF_OK;
}

char hf_display_char(uint8_t c)
{
    if (printable(c) || c == '\t' || c == '\r' || c == '\n') {
        return (char)c;
    }
    return '.';
}
