#ifndef OS_KERNEL_LIB_MEMORY_H
#define OS_KERNEL_LIB_MEMORY_H

#include <stdint.h>

void *memcpy(void *destination, const void *source, uint64_t size);
void *memset(void *destination, int value, uint64_t size);
int memcmp(const void *left, const void *right, uint64_t size);

#endif
