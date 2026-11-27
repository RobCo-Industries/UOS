/*
 * The six panel buttons. Pure: src/kernel/buttons.c. The boot step in
 * main.c hands the table to the library's GPIO input backend.
 *
 * The pins are fixed, those of the board support spec and of MFBootAgent.
 * UOS.CFG [input] buttons can only switch them off. Each button is active
 * low with a pull-up and a 20 ms debounce.
 */
#ifndef UOS_BUTTONS_H
#define UOS_BUTTONS_H

#include <stdint.h>

#define UOS_BUTTONS_COUNT       6
#define UOS_BUTTONS_DEBOUNCE_MS 20
#define UOS_BUTTONS_WAIT_TICKS  30      /* longest wait for the stuck-line guard */
#define UOS_BUTTONS_REPEAT_MS   500     /* first repeat of a held button */
#define UOS_BUTTONS_RATE_MS     150     /* then one repeat every */

typedef struct {
    unsigned pin;           /* BCM GPIO number */
    unsigned code;          /* INPUT_KEY_* */
} uos_button_t;

/* The six buttons, in the order UP, DOWN, LEFT, RIGHT, SELECT, BACK. */
extern const uos_button_t uos_buttons[UOS_BUTTONS_COUNT];

/* Bit n set for each button pin n. */
uint32_t uos_buttons_mask(void);

/* The boot step's results. */
#define UOS_BUTTONS_ACTIVE      0
#define UOS_BUTTONS_STUCK       1
#define UOS_BUTTONS_OFF         2
#define UOS_BUTTONS_ERROR       3
#define UOS_BUTTONS_CHECKING    4

/*
 * The result from the setting, the first failing library code `rc` of the
 * adds and the start (0 when none failed), and input_gpio_status() after
 * the wait. A setting of 0 gives OFF whatever the rest.
 */
int uos_buttons_result(int setting, int rc, int status);

/* "active", "disabled_stuck", "off", "error", "checking"; "unknown" otherwise. */
const char *uos_buttons_result_name(int result);

#endif
