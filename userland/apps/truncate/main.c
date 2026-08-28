#include "../../lib/runtime.h"



int truncate_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t size;
    if (argc != 3 || !argv[1] || !argv[2] ||
        !os_userland_parse_u64(argv[2], &size) || size > UINT32_MAX)
        os_exit(2);
    uint64_t descriptor = os_open(argv[1], 0, 2);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    uint64_t result = os_truncate(descriptor, size);
    if (os_close(descriptor) == OS_SYSCALL_ERROR ||
        result == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
