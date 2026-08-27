#ifndef OS_KERNEL_DEBUG_ASSERT_H
#define OS_KERNEL_DEBUG_ASSERT_H

#include <stdint.h>

void kernel_assert(int condition, const char *reason);
int kernel_debug_range_valid(uint64_t address, uint64_t size,
                             uint64_t limit);

#endif
