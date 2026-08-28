#include "../../lib/runtime.h"

int args_main(uint64_t argc, char **argv, char **environment) {
    if (!environment) os_exit(1);
    for (uint64_t index = 1; index < argc; ++index) {
        uint64_t size = os_userland_length(argv[index]);
        if (!os_userland_write_all(1, argv[index], size)) os_exit(1);
        if (index + 1 < argc && !os_userland_write_all(1, " ", 1))
            os_exit(1);
    }
    if (!os_userland_write_all(1, "\r\n", 2)) os_exit(1);
    os_exit(0);
}
