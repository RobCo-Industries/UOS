#ifndef UOS_DEVS_H
#define UOS_DEVS_H

/*
 * The device service: DEVICES.CFG applied to the GPIO pins. Other
 * services call these functions; devglue.c holds them.
 *
 * Context: every function runs in the loop's context, which includes a
 * maintenance command between two events. None blocks: each one does at
 * most two GPIO register writes. None may be called from an IRQ or the
 * panic path. Before the service's init, or when it has no devices, they
 * answer DEVS_ERR_DOWN or a count of 0; they never fault.
 *
 * Codes are DV_ERR_* of dev.h and DEVS_ERR_* of devsplan.h; devs_message()
 * gives the "%DEVICE-..." text of each.
 */

#include <stddef.h>
#include <stdint.h>

#include "devsplan.h"

/*
 * Runs `action` ("UNLOCK", "toggle"...) on device `name` (any case) for a
 * caller with privilege bits `privs`. `by` is one word naming the source,
 * such as a user name or "dev"; it goes in the marker. On success prints
 * "ev=dev name=<n> state=<word> by=<by>". `*text`, when not NULL, gets a
 * static string: the new state word, or the error message. Returns 0 or
 * a code. A refused command changes nothing.
 */
int devs_command(const char *name, const char *action, uint32_t privs, const char *by,
                 const char **text);

/* Devices loaded: the rows of SHOW DEVICES. */
unsigned devs_count(void);

/*
 * Errors in DEVICES.CFG: each one has a deverr marker. 1 when the file
 * exists but could not be read. 0 when it is missing.
 */
unsigned devs_cfg_errors(void);

/* 1 when DEVICES.CFG exists but could not be read, else 0. */
int devs_cfg_unreadable(void);

/* The SHOW DEVICES header line. Length, or DV_ERR_SPACE. Caller's buffer. */
int devs_status_header(char *buf, size_t size);

/* Row `index` of SHOW DEVICES, 0 to devs_count() - 1. Length or a code. */
int devs_status_line(unsigned index, char *buf, size_t size);

/* The text of a code, static. */
const char *devs_message(int code);

/*
 * DEV: pseudo-files, for the file system service.
 *   read:  the device's status line into `buf`; length or a code.
 *   write: `text` is one command word, as devs_command() with by "dev".
 *          Leading and trailing blanks and line ends are ignored. 0 or a code.
 *   list:  the name of device `index` into `buf`; length, or
 *          DV_ERR_NOSUCHDEV past the last one.
 * Same context rules as above. The buffers are the caller's.
 */
typedef struct {
    int (*read)(const char *name, char *buf, size_t size);
    int (*write)(const char *name, const char *text, uint32_t privs);
    int (*list)(unsigned index, char *buf, size_t size);
} devs_hooks_t;

/* The three DEV: functions. Static, valid for good. */
const devs_hooks_t *devs_hooks(void);

#endif
