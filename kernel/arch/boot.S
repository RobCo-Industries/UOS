/*
 * UOS Kernel Entry Point
 * ARM Assembly Bootstrap Code
 * Copyright 2075-2077 RobCo Industries
 */

.section ".text.boot"

.global _start

_start:
    // Save boot parameters from MFBootAgent
    // r0 = board type
    // r1 = machine type  
    // r2 = ATAGS pointer
    mov r4, r0
    mov r5, r1
    mov r6, r2

    // Set stack pointer
    ldr sp, =__stack_end

    // Clear BSS section
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0

clear_bss_loop:
    cmp r0, r1
    bge clear_bss_done
    str r2, [r0], #4
    b clear_bss_loop

clear_bss_done:
    // Restore boot parameters
    mov r0, r4
    mov r1, r5
    mov r2, r6

    // Jump to C kernel main
    bl kernel_main

    // Halt if kernel returns (should never happen)
halt:
    wfe
    b halt

// Interrupt vector table (placeholder)
.section ".text"

.global __aeabi_uidiv
.global __aeabi_uidivmod
.global __aeabi_idiv
.global __aeabi_idivmod

// Unsigned division (placeholder - will use libgcc)
__aeabi_uidiv:
__aeabi_uidivmod:
    bx lr

// Signed division (placeholder - will use libgcc)
__aeabi_idiv:
__aeabi_idivmod:
    bx lr
