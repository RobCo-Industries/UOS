/*
 * UOS Memory Management
 * Simple flat memory allocator
 * Copyright 2075-2077 RobCo Industries
 */

#include "kernel/types.h"
#include "kernel/uos_config.h"
#include "kernel/memory.h"

/* External symbols from linker script */
extern uint32_t __heap_start;
extern uint32_t __heap_end;
extern uint32_t __kernel_end;

/* Memory manager state */
static uint32_t heap_current;
static uint32_t heap_max;
static uint32_t total_allocated;

/*
 * Initialize memory management
 */
void memory_init(void)
{
    heap_current = (uint32_t)&__heap_start;
    heap_max = (uint32_t)&__heap_start + USER_SIZE + APP_BASE;
    total_allocated = 0;
}

/*
 * Simple bump allocator (for now)
 * TODO: Implement proper free/realloc
 */
void* kmalloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    
    // Align to 4-byte boundary
    size = (size + 3) & ~3;
    
    // Check if we have enough space
    if (heap_current + size > heap_max) {
        return NULL; // Out of memory
    }
    
    void* ptr = (void*)heap_current;
    heap_current += size;
    total_allocated += size;
    
    return ptr;
}

/*
 * Allocate zeroed memory
 */
void* kzalloc(size_t size)
{
    void* ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Free memory (stub - not implemented yet)
 */
void kfree(void* ptr)
{
    (void)ptr;
    // TODO: Implement proper memory freeing
}

/*
 * Get memory usage statistics
 */
void memory_stats(uint32_t* total, uint32_t* used, uint32_t* free)
{
    if (total) {
        *total = heap_max - (uint32_t)&__heap_start;
    }
    if (used) {
        *used = total_allocated;
    }
    if (free) {
        *free = heap_max - heap_current;
    }
}

/*
 * Memory operations
 */

void* memset(void* dest, int c, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    while (n--) {
        *d++ = (uint8_t)c;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}
