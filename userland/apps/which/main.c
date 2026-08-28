#include "../../lib/runtime.h"

static int probe(const char *path, uint64_t length) {
    uint64_t descriptor = os_open(path, length, 1);
    if (descriptor == OS_SYSCALL_ERROR) return 0;
    return os_close(descriptor) != OS_SYSCALL_ERROR;
}

int which_main(uint64_t argc, char **argv, char **environment) {
    if (argc != 2 || !environment) os_exit(2);
    const char *name = argv[1];
    uint64_t name_length = os_userland_length(name);
    if (name_length == 0) os_exit(1);
    for (uint64_t index = 0; index < name_length; ++index)
        if (name[index] == '/') {
            if (probe(name, name_length) &&
                os_userland_write_all(1, name, name_length) &&
                os_userland_write_all(1, "\r\n", 2)) os_exit(0);
            os_exit(1);
        }
    char path[128];
    char path_variable[128];
    uint64_t path_length = os_getenv("PATH", path_variable,
                                     sizeof(path_variable));
    if (path_length == OS_SYSCALL_ERROR || path_length >= sizeof(path_variable))
        os_exit(1);
    uint64_t start = 0;
    while (start <= path_length) {
        uint64_t end = start;
        while (end < path_length && path_variable[end] != ':') ++end;
        uint64_t directory_length = end - start;
        uint64_t candidate_length = directory_length == 0 ? name_length :
                                     directory_length + 1U + name_length;
        if (candidate_length < sizeof(path)) {
            uint64_t output = 0;
            for (uint64_t index = 0; index < directory_length; ++index)
                path[output++] = path_variable[start + index];
            if (directory_length != 0) path[output++] = '/';
            for (uint64_t index = 0; index < name_length; ++index)
                path[output++] = name[index];
            path[output] = 0;
            if (probe(path, output) &&
                os_userland_write_all(1, path, output) &&
                os_userland_write_all(1, "\r\n", 2)) os_exit(0);
        }
        if (end == path_length) break;
        start = end + 1U;
    }
    os_exit(1);
}
