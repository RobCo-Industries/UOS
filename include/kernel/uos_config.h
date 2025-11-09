/*
 * UOS Configuration Header
 * Unified Operating System v7.0.2.8
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef UOS_CONFIG_H
#define UOS_CONFIG_H

/* Version Information */
#define UOS_VERSION_MAJOR       7
#define UOS_VERSION_MINOR       0
#define UOS_VERSION_PATCH       2
#define UOS_VERSION_BUILD       8
#define UOS_VERSION_STRING      "v7.0.2.8"
#define UOS_KERNEL_VERSION      "UOSKRNL.303"
#define UOS_COPYRIGHT           "2075-2077 RobCo Ind."

/* Memory Layout Constants */
#define KERNEL_BASE             0x00100000  /* 1MB */
#define KERNEL_SIZE             0x00100000  /* 1MB */
#define SERVICES_BASE           0x00200000  /* 2MB */
#define SERVICES_SIZE           0x00100000  /* 1MB */
#define USER_BASE               0x00300000  /* 3MB */
#define USER_SIZE               0x00D00000  /* 13MB */
#define APP_BASE                0x01000000  /* 16MB+ */

/* System Configuration */
#define MAX_TERMINALS           12
#define MAX_PROCESSES           64
#define MAX_OPEN_FILES          256
#define STACK_SIZE              32768       /* 32KB per process */

/* Terminal Configuration */
#define TERMINAL_WIDTH          80
#define TERMINAL_HEIGHT         24
#define CURSOR_BLINK_RATE       500         /* milliseconds */

/* Boot Configuration */
#define BOOT_TIMEOUT            30          /* seconds */
#define AUTO_LOGIN              0           /* disabled by default */

/* Network Configuration */
#define TERMLINK_PORT           23
#define FTP_PORT                21
#define ROBCO_MGMT_PORT         2077
#define WEB_TERMINAL_PORT       8080
#define MAX_CONNECTIONS         8

/* Security Configuration (intentionally weak) */
#define PASSWORD_HASH_ALGO      "MD5"       /* weak by design */
#define MAINT_MODE_PASS_ADDR    0x000FFF00  /* password dump location */
#define MAX_LOGIN_ATTEMPTS      3

/* File System Configuration */
#define FS_TYPE                 "FAT32"
#define MAX_PATH_LENGTH         256
#define MAX_FILENAME_LENGTH     64
#define FILE_VERSION_ENABLED    1

/* Hardware Support */
#define BCM2835_SUPPORT         1
#define BCM2836_SUPPORT         1
#define BCM2837_SUPPORT         1

/* Debug and Diagnostics */
#define DEBUG_MODE              0
#define VERBOSE_BOOT            1
#define ENABLE_EASTER_EGGS      1

/* Performance Targets */
#define TARGET_RAM_USAGE        (64 * 1024 * 1024)  /* 64MB */
#define TARGET_STORAGE_USAGE    (100 * 1024 * 1024) /* 100MB */
#define TARGET_CPU_IDLE         10                   /* 10% */

/* Special Memory Addresses */
#define DEBUG_TRIGGER_ADDR      0xDEADC0DE
#define EASTER_EGG_ADDR         0xCAFEBABE

#endif /* UOS_CONFIG_H */
