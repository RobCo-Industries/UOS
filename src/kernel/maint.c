/*
 * The input layer and the maintenance port. The port is the mini UART
 * script channel: in QEMU it is the second serial port. On hardware the
 * mini UART reaches no pin, see BOARD_SUPPORT.md in RobCo-Core. It is for
 * tests and bring-up, not for users.
 *
 * The library answers its built-in commands (key, char, text, dial, wait,
 * stats). Every other line goes to dispatch(), which looks the first word
 * up in the registry of uos_maint.h. Commands registered here:
 *   help           the registered commands with their help, sorted
 *   state          one line: phase, screen, user, card, last error
 *   screens        screens depth=<n> stack=<bottom,...,top>
 *   dump           the console cells, through console_dump()
 *   type <words>   the words as characters, then SELECT: one typed line of
 *                  at most 30 characters, or "err full"
 *   timers         the live timers, one line each after a count line
 *   reboot         shuts the services down, then restarts the board
 * Test builds only (UOS_TEST_HOOKS=1):
 *   fault undef|pabt   runs an undefined instruction or a breakpoint
 *   panic              calls uos_panic()
 *   halt               calls uos_halt()
 *   bench uaf <n>      times uaf_hash() over n iterations
 *   hold <key> <n>     a held key: one press, n repeats, then the release
 * Every answer line is complete; the port then says "ok" or "err <why>".
 */

#include <stdarg.h>

#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "uos.h"
#include "uos_maint.h"

#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
#include <robco/timer.h>

#include "uaf.h"
#endif

static uos_maint_table_t table;
static input_script_reply_t *cur_reply;   /* the line being answered */
static int table_on;    /* the table holds the base commands */
static int input_on;    /* input_init() ran and the handler is set */
static int script_on;   /* the script channel answers */

static void fill_table(void);

/* ---- The registry on the port -------------------------------------------- */

int uos_maint_register(const char *name, uos_maint_fn fn, const char *help)
{
    fill_table();
    return uos_mt_add(&table, name, fn, help);
}

void uos_maint_reply(const char *fmt, ...)
{
    char line[124];
    va_list ap;
    size_t n;

    if (cur_reply == NULL) {
        return;
    }
    va_start(ap, fmt);
    k_vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    n = k_strlen(line);
    line[n] = '\n';
    line[n + 1] = '\0';
    cur_reply->write(line);
}

int uos_maint_fail(const char *reason)
{
    if (cur_reply != NULL) {
        cur_reply->reason = reason;
    }
    return UOS_MAINT_FAIL;
}

static int dispatch(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    char *args[UOS_MAINT_ARGS_MAX + 1];
    const uos_maint_cmd_t *cmd = uos_mt_find(&table, argv[0]);
    char *rest = "";
    int n, rc;

    (void)ctx;
    if (cmd == NULL) {
        reply->reason = "unknown command";
        return -1;
    }
    /* The words after the name, joined back, then split again with quotes. */
    if (argc > 1) {
        rest = uos_mt_rejoin(argv[1], argv[argc - 1]);
    }
    n = uos_mt_split(rest, args, UOS_MAINT_ARGS_MAX);
    if (n < 0) {
        reply->reason = n == UOS_MAINT_SPLIT_QUOTE ? "bad quotes" : "too many words";
        return -1;
    }
    cur_reply = reply;
    uos_port_command(1);
    rc = cmd->fn(n, args);
    uos_port_command(0);
    cur_reply = NULL;
    return rc < 0 ? -1 : 0;
}

/* ---- Base commands ----------------------------------------------------------- */

static const char *phase_name(int phase)
{
    switch (phase) {
    case UOS_PHASE_INIT:  return "init";
    case UOS_PHASE_READY: return "ready";
    case UOS_PHASE_PANIC: return "panic";
    case UOS_PHASE_HALT:  return "halt";
    default:              return "unknown";
    }
}

static int cmd_help(int argc, char **argv)
{
    unsigned i;

    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    for (i = 0; i < table.count; i++) {
        uos_maint_reply("%-12s %s", table.cmd[i].name, table.cmd[i].help);
    }
    return 0;
}

static int cmd_state(int argc, char **argv)
{
    const uos_screen_t *top = uos.phase == UOS_PHASE_READY || uos.phase == UOS_PHASE_INIT
                              ? uos_screen_top() : NULL;

    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    uos_maint_reply("phase=%s screen=%s user=%s card=%s trusted=%d error=%s",
                    phase_name(uos.phase),
                    top != NULL && top->name != NULL ? top->name : "none",
                    uos.user[0] != '\0' ? uos.user : "none", uos_card_name(uos.card),
                    uos.trusted, uos.error[0] != '\0' ? uos.error : "none");
    return 0;
}

static int cmd_screens(int argc, char **argv)
{
    char names[96];
    size_t n = 0;
    unsigned i;
    uos_screen_t *s;

    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    names[0] = '\0';
    for (i = 0; (s = uos_screen_at(i)) != NULL; i++) {
        int w = k_snprintf(names + n, sizeof(names) - n, "%s%s", i > 0 ? "," : "",
                           s->name != NULL ? s->name : "?");

        if (w < 0 || (size_t)w >= sizeof(names) - n) {
            break;
        }
        n += (size_t)w;
    }
    uos_maint_reply("screens depth=%u stack=%s", i, i > 0 ? names : "none");
    return 0;
}

static void dump_char(char c)
{
    char s[2] = { c, '\0' };

    cur_reply->write(s);
}

static int cmd_dump(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    if (!uos.fb_ok) {
        return uos_maint_fail("no screen");
    }
    console_dump(dump_char);
    return 0;
}

static void push_char(char c)
{
    input_event_t ev = { .type = INPUT_EV_CHAR, .source = INPUT_SRC_SCRIPT,
                         .code = (uint8_t)c };

    (void)input_push(&ev);
}

static int cmd_type(int argc, char **argv)
{
    input_event_t key = { .type = INPUT_EV_KEY, .source = INPUT_SRC_SCRIPT,
                          .code = INPUT_KEY_SELECT, .value = INPUT_PRESS };
    unsigned n = 2;
    int i;
    const char *p;

    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("not ready");
    }
    /* One event per character, a space between words, SELECT pressed and released. */
    for (i = 0; i < argc; i++) {
        n += (unsigned)k_strlen(argv[i]) + (i > 0 ? 1u : 0u);
    }
    if (!input_room(n)) {
        return uos_maint_fail("full");
    }
    for (i = 0; i < argc; i++) {
        if (i > 0) {
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

static int cmd_timers(int argc, char **argv)
{
    const uos_timerq_t *tq = uos_timer_table();
    uint32_t now = uos_ticks();
    unsigned i;

    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    uos_maint_reply("timers count=%u expired=%u", uos_tq_live(tq), (unsigned)tq->expired);
    for (i = 0; i < UOS_TIMER_MAX; i++) {
        uos_timer_id_t id;
        uint32_t due, period;

        if (uos_tq_slot(tq, i, now, &id, &due, &period)) {
            uos_maint_reply("timer id=0x%08x due=%u period=%u", (unsigned)id, (unsigned)due,
                            (unsigned)period);
        }
    }
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    uos_reboot("maint");
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

static void uos_test_halt(void *arg)
{
    (void)arg;
    uos_halt();
}

/* A fixed input: the bench measures the hash, not the data. */
static void uos_test_bench_uaf(void *arg)
{
    static const uint8_t salt[UAF_SALT_SIZE] = { 'U', 'O', 'S', '-', 'B', 'E', 'N', 'C', 'H' };
    uint8_t out[UAF_HASH_SIZE];
    uint32_t iterations = (uint32_t)(uintptr_t)arg;
    uint32_t t0 = uos_ticks();
    uint64_t us0 = timer_now_us();
    uaf_err_t rc = uaf_hash(salt, "robco-bench", iterations, out, NULL);

    UOS_MARK("bench name=uaf iterations=%u rc=%d ticks=%u us=%u", (unsigned)iterations,
             (int)rc, (unsigned)(uos_ticks() - t0), (unsigned)(timer_now_us() - us0));
}

/* Each refuses to run without the loop: after a panic or a halt nothing
 * would take the posted call. */
static int test_post(uos_call_fn fn, void *arg)
{
    if (uos.phase != UOS_PHASE_READY) {
        return uos_maint_fail("unknown command");
    }
    return uos_post(fn, arg) == 0 ? 0 : uos_maint_fail("full");
}

static int cmd_fault(int argc, char **argv)
{
    if (uos.phase != UOS_PHASE_READY || argc != 1) {
        return uos_maint_fail("unknown command");
    }
    if (k_strcmp(argv[0], "undef") == 0) {
        return test_post(uos_test_fault, NULL);
    }
    if (k_strcmp(argv[0], "pabt") == 0) {
        return test_post(uos_test_fault, (void *)1);
    }
    return uos_maint_fail("unknown fault");
}

static int cmd_panic(int argc, char **argv)
{
    (void)argv;
    return argc != 0 ? uos_maint_fail("unknown command") : test_post(uos_test_panic, NULL);
}

static int cmd_halt(int argc, char **argv)
{
    (void)argv;
    return argc != 0 ? uos_maint_fail("unknown command") : test_post(uos_test_halt, NULL);
}

/* A held key, as the repeat timer would send it. UOS sets no repeat rate,
 * so the script channel alone cannot send INPUT_REPEAT. */
static int cmd_hold(int argc, char **argv)
{
    input_event_t ev = { .type = INPUT_EV_KEY, .source = INPUT_SRC_SCRIPT,
                         .value = INPUT_PRESS };
    unsigned code, i, n = 0;
    const char *p;

    if (uos.phase != UOS_PHASE_READY || argc != 2) {
        return uos_maint_fail("usage: hold <key> <repeats>");
    }
    for (code = 1; code < INPUT_KEY_COUNT; code++) {
        if (k_strcmp(input_key_name(code), argv[0]) == 0) {
            break;
        }
    }
    for (p = argv[1]; *p >= '0' && *p <= '9' && n <= 16u; p++) {
        n = n * 10u + (unsigned)(*p - '0');
    }
    if (code >= INPUT_KEY_COUNT || *p != '\0' || p == argv[1] || n > 16u) {
        return uos_maint_fail("usage: hold <key> <repeats>");
    }
    if (!input_room(n + 2u)) {
        return uos_maint_fail("full");
    }
    ev.code = (uint16_t)code;
    (void)input_push(&ev);
    ev.value = INPUT_REPEAT;
    for (i = 0; i < n; i++) {
        (void)input_push(&ev);
    }
    ev.value = INPUT_RELEASE;
    (void)input_push(&ev);
    return 0;
}

static int cmd_bench(int argc, char **argv)
{
    uint32_t n = 0;
    const char *p;

    if (argc != 2 || k_strcmp(argv[0], "uaf") != 0) {
        return uos_maint_fail("usage: bench uaf <iterations>");
    }
    for (p = argv[1]; *p >= '0' && *p <= '9' && n <= 100000u; p++) {
        n = n * 10u + (uint32_t)(*p - '0');
    }
    if (*p != '\0' || n < 1u || n > 100000u) {
        return uos_maint_fail("iterations 1 to 100000");
    }
    return test_post(uos_test_bench_uaf, (void *)(uintptr_t)n);
}
#endif

/*
 * The base commands, once. uos_maint_start(), uos_maint_register() and the
 * panic path call it, so the base commands answer even after a panic that
 * came before uos_maint_start(). Pure table work: safe with IRQs off.
 */
static void fill_table(void)
{
    if (table_on) {
        return;
    }
    table_on = 1;
    uos_mt_init(&table);
    (void)uos_mt_add(&table, "help", cmd_help, "lists the commands");
    (void)uos_mt_add(&table, "state", cmd_state, "phase, screen, user, card, trust, last error");
    (void)uos_mt_add(&table, "dump", cmd_dump, "the console cells");
    (void)uos_mt_add(&table, "screens", cmd_screens, "the screen stack, bottom to top");
    (void)uos_mt_add(&table, "type", cmd_type, "types the words, then SELECT");
    (void)uos_mt_add(&table, "timers", cmd_timers, "the live timers");
    (void)uos_mt_add(&table, "reboot", cmd_reboot, "shuts the services down and restarts");
#if defined(UOS_TEST_HOOKS) && UOS_TEST_HOOKS
    (void)uos_mt_add(&table, "fault", cmd_fault, "test: fault undef|pabt");
    (void)uos_mt_add(&table, "panic", cmd_panic, "test: calls uos_panic()");
    (void)uos_mt_add(&table, "halt", cmd_halt, "test: calls uos_halt()");
    (void)uos_mt_add(&table, "bench", cmd_bench, "test: bench uaf <iterations>");
    (void)uos_mt_add(&table, "hold", cmd_hold, "test: hold <key> <repeats>");
#endif
}

void uos_maint_start(void)
{
    int serial, script = INPUT_ERR_NO_UART;

    fill_table();
    input_init();
    serial = input_serial_start(INPUT_SERIAL_MAIN);
    input_script_set_handler(dispatch, NULL);
    input_on = 1;
    if (uart_mini_ready()) {
        script = input_script_start();
    }
    script_on = script == 0;
    UOS_MARK("input serial=%d script=%d gpio=later", serial, script);
}

/*
 * A panic can come before uos_maint_start(), from the exception hook or a
 * failed tick. The port then starts here, polled: the script channel needs
 * the mini UART and the input layer, not the tick or IRQs. The table gets
 * its base commands first, so help, state, dump and reboot answer.
 */
static void late_start(void)
{
    int mini = 0, script;

    fill_table();
    if (script_on) {
        return;
    }
    if (!input_on) {
        input_init();
        input_script_set_handler(dispatch, NULL);
        input_on = 1;
    }
    if (!uart_mini_ready()) {
        mini = uart_mini_init(115200);
    }
    script = input_script_start();
    script_on = script == 0;
    UOS_MARK("port late=1 mini=%d script=%d", mini, script);
}

void uos_maint_idle(void)
{
    input_event_t ev;

    late_start();
    UOS_MARK("idle");
    for (;;) {
        /* IRQs are off: poll. Keys are dropped; script lines are answered. */
        (void)input_poll(&ev);
    }
}
