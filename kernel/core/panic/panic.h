#ifndef OS_KERNEL_CORE_PANIC_H
#define OS_KERNEL_CORE_PANIC_H

#include <stdint.h>

__attribute__((noreturn)) void kernel_panic(const char *reason);
__attribute__((noreturn)) void kernel_panic_exception(uint64_t vector);
uint32_t kernel_panic_state(void);

#endif
