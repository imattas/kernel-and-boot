#include "../../lib/runtime.h"

typedef struct {
    char name[32];
    uint32_t type;
} find_dirent_t;

static int write_path(const char *path, uint64_t length) {
    return os_userland_write_all(1, path, length) &&
           os_userland_write_all(1, "\r\n", 2);
}

static int find_path(const char *path, uint64_t length, uint32_t depth,
                     uint32_t filter) {
    if (!path || length == 0 || length >= 256 || depth > 16) return 0;
    uint64_t descriptor = os_open(path, length, 1);
    if (descriptor == OS_SYSCALL_ERROR) return 0;
    os_stat_t stat;
    int success = os_fstat(descriptor, &stat) != OS_SYSCALL_ERROR;
    if (success && (filter == 0 || (filter == 1 && stat.type == 1) ||
                    (filter == 2 && stat.type == 0)))
        success = write_path(path, length);
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
            if (!find_path(child, child_length, depth + 1U, filter)) {
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
    if (argc != 1 && argc != 2 && argc != 4) os_exit(2);
    const char *path = argc == 2 && argv[1] ? argv[1] : ".";
    uint32_t filter = 0;
    if (argc == 4) {
        if (!argv[1] || !argv[2] || !argv[3] ||
            os_userland_length(argv[2]) != 5 || argv[2][0] != '-' ||
            argv[2][1] != 't' || argv[2][2] != 'y' || argv[2][3] != 'p' ||
            argv[2][4] != 'e' || os_userland_length(argv[3]) != 1 ||
            (argv[3][0] != 'f' && argv[3][0] != 'd')) os_exit(2);
        path = argv[1];
        filter = argv[3][0] == 'f' ? 1 : 2;
    }
    os_exit(find_path(path, os_userland_length(path), 0, filter) ? 0 : 1);
}
