/*
 * UOS: the third stage of the terminal chain.
 */

#include <robco/console.h>
#include <robco/fb.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "bootlines.h"

void robco_main(void)
{
    unsigned i;

    uart_init(115200);
    k_printf("\n@RC stage=" ROBCO_STAGE_NAME " ev=boot version=" UOS_VERSION_STR "\n");
    if (fb_init(640, 480, 32) == 0 && console_init(CONSOLE_SCHEME_GREEN) == 0) {
        for (i = 0; i < UOS_BOOTLINES; i++) {
            console_puts(uos_bootline(i));
            console_putc('\n');
        }
    }
    for (;;) {
        __asm__ volatile("wfi");
    }
}
