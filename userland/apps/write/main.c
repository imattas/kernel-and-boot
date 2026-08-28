#include "../../lib/runtime.h"


int write_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc < 3 || !argv[1]) os_exit(2);
    uint64_t descriptor = os_create(argv[1], os_userland_length(argv[1]), 3);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    for (uint64_t index = 2; index < argc; ++index) {
        uint64_t size = os_userland_length(argv[index]);
        if (os_file_write(descriptor, argv[index], size) != size ||
            (index + 1 < argc && os_file_write(descriptor, " ", 1) != 1)) {
            (void)os_close(descriptor);
            os_exit(1);
        }
    }
    if (os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
