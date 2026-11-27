/* Host tests of src/kernel/buttons.c: the six button pins and the boot step's result. */

#include "rc_test.h"

#include <robco/input.h>

#include "dev.h"
#include "uos_buttons.h"

TEST(six_buttons_in_order)
{
    static const unsigned pins[] = { 5, 6, 17, 22, 26, 27 };
    static const unsigned codes[] = { INPUT_KEY_UP, INPUT_KEY_DOWN, INPUT_KEY_LEFT,
                                      INPUT_KEY_RIGHT, INPUT_KEY_SELECT, INPUT_KEY_BACK };
    unsigned i;

    CHECK_EQ(UOS_BUTTONS_COUNT, 6);
    for (i = 0; i < UOS_BUTTONS_COUNT; i++) {
        CHECK_EQ(uos_buttons[i].pin, pins[i]);
        CHECK_EQ(uos_buttons[i].code, codes[i]);
    }
}

TEST(mask_is_the_refused_button_pins)
{
    CHECK_EQ(uos_buttons_mask(), (uint32_t)DV_PINS_BUTTONS);
    CHECK_EQ(uos_buttons_mask() & (uint32_t)DV_PINS_FIXED, 0u);
    CHECK_EQ(uos_buttons_mask() & ~(uint32_t)DV_REFUSED_UOS, 0u);
    CHECK_EQ(uos_buttons_mask() & ((1u << 16) | (1u << 23) | (1u << 24)), 0u);
}

TEST(result_follows_the_table)
{
    CHECK_EQ(uos_buttons_result(0, 0, INPUT_GPIO_OFF), UOS_BUTTONS_OFF);
    CHECK_EQ(uos_buttons_result(0, INPUT_ERR_STATE, INPUT_GPIO_ACTIVE), UOS_BUTTONS_OFF);
    CHECK_EQ(uos_buttons_result(1, INPUT_ERR_SPACE, INPUT_GPIO_OFF), UOS_BUTTONS_ERROR);
    CHECK_EQ(uos_buttons_result(1, INPUT_ERR_TICK, INPUT_GPIO_CHECKING), UOS_BUTTONS_ERROR);
    CHECK_EQ(uos_buttons_result(1, 0, INPUT_GPIO_ACTIVE), UOS_BUTTONS_ACTIVE);
    CHECK_EQ(uos_buttons_result(1, 0, INPUT_GPIO_DISABLED_STUCK), UOS_BUTTONS_STUCK);
    CHECK_EQ(uos_buttons_result(1, 0, INPUT_GPIO_CHECKING), UOS_BUTTONS_CHECKING);
    CHECK_EQ(uos_buttons_result(1, 0, INPUT_GPIO_OFF), UOS_BUTTONS_ERROR);
}

TEST(every_result_name)
{
    CHECK_STR(uos_buttons_result_name(UOS_BUTTONS_ACTIVE), "active");
    CHECK_STR(uos_buttons_result_name(UOS_BUTTONS_STUCK), "disabled_stuck");
    CHECK_STR(uos_buttons_result_name(UOS_BUTTONS_OFF), "off");
    CHECK_STR(uos_buttons_result_name(UOS_BUTTONS_ERROR), "error");
    CHECK_STR(uos_buttons_result_name(UOS_BUTTONS_CHECKING), "checking");
    CHECK_STR(uos_buttons_result_name(5), "unknown");
    CHECK_STR(uos_buttons_result_name(-1), "unknown");
}
