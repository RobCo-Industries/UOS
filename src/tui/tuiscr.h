/*
 * The terminal menu screen. Prefix menu_. The model is tui.c; this is the
 * glue that loads files, runs the entries and draws.
 *
 * The menu opens two ways:
 *   - as the session's screen, after the login, for a user whose home
 *     holds TERMINAL.CFG. BACK at the top asks to log out.
 *   - through the shell's RUN TERMINAL, over the shell. BACK at the top
 *     asks to close the menu, and the shell's prompt comes back.
 *
 * Context: loop only, unless a function says otherwise.
 */
#ifndef UOS_TUISCR_H
#define UOS_TUISCR_H

#include "uos_screen.h"

/* The root menu file in the user's home. */
#define MENU_ROOT           "TERMINAL.CFG"
/* Largest menu, header or text file the menu reads. */
#define MENU_FILE_MAX       (64u * 1024u)

/* any: non-zero while the menu screen is on the stack. */
int menu_active(void);

/* any: the menu's screen, for the stack checks of other modules. */
uos_screen_t *menu_screen(void);

#endif
