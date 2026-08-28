#include "../../lib/runtime.h"

int setenv_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3 || !argv[1] || !argv[2] ||
        os_setenv(argv[1], argv[2]) == OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
