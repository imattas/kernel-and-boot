#include "../../lib/runtime.h"


int mv_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3 || !argv[1] || !argv[2] ||
        os_rename(argv[1], os_userland_length(argv[1]), argv[2], os_userland_length(argv[2])) ==
            OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
