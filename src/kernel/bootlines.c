/*
 * The fixed text of the first screen. Pure.
 *
 * LORE: the banner is in-world flavour text, original to UOS.
 */

#include <robco/klibc.h>

#include "bootlines.h"

static const char *const banner[UOS_BOOTLINES] = {
    "ROBCO UNIFIED OPERATING SYSTEM",
    "PROPERTY OF ROBCO INDUSTRIES. UNAUTHORISED USE IS LOGGED.",
    "KERNEL SERVICES ONLINE. TERMINAL LINK ESTABLISHED.",
    "STAND BY FOR OPERATOR ACCESS.",
};

const char *uos_bootline(unsigned i)
{
    return i < UOS_BOOTLINES ? banner[i] : "";
}

/* Returns n when it fits, else empties buf and returns -1. */
static int fit(char *buf, size_t size, int n)
{
    if (n < 0 || (size_t)n >= size || n > UOS_LINE_MAX) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    return n;
}

int uos_version_line(char *buf, size_t size, const char *version)
{
    if (buf == NULL || version == NULL) {
        return -1;
    }
    return fit(buf, size, k_snprintf(buf, size, "UOS KERNEL V%s", version));
}

const char *uos_card_name(int card)
{
    switch (card) {
    case UOS_CARD_OK:     return "ok";
    case UOS_CARD_NONE:   return "none";
    case UOS_CARD_NOTREE: return "notree";
    default:              return "unknown";
    }
}

static const char *card_text(int card)
{
    switch (card) {
    case UOS_CARD_OK:     return "CARD: READY";
    case UOS_CARD_NONE:   return "CARD: NO CARD. INSERT A CARD AND RESTART.";
    case UOS_CARD_NOTREE: return "CARD: NO /UOS FOLDER. SYSTEM FILES ARE MISSING.";
    default:              return "CARD: UNKNOWN STATE";
    }
}

int uos_first_line(unsigned row, const uos_first_t *info, char *buf, size_t size)
{
    int n;

    if (buf == NULL || info == NULL || info->name == NULL) {
        if (buf != NULL && size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    switch (row) {
    case 0:
        n = k_snprintf(buf, size, "%s", info->name);
        break;
    case 1:
        n = k_snprintf(buf, size, "%s", "------------------------------------------------");
        break;
    case UOS_FIRST_ROW_NOTICE:
        n = k_snprintf(buf, size, "%s", info->notice[0] != NULL ? info->notice[0] :
                       "LOGIN IS NOT AVAILABLE.");
        break;
    case UOS_FIRST_ROW_NOTICE + 1:
        n = k_snprintf(buf, size, "%s", info->notice[1] != NULL ? info->notice[1] :
                       "THIS TERMINAL RUNS THE KERNEL BASE ONLY.");
        break;
    case 4:
        n = k_snprintf(buf, size, "%s", card_text(info->card));
        break;
    case 5:
        n = k_snprintf(buf, size, "%s", info->cfg_bad ? "SETTINGS: SOME VALUES IGNORED" :
                       "SETTINGS: OK");
        break;
    case UOS_FIRST_ROW_CLOCK:
        if (info->rtc) {
            n = k_snprintf(buf, size, "CLOCK: %04u-%02u-%02u %02u:%02u:%02u",
                           (unsigned)info->year, (unsigned)info->month, (unsigned)info->day,
                           (unsigned)info->hour, (unsigned)info->minute,
                           (unsigned)info->second);
        } else {
            n = k_snprintf(buf, size, "%s", "CLOCK: NO CLOCK FITTED");
        }
        break;
    case 7:
        n = k_snprintf(buf, size, "%s", "PRESS ANY KEY TO TEST THE INPUT.");
        break;
    case UOS_FIRST_ROW_KEYS:
        if (info->keys == 0 || info->last_key == NULL) {
            n = k_snprintf(buf, size, "%s", "KEYS: NONE YET");
        } else {
            n = k_snprintf(buf, size, "KEYS: %u, LAST %s", info->keys, info->last_key);
        }
        break;
    case 9:
        n = k_snprintf(buf, size, "%s", ">");
        break;
    default:
        n = -1;
        break;
    }
    return fit(buf, size, n);
}
