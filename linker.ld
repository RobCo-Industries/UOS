/*
 * UOS Linker Script
 * Memory layout for ARM kernel
 * Copyright 2075-2077 RobCo Industries
 */

/* Entry point */
ENTRY(_start)

/* Memory regions */
MEMORY
{
    /* Kernel loads at 1MB (0x100000) */
    ram : ORIGIN = 0x00100000, LENGTH = 0x00F00000
}

SECTIONS
{
    /* Kernel code section */
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
        . = ALIGN(4096);
    } > ram

    /* Read-only data */
    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(4096);
    } > ram

    /* Initialized data */
    .data : {
        *(.data .data.*)
        . = ALIGN(4096);
    } > ram

    /* Uninitialized data */
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
        . = ALIGN(4096);
    } > ram

    /* Stack (grows downward) */
    .stack : {
        . = ALIGN(4096);
        __stack_start = .;
        . += 0x8000; /* 32KB stack */
        __stack_end = .;
    } > ram

    /* Heap (grows upward) */
    .heap : {
        . = ALIGN(4096);
        __heap_start = .;
        /* Heap extends to end of RAM */
    } > ram

    /* Symbol table (debugging) */
    __kernel_end = .;
    
    /* Discard sections */
    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}
