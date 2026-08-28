#include "../../lib/runtime.h"


int cat_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if ((argc != 1 && argc != 2) || (argc == 2 && !argv[1])) os_exit(2);
    uint64_t descriptor = argc == 1 ? 0 : os_open(argv[1], os_userland_length(argv[1]), 1);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    int close_descriptor = argc == 2;
    char buffer[256];
    for (;;) {
        uint64_t count = argc == 1 ? os_read(descriptor, buffer, sizeof(buffer)) :
                         os_file_read(descriptor, buffer, sizeof(buffer));
        if (count == OS_SYSCALL_ERROR) {
            if (close_descriptor) (void)os_close(descriptor);
            os_exit(1);
        }
        if (count == 0) break;
        if (os_write(1, buffer, count) != count) {
            if (close_descriptor) (void)os_close(descriptor);
            os_exit(1);
        }
    }
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
