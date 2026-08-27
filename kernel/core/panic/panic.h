#ifndef OS_KERNEL_CORE_PANIC_H
#define OS_KERNEL_CORE_PANIC_H

#include <stdint.h>

__attribute__((noreturn)) void kernel_panic(const char *reason);
__attribute__((noreturn)) void kernel_panic_exception(uint64_t vector);
__attribute__((noreturn)) void kernel_panic_exception_frame(
    uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs,
    uint64_t rflags);
uint32_t kernel_panic_state(void);

#endif
