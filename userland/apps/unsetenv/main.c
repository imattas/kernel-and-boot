#include "../../lib/runtime.h"

int unsetenv_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc < 2) os_exit(2);
    for (uint64_t index = 1; index < argc; ++index) {
        if (!argv[index] || os_userland_length(argv[index]) == 0 ||
            os_unsetenv(argv[index]) == OS_SYSCALL_ERROR)
            os_exit(2);
    }
    os_exit(0);
}
