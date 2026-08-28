#ifndef OS_USERLAND_LIB_OS_H
#define OS_USERLAND_LIB_OS_H

#include <stdint.h>

#define OS_SYSCALL_ERROR UINT64_MAX
#define OS_SPAWN_REDIRECT_MAGIC UINT64_C(0x4f53505245444952)

typedef struct {
    uint64_t magic;
    uint64_t arguments;
    uint32_t input_handle;
    uint32_t output_handle;
} os_spawn_redirect_t;

typedef struct {
    uint64_t owner_uid;
    uint64_t owner_gid;
    uint32_t mode;
    uint32_t type;
} os_stat_t;

typedef struct {
    uint64_t id;
    uint64_t parent_id;
    uint32_t state;
    int32_t exit_status;
} os_process_info_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} os_datetime_t;

enum {
    OS_PROCESS_NEW,
    OS_PROCESS_READY,
    OS_PROCESS_RUNNING,
    OS_PROCESS_EXITED
};

uint64_t os_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3);

static inline uint64_t os_getpid(void) { return os_syscall3(3, 0, 0, 0); }
static inline uint64_t os_clock_monotonic(void) {
    return os_syscall3(2, 0, 0, 0);
}
static inline uint64_t os_clock_realtime(os_datetime_t *datetime) {
    return os_syscall3(49, (uint64_t)(uintptr_t)datetime, 0, 0);
}
static inline uint64_t os_getppid(void) { return os_syscall3(40, 0, 0, 0); }
static inline uint64_t os_getuid(void) { return os_syscall3(38, 0, 0, 0); }
static inline uint64_t os_getgid(void) { return os_syscall3(39, 0, 0, 0); }

static inline uint64_t os_signal_send_to(uint64_t process_id,
                                         uint64_t signal_number) {
    return os_syscall3(7, process_id, signal_number, 0);
}

static inline uint64_t os_channel_create(void) {
    return os_syscall3(16, 0, 0, 0);
}

static inline uint64_t os_channel_send(uint64_t channel, const void *data,
                                       uint64_t length) {
    return os_syscall3(17, channel, (uint64_t)(uintptr_t)data, length);
}

static inline uint64_t os_channel_receive(uint64_t channel, void *data,
                                           uint64_t capacity) {
    return os_syscall3(18, channel, (uint64_t)(uintptr_t)data, capacity);
}

static inline uint64_t os_channel_send_wait(uint64_t channel, const void *data,
                                             uint64_t length) {
    return os_syscall3(19, channel, (uint64_t)(uintptr_t)data, length);
}

static inline uint64_t os_channel_receive_wait(uint64_t channel, void *data,
                                                uint64_t capacity) {
    return os_syscall3(20, channel, (uint64_t)(uintptr_t)data, capacity);
}

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

static inline uint64_t os_setenv(const char *key, const char *value) {
    return os_syscall3(46, (uint64_t)(uintptr_t)key,
                       (uint64_t)(uintptr_t)value, 0);
}

static inline uint64_t os_unsetenv(const char *key) {
    return os_syscall3(47, (uint64_t)(uintptr_t)key, 0, 0);
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

static inline uint64_t os_seek(uint64_t descriptor, uint64_t offset) {
    return os_syscall3(13, descriptor, offset, 0);
}

static inline uint64_t os_truncate(uint64_t descriptor, uint64_t size) {
    return os_syscall3(26, descriptor, size, 0);
}

static inline uint64_t os_dup(uint64_t descriptor, uint64_t rights) {
    return os_syscall3(25, descriptor, rights, 0);
}

static inline uint64_t os_pipe(uint32_t *read_handle, uint32_t *write_handle) {
    struct { uint32_t read_handle; uint32_t write_handle; } result;
    uint64_t status = os_syscall3(48, (uint64_t)(uintptr_t)&result, 0, 0);
    if (status == OS_SYSCALL_ERROR) return status;
    if (read_handle) *read_handle = result.read_handle;
    if (write_handle) *write_handle = result.write_handle;
    return 0;
}

static inline uint64_t os_fstat(uint64_t descriptor, os_stat_t *stat) {
    return os_syscall3(24, descriptor, (uint64_t)(uintptr_t)stat, 0);
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

static inline uint64_t os_rename(const char *old_path, uint64_t old_length,
                                 const char *new_path, uint64_t new_length) {
    return os_syscall3(34, (uint64_t)(uintptr_t)old_path,
                       (uint64_t)(uintptr_t)new_path,
                       old_length | (new_length << 32));
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

static inline uint64_t os_spawn_redirected(const char *path, uint64_t length,
                                           const char *arguments,
                                           uint32_t input_handle,
                                           uint32_t output_handle) {
    os_spawn_redirect_t request = {OS_SPAWN_REDIRECT_MAGIC,
                                   (uint64_t)(uintptr_t)arguments,
                                   input_handle, output_handle};
    return os_syscall3(41, (uint64_t)(uintptr_t)path, length,
                       (uint64_t)(uintptr_t)&request);
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
