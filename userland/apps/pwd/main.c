#include "../../lib/runtime.h"

int pwd_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    if (argc != 1) os_exit(2);
    char path[256];
    uint64_t length = os_getcwd(path, sizeof(path));
    if (length == OS_SYSCALL_ERROR || os_write(1, path, length) != length ||
        os_write(1, "\r\n", 2) != 2) os_exit(1);
    os_exit(0);
}
