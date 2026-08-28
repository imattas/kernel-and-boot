#include "../../lib/runtime.h"


int mkdir_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    uint64_t path_length = os_userland_length(argv[1]);
    if (os_mkdir(argv[1], path_length, 0755) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}
