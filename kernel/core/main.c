/*
 * UOS Kernel Main
 * Unified Operating System Core Initialization
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "kernel/uos_config.h"
#include "drivers/uart.h"
#include "drivers/framebuffer.h"
#include "drivers/timer.h"
#include "kernel/memory.h"
#include "services/terminal.h"

/* Forward declarations */
static void print_boot_header(void);
static void initialize_subsystems(void);
static void boot_sequence_animation(void);

/*
 * Kernel Main Entry Point
 * Called from boot.S after basic initialization
 */
void kernel_main(uint32_t board_type, uint32_t machine_type, uint32_t atags_addr)
{
    (void)board_type;    // Reserved for future use
    (void)machine_type;  // Reserved for future use
    (void)atags_addr;    // Reserved for future use

    // Initialize UART for early debugging
    uart_init();
    
    // Initialize framebuffer for terminal display
    framebuffer_init();
    
    // Print boot header
    print_boot_header();
    
    // Boot sequence with authentic delays
    boot_sequence_animation();
    
    // Initialize core subsystems
    initialize_subsystems();
    
    // Initialize terminal service
    terminal_init();
    
    // Start command interpreter
    terminal_main_loop();
    
    // Should never reach here
    while(1) {
        __asm__ volatile("wfe");
    }
}

/*
 * Print UOS Boot Header
 */
static void print_boot_header(void)
{
    uart_puts("\r\n");
    uart_puts("================================================================================\r\n");
    uart_puts("         UNIFIED OPERATING SYSTEM (UOS) " UOS_VERSION_STRING "\r\n");
    uart_puts("         KERNEL VERSION: " UOS_KERNEL_VERSION "\r\n");
    uart_puts("         COPYRIGHT " UOS_COPYRIGHT "\r\n");
    uart_puts("         ROBCO INDUSTRIES TERMINAL OPERATING SYSTEM\r\n");
    uart_puts("================================================================================\r\n");
    uart_puts("\r\n");
    
    fb_puts("================================================================================\n");
    fb_puts("         UNIFIED OPERATING SYSTEM (UOS) " UOS_VERSION_STRING "\n");
    fb_puts("         KERNEL VERSION: " UOS_KERNEL_VERSION "\n");
    fb_puts("         COPYRIGHT " UOS_COPYRIGHT "\n");
    fb_puts("         ROBCO INDUSTRIES TERMINAL OPERATING SYSTEM\n");
    fb_puts("================================================================================\n");
    fb_puts("\n");
}

/*
 * Animate boot sequence
 */
static void boot_sequence_animation(void)
{
    const char* boot_messages[] = {
        "RETROS-BIOS v4.2.0.1 - Basic system initialization complete",
        "MFBootAgent v2.3.0 - Boot device scan successful",
        "Loading UOS kernel from SD card...",
        "Kernel image verified",
        "Transferring control to operating system...",
        "",
        "Initializing memory subsystem...",
        "Detecting hardware configuration...",
        "Starting system services...",
        ""
    };
    
    for (int i = 0; boot_messages[i] != NULL && boot_messages[i][0] != '\0'; i++) {
        // Check for empty string to end
        if (boot_messages[i][0] == '\0') {
            break;
        }
        
        uart_puts(boot_messages[i]);
        uart_puts("\r\n");
        
        fb_puts(boot_messages[i]);
        fb_puts("\n");
        
        // Authentic delay for terminal effect
        timer_delay_ms(150);
    }
}

/*
 * Initialize core subsystems
 */
static void initialize_subsystems(void)
{
    uart_puts("Initializing subsystems:\r\n");
    fb_puts("Initializing subsystems:\n");
    
    // Memory management
    uart_puts("  [OK] Memory Manager\r\n");
    fb_puts("  [OK] Memory Manager\n");
    memory_init();
    timer_delay_ms(100);
    
    // Timer system
    uart_puts("  [OK] System Timer\r\n");
    fb_puts("  [OK] System Timer\n");
    timer_init();
    timer_delay_ms(100);
    
    // File system (placeholder)
    uart_puts("  [OK] File System Service\r\n");
    fb_puts("  [OK] File System Service\n");
    timer_delay_ms(100);
    
    // Network service (placeholder)
    uart_puts("  [OK] Network Service\r\n");
    fb_puts("  [OK] Network Service\n");
    timer_delay_ms(100);
    
    // Security service (placeholder)
    uart_puts("  [OK] Security Service\r\n");
    fb_puts("  [OK] Security Service\n");
    timer_delay_ms(100);
    
    uart_puts("\r\n");
    fb_puts("\n");
}
