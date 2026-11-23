/*
 * The one path for fatal errors. uos_panic() and the library's exception
 * hook both end here: IRQs off, one "ev=panic" marker, a cleared screen
 * with the reason (and the registers after an exception), then the
 * maintenance port keeps answering.
 */

#include <stdarg.h>

#include <robco/console.h>
#include <robco/exc.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "uos.h"

#define REASON_MAX 64

static char reason[REASON_MAX];
static int in_panic;

static const char *exc_name(unsigned type)
{
    switch (type) {
    case ROBCO_EXC_UNDEF: return "undef";
    case ROBCO_EXC_PABT:  return "pabt";
    case ROBCO_EXC_DABT:  return "dabt";
    default:              return "other";
    }
}

/* The marker wants one word: spaces become underscores. */
static void marker_word(char *out, size_t size, const char *in)
{
    size_t i;

    for (i = 0; i + 1 < size && in[i] != '\0'; i++) {
        out[i] = (in[i] == ' ' || in[i] == '\t') ? '_' : in[i];
    }
    out[i] = '\0';
}

static void put(const char *line)
{
    if (uos.fb_ok) {
        console_puts(line);
        console_putc('\n');
    }
}

static void stop(const char *kind, const exc_frame_t *f) __attribute__((noreturn));

static void stop(const char *kind, const exc_frame_t *f)
{
    char word[REASON_MAX];
    char line[96];
    unsigned i;

    irq_cpu_disable();
    if (in_panic) {
        /* A fault inside the panic path: no screen, only the port. */
        uos_maint_idle();
    }
    in_panic = 1;
    uos.phase = UOS_PHASE_PANIC;
    marker_word(word, sizeof(word), reason);
    if (f != NULL) {
        UOS_MARK("panic kind=%s pc=0x%08x lr=0x%08x cpsr=0x%08x fsr=0x%08x far=0x%08x "
                 "reason=%s", kind, (unsigned)f->pc, (unsigned)f->lr, (unsigned)f->cpsr,
                 (unsigned)f->fsr, (unsigned)f->far, word);
    } else {
        UOS_MARK("panic kind=%s reason=%s", kind, word);
    }
    uos_set_error("panic");
    if (uos.fb_ok) {
        console_set_attr(CONSOLE_ATTR_NORMAL);
        console_clear();
        console_set_cursor(0, 0);
        console_cursor_show(0);
        console_set_attr(CONSOLE_ATTR_INVERSE);
        put(" *** UOS HALTED: KERNEL PANIC *** ");
        console_set_attr(CONSOLE_ATTR_NORMAL);
        put("");
        k_snprintf(line, sizeof(line), "REASON: %s", reason);
        put(line);
        if (f != NULL) {
            k_snprintf(line, sizeof(line), "EXCEPTION: %s", kind);
            put(line);
            put("");
            for (i = 0; i < 13; i += 4) {
                if (i + 3 < 13) {
                    k_snprintf(line, sizeof(line), "R%-2u %08x  R%-2u %08x  R%-2u %08x  R%-2u %08x",
                               i, (unsigned)f->r[i], i + 1, (unsigned)f->r[i + 1], i + 2,
                               (unsigned)f->r[i + 2], i + 3, (unsigned)f->r[i + 3]);
                } else {
                    k_snprintf(line, sizeof(line), "R%-2u %08x", i, (unsigned)f->r[i]);
                }
                put(line);
            }
            k_snprintf(line, sizeof(line), "SP  %08x  LR  %08x  PC  %08x  CPSR %08x",
                       (unsigned)f->sp, (unsigned)f->lr, (unsigned)f->pc, (unsigned)f->cpsr);
            put(line);
            k_snprintf(line, sizeof(line), "FSR %08x  FAR %08x", (unsigned)f->fsr,
                       (unsigned)f->far);
            put(line);
        }
        put("");
        put("THE SYSTEM HAS STOPPED. RESTART THE TERMINAL.");
    }
    uart_puts("UOS HALTED: ");
    uart_puts(reason);
    uart_puts("\n");
    uos_maint_idle();
}

void uos_panic(const char *fmt, ...)
{
    va_list ap;

    irq_cpu_disable();
    va_start(ap, fmt);
    k_vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);
    stop("call", NULL);
}

static int on_exception(unsigned type, exc_frame_t *frame)
{
    k_snprintf(reason, sizeof(reason), "CPU EXCEPTION %s", exc_name(type));
    stop(exc_name(type), frame);
}

void uos_panic_init(void)
{
    (void)exc_set_hook(on_exception);
}
