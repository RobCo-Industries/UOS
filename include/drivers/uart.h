/*
 * UOS UART Driver Header
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef DRIVERS_UART_H
#define DRIVERS_UART_H

#include "kernel/types.h"

/* UART initialization and basic I/O */
void uart_init(void);
void uart_putc(char c);
char uart_getc(void);
void uart_puts(const char* str);
void uart_printf(const char* fmt, ...);
bool uart_has_data(void);

#endif /* DRIVERS_UART_H */
