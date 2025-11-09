/*
 * UOS Memory Management Header
 * Copyright 2075-2077 RobCo Industries
 */

#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include "kernel/types.h"

/* Memory management functions */
void memory_init(void);
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void kfree(void* ptr);
void memory_stats(uint32_t* total, uint32_t* used, uint32_t* free);

/* Memory operations */
void* memset(void* dest, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

#endif /* KERNEL_MEMORY_H */
