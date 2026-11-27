/*
 * The fixed text of the first screen: the boot banner and the placeholder
 * screen that stands in for the login until the session task builds it.
 * Pure: src/kernel/bootlines.c.
 *
 * LORE: the banner lines are in-world flavour text, original to UOS.
 */
#ifndef UOS_BOOTLINES_H
#define UOS_BOOTLINES_H

#include <stddef.h>
#include <stdint.h>

/* Longest line any function here writes, without the NUL. */
#define UOS_LINE_MAX            79

/* Banner lines, uos_bootline(0) to uos_bootline(UOS_BOOTLINES - 1). */
#define UOS_BOOTLINES           4

/* Card states the placeholder screen shows. */
#define UOS_CARD_OK             0   /* the boot volume and its /UOS tree */
#define UOS_CARD_NONE           1   /* no card, or no volume mounts */
#define UOS_CARD_NOTREE         2   /* a volume, but no /UOS folder */
#define UOS_CARD_STATES         3
/* Mounted at boot, then the medium left. Never a boot state. */
#define UOS_CARD_GONE           3

/* What the placeholder screen shows. */
typedef struct {
    const char *name;           /* system name from UOS.CFG */
    int card;                   /* UOS_CARD_* */
    int cfg_bad;                /* bad or clamped values in UOS.CFG */
    int rtc;                    /* 1: the clock fields below are valid */
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    unsigned keys;              /* keys seen on this screen */
    const char *last_key;       /* name of the last key, NULL for none */
    const char *notice[2];      /* rows 2 and 3, NULL for the default text */
} uos_first_t;

/* Rows of the placeholder screen, uos_first_line(0) to (UOS_FIRST_ROWS - 1). */
#define UOS_FIRST_ROWS          10
/* The notice rows: what the system can do, set by the session service. */
#define UOS_FIRST_ROW_NOTICE    2
/* Rows that change: the clock and the key line. */
#define UOS_FIRST_ROW_CLOCK     6
#define UOS_FIRST_ROW_KEYS      8

/* A banner line, or "" when `i` is out of range. */
const char *uos_bootline(unsigned i);

/* The version line: "UOS KERNEL V<version>". -1 when it does not fit. */
int uos_version_line(char *buf, size_t size, const char *version);

/*
 * Writes row `row` of the placeholder screen into buf. Returns the length,
 * or -1 when `row` is out of range, an argument is NULL, or the line does
 * not fit `size` (buf then holds "").
 */
int uos_first_line(unsigned row, const uos_first_t *info, char *buf, size_t size);

/* Short name of a card state: "ok", "none", "notree", "gone", or "unknown". */
const char *uos_card_name(int card);

/*
 * The card state now: UOS_CARD_GONE when a card was found at boot and the
 * library's change count moved since the mount, else `boot_card`.
 */
int uos_card_live(int boot_card, uint32_t at_mount, uint32_t now);

#endif
