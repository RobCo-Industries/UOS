/*
 * UOS Framebuffer Driver Header
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef DRIVERS_FRAMEBUFFER_H
#define DRIVERS_FRAMEBUFFER_H

#include "kernel/types.h"

/* Framebuffer initialization and control */
void framebuffer_init(void);
void fb_clear(void);
void fb_scroll(void);
void fb_set_colors(uint32_t fg, uint32_t bg);

/* Character output */
void fb_putc(char c);
void fb_puts(const char* str);

#endif /* DRIVERS_FRAMEBUFFER_H */
