#include "../../lib/runtime.h"

int dirname_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2) os_exit(2);
    const char *path = argv[1];
    uint64_t length = os_userland_length(path);
    while (length > 1 && path[length - 1U] == '/') --length;
    uint64_t separator = length;
    while (separator != 0 && path[separator - 1U] != '/') --separator;
    const char *output = ".";
    uint64_t output_length = 1;
    if (separator != 0) {
        output = path;
        output_length = separator == 1 ? 1 : separator - 1U;
    }
    if (!os_userland_write_all(1, output, output_length) ||
        !os_userland_write_all(1, "\r\n", 2)) os_exit(1);
    os_exit(0);
}
