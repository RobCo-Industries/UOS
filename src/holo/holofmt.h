#ifndef UOS_HOLOFMT_H
#define UOS_HOLOFMT_H

/*
 * Data holotapes: validation, accessors and the reader model.
 * See docs/modules/holo.md for the file layout.
 *
 * A tape is a sealed rimg, magic ROBCO78, subtype 2 (data), with the META
 * block. The payload after META is an HTXT archive of text entries.
 * Everything here is pure: the caller reads the file and owns all memory.
 */

#include <stddef.h>
#include <stdint.h>

/* rimg layout of a data tape. */
#define HF_PAYLOAD_OFFSET   0x120u      /* stub 0x20 + header 0x40 + META 0xC0 */
#define HF_SUBTYPE_DATA     2u
#define HF_LOAD_ADDR        0u          /* data tapes never run: load_addr is 0 */
#define HF_ABI_MAJOR        1u
#define HF_STUB_WORD        0xEAFFFFFEu /* ARM "b ." in word 0, rest of the stub 0 */

/* HTXT archive. */
#define HF_ARC_MAGIC        "HTXT"
#define HF_ARC_VERSION      1u
#define HF_ARC_HDR_SIZE     8u          /* magic 4, version u16, count u16 */
#define HF_ENTRY_SIZE       40u         /* name 32, offset u32, length u32 */
#define HF_NAME_FIELD       32u
#define HF_NAME_MAX         31u         /* characters; the field always ends in NUL */
#define HF_TITLE_FIELD      64u
#define HF_TITLE_MAX        63u
#define HF_ENTRIES_MAX      32u
#define HF_ENTRY_TEXT_MAX   65536u
/* Largest tape file the reader accepts: 2098728 bytes. The caller needs a
 * buffer this large from the kernel allocator to read any valid tape. */
#define HF_TAPE_MAX         (HF_PAYLOAD_OFFSET + HF_ARC_HDR_SIZE + \
                             HF_ENTRIES_MAX * HF_ENTRY_SIZE +      \
                             HF_ENTRIES_MAX * HF_ENTRY_TEXT_MAX)

typedef enum hf_err {
    HF_OK = 0,
    HF_E_ARG,       /* NULL pointer or bad argument */
    HF_E_TOOBIG,    /* file larger than HF_TAPE_MAX */
    HF_E_IO,        /* fewer bytes than the rimg header (rimg step 0) */
    HF_E_MAGIC,     /* rimg step 1 */
    HF_E_VERSION,   /* rimg step 2 */
    HF_E_SIZE,      /* rimg step 3 */
    HF_E_SUBTYPE,   /* subtype is not 2 */
    HF_E_BOUNDS,    /* load_addr != 0 or mem_size != file_size */
    HF_E_ABI,       /* ABI major != 1 */
    HF_E_CRC,       /* rimg step 6 */
    HF_E_TITLE,     /* META title empty, unterminated or not printable */
    HF_E_ARCSHORT,  /* payload shorter than the archive header */
    HF_E_ARCMAGIC,
    HF_E_ARCVER,
    HF_E_COUNT,     /* count not 1 to 32 */
    HF_E_TABLE,     /* entry table runs past the payload */
    HF_E_NAME,      /* entry name empty, unterminated, bad padding, not printable */
    HF_E_LENGTH,    /* entry longer than 64 KiB */
    HF_E_RANGE,     /* body outside the payload or inside the table */
    HF_E_OVERLAP,   /* two bodies share a byte */
    HF_E_INDEX,     /* no such entry or tape */
    HF_E_BUF,       /* caller buffer too small */
    HF_E_STATE,     /* call not valid on the current screen */
    HF_E_UNCHECKED, /* the caller could not read the tape: not known to be damaged */
    HF_E_COUNT_OF
} hf_err_t;

/* Short name of a code, such as "crc". "unknown" for others. */
const char *hf_err_name(int code);
/* One line of text for a code, at most HF_REASON_MAX characters. */
const char *hf_strerror(int code);
#define HF_REASON_MAX 48u

/* A validated tape. Points into the caller's image; valid while it lives. */
typedef struct hf_tape {
    const uint8_t *image;
    uint32_t size;
    const uint8_t *payload;
    uint32_t payload_len;
    uint32_t count;
} hf_tape_t;

/*
 * Validates a whole tape file of `size` bytes in memory, in this order:
 * size limit, rimg steps 1 to 3 through rimg_check_header(), subtype,
 * load address and memory size, ABI, CRC through rimg_crc(), META title,
 * then the archive: header, count, table, each entry in order (name,
 * length, range), then overlaps. Returns the first failure. On HF_OK
 * fills *t. Body bytes are not checked: see hf_display_char().
 */
int hf_open(hf_tape_t *t, const void *image, uint32_t size);

/* The tape title from META, NUL-terminated, 1 to 63 characters. */
const char *hf_title(const hf_tape_t *t);
/* Number of entries, 1 to 32. */
uint32_t hf_count(const hf_tape_t *t);
/* Entry `idx`'s name, NUL-terminated, 1 to 31 characters. NULL if out of range. */
const char *hf_entry_name(const hf_tape_t *t, uint32_t idx);
/* Entry `idx`'s text: not NUL-terminated. HF_E_INDEX if out of range. */
int hf_entry_text(const hf_tape_t *t, uint32_t idx, const char **text, uint32_t *len);

/*
 * The character to draw for a body byte. Printable ASCII, tab, CR and LF
 * stay as they are; any other byte shows as '.'. This is not an error.
 */
char hf_display_char(uint8_t c);

/* ---- Reader model ---- */

#define HF_COLS         80u
#define HF_ROWS         30u
#define HF_LINE_MAX     78u
#define HF_LINE_SIZE    (HF_LINE_MAX + 1u)
#define HF_LIST_ROWS    25u         /* rows 3 to 27 */
#define HF_TAPES_MAX    32u         /* tapes the list holds */
#define HF_FILE_MAX     12u         /* 8.3 name */

/*
 * A tape name as a person types it: a stem of 1 to 8 of A-Z 0-9 $ _ -, any
 * case, then nothing or ".HTP". Writes the file name the tape list holds,
 * "STEM.HTP", and returns 0; returns -1 for any other name and writes "".
 * A longer name is refused, never cut.
 */
int hf_tape_name(const char *in, char out[HF_FILE_MAX + 1]);

typedef struct hf_tape_info {
    char file[HF_FILE_MAX + 1];     /* "TECHLOG.HTP" */
    char title[HF_TITLE_MAX + 1];   /* from hf_title() when err is HF_OK */
    int err;                        /* HF_OK, HF_E_UNCHECKED, or why the tape is damaged */
} hf_tape_info_t;

typedef enum hf_screen {
    HF_SCR_TAPES = 0,
    HF_SCR_ENTRIES,
    HF_SCR_READING,
} hf_screen_t;

typedef enum hf_event {
    HF_EV_UP = 0,
    HF_EV_DOWN,
    HF_EV_ENTER,    /* ENTER or SELECT */
    HF_EV_BACK,
} hf_event_t;

typedef enum hf_act_kind {
    HF_ACT_NONE = 0,
    HF_ACT_LOAD,    /* read tapes[tape].file and call hf_reader_loaded() (good or unchecked) */
    HF_ACT_REFUSED, /* the selected tape is damaged; nothing opens */
    HF_ACT_SHOW,    /* show text/len with title in the pager, then send BACK */
    HF_ACT_CLOSE,   /* the open tape closed; its buffer may be freed */
    HF_ACT_EXIT,    /* leave the reader */
} hf_act_kind_t;

typedef struct hf_action {
    hf_act_kind_t kind;
    uint32_t tape;          /* LOAD, REFUSED: index into the tape list */
    const char *title;      /* SHOW: entry name */
    const char *text;       /* SHOW */
    uint32_t len;           /* SHOW */
} hf_action_t;

typedef struct hf_reader {
    const hf_tape_info_t *tapes;
    uint32_t ntapes;
    hf_screen_t screen;
    uint32_t sel_tape, top_tape;
    uint32_t sel_entry, top_entry;
    uint32_t open_tape;
    hf_tape_t tape;
} hf_reader_t;

/* Starts on the tape list. HF_E_ARG if ntapes > HF_TAPES_MAX or tapes is NULL with ntapes > 0. */
int hf_reader_init(hf_reader_t *r, const hf_tape_info_t *tapes, uint32_t ntapes);

/*
 * Applies one event and fills *act. UP and DOWN wrap. ENTER on the tape
 * list asks to load a good or unchecked tape (HF_E_UNCHECKED: the scan
 * could not read it, so the load tries again) or refuses a damaged one;
 * ENTER on the
 * entry list shows an entry. BACK goes reading -> entries -> tapes -> exit.
 * Events on an empty list do nothing. Events while reading other than
 * BACK do nothing.
 */
int hf_reader_event(hf_reader_t *r, hf_event_t ev, hf_action_t *act);

/*
 * The caller read the tape asked for by HF_ACT_LOAD. Validates it; on
 * HF_OK opens the entry list. On an error the model stays on the tape
 * list and returns the code: the caller should set that tape's err.
 */
int hf_reader_loaded(hf_reader_t *r, const void *image, uint32_t size);

/*
 * Fills HF_ROWS lines of at most HF_LINE_MAX characters for the tape list
 * or the entry list. `nlines` below HF_ROWS gives HF_E_BUF and writes
 * nothing. While reading gives HF_E_STATE: the pager draws that screen.
 */
int hf_layout(const hf_reader_t *r, char lines[][HF_LINE_SIZE], uint32_t nlines);

#endif
