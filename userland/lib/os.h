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

static inline uint64_t os_read(uint64_t descriptor, void *buffer,
                               uint64_t length) {
    return os_syscall3(10, descriptor, (uint64_t)(uintptr_t)buffer, length);
}

static inline uint64_t os_getcwd(char *buffer, uint64_t capacity) {
    return os_syscall3(23, (uint64_t)(uintptr_t)buffer, capacity, 0);
}

static inline uint64_t os_chdir(const char *path, uint64_t length) {
    return os_syscall3(22, (uint64_t)(uintptr_t)path, length, 0);
}

static inline uint64_t os_open(const char *path, uint64_t length,
                               uint64_t flags) {
    return os_syscall3(9, (uint64_t)(uintptr_t)path, length, flags);
}

static inline uint64_t os_close(uint64_t descriptor) {
    return os_syscall3(12, descriptor, 0, 0);
}

static inline uint64_t os_readdir(uint64_t descriptor, void *entry) {
    return os_syscall3(14, descriptor, (uint64_t)(uintptr_t)entry, 0);
}

static inline uint64_t os_yield(void) { return os_syscall3(21, 0, 0, 0); }

__attribute__((noreturn)) static inline void os_exit(int32_t status) {
    (void)os_syscall3(15, (uint32_t)status, 0, 0);
    for (;;) __asm__ volatile ("ud2");
}

#endif
