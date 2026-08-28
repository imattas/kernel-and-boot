#include "../../lib/runtime.h"

int clear_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    if (argc != 1 || !os_userland_write_all(1, "\x1b[2J\x1b[H", 7))
        os_exit(2);
    os_exit(0);
}
