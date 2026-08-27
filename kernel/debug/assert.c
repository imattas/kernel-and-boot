#include "assert.h"
#include "../core/panic/panic.h"

void kernel_assert(int condition, const char *reason) {
    if (!condition) kernel_panic(reason ? reason : "kernel assertion failed");
}

int kernel_debug_range_valid(uint64_t address, uint64_t size,
                             uint64_t limit) {
    return address <= limit && size <= limit - address;
}
