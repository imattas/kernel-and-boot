#ifndef OS_USERLAND_LIB_OS_H
#define OS_USERLAND_LIB_OS_H

#include <stdint.h>

#define OS_SYSCALL_ERROR UINT64_MAX

typedef struct {
    uint64_t id;
    uint64_t parent_id;
    uint32_t state;
    int32_t exit_status;
} os_process_info_t;

uint64_t os_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3);

static inline uint64_t os_getpid(void) { return os_syscall3(3, 0, 0, 0); }
static inline uint64_t os_getppid(void) { return os_syscall3(40, 0, 0, 0); }
static inline uint64_t os_getuid(void) { return os_syscall3(38, 0, 0, 0); }
static inline uint64_t os_getgid(void) { return os_syscall3(39, 0, 0, 0); }

static inline uint64_t os_process_list(uint64_t *ids, uint64_t capacity) {
    return os_syscall3(42, (uint64_t)(uintptr_t)ids, capacity, 0);
}

static inline uint64_t os_process_status(uint64_t process_id,
                                         os_process_info_t *info) {
    return os_syscall3(43, process_id, (uint64_t)(uintptr_t)info, 0);
}

static inline uint64_t os_reap(uint64_t process_id) {
    return os_syscall3(44, process_id, 0, 0);
}

static inline uint64_t os_getenv(const char *key, void *value,
                                 uint64_t capacity) {
    return os_syscall3(45, (uint64_t)(uintptr_t)key,
                       (uint64_t)(uintptr_t)value, capacity);
}

static inline uint64_t os_set_inheritable(uint64_t descriptor,
                                          uint64_t enabled) {
    return os_syscall3(30, descriptor, enabled, 0);
}

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

static inline uint64_t os_file_read(uint64_t descriptor, void *buffer,
                                    uint64_t length) {
    return os_syscall3(10, descriptor, (uint64_t)(uintptr_t)buffer, length);
}

static inline uint64_t os_mkdir(const char *path, uint64_t length,
                                uint64_t mode) {
    return os_syscall3(32, (uint64_t)(uintptr_t)path, length, mode);
}

static inline uint64_t os_unlink(const char *path, uint64_t length) {
    return os_syscall3(33, (uint64_t)(uintptr_t)path, length, 0);
}

static inline uint64_t os_rmdir(const char *path, uint64_t length) {
    return os_syscall3(35, (uint64_t)(uintptr_t)path, length, 0);
}

static inline uint64_t os_chmod(const char *path, uint64_t length,
                                uint64_t mode) {
    return os_syscall3(36, (uint64_t)(uintptr_t)path, length, mode);
}

static inline uint64_t os_create(const char *path, uint64_t length,
                                 uint64_t flags) {
    return os_syscall3(31, (uint64_t)(uintptr_t)path, length, flags);
}

static inline uint64_t os_file_write(uint64_t descriptor, const void *buffer,
                                     uint64_t length) {
    return os_syscall3(11, descriptor, (uint64_t)(uintptr_t)buffer, length);
}

static inline uint64_t os_spawn(const char *path, uint64_t length,
                                const char *arguments) {
    return os_syscall3(41, (uint64_t)(uintptr_t)path, length,
                       (uint64_t)(uintptr_t)arguments);
}

static inline uint64_t os_wait(uint64_t process_id, int32_t *status) {
    return os_syscall3(8, process_id, (uint64_t)(uintptr_t)status, 0);
}

static inline uint64_t os_yield(void) { return os_syscall3(21, 0, 0, 0); }

__attribute__((noreturn)) static inline void os_exit(int32_t status) {
    (void)os_syscall3(15, (uint32_t)status, 0, 0);
    for (;;) __asm__ volatile ("ud2");
}

#endif
