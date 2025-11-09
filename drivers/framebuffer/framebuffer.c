/*
 * UOS Framebuffer Driver
 * Display driver for terminal output
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "drivers/framebuffer.h"

/* Mailbox registers */
#ifndef PERIPHERAL_BASE
#define PERIPHERAL_BASE 0x3F000000
#endif

#define MAILBOX_BASE (PERIPHERAL_BASE + 0xB880)
#define MAILBOX_READ  ((volatile uint32_t*)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS ((volatile uint32_t*)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE ((volatile uint32_t*)(MAILBOX_BASE + 0x20))

#define MAILBOX_EMPTY 0x40000000
#define MAILBOX_FULL  0x80000000

/* Framebuffer request structure (aligned to 16 bytes) */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t virtual_width;
    uint32_t virtual_height;
    uint32_t pitch;
    uint32_t depth;
    uint32_t x_offset;
    uint32_t y_offset;
    uint32_t pointer;
    uint32_t size;
} __attribute__((aligned(16))) fb_init_t;

/* Global framebuffer state */
static fb_init_t fb_info;
static uint32_t* framebuffer = NULL;
static uint32_t fb_width = 640;
static uint32_t fb_height = 480;
static uint32_t fb_pitch = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

/* Font: 8x16 (simplified - drawing rectangles for now) */
#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

/* Color scheme (green phosphor by default) */
static uint32_t fg_color = 0x0033FF33; // Green
static uint32_t bg_color = 0x00000000; // Black

/*
 * Mailbox write
 */
static void mailbox_write(uint32_t channel, uint32_t data)
{
    while (*MAILBOX_STATUS & MAILBOX_FULL) {
        __asm__ volatile("nop");
    }
    *MAILBOX_WRITE = (data & 0xFFFFFFF0) | (channel & 0xF);
}

/*
 * Mailbox read
 */
static uint32_t mailbox_read(uint32_t channel)
{
    while (1) {
        while (*MAILBOX_STATUS & MAILBOX_EMPTY) {
            __asm__ volatile("nop");
        }
        uint32_t data = *MAILBOX_READ;
        if ((data & 0xF) == channel) {
            return data & 0xFFFFFFF0;
        }
    }
}

/*
 * Initialize framebuffer
 */
void framebuffer_init(void)
{
    fb_info.width = fb_width;
    fb_info.height = fb_height;
    fb_info.virtual_width = fb_width;
    fb_info.virtual_height = fb_height;
    fb_info.pitch = 0;
    fb_info.depth = 32;
    fb_info.x_offset = 0;
    fb_info.y_offset = 0;
    fb_info.pointer = 0;
    fb_info.size = 0;
    
    // Send request via mailbox (channel 1)
    mailbox_write(1, ((uint32_t)&fb_info) | 0x40000000);
    mailbox_read(1);
    
    if (fb_info.pointer) {
        framebuffer = (uint32_t*)(fb_info.pointer & 0x3FFFFFFF);
        fb_pitch = fb_info.pitch;
        fb_width = fb_info.width;
        fb_height = fb_info.height;
        
        // Clear screen
        fb_clear();
    }
}

/*
 * Clear screen
 */
void fb_clear(void)
{
    if (!framebuffer) return;
    
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            framebuffer[y * (fb_pitch / 4) + x] = bg_color;
        }
    }
    
    cursor_x = 0;
    cursor_y = 0;
}

/*
 * Draw a character (simplified - just a rectangle for now)
 */
static void draw_char(char c, uint32_t x, uint32_t y)
{
    if (!framebuffer) return;
    if (x + CHAR_WIDTH > fb_width || y + CHAR_HEIGHT > fb_height) return;
    
    // For now, just draw a colored rectangle to represent the character
    // TODO: Implement proper font rendering
    for (uint32_t dy = 0; dy < CHAR_HEIGHT; dy++) {
        for (uint32_t dx = 0; dx < CHAR_WIDTH; dx++) {
            if (c >= ' ' && c <= '~') {
                // Draw foreground for printable characters
                framebuffer[(y + dy) * (fb_pitch / 4) + (x + dx)] = fg_color;
            } else {
                // Draw background for non-printable
                framebuffer[(y + dy) * (fb_pitch / 4) + (x + dx)] = bg_color;
            }
        }
    }
}

/*
 * Put a character at current cursor position
 */
void fb_putc(char c)
{
    if (!framebuffer) return;
    
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += CHAR_HEIGHT;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c >= ' ') {
        draw_char(c, cursor_x, cursor_y);
        cursor_x += CHAR_WIDTH;
        
        if (cursor_x + CHAR_WIDTH > fb_width) {
            cursor_x = 0;
            cursor_y += CHAR_HEIGHT;
        }
    }
    
    // Scroll if needed
    if (cursor_y + CHAR_HEIGHT > fb_height) {
        fb_scroll();
        cursor_y = fb_height - CHAR_HEIGHT;
    }
}

/*
 * Put a string
 */
void fb_puts(const char* str)
{
    while (*str) {
        fb_putc(*str++);
    }
}

/*
 * Scroll screen up by one line
 */
void fb_scroll(void)
{
    if (!framebuffer) return;
    
    // Copy lines up
    for (uint32_t y = CHAR_HEIGHT; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            framebuffer[(y - CHAR_HEIGHT) * (fb_pitch / 4) + x] = 
                framebuffer[y * (fb_pitch / 4) + x];
        }
    }
    
    // Clear bottom line
    for (uint32_t y = fb_height - CHAR_HEIGHT; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            framebuffer[y * (fb_pitch / 4) + x] = bg_color;
        }
    }
}

/*
 * Set color scheme
 */
void fb_set_colors(uint32_t fg, uint32_t bg)
{
    fg_color = fg;
    bg_color = bg;
}
