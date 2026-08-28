#include "../../lib/runtime.h"

int basename_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2) os_exit(2);
    const char *path = argv[1];
    uint64_t length = os_userland_length(path);
    while (length > 1 && path[length - 1U] == '/') --length;
    uint64_t start = length;
    while (start != 0 && path[start - 1U] != '/') --start;
    if (!os_userland_write_all(1, path + start, length - start) ||
        !os_userland_write_all(1, "\r\n", 2)) os_exit(1);
    os_exit(0);
}
