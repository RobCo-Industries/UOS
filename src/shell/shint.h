/*
 * Shared state of the shell's thin files: shell.c (the screen, the
 * executor, the service and the `sh` command), shcmds.c (the file, SET,
 * SHOW, HELP and device commands) and shuaf.c (AUTHORIZE and SET
 * PASSWORD). Not for other modules: they use sh.h.
 */
#ifndef UOS_SHINT_H
#define UOS_SHINT_H

#include <stddef.h>
#include <stdint.h>

#include "con.h"
#include "dcl.h"
#include "sh.h"
#include "uaf.h"
#include "vfs.h"

/* What a command's start or step returns. */
#define SH_DONE     0       /* the command ended: sh_end() set its message */
#define SH_MORE     1       /* post another step */
#define SH_WAIT     2       /* a read, a pager or a program calls sh_resume() later */

/* What runs after the command ended and its callback returned. */
#define SH_AFTER_NONE     0
#define SH_AFTER_LOGOUT   1
#define SH_AFTER_HALT     2
#define SH_AFTER_RESTART  3

/* Lines a command reads with CREATE, at most. */
#define SH_CREATE_MAX     16384u
/* TYPE reads files up to this size. */
#define SH_TYPE_MAX       CON_PAGE_MAX
/* /OUTPUT and DIRECTORY collect at most this much text. */
#define SH_OUT_MAX        65536u
/* DIRECTORY lists at most this many entries of one folder. */
#define SH_DIR_MAX        256u
/* Output lines or files per step. */
#define SH_LINES_PER_STEP 8u

typedef struct sh_job sh_job_t;

typedef struct {
    const char *verb;
    const char *key;                /* the keyword, NULL for a verb without one */
    int (*start)(sh_job_t *j);
    int (*step)(sh_job_t *j);       /* NULL when start never returns SH_MORE */
    void (*stop)(sh_job_t *j);      /* frees what the command holds; NULL for nothing */
} sh_impl_t;

struct sh_job {
    int active;
    unsigned flags;                 /* SH_RUN_* */
    const char *src;                /* "shell", "port" or "api" */
    char line[DCL_LINE_MAX + 1];
    dcl_cmd_t cmd;
    const sh_impl_t *impl;
    vp_ctx_t *ctx;                  /* the shell's default folder */
    uint32_t privs;
    char user[UAF_NAME_MAX + 1];
    char verb[32];                  /* the marker word */
    int cancel;
    int after;                      /* SH_AFTER_* */
    uint32_t steps, maxticks;
    /* The final message. */
    dcl_msg_t msg;
    char msg_arg[VP_DISPLAY_MAX];
    uint32_t msg_num;
    int msg_print;
    char res_text[DCL_MSG_SIZE];    /* a program's own result line, "" for none */
    /* /OUTPUT text. */
    const char *out_spec;
    char *obuf;
    size_t olen, ocap;
    int ofull;
    /* Command state. */
    int phase;
    sh_tally_t tally;
    vfs_iter_t it;
    int it_open;
    const char *cur;
    uint8_t all;
    int confirm, log;
    char *buf;
    size_t blen, bpos;
    vfs_entry_t *ents;
    size_t ecount, etotal, eidx;
    uint64_t ebytes;
    unsigned cols;
    char tmp[VP_DISPLAY_MAX];
    sh_done_fn cb;
    void *cbctx;
};

/* shell.c: the one job. */
extern sh_job_t sh_job;

/* shell.c: the command implementations of `cmd`, or NULL. */
const sh_impl_t *sh_impl_find(const dcl_cmd_t *cmd);

/* shell.c: one line of output. /OUTPUT text when a file collects it, else
 * the console, unless SH_RUN_QUIET. */
void sh_out(sh_job_t *j, const char *text);

/* shell.c: a message on the console, unless SH_RUN_QUIET. Never to /OUTPUT. */
void sh_say(sh_job_t *j, dcl_msg_t id, const char *arg, uint32_t num);

/* shell.c: sets the final message and returns SH_DONE. `print` 0 keeps it
 * off the console: a quiet success such as TYPE. */
int sh_end(sh_job_t *j, dcl_msg_t id, const char *arg, uint32_t num, int print);

/* shell.c: the result of a call in source `src` (SH_SRC_*), through the
 * one error map, as the final message. */
int sh_end_err(sh_job_t *j, int src, int code, const char *arg);

/* shell.c: a callback hands the command back: SH_MORE posts a step,
 * SH_DONE ends it, SH_WAIT waits again. */
void sh_resume(sh_job_t *j, int rc);

/* shell.c: reads a line for the running command, with a marker
 * "ev=sh state=prompt kind=<kind>". 0 or a CON_ERR_* code. */
int sh_ask(const char *kind, const char *prompt, unsigned flags, unsigned max, con_line_fn cb);

/* shell.c: one marker per file of a file loop. `result` is "ok", "skip",
 * "refused" or a message ident. */
void sh_file_mark(sh_job_t *j, const char *op, const char *spec, const char *result);

/* shell.c: the registered programs. */
extern sh_progtab_t sh_progs;

/* shcmds.c and shuaf.c: the tables of command implementations. */
extern const sh_impl_t sh_cmds[];
extern const unsigned sh_cmds_count;
extern const sh_impl_t sh_uaf_cmds[];
extern const unsigned sh_uaf_cmds_count;

#endif
