#include "../../lib/runtime.h"

static int probe(const char *path, uint64_t length) {
    uint64_t descriptor = os_open(path, length, 1);
    if (descriptor == OS_SYSCALL_ERROR) return 0;
    return os_close(descriptor) != OS_SYSCALL_ERROR;
}

static uint64_t find_command(const char *name, uint64_t name_length,
                             const char *path_variable, uint64_t path_length,
                             char *path, uint64_t capacity) {
    for (uint64_t index = 0; index < name_length; ++index) {
        if (name[index] != '/') continue;
        if (name_length >= capacity || !probe(name, name_length)) return 0;
        for (uint64_t copy = 0; copy < name_length; ++copy) path[copy] = name[copy];
        path[name_length] = 0;
        return name_length;
    }
    uint64_t start = 0;
    while (start <= path_length) {
        uint64_t end = start;
        while (end < path_length && path_variable[end] != ':') ++end;
        uint64_t directory_length = end - start;
        uint64_t candidate_length = directory_length == 0 ? name_length :
                                     directory_length + 1U + name_length;
        if (candidate_length < capacity) {
            uint64_t output = 0;
            for (uint64_t index = 0; index < directory_length; ++index)
                path[output++] = path_variable[start + index];
            if (directory_length != 0) path[output++] = '/';
            for (uint64_t index = 0; index < name_length; ++index)
                path[output++] = name[index];
            path[output] = 0;
            if (probe(path, output)) return output;
        }
        if (end == path_length) break;
        start = end + 1U;
    }
    return 0;
}

int which_main(uint64_t argc, char **argv, char **environment) {
    if (argc < 2 || !environment) os_exit(2);
    char path_variable[128];
    uint64_t path_length = os_getenv("PATH", path_variable,
                                     sizeof(path_variable));
    if (path_length == OS_SYSCALL_ERROR || path_length >= sizeof(path_variable))
        os_exit(1);
    int failed = 0;
    for (uint64_t argument = 1; argument < argc; ++argument) {
        const char *name = argv[argument];
        uint64_t name_length = os_userland_length(name);
        char path[128];
        uint64_t resolved = name_length == 0 ? 0 :
            find_command(name, name_length, path_variable, path_length,
                         path, sizeof(path));
        if (resolved == 0 || !os_userland_write_all(1, path, resolved) ||
            !os_userland_write_all(1, "\r\n", 2)) failed = 1;
    }
    os_exit(failed ? 1 : 0);
}
