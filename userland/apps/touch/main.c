#include "../../lib/runtime.h"


int touch_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    uint64_t descriptor = os_create(argv[1], os_userland_length(argv[1]), 3);
    if (descriptor == OS_SYSCALL_ERROR ||
        os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
