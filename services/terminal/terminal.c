/*
 * UOS Terminal Service
 * Command interpreter and terminal management
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "kernel/uos_config.h"
#include "kernel/memory.h"
#include "drivers/uart.h"
#include "drivers/framebuffer.h"
#include "drivers/timer.h"
#include "services/terminal.h"
#include "userspace/commands.h"

/* Terminal state */
#define MAX_COMMAND_LENGTH 256
static char command_buffer[MAX_COMMAND_LENGTH];
static uint32_t command_pos = 0;
static bool logged_in = false;
static char current_user[32] = "GUEST";

/* Forward declarations */
static void print_prompt(void);
static void process_command(const char* cmd);
static void print_login_screen(void);
static bool do_login(void);
static void command_to_upper(char* str);

/*
 * Initialize terminal service
 */
void terminal_init(void)
{
    command_pos = 0;
    logged_in = false;
    
    uart_puts("\r\nTerminal service initialized\r\n");
    fb_puts("\nTerminal service initialized\n");
}

/*
 * Main terminal loop
 */
void terminal_main_loop(void)
{
    // Show login screen
    print_login_screen();
    
    // Login process
    if (!do_login()) {
        uart_puts("Login failed. System halted.\r\n");
        fb_puts("Login failed. System halted.\n");
        while(1) {
            __asm__ volatile("wfe");
        }
    }
    
    // Main command loop
    uart_puts("\r\n");
    fb_puts("\n");
    print_prompt();
    
    while (1) {
        if (uart_has_data()) {
            char c = uart_getc();
            
            // Echo character
            uart_putc(c);
            if (c != '\r' && c != '\n') {
                fb_putc(c);
            }
            
            if (c == '\r' || c == '\n') {
                // Process command
                command_buffer[command_pos] = '\0';
                uart_puts("\r\n");
                fb_puts("\n");
                
                if (command_pos > 0) {
                    process_command(command_buffer);
                }
                
                command_pos = 0;
                print_prompt();
            } else if (c == 0x7F || c == 0x08) {
                // Backspace
                if (command_pos > 0) {
                    command_pos--;
                    uart_puts("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                // Printable character
                if (command_pos < MAX_COMMAND_LENGTH - 1) {
                    command_buffer[command_pos++] = c;
                }
            }
        }
        
        // Small delay to reduce CPU usage
        timer_delay_us(100);
    }
}

/*
 * Print command prompt
 */
static void print_prompt(void)
{
    uart_puts("UOS> ");
    fb_puts("UOS> ");
}

/*
 * Convert command to uppercase (OpenVMS style)
 */
static void command_to_upper(char* str)
{
    while (*str) {
        if (*str >= 'a' && *str <= 'z') {
            *str = *str - 'a' + 'A';
        }
        str++;
    }
}

/*
 * Process and execute command
 */
static void process_command(const char* cmd)
{
    char cmd_copy[MAX_COMMAND_LENGTH];
    int i = 0;
    
    // Copy and convert to uppercase
    while (cmd[i] && i < MAX_COMMAND_LENGTH - 1) {
        cmd_copy[i] = cmd[i];
        i++;
    }
    cmd_copy[i] = '\0';
    command_to_upper(cmd_copy);
    
    // Parse command
    char* command = cmd_copy;
    
    // Skip leading spaces
    while (*command == ' ') command++;
    
    // Find end of command word
    char* args = command;
    while (*args && *args != ' ' && *args != '/') args++;
    if (*args) {
        *args++ = '\0';
    }
    
    // Execute command
    execute_command(command, args);
}

/*
 * Print login screen
 */
static void print_login_screen(void)
{
    timer_delay_ms(500);
    
    uart_puts("\r\n");
    uart_puts("--------------------------------------------------------------------------------\r\n");
    uart_puts("                      ROBCO INDUSTRIES UNIFIED OPERATING SYSTEM\r\n");
    uart_puts("                                COPYRIGHT 2075-2077\r\n");
    uart_puts("--------------------------------------------------------------------------------\r\n");
    uart_puts("\r\n");
    
    fb_puts("\n");
    fb_puts("--------------------------------------------------------------------------------\n");
    fb_puts("                      ROBCO INDUSTRIES UNIFIED OPERATING SYSTEM\n");
    fb_puts("                                COPYRIGHT 2075-2077\n");
    fb_puts("--------------------------------------------------------------------------------\n");
    fb_puts("\n");
}

/*
 * Simple login process (intentionally weak)
 */
static bool do_login(void)
{
    char username[32] = {0};
    char password[32] = {0};
    int pos = 0;
    
    // Get username
    uart_puts("Username: ");
    fb_puts("Username: ");
    
    while (1) {
        if (uart_has_data()) {
            char c = uart_getc();
            
            if (c == '\r' || c == '\n') {
                username[pos] = '\0';
                uart_puts("\r\n");
                fb_puts("\n");
                break;
            } else if (c == 0x7F || c == 0x08) {
                if (pos > 0) {
                    pos--;
                    uart_puts("\b \b");
                }
            } else if (c >= 32 && c < 127 && pos < 31) {
                username[pos++] = c;
                uart_putc(c);
                fb_putc(c);
            }
        }
        timer_delay_us(100);
    }
    
    // Get password (don't echo)
    uart_puts("Password: ");
    fb_puts("Password: ");
    pos = 0;
    
    while (1) {
        if (uart_has_data()) {
            char c = uart_getc();
            
            if (c == '\r' || c == '\n') {
                password[pos] = '\0';
                uart_puts("\r\n");
                fb_puts("\n");
                break;
            } else if (c == 0x7F || c == 0x08) {
                if (pos > 0) {
                    pos--;
                    uart_putc('*');
                }
            } else if (c >= 32 && c < 127 && pos < 31) {
                password[pos++] = c;
                uart_putc('*');
                fb_putc('*');
            }
        }
        timer_delay_us(100);
    }
    
    // Convert to uppercase
    command_to_upper(username);
    command_to_upper(password);
    
    // Check credentials (intentionally weak - default accounts)
    bool valid = false;
    
    if ((username[0] == 'A' && username[1] == 'D' && username[2] == 'M' && username[3] == 'I' && username[4] == 'N') &&
        (password[0] == 'A' && password[1] == 'D' && password[2] == 'M' && password[3] == 'I' && password[4] == 'N')) {
        valid = true;
        memcpy(current_user, "ADMIN", 6);
    } else if ((username[0] == 'G' && username[1] == 'U' && username[2] == 'E' && username[3] == 'S' && username[4] == 'T') &&
               (password[0] == 'G' && password[1] == 'U' && password[2] == 'E' && password[3] == 'S' && password[4] == 'T')) {
        valid = true;
        memcpy(current_user, "GUEST", 6);
    } else if ((username[0] == 'M' && username[1] == 'A' && username[2] == 'I' && username[3] == 'N' && username[4] == 'T') &&
               (password[0] == 'M' && password[1] == 'A' && password[2] == 'I' && password[3] == 'N' && password[4] == 'T')) {
        valid = true;
        memcpy(current_user, "MAINT", 6);
    }
    
    if (valid) {
        uart_puts("\r\nLogin successful. Welcome, ");
        uart_puts(current_user);
        uart_puts("!\r\n");
        
        fb_puts("\nLogin successful. Welcome, ");
        fb_puts(current_user);
        fb_puts("!\n");
        
        logged_in = true;
        return true;
    }
    
    uart_puts("\r\nAccess denied. Invalid credentials.\r\n");
    fb_puts("\nAccess denied. Invalid credentials.\n");
    return false;
}

/*
 * Get current user
 */
const char* terminal_get_user(void)
{
    return current_user;
}

/*
 * Check if logged in
 */
bool terminal_is_logged_in(void)
{
    return logged_in;
}
