#ifndef OS_KERNEL_SYSCALL_ABI_H
#define OS_KERNEL_SYSCALL_ABI_H

#include <stdint.h>

enum {
    OS_SYSCALL_DEBUG = 0,
    OS_SYSCALL_WRITE = 1,
    OS_SYSCALL_CLOCK_MONOTONIC = 2,
    OS_SYSCALL_GETPID = 3,
    OS_SYSCALL_SIGNAL_MASK = 4,
    OS_SYSCALL_SIGNAL_SEND = 5,
    OS_SYSCALL_SIGNAL_NEXT = 6,
    OS_SYSCALL_SIGNAL_SEND_TO = 7,
    OS_SYSCALL_PROCESS_WAIT = 8,
    OS_SYSCALL_OPEN = 9,
    OS_SYSCALL_READ = 10,
    OS_SYSCALL_WRITE_FILE = 11,
    OS_SYSCALL_CLOSE = 12,
    OS_SYSCALL_SEEK = 13,
    OS_SYSCALL_READDIR = 14,
    OS_SYSCALL_EXIT = 15,
    OS_SYSCALL_CHANNEL_CREATE = 16,
    OS_SYSCALL_CHANNEL_SEND = 17,
    OS_SYSCALL_CHANNEL_RECEIVE = 18,
    OS_SYSCALL_CHANNEL_SEND_WAIT = 19,
    OS_SYSCALL_CHANNEL_RECEIVE_WAIT = 20,
    OS_SYSCALL_YIELD = 21,
    OS_SYSCALL_CHDIR = 22,
    OS_SYSCALL_GETCWD = 23,
    OS_SYSCALL_FSTAT = 24,
    OS_SYSCALL_DUP = 25,
    OS_SYSCALL_TRUNCATE = 26,
    OS_SYSCALL_MAP_ANONYMOUS = 27,
    OS_SYSCALL_UNMAP_ANONYMOUS = 28,
    OS_SYSCALL_PROTECT_MEMORY = 29,
    OS_SYSCALL_SET_INHERITABLE = 30,
    OS_SYSCALL_CREATE = 31,
    OS_SYSCALL_MKDIR = 32,
    OS_SYSCALL_UNLINK = 33,
    OS_SYSCALL_RENAME = 34,
    OS_SYSCALL_RMDIR = 35,
    OS_SYSCALL_CHMOD = 36,
    OS_SYSCALL_STAT = 37,
    OS_SYSCALL_GETUID = 38,
    OS_SYSCALL_GETGID = 39,
    OS_SYSCALL_GETPPID = 40,
    OS_SYSCALL_SPAWN = 41,
    OS_SYSCALL_PROCESS_LIST = 42,
    OS_SYSCALL_PROCESS_STATUS = 43,
    OS_SYSCALL_PROCESS_REAP = 44,
    OS_SYSCALL_GETENV = 45,
    OS_SYSCALL_SETENV = 46,
    OS_SYSCALL_UNSETENV = 47,
    OS_SYSCALL_PIPE = 48,
    OS_SYSCALL_CLOCK_REALTIME = 49
};

#define OS_SYSCALL_ERROR UINT64_MAX
#define OS_SYSCALL_MAX_WRITE 256U
#define OS_SYSCALL_MAX_PATH 256U
#define OS_SYSCALL_MAX_MESSAGE 64U
#define OS_SPAWN_REDIRECT_MAGIC UINT64_C(0x4f53505245444952)

typedef struct {
    char name[32];
    uint32_t type;
} os_syscall_dirent_t;

typedef struct {
    uint64_t owner_uid;
    uint64_t owner_gid;
    uint32_t mode;
    uint32_t type;
} os_syscall_stat_t;

typedef struct {
    uint64_t id;
    uint64_t parent_id;
    uint32_t state;
    int32_t exit_status;
} os_syscall_process_info_t;

typedef struct {
    uint32_t read_handle;
    uint32_t write_handle;
} os_syscall_pipe_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} os_syscall_datetime_t;

typedef struct {
    uint64_t magic;
    uint64_t arguments;
    uint32_t input_handle;
    uint32_t output_handle;
} os_spawn_redirect_t;

#endif
