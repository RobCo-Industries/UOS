/*
 * UOS Command Execution
 * OpenVMS-style command processor
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "kernel/uos_config.h"
#include "kernel/memory.h"
#include "drivers/uart.h"
#include "drivers/framebuffer.h"
#include "userspace/commands.h"

/* String utility functions */
static int str_compare(const char* s1, const char* s2);
static int str_length(const char* str);

/* Command implementations */
static void cmd_help(const char* args);
static void cmd_directory(const char* args);
static void cmd_type(const char* args);
static void cmd_copy(const char* args);
static void cmd_delete(const char* args);
static void cmd_rename(const char* args);
static void cmd_set(const char* args);
static void cmd_show(const char* args);
static void cmd_run(const char* args);
static void cmd_logout(const char* args);
static void cmd_robco(const char* args);
static void cmd_exit(const char* args);

/* Command table */
typedef struct {
    const char* name;
    void (*handler)(const char* args);
    const char* description;
} command_entry_t;

static const command_entry_t command_table[] = {
    {"HELP",      cmd_help,      "Display command help"},
    {"?",         cmd_help,      "Display command help"},
    {"DIRECTORY", cmd_directory, "List files and directories"},
    {"DIR",       cmd_directory, "List files and directories"},
    {"TYPE",      cmd_type,      "Display file contents"},
    {"COPY",      cmd_copy,      "Copy files"},
    {"DELETE",    cmd_delete,    "Remove files"},
    {"DEL",       cmd_delete,    "Remove files"},
    {"RENAME",    cmd_rename,    "Rename files"},
    {"SET",       cmd_set,       "Configure system settings"},
    {"SHOW",      cmd_show,      "Display system information"},
    {"RUN",       cmd_run,       "Execute a program"},
    {"LOGOUT",    cmd_logout,    "End current session"},
    {"EXIT",      cmd_exit,      "Exit system"},
    {"ROBCO",     cmd_robco,     "RobCo system information"},
    {NULL,        NULL,          NULL}
};

/*
 * Execute a command
 */
void execute_command(const char* command, const char* args)
{
    // Empty command
    if (!command || command[0] == '\0') {
        return;
    }
    
    // Find and execute command
    for (int i = 0; command_table[i].name != NULL; i++) {
        if (str_compare(command, command_table[i].name) == 0) {
            command_table[i].handler(args);
            return;
        }
    }
    
    // Command not found
    uart_puts("% Invalid command: ");
    uart_puts(command);
    uart_puts("\r\n");
    uart_puts("Type HELP for a list of commands.\r\n");
    
    fb_puts("% Invalid command: ");
    fb_puts(command);
    fb_puts("\n");
    fb_puts("Type HELP for a list of commands.\n");
}

/*
 * HELP command
 */
static void cmd_help(const char* args)
{
    (void)args;
    
    uart_puts("\r\nUOS Command Reference:\r\n");
    uart_puts("======================\r\n\r\n");
    
    fb_puts("\nUOS Command Reference:\n");
    fb_puts("======================\n\n");
    
    for (int i = 0; command_table[i].name != NULL; i++) {
        // Skip aliases in help
        if (str_compare(command_table[i].name, "?") == 0 ||
            str_compare(command_table[i].name, "DIR") == 0 ||
            str_compare(command_table[i].name, "DEL") == 0) {
            continue;
        }
        
        uart_puts("  ");
        uart_puts(command_table[i].name);
        
        // Pad to 15 characters
        int len = str_length(command_table[i].name);
        for (int j = len; j < 15; j++) {
            uart_putc(' ');
        }
        
        uart_puts(command_table[i].description);
        uart_puts("\r\n");
        
        fb_puts("  ");
        fb_puts(command_table[i].name);
        
        // Pad to 15 characters for FB
        for (int j = len; j < 15; j++) {
            fb_putc(' ');
        }
        
        fb_puts(command_table[i].description);
        fb_puts("\n");
    }
    
    uart_puts("\r\n");
    fb_puts("\n");
}

/*
 * DIRECTORY command
 */
static void cmd_directory(const char* args)
{
    (void)args;
    
    uart_puts("\r\nDirectory of /SYSTEM\r\n\r\n");
    uart_puts("CONFIG.SYS;1        1024  2077-10-23 09:15\r\n");
    uart_puts("USERS.DB;1          2048  2077-10-23 09:15\r\n");
    uart_puts("TERMINAL.CONF;1      512  2077-10-23 09:15\r\n");
    uart_puts("NETWORK.CONF;1       512  2077-10-23 09:15\r\n");
    uart_puts("\r\nTotal of 4 files, 4096 blocks\r\n\r\n");
    
    fb_puts("\nDirectory of /SYSTEM\n\n");
    fb_puts("CONFIG.SYS;1        1024  2077-10-23 09:15\n");
    fb_puts("USERS.DB;1          2048  2077-10-23 09:15\n");
    fb_puts("TERMINAL.CONF;1      512  2077-10-23 09:15\n");
    fb_puts("NETWORK.CONF;1       512  2077-10-23 09:15\n");
    fb_puts("\nTotal of 4 files, 4096 blocks\n\n");
}

/*
 * TYPE command
 */
static void cmd_type(const char* args)
{
    (void)args;
    
    uart_puts("\r\n% File not found or access denied\r\n\r\n");
    fb_puts("\n% File not found or access denied\n\n");
}

/*
 * COPY command
 */
static void cmd_copy(const char* args)
{
    (void)args;
    
    uart_puts("\r\n% COPY command not yet implemented\r\n\r\n");
    fb_puts("\n% COPY command not yet implemented\n\n");
}

/*
 * DELETE command
 */
static void cmd_delete(const char* args)
{
    (void)args;
    
    uart_puts("\r\n% DELETE command not yet implemented\r\n\r\n");
    fb_puts("\n% DELETE command not yet implemented\n\n");
}

/*
 * RENAME command
 */
static void cmd_rename(const char* args)
{
    (void)args;
    
    uart_puts("\r\n% RENAME command not yet implemented\r\n\r\n");
    fb_puts("\n% RENAME command not yet implemented\n\n");
}

/*
 * SET command
 */
static void cmd_set(const char* args)
{
    // Check for maintenance mode exploit
    if (args && args[0] == 'H' && args[1] == 'A' && args[2] == 'L' && args[3] == 'T') {
        uart_puts("\r\n% WARNING: Maintenance mode initiated\r\n");
        uart_puts("% System will reboot and dump credentials to memory\r\n");
        uart_puts("% Memory address: 0x000FFF00\r\n\r\n");
        
        fb_puts("\n% WARNING: Maintenance mode initiated\n");
        fb_puts("% System will reboot and dump credentials to memory\n");
        fb_puts("% Memory address: 0x000FFF00\n\n");
        
        // Simulate password dump
        uart_puts("% Dumping credentials:\r\n");
        uart_puts("  ADMIN:ADMIN\r\n");
        uart_puts("  GUEST:GUEST\r\n");
        uart_puts("  MAINT:MAINT\r\n\r\n");
        
        fb_puts("% Dumping credentials:\n");
        fb_puts("  ADMIN:ADMIN\n");
        fb_puts("  GUEST:GUEST\n");
        fb_puts("  MAINT:MAINT\n\n");
        
        return;
    }
    
    uart_puts("\r\n% SET command not fully implemented\r\n\r\n");
    fb_puts("\n% SET command not fully implemented\n\n");
}

/*
 * SHOW command
 */
static void cmd_show(const char* args)
{
    (void)args;
    
    uart_puts("\r\nSystem Status:\r\n");
    uart_puts("==============\r\n");
    uart_puts("UOS Version:     " UOS_VERSION_STRING "\r\n");
    uart_puts("Kernel:          " UOS_KERNEL_VERSION "\r\n");
    uart_puts("Uptime:          Running\r\n");
    uart_puts("Memory:          Available\r\n");
    uart_puts("Terminal:        Active\r\n\r\n");
    
    fb_puts("\nSystem Status:\n");
    fb_puts("==============\n");
    fb_puts("UOS Version:     " UOS_VERSION_STRING "\n");
    fb_puts("Kernel:          " UOS_KERNEL_VERSION "\n");
    fb_puts("Uptime:          Running\n");
    fb_puts("Memory:          Available\n");
    fb_puts("Terminal:        Active\n\n");
}

/*
 * RUN command
 */
static void cmd_run(const char* args)
{
    (void)args;
    
    uart_puts("\r\n% RUN command not yet implemented\r\n\r\n");
    fb_puts("\n% RUN command not yet implemented\n\n");
}

/*
 * LOGOUT command
 */
static void cmd_logout(const char* args)
{
    (void)args;
    
    uart_puts("\r\nLogging out...\r\n");
    uart_puts("Session terminated.\r\n\r\n");
    
    fb_puts("\nLogging out...\n");
    fb_puts("Session terminated.\n\n");
    
    // In a real system, this would return to login
    uart_puts("System halted. Please reset.\r\n");
    fb_puts("System halted. Please reset.\n");
    
    while(1) {
        __asm__ volatile("wfe");
    }
}

/*
 * EXIT command
 */
static void cmd_exit(const char* args)
{
    cmd_logout(args);
}

/*
 * ROBCO command - Display system information
 */
static void cmd_robco(const char* args)
{
    (void)args;
    
    uart_puts("\r\n");
    uart_puts("================================================================================\r\n");
    uart_puts("                         ROBCO INDUSTRIES (TM)\r\n");
    uart_puts("                  UNIFIED OPERATING SYSTEM " UOS_VERSION_STRING "\r\n");
    uart_puts("================================================================================\r\n");
    uart_puts("\r\n");
    uart_puts("  Kernel Version:    " UOS_KERNEL_VERSION "\r\n");
    uart_puts("  Copyright:         " UOS_COPYRIGHT "\r\n");
    uart_puts("  Build Date:        2077-10-23\r\n");
    uart_puts("  Target Platform:   ARM Cortex-A Series\r\n");
    uart_puts("\r\n");
    uart_puts("  \"The Future of Computing, Yesterday!\"\r\n");
    uart_puts("\r\n");
    uart_puts("================================================================================\r\n");
    uart_puts("\r\n");
    
    fb_puts("\n");
    fb_puts("================================================================================\n");
    fb_puts("                         ROBCO INDUSTRIES (TM)\n");
    fb_puts("                  UNIFIED OPERATING SYSTEM " UOS_VERSION_STRING "\n");
    fb_puts("================================================================================\n");
    fb_puts("\n");
    fb_puts("  Kernel Version:    " UOS_KERNEL_VERSION "\n");
    fb_puts("  Copyright:         " UOS_COPYRIGHT "\n");
    fb_puts("  Build Date:        2077-10-23\n");
    fb_puts("  Target Platform:   ARM Cortex-A Series\n");
    fb_puts("\n");
    fb_puts("  \"The Future of Computing, Yesterday!\"\n");
    fb_puts("\n");
    fb_puts("================================================================================\n");
    fb_puts("\n");
}

/*
 * String comparison
 */
static int str_compare(const char* s1, const char* s2)
{
    while (*s1 && *s2) {
        if (*s1 != *s2) {
            return *s1 - *s2;
        }
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

/*
 * String length
 */
static int str_length(const char* str)
{
    int len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}
