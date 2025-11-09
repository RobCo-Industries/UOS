/*
 * UOS System Timer Driver
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "drivers/timer.h"

/* System Timer registers */
#ifndef PERIPHERAL_BASE
#define PERIPHERAL_BASE 0x3F000000
#endif

#define TIMER_BASE (PERIPHERAL_BASE + 0x3000)
#define TIMER_CLO  ((volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_CHI  ((volatile uint32_t*)(TIMER_BASE + 0x08))

/*
 * Initialize system timer
 */
void timer_init(void)
{
    // Timer is already initialized by bootloader
    // Nothing special needed here
}

/*
 * Get current timer value (microseconds)
 */
uint64_t timer_get_ticks(void)
{
    uint32_t hi = *TIMER_CHI;
    uint32_t lo = *TIMER_CLO;
    
    // Check for overflow
    if (hi != *TIMER_CHI) {
        hi = *TIMER_CHI;
        lo = *TIMER_CLO;
    }
    
    return ((uint64_t)hi << 32) | lo;
}

/*
 * Delay for specified microseconds
 */
void timer_delay_us(uint32_t us)
{
    uint64_t start = timer_get_ticks();
    uint64_t target = start + us;
    
    while (timer_get_ticks() < target) {
        __asm__ volatile("nop");
    }
}

/*
 * Delay for specified milliseconds
 */
void timer_delay_ms(uint32_t ms)
{
    timer_delay_us(ms * 1000);
}

/*
 * Delay for specified seconds
 */
void timer_delay_s(uint32_t s)
{
    timer_delay_ms(s * 1000);
}
