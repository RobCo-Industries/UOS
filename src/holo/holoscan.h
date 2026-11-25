/*
 * The holotape reader screen's own decisions, pure, prefix hs_: what the
 * folder scan found, the texts the screen adds to the model's layout, and
 * the words of the markers. The model itself is holofmt.h; the screen is
 * holoscr.c.
 */
#ifndef UOS_HOLOSCAN_H
#define UOS_HOLOSCAN_H

#include <stddef.h>
#include <stdint.h>

#include "holofmt.h"

/* What the scan of SYS$HOLO found. */
typedef enum hs_scan {
    HS_SCAN_LIST = 0,       /* one tape or more */
    HS_SCAN_EMPTY,          /* the folder holds no tape */
    HS_SCAN_NOCARD,         /* no card mounted */
    HS_SCAN_NOFOLDER,       /* a card, but no /UOS or no /UOS/HOLO */
    HS_SCAN_ERROR,          /* the listing failed for another reason */
    HS_SCAN_NUM
} hs_scan_t;

/* The scan result for a listing's return code (VFS_OK or VFS_E_*) and
 * the files it matched. */
hs_scan_t hs_scan_result(int list_rc, size_t total);

/* "list", "empty", "nocard", "nofolder", "error". */
const char *hs_scan_name(hs_scan_t s);

/* What loading a tape gave. */
typedef enum hs_load {
    HS_LOAD_OK = 0,
    HS_LOAD_NOMEM,          /* the heap has no room for the tape */
    HS_LOAD_NOCARD,         /* the card went away */
    HS_LOAD_GONE,           /* the file is not there any more */
    HS_LOAD_DAMAGED,        /* a file too large, or hf_open() refused it */
    HS_LOAD_RETRY,          /* the read failed for a reason that is not the tape's bytes */
    HS_LOAD_NUM
} hs_load_t;

/* The load result for a read's return code and, when the read worked,
 * hf_open()'s code. A damaged tape is marked damaged in the list; the
 * others leave the list as it is. */
hs_load_t hs_load_result(int read_rc, int hf_rc);

/* The tape's error code when a scan or load read fails before
 * hf_open() runs. Only VFS_E_TOOBIG says something about the tape itself:
 * HF_E_TOOBIG. Every other code (no memory, card busy or gone, file gone,
 * a read error that may pass, no access) gives HF_E_UNCHECKED: the list
 * shows the tape as not checked and opening it reads it again. */
int hs_read_err(int read_rc);

/* "ok", "nomem", "nocard", "gone", "damaged", "retry". */
const char *hs_load_name(hs_load_t l);

/*
 * Applies the screen's own texts to the model's 30 lines from hf_layout():
 *   - for HS_SCAN_NOCARD, NOFOLDER and ERROR, line 3 says why there is no
 *     list in place of the model's empty-list line;
 *   - when the scan kept fewer tapes than it found, line 1 says so;
 *   - a message, when `msg` is not empty, replaces the rule of line 28.
 * `kept` and `total` are the tapes in the list and the tapes found.
 */
void hs_apply(char lines[][HF_LINE_SIZE], int on_tape_list, hs_scan_t scan, uint32_t kept,
              uint32_t total, const char *msg);

/* The message row text for a load result, "" for HS_LOAD_OK. */
const char *hs_load_text(hs_load_t l);

/* The message row while a tape loads: "LOADING NAME.HTP ...". */
void hs_loading_text(const char *file, char *buf, size_t size);

/* A key for the tape and entry lists. UP and DOWN move on a press or a
 * repeat. SELECT and RIGHT enter, BACK and LEFT go back: press only, so a
 * held key does one step. Returns 1 and sets *out, or 0 for no action. */
int hs_key(unsigned code, int32_t value, hf_event_t *out);

#endif
