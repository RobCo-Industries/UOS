/*
 * UOS Terminal Service Header
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef SERVICES_TERMINAL_H
#define SERVICES_TERMINAL_H

#include "kernel/types.h"

/* Terminal initialization and main loop */
void terminal_init(void);
void terminal_main_loop(void);

/* Terminal state */
const char* terminal_get_user(void);
bool terminal_is_logged_in(void);

#endif /* SERVICES_TERMINAL_H */
