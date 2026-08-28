#include "../../lib/runtime.h"

typedef struct {
    char name[32];
    uint32_t type;
} find_dirent_t;

static int write_path(const char *path, uint64_t length) {
    return os_userland_write_all(1, path, length) &&
           os_userland_write_all(1, "\r\n", 2);
}

static int find_path(const char *path, uint64_t length, uint32_t depth) {
    if (!path || length == 0 || length >= 256 || depth > 16 ||
        !write_path(path, length)) return 0;
    uint64_t descriptor = os_open(path, length, 1);
    if (descriptor == OS_SYSCALL_ERROR) return 0;
    os_stat_t stat;
    int success = os_fstat(descriptor, &stat) != OS_SYSCALL_ERROR;
    if (success && stat.type == 0) {
        find_dirent_t entry;
        uint64_t result;
        while ((result = os_readdir(descriptor, &entry)) == 1) {
            uint64_t name_length = os_userland_length(entry.name);
            if (name_length == 0 || name_length >= sizeof(entry.name) ||
                length + 1U + name_length >= 256) {
                success = 0;
                break;
            }
            char child[256];
            uint64_t child_length = 0;
            for (uint64_t index = 0; index < length; ++index)
                child[child_length++] = path[index];
            if (child_length != 1 || child[0] != '/')
                child[child_length++] = '/';
            for (uint64_t index = 0; index < name_length; ++index)
                child[child_length++] = entry.name[index];
            child[child_length] = 0;
            if (!find_path(child, child_length, depth + 1U)) {
                success = 0;
                break;
            }
        }
        if (result != 0 && result != 1) success = 0;
    }
    if (os_close(descriptor) == OS_SYSCALL_ERROR) success = 0;
    return success;
}

int find_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc > 2) os_exit(2);
    const char *path = argc == 2 && argv[1] ? argv[1] : ".";
    os_exit(find_path(path, os_userland_length(path), 0) ? 0 : 1);
}
