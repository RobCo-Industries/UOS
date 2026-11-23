/*
 * The input layer and the maintenance port. The port is the mini UART
 * script channel: in QEMU it is the second serial port. On hardware the
 * mini UART reaches no pin, see BOARD_SUPPORT.md in RobCo-Core. It is for
 * tests and bring-up, not for users.
 *
 * Commands, besides the library's built-in ones (key, char, text, dial):
 *   state          one line: phase, screen, user, card, last error
 *   dump           the console cells, through console_dump()
 *   type <words>   the words as characters, then SELECT: one typed line
 *   timers         the live timers, one line each after a count line
 *   reboot         restarts the board through the watchdog
 * Test builds only (UOS_TEST_HOOKS=1):
 *   fault undef|pabt   runs an undefined instruction or a breakpoint
 *   panic              calls uos_panic()
 * Every answer line is complete; the port then says "ok" or "err <why>".
 */

#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/power.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "uos.h"

static input_script_reply_t *dump_reply;

static void dump_char(char c)
{
    char s[2] = { c, '\0' };

    dump_reply->write(s);
}

static const char *phase_name(int phase)
{
    switch (phase) {
    case UOS_PHASE_INIT:  return "init";
    case UOS_PHASE_READY: return "ready";
    case UOS_PHASE_PANIC: return "panic";
    default:              return "unknown";
    }
}

static void state_line(char *buf, unsigned size)
{
    const uos_screen_t *top = uos.phase == UOS_PHASE_PANIC ? NULL : uos_screen_top();

    k_snprintf(buf, size, "phase=%s screen=%s user=%s card=%s trusted=%d error=%s",
               phase_name(uos.phase), top != NULL && top->name != NULL ? top->name : "none",
               uos.user[0] != '\0' ? uos.user : "none", uos_card_name(uos.card), uos.trusted,
               uos.error[0] != '\0' ? uos.error : "none");
}

static void push_char(char c)
{
    input_event_t ev = { .type = INPUT_EV_CHAR, .source = INPUT_SRC_SCRIPT,
                         .code = (uint8_t)c };

    (void)input_push(&ev);
}

static int cmd_type(int argc, char *argv[], input_script_reply_t *reply)
{
    input_event_t key = { .type = INPUT_EV_KEY, .source = INPUT_SRC_SCRIPT,
                          .code = INPUT_KEY_SELECT, .value = INPUT_PRESS };
    unsigned n = 2;
    int i;
    const char *p;

    for (i = 1; i < argc; i++) {
        n += (unsigned)k_strlen(argv[i]) * 2u + 2u;
    }
    /* Each character and the key: a press and maybe a release in the queue. */
    if (!input_room(n)) {
        reply->reason = "full";
        return -1;
    }
    for (i = 1; i < argc; i++) {
        if (i > 1) {
            push_char(' ');
        }
        for (p = argv[i]; *p != '\0'; p++) {
            push_char(*p);
        }
    }
    (void)input_push(&key);
    key.value = INPUT_RELEASE;
    (void)input_push(&key);
    return 0;
}

static int cmd_timers(input_script_reply_t *reply)
{
    const uos_timerq_t *tq = uos_timer_table();
    uint32_t now = uos_ticks();
    char line[96];
    unsigned i;

    k_snprintf(line, sizeof(line), "timers count=%u expired=%u\n", uos_tq_live(tq),
               (unsigned)tq->expired);
    reply->write(line);
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        uos_timer_id_t id;
        uint32_t due, period;

        if (uos_tq_slot(tq, i, now, &id, &due, &period)) {
            k_snprintf(line, sizeof(line), "timer id=0x%08x due=%u period=%u\n",
                       (unsigned)id, (unsigned)due, (unsigned)period);
            reply->write(line);
        }
    }
    return 0;
}

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
/* Test builds only: the nm test proves the shipped image has none of this. */
static void uos_test_fault(void *arg)
{
    if (arg != NULL) {
        __asm__ volatile("bkpt #0x55");
    } else {
        __asm__ volatile(".word 0xe7f000f0");   /* udf: undefined in ARM state */
    }
}

static void uos_test_panic(void *arg)
{
    (void)arg;
    uos_panic("TEST PANIC FROM THE MAINTENANCE PORT");
}

static int test_command(int argc, char *argv[], input_script_reply_t *reply, int *rc)
{
    if (k_strcmp(argv[0], "fault") == 0 && argc == 2) {
        if (k_strcmp(argv[1], "undef") == 0) {
            *rc = uos_post(uos_test_fault, NULL);
        } else if (k_strcmp(argv[1], "pabt") == 0) {
            *rc = uos_post(uos_test_fault, (void *)1);
        } else {
            reply->reason = "unknown fault";
            *rc = -1;
        }
        return 1;
    }
    if (k_strcmp(argv[0], "panic") == 0 && argc == 1) {
        *rc = uos_post(uos_test_panic, NULL);
        return 1;
    }
    return 0;
}
#endif

static int command(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    char line[128];

    (void)ctx;
    if (k_strcmp(argv[0], "state") == 0 && argc == 1) {
        state_line(line, sizeof(line));
        reply->write(line);
        reply->write("\n");
        return 0;
    }
    if (k_strcmp(argv[0], "dump") == 0 && argc == 1) {
        if (!uos.fb_ok) {
            reply->reason = "no screen";
            return -1;
        }
        dump_reply = reply;
        console_dump(dump_char);
        return 0;
    }
    if (k_strcmp(argv[0], "type") == 0) {
        if (uos.phase != UOS_PHASE_READY) {
            reply->reason = "not ready";
            return -1;
        }
        return cmd_type(argc, argv, reply);
    }
    if (k_strcmp(argv[0], "timers") == 0 && argc == 1) {
        return cmd_timers(reply);
    }
    if (k_strcmp(argv[0], "reboot") == 0 && argc == 1) {
        UOS_MARK("reboot source=maint");
        uart_flush();
        power_reboot();
    }
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    {
        int rc = 0;

        if (uos.phase == UOS_PHASE_READY && test_command(argc, argv, reply, &rc)) {
            return rc;
        }
    }
#endif
    reply->reason = "unknown command";
    return -1;
}

void uos_maint_start(void)
{
    int serial, script = INPUT_ERR_NO_UART;

    input_init();
    serial = input_serial_start(INPUT_SERIAL_MAIN);
    input_script_set_handler(command, NULL);
    if (uart_mini_ready()) {
        script = input_script_start();
    }
    UOS_MARK("input serial=%d script=%d gpio=off", serial, script);
}

void uos_maint_idle(void)
{
    input_event_t ev;

    UOS_MARK("idle");
    for (;;) {
        /* IRQs are off: poll. Keys are dropped; script lines are answered. */
        (void)input_poll(&ev);
    }
}
