/*
 * UOS Timer Driver Header
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include "kernel/types.h"

/* Timer initialization and control */
void timer_init(void);
uint64_t timer_get_ticks(void);

/* Delay functions */
void timer_delay_us(uint32_t us);
void timer_delay_ms(uint32_t ms);
void timer_delay_s(uint32_t s);

#endif /* DRIVERS_TIMER_H */
