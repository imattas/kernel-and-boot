#include "../../lib/runtime.h"


int rmdir_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    if (os_rmdir(argv[1], os_userland_length(argv[1])) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}
