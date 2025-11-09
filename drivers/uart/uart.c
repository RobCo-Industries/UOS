/*
 * UOS UART Driver
 * Serial communication for debugging and console
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "drivers/uart.h"

/* UART registers for BCM283x */
#ifndef PERIPHERAL_BASE
#define PERIPHERAL_BASE 0x3F000000
#endif

#define UART0_BASE (PERIPHERAL_BASE + 0x201000)

#define UART0_DR     ((volatile uint32_t*)(UART0_BASE + 0x00))
#define UART0_FR     ((volatile uint32_t*)(UART0_BASE + 0x18))
#define UART0_IBRD   ((volatile uint32_t*)(UART0_BASE + 0x24))
#define UART0_FBRD   ((volatile uint32_t*)(UART0_BASE + 0x28))
#define UART0_LCRH   ((volatile uint32_t*)(UART0_BASE + 0x2C))
#define UART0_CR     ((volatile uint32_t*)(UART0_BASE + 0x30))
#define UART0_ICR    ((volatile uint32_t*)(UART0_BASE + 0x44))

/* GPIO registers */
#define GPIO_BASE (PERIPHERAL_BASE + 0x200000)
#define GPFSEL1   ((volatile uint32_t*)(GPIO_BASE + 0x04))
#define GPPUD     ((volatile uint32_t*)(GPIO_BASE + 0x94))
#define GPPUDCLK0 ((volatile uint32_t*)(GPIO_BASE + 0x98))

/* UART flags */
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO full
#define UART_FR_RXFE (1 << 4)  // Receive FIFO empty

/*
 * Simple delay function
 */
static void delay(uint32_t count)
{
    while (count--) {
        __asm__ volatile("nop");
    }
}

/*
 * Initialize UART0 at 115200 baud
 */
void uart_init(void)
{
    // Disable UART0
    *UART0_CR = 0;
    
    // Setup GPIO pins 14 and 15 for UART
    uint32_t ra = *GPFSEL1;
    ra &= ~((7 << 12) | (7 << 15)); // Clear bits for GPIO 14 and 15
    ra |= (4 << 12) | (4 << 15);     // Set alt0 for UART
    *GPFSEL1 = ra;
    
    // Disable pull up/down for pins 14 and 15
    *GPPUD = 0;
    delay(150);
    
    *GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    
    *GPPUDCLK0 = 0;
    
    // Clear interrupts
    *UART0_ICR = 0x7FF;
    
    // Set baud rate to 115200
    // UART clock is 3MHz on RPi
    // Divisor = 3000000 / (16 * 115200) = 1.627
    // Integer part = 1, Fractional part = 0.627 * 64 = 40
    *UART0_IBRD = 1;
    *UART0_FBRD = 40;
    
    // Enable FIFO, 8 bit, no parity, 1 stop bit
    *UART0_LCRH = (1 << 4) | (3 << 5);
    
    // Enable UART0, transmit and receive
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

/*
 * Send a character
 */
void uart_putc(char c)
{
    // Wait for transmit FIFO to have space
    while (*UART0_FR & UART_FR_TXFF) {
        __asm__ volatile("nop");
    }
    
    // Write character
    *UART0_DR = (uint32_t)c;
}

/*
 * Receive a character
 */
char uart_getc(void)
{
    // Wait for data in receive FIFO
    while (*UART0_FR & UART_FR_RXFE) {
        __asm__ volatile("nop");
    }
    
    // Read character
    return (char)(*UART0_DR & 0xFF);
}

/*
 * Send a string
 */
void uart_puts(const char* str)
{
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

/*
 * Simple printf-like function (limited format support)
 */
void uart_printf(const char* fmt, ...)
{
    // TODO: Implement full printf support
    uart_puts(fmt);
}

/*
 * Check if data is available
 */
bool uart_has_data(void)
{
    return !(*UART0_FR & UART_FR_RXFE);
}
