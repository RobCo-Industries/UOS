#include <robco/klibc.h>

#include "holoscan.h"
#include "vfs.h"

#define ROW_SUB     1u
#define ROW_LIST    3u
#define ROW_RULE2   28u

hs_scan_t hs_scan_result(int list_rc, size_t total)
{
    switch (list_rc) {
    case VFS_OK:
        return total == 0 ? HS_SCAN_EMPTY : HS_SCAN_LIST;
    case VFS_E_NOFILES:
    case VFS_E_NOTFOUND:
        return HS_SCAN_EMPTY;
    case VFS_E_NOCARD:
        return HS_SCAN_NOCARD;
    case VFS_E_NOTREE:
    case VFS_E_DIRNF:
        return HS_SCAN_NOFOLDER;
    default:
        return HS_SCAN_ERROR;
    }
}

const char *hs_scan_name(hs_scan_t s)
{
    static const char *const names[HS_SCAN_NUM] = {
        "list", "empty", "nocard", "nofolder", "error",
    };

    return (unsigned)s < HS_SCAN_NUM ? names[s] : "unknown";
}

hs_load_t hs_load_result(int read_rc, int hf_rc)
{
    switch (read_rc) {
    case VFS_OK:
        return hf_rc == HF_OK ? HS_LOAD_OK : HS_LOAD_DAMAGED;
    case VFS_E_NOMEM:
        return HS_LOAD_NOMEM;
    case VFS_E_NOCARD:
    case VFS_E_NOTREE:
        return HS_LOAD_NOCARD;
    case VFS_E_NOTFOUND:
    case VFS_E_DIRNF:
        return HS_LOAD_GONE;
    case VFS_E_TOOBIG:
        return HS_LOAD_DAMAGED;
    default:
        return HS_LOAD_RETRY;
    }
}

int hs_read_err(int read_rc)
{
    return read_rc == VFS_E_TOOBIG ? HF_E_TOOBIG : HF_E_UNCHECKED;
}

const char *hs_load_name(hs_load_t l)
{
    static const char *const names[HS_LOAD_NUM] = {
        "ok", "nomem", "nocard", "gone", "damaged", "retry",
    };

    return (unsigned)l < HS_LOAD_NUM ? names[l] : "unknown";
}

const char *hs_load_text(hs_load_t l)
{
    static const char *const text[HS_LOAD_NUM] = {
        "",
        "NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.",
        "NO CARD: PUT THE UOS CARD BACK AND OPEN THE READER AGAIN.",
        "THIS HOLOTAPE IS NOT ON THE CARD ANY MORE.",
        "THIS HOLOTAPE IS DAMAGED AND CANNOT BE READ.",
        "THIS HOLOTAPE COULD NOT BE READ NOW. TRY AGAIN.",
    };

    return (unsigned)l < HS_LOAD_NUM ? text[l] : "";
}

void hs_loading_text(const char *file, char *buf, size_t size)
{
    k_snprintf(buf, size, "LOADING %.12s ...", file != NULL ? file : "");
}

static void put(char *line, const char *text)
{
    k_snprintf(line, HF_LINE_SIZE, "%.78s", text);
}

void hs_apply(char lines[][HF_LINE_SIZE], int on_tape_list, hs_scan_t scan, uint32_t kept,
              uint32_t total, const char *msg)
{
    if (on_tape_list) {
        if (scan == HS_SCAN_NOCARD) {
            put(lines[ROW_LIST], "  No card. Insert a UOS card to read holotapes.");
        } else if (scan == HS_SCAN_NOFOLDER) {
            put(lines[ROW_LIST], "  This card has no holotape folder, SYS$HOLO.");
        } else if (scan == HS_SCAN_ERROR) {
            put(lines[ROW_LIST], "  The holotape folder cannot be read.");
        }
        if (kept < total) {
            k_snprintf(lines[ROW_SUB], HF_LINE_SIZE, "Holotapes in SYS$HOLO: %u, first %u shown",
                       (unsigned)total, (unsigned)kept);
        }
    }
    if (msg != NULL && msg[0] != '\0') {
        k_snprintf(lines[ROW_RULE2], HF_LINE_SIZE, "** %.72s", msg);
    }
}
