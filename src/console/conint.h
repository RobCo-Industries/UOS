/*
 * Shared between the console service's own files only. Not for other
 * services: they use con.h.
 */
#ifndef UOS_CONINT_H
#define UOS_CONINT_H

#include "con.h"
#include "lineedit.h"

/* The history every line read shares, in console.c. */
extern le_history_t con_history;
/* Set by the service's init, in console.c. */
extern int con_up;

/* Writes `n` characters of `text` at (col, row) with `attr`, then
 * blanks the rest of the row when `fill` is set. Nothing without a screen. */
void con_put(unsigned col, unsigned row, const char *text, size_t n, unsigned attr, int fill);

/* Columns and rows of the console, 80 and 30 when there is no screen. */
unsigned con_cols(void);
unsigned con_rows(void);

#endif
