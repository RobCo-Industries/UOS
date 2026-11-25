/*
 * The shell's rules. Pure: no kernel calls, no hardware, no allocation.
 * shell.c and shcmds.c call these; tests/host/test_shell.c tests them.
 * Prefix sh_.
 */
#ifndef UOS_SHRULES_H
#define UOS_SHRULES_H

#include <stddef.h>
#include <stdint.h>

#include "dcl.h"

/* ---- Errors: the one map from service codes to messages ---- */

/* Where a code comes from. */
#define SH_SRC_VFS  0   /* a VFS_E_* code of a file call */
#define SH_SRC_DEV  1   /* a VFS_E_* code of a DEV: call: the device service's meaning */
#define SH_SRC_UAF  2   /* a uaf_err_t of the user database module */
#define SH_SRC_VP   3   /* a vp_err_t of the path module */

/*
 * The message for `code` from `src`. 0 is DCL_NORMAL for every source.
 * `vp` is the path module's reason behind a refused wildcard match
 * (vfs_last_vp()), used for VFS_E_NOFILES when the access table refused
 * every match: VP_E_PROTECTED gives PROTECT, any other refusal NOPRIV.
 * Pass -1 when there is none. No command formats its own error text.
 */
dcl_msg_t sh_err_msg(int src, int code, int vp, uint32_t refused);

/* ---- Wildcard file loops ---- */

/* Answers to a /CONFIRM question. */
#define SH_ANS_NO     0     /* N, NO, F, FALSE, 0, or an empty line */
#define SH_ANS_YES    1     /* Y, YES, T, TRUE, 1 */
#define SH_ANS_QUIT   2     /* Q, QUIT, or Ctrl-C */
#define SH_ANS_ALL    3     /* A, ALL: yes to this file and every later one */
#define SH_ANS_AGAIN  4     /* anything else: ask again */

/* Reads a typed answer; case and blanks around it are ignored. */
int sh_confirm_answer(const char *text);

/* What a file loop does with the current file. */
#define SH_DO_ACT     0     /* act on the file */
#define SH_DO_SKIP    1     /* leave it and go to the next */
#define SH_DO_STOP    2     /* end the loop */
#define SH_DO_ASK     3     /* ask the question (again) */

/* The step for an answer. *all is set by SH_ANS_ALL. */
int sh_confirm_step(int answer, uint8_t *all);

/* Before each file: SH_DO_ASK when /CONFIRM is on and no ALL was given, else SH_DO_ACT. */
int sh_confirm_need(int confirm, uint8_t all);

/* The counts of a file loop. */
typedef struct {
    uint32_t done;          /* files acted on */
    uint32_t failed;        /* files that failed */
    uint32_t refused;       /* matches the access table refused */
    uint32_t declined;      /* files /CONFIRM answered no */
    dcl_msg_t last_err;     /* the message of the last failure */
    uint8_t cancelled;      /* Ctrl-C, or QUIT */
} sh_tally_t;

void sh_tally_init(sh_tally_t *t);

/*
 * The final message of a file loop: CANCEL when cancelled with nothing
 * done; `ok` with *num = done when a file was done or nothing failed;
 * else the last failure, or NOPRIV when only refusals stopped it.
 */
dcl_msg_t sh_tally_result(const sh_tally_t *t, dcl_msg_t ok, uint32_t *num);

/* ---- Text ---- */

/*
 * Decodes the console's marker quoting: "%XX" is one byte, a surrounding
 * pair of double quotes is dropped. Returns the length, or -1 for a bad
 * escape or an `out` too small (out then holds "").
 */
int sh_unquote(const char *in, char *out, size_t size);

/* The file part of an absolute specification: after the last ']' or ':'. */
const char *sh_spec_tail(const char *spec);

/* The next line of `text` from *pos: *start and *len exclude the line
 * end (LF, CR LF or CR). Returns 0 at the end of the text, else 1. */
int sh_next_line(const char *text, size_t size, size_t *pos, size_t *start, size_t *len);

/*
 * The next display row of `text` from *pos, at most `width` bytes: a line
 * longer than `width` gives its first `width` bytes and leaves *pos inside
 * the line; the last row of a line also steps over its end (LF, CR LF or
 * CR). An empty line is one empty row. Reads at most width + 2 bytes per
 * call, so a 1 MiB line costs the same per row as a short one. Returns 0
 * at the end of the text, else 1. `width` 0 is taken as 1.
 */
int sh_next_row(const char *text, size_t size, size_t width, size_t *pos, size_t *start,
                size_t *len);

/* "D HH:MM:SS" for an uptime in seconds. Length or -1. */
int sh_fmt_uptime(uint32_t seconds, char *buf, size_t size);

/* "DD-MMM-YYYY HH:MM:SS". Length, or -1 for a field out of range. */
int sh_fmt_datetime(unsigned year, unsigned month, unsigned day, unsigned hour,
                    unsigned minute, unsigned second, char *buf, size_t size);

/* A byte count for listings: "512", "12 KiB", "3 MiB", rounded down. Length or -1. */
int sh_fmt_size(uint64_t bytes, char *buf, size_t size);

/* DIRECTORY columns. */
#define SH_DIR_SIZE   0x01u
#define SH_DIR_DATE   0x02u

/*
 * One DIRECTORY row: the name and type, then the size in bytes and the
 * date when asked. `date` is the "DD-MMM-YYYY HH:MM:SS" text, may be NULL.
 * A folder shows "NAME.DIR" and no size. Length or -1.
 */
int sh_dir_line(const char *name, const char *type, int is_dir, uint32_t bytes, const char *date,
                unsigned cols, char *buf, size_t size);

/* "Total of N file(s), M bytes." with the size only when `cols` has SH_DIR_SIZE. */
int sh_dir_total(uint32_t files, uint64_t bytes, unsigned cols, char *buf, size_t size);

/* 1 when a CREATE line ends the text: a line that holds one '.'. */
int sh_create_end(const char *line);

/* ---- /OUTPUT and job identity ---- */

/*
 * 1 when a command's /OUTPUT text replaces the file: the final message is
 * S or I and the command was not cancelled. A warning (W), an error, a
 * cancel or a full buffer leave the file as it was.
 */
int sh_output_write(dcl_msg_t final, int cancelled);

/*
 * 1 when a callback tagged with job `tag` belongs to the running job:
 * a job is active and its identity is `tag`. Identities start at 1, so
 * tag 0 never matches. A late callback of an ended job gets 0.
 */
int sh_tag_live(int active, uint32_t current, uint32_t tag);

/* ---- A job under a covered shell ---- */

/* What a covered shell owes its job. An end outranks a step. */
#define SH_OWE_NONE     0
#define SH_OWE_STEP     1       /* a posted step found the shell covered */
#define SH_OWE_END      2       /* the job ended while the shell was covered */

/*
 * 1 when a job must wait: the shell is the session's screen and another
 * screen is on top. A job's step may ask, page, start a program or print,
 * and each of those would land on top of the covering screen.
 */
int sh_job_waits(int shell_on, int shell_on_top);
/* 1 when the shell may start a job: its screen is on the stack and its
 * unit is on top. Else sh_run_line() returns SH_ERR_COVERED. */
int sh_takes_job(int shell_on, int shell_on_top);
/* Records `what` in *owed, keeping an end over a step. Returns 1 when the
 * shell owed nothing before: the caller marks the wait once. */
int sh_owe(int *owed, int what);
/* The owed work, once: returns it and clears *owed. */
int sh_owe_take(int *owed);

/* ---- HELP ---- */

/*
 * The help file of `topic` from the table: "" or NULL gives the HELP
 * verb's file; a verb or its abbreviation gives the verb's file. Returns
 * DCL_NORMAL with *spec set, DCL_ABVERB for an ambiguous one, or
 * DCL_SYS_NOHELP. The topic is compared upper-case.
 */
dcl_msg_t sh_help_spec(const dcl_table_t *table, const char *topic, const char **spec);

/* ---- Programs for RUN ---- */

#define SH_PROG_MAX       8
#define SH_PROG_NAME_MAX  12

/* A program ends once with done(tag, status, text), passing back the `tag`
 * its start received: the shell knows the run from it. */
typedef void (*sh_prog_done_fn)(void *tag, int status, const char *text);
typedef int (*sh_prog_start_fn)(const char *args, sh_prog_done_fn done, void *tag);

typedef struct {
    char name[SH_PROG_NAME_MAX + 1];
    sh_prog_start_fn start;
} sh_prog_t;

typedef struct {
    sh_prog_t prog[SH_PROG_MAX];
    unsigned count;
} sh_progtab_t;

#define SH_PROG_ERR_ARG   (-1)  /* NULL function, or a name not 1 to 12 of A-Z 0-9 $ _ */
#define SH_PROG_ERR_DUP   (-2)
#define SH_PROG_ERR_FULL  (-3)

void sh_prog_init(sh_progtab_t *t);
/* Adds a program; the name is stored upper-case. 0 or SH_PROG_ERR_*. */
int sh_prog_add(sh_progtab_t *t, const char *name, sh_prog_start_fn start);
/* The program of `name`, any case, or NULL. */
sh_prog_start_fn sh_prog_find(const sh_progtab_t *t, const char *name);

/* ---- Markers ---- */

/* The verb word of a marker: "DELETE", "SET_HALT", "-" for none. */
void sh_verb_word(const dcl_cmd_t *cmd, char *out, size_t size);

#endif
