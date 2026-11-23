/*
 * The maintenance command registry. Pure: maintreg.c holds the table
 * rules and the word splitting, with host tests. maint.c runs one table
 * on the maintenance port.
 *
 * Adding a command, from a service's init or from uos_maint_start():
 *
 *     static int cmd_ping(int argc, char **argv)
 *     {
 *         (void)argv;
 *         if (argc != 0) {
 *             return uos_maint_fail("unknown command");
 *         }
 *         uos_maint_reply("pong n=%d", 1);
 *         return 0;
 *     }
 *     ...
 *     (void)uos_maint_register("ping", cmd_ping, "answers pong");
 *
 * Context. A command function runs inside the input layer's script
 * handler, called from input_poll(). That happens in two places:
 *   - in the event loop, between two events, with IRQs on;
 *   - after a panic or a halt, in uos_maint_idle(), with IRQs off and no
 *     loop: uos.phase is then UOS_PHASE_PANIC or UOS_PHASE_HALT.
 * No other script line is read while it runs. A command may read state,
 * answer with uos_maint_reply() and push input events. It must not block,
 * wait for ticks, call input_poll() or input_wait(), or do anything that
 * can fault: it posts such work to the loop with uos_post(), and checks
 * that uos.phase is UOS_PHASE_READY first, since no loop runs after a
 * panic or a halt. A posted call can report its result with a marker.
 */
#ifndef UOS_MAINT_H
#define UOS_MAINT_H

/* Registered commands, at most. */
#define UOS_MAINT_MAX           48
/* A command name: 1 to 12 characters of a-z. */
#define UOS_MAINT_NAME_MAX      12
/* Words after the command name, at most. The library also refuses a line
 * of more than 8 blank-separated words in all, quotes or not. */
#define UOS_MAINT_ARGS_MAX      8
/* A line is at most 127 characters, the script channel's limit. */
#define UOS_MAINT_LINE_MAX      127

/* uos_maint_register() and the pure table. */
#define UOS_MAINT_ERR_NAME      (-1)    /* empty, too long or not a-z */
#define UOS_MAINT_ERR_DUP       (-2)    /* already registered, or a library built-in */
#define UOS_MAINT_ERR_FULL      (-3)    /* UOS_MAINT_MAX commands already */
#define UOS_MAINT_ERR_ARG       (-4)    /* NULL function */

/* uos_mt_split(). */
#define UOS_MAINT_SPLIT_WORDS   (-1)    /* more than `max` words */
#define UOS_MAINT_SPLIT_QUOTE   (-2)    /* a quote is not closed, or text follows it */
#define UOS_MAINT_SPLIT_LONG    (-3)    /* longer than UOS_MAINT_LINE_MAX */

/* A command's return value after uos_maint_fail(). */
#define UOS_MAINT_FAIL          (-1)

/*
 * One maintenance command. `fn` gets the words of the line after the
 * command name: argv[0] is the first argument, argc counts the arguments,
 * argv[argc] is NULL. A word in double quotes keeps its spaces; "" inside
 * quotes is one quote. Answers through uos_maint_reply(). Returns 0 for
 * "ok", or a negative code that the port reports as "err <text>": the
 * text given to uos_maint_fail(), else "failed".
 */
typedef int (*uos_maint_fn)(int argc, char **argv);

/* Adds a command. `help` is one short line, may be NULL. Main context,
 * before or after the port starts. 0 or UOS_MAINT_ERR_*. */
int  uos_maint_register(const char *name, uos_maint_fn fn, const char *help);

/* One complete answer line per call, without the newline. Only inside a
 * command function. Longer than 120 characters is cut. */
void uos_maint_reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Sets the "err <reason>" text of the current command and returns
 * UOS_MAINT_FAIL. `reason` must stay valid: use a string literal. */
int  uos_maint_fail(const char *reason);

/* ---- The pure table ------------------------------------------------------ */

typedef struct {
    char name[UOS_MAINT_NAME_MAX + 1];
    uos_maint_fn fn;
    const char *help;
} uos_maint_cmd_t;

/* Commands kept sorted by name. */
typedef struct {
    uos_maint_cmd_t cmd[UOS_MAINT_MAX];
    unsigned count;
} uos_maint_table_t;

void uos_mt_init(uos_maint_table_t *t);
/* Adds a command in name order. Refuses the library's built-in names
 * (key, char, text, dial, wait, stats): they never reach the table. */
int uos_mt_add(uos_maint_table_t *t, const char *name, uos_maint_fn fn, const char *help);
/* The command named `name`, or NULL. */
const uos_maint_cmd_t *uos_mt_find(const uos_maint_table_t *t, const char *name);

/*
 * Splits `line` in place into at most `max` words: runs of spaces and
 * tabs separate words, and "..." is one word that keeps its blanks, with
 * "" for a quote inside. argv holds max + 1 entries and ends with NULL.
 * Returns the word count, or UOS_MAINT_SPLIT_*.
 */
int uos_mt_split(char *line, char **argv, unsigned max);

/*
 * The script channel splits a line at every blank and writes a NUL over
 * each one. Puts the text from `first` to the end of the word `last`
 * back together with spaces, in place, and returns `first`.
 */
char *uos_mt_rejoin(char *first, char *last);

#endif
