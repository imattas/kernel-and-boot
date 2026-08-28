#include "../../lib/runtime.h"

int chmod_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t permissions;
    if (argc != 3 || !argv[1] || !argv[2] ||
        !os_userland_parse_octal(argv[2], &permissions, 4) ||
        os_chmod(argv[1], os_userland_length(argv[1]), permissions) ==
            OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
