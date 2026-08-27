#ifndef OS_KERNEL_HEAP_H
#define OS_KERNEL_HEAP_H

#include <stdint.h>

int heap_initialize(void);
void *kmalloc(uint64_t size);
void kfree(void *pointer);

#endif
