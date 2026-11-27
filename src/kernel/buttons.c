/* The six panel buttons and the boot step's result. Pure. */

#include <stddef.h>

#include <robco/input.h>

#include "uos_buttons.h"

const uos_button_t uos_buttons[UOS_BUTTONS_COUNT] = {
    { 5, INPUT_KEY_UP },
    { 6, INPUT_KEY_DOWN },
    { 17, INPUT_KEY_LEFT },
    { 22, INPUT_KEY_RIGHT },
    { 26, INPUT_KEY_SELECT },
    { 27, INPUT_KEY_BACK },
};

static const char *const names[] = { "active", "disabled_stuck", "off", "error", "checking" };

uint32_t uos_buttons_mask(void)
{
    uint32_t m = 0;
    size_t i;

    for (i = 0; i < UOS_BUTTONS_COUNT; i++) {
        m |= 1u << uos_buttons[i].pin;
    }
    return m;
}

int uos_buttons_result(int setting, int rc, int status)
{
    if (!setting) {
        return UOS_BUTTONS_OFF;
    }
    if (rc != 0) {
        return UOS_BUTTONS_ERROR;
    }
    switch (status) {
    case INPUT_GPIO_ACTIVE:
        return UOS_BUTTONS_ACTIVE;
    case INPUT_GPIO_DISABLED_STUCK:
        return UOS_BUTTONS_STUCK;
    case INPUT_GPIO_CHECKING:
        return UOS_BUTTONS_CHECKING;
    default:
        return UOS_BUTTONS_ERROR;
    }
}

const char *uos_buttons_result_name(int result)
{
    if (result < 0 || (size_t)result >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[result];
}
