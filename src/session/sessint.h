/*
 * Shared state of the session service's two thin files: sessvc.c (the
 * service, the session fields, the maintenance command) and sesscreen.c
 * (the login, lockout and welcome screens). Not for other
 * modules: they use sess.h.
 */
#ifndef UOS_SESSINT_H
#define UOS_SESSINT_H

#include <stdint.h>

#include "login.h"
#include "sess.h"
#include "uos.h"

#define SESS_UAF_PATH       "/UOS/SYSUAF.DAT"

typedef struct sess_state {
    /* The database, loaded once at init. */
    lg_db_t db;
    char why[16];               /* the reason word of a damaged database */
    uaf_db_t uaf;
    /* [login] values. */
    lk_t lk;
    unsigned idle_s;
    /* The login screen. */
    lg_state_t st;
    lg_msg_t msg;
    char msg_text[80];          /* the message row, "" for none */
    char typed[UAF_NAME_MAX + 1];   /* the user name as typed, for the screen */
    /* The session. */
    int in;
    int idx;
    char user[UAF_NAME_MAX + 1];
    char home[UAF_HOME_MAX + 1];
    uint32_t privs;
    uint32_t flags;
    uint32_t start_tick;
    sess_next_fn next;
    int next_set;
    sess_hack_fn hack;
    int lock_hack;              /* the running lock comes from the password game */
} sess_state_t;

extern sess_state_t sess;

/* The tick count as milliseconds, for the lockout and the idle rule. */
uint32_t sess_now_ms(void);

/* sesscreen.c: the login screen, at the bottom of the stack while nobody
 * is logged in. */
extern uos_screen_t sess_login_screen;

/* sesscreen.c: applies `e` to the state machine and shows the screen of the
 * new state at the bottom of the stack: every screen above it is removed
 * first. Loop. */
void sess_go(lg_event_t e);

/* sesscreen.c: the stack holds one screen, then it becomes `s`. Loop. */
void sess_show_only(uos_screen_t *s);

/* sessvc.c: sets the session fields from record `idx` and prints the session
 * start marker with `how`. Loop. */
void sess_begin(int idx, const char *how);

/* sesscreen.c: starts the user name prompt on the next pass. Loop. */
void sess_prompt_later(void);

/* sesscreen.c: a line on the PL011 only, on a line of its own. Any. */
void sess_serial(const char *line);

#endif
