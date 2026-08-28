#ifndef OS_USERLAND_LIB_OS_H
#define OS_USERLAND_LIB_OS_H

#include <stdint.h>

#define OS_SYSCALL_ERROR UINT64_MAX

uint64_t os_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3);

static inline uint64_t os_write(uint64_t descriptor, const void *buffer,
                                uint64_t length) {
    return os_syscall3(1, descriptor, (uint64_t)(uintptr_t)buffer, length);
}

__attribute__((noreturn)) static inline void os_exit(int32_t status) {
    (void)os_syscall3(15, (uint32_t)status, 0, 0);
    for (;;) __asm__ volatile ("ud2");
}

#endif
