#include "../../lib/runtime.h"


int chdir_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    uint64_t path_length = os_userland_length(argv[1]);
    if (os_chdir(argv[1], path_length) == OS_SYSCALL_ERROR) os_exit(1);
    char path[256];
    uint64_t result = os_getcwd(path, sizeof(path));
    if (result == OS_SYSCALL_ERROR || os_write(1, "cwd=", 4) != 4 ||
        os_write(1, path, result) != result || os_write(1, "\r\n", 2) != 2)
        os_exit(1);
    os_exit(0);
}
