/*
 * UOS Type Definitions
 * Unified Operating System
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef UOS_TYPES_H
#define UOS_TYPES_H

/* Standard integer types */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* Size types */
typedef unsigned long       size_t;
typedef signed long         ssize_t;
typedef unsigned long       uintptr_t;

/* Boolean type */
typedef enum {
    false = 0,
    true = 1
} bool;

/* NULL pointer */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Status codes */
typedef enum {
    UOS_OK                  = 0x00000000,
    UOS_ERROR               = 0xFFFFFFFF,
    UOS_ERROR_NETWORK       = 0x03C663A1,
    UOS_ERROR_CORRUPTION    = 0xFFF7329A,
    UOS_ERROR_PROCESSOR     = 0xC00FE321,
    UOS_ERROR_PANIC         = 0xDEADBEEF,
    UOS_ERROR_NO_MEMORY     = 0x01010001,
    UOS_ERROR_NOT_FOUND     = 0x01020001,
    UOS_ERROR_ACCESS_DENIED = 0x02010001,
    UOS_ERROR_INVALID_PARAM = 0x01030001
} uos_status_t;

/* Process states */
typedef enum {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

/* File types */
typedef enum {
    FILE_TYPE_TXT = 0,
    FILE_TYPE_DAT,
    FILE_TYPE_EXE,
    FILE_TYPE_HLP,
    FILE_TYPE_LOG,
    FILE_TYPE_CFG,
    FILE_TYPE_SYS,
    FILE_TYPE_HOL
} file_type_t;

/* Terminal colors */
typedef enum {
    COLOR_GREEN_PHOSPHOR = 0,
    COLOR_AMBER_PHOSPHOR,
    COLOR_BLUE_PHOSPHOR,
    COLOR_WHITE_ON_BLACK
} terminal_color_t;

#endif /* UOS_TYPES_H */
