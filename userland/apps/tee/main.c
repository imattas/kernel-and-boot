#include "../../lib/runtime.h"

int tee_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc < 2) os_exit(2);
    uint64_t descriptors[8];
    uint64_t count = argc - 1U;
    if (count > 8U) os_exit(2);
    for (uint64_t index = 0; index < count; ++index) {
        descriptors[index] = os_create(argv[index + 1U],
                                       os_userland_length(argv[index + 1U]), 3);
        if (descriptors[index] == OS_SYSCALL_ERROR) {
            while (index != 0) (void)os_close(descriptors[--index]);
            os_exit(1);
        }
    }
    char buffer[256];
    for (;;) {
        uint64_t bytes = os_read(0, buffer, sizeof(buffer));
        if (bytes == OS_SYSCALL_ERROR) os_exit(1);
        if (bytes == 0) break;
        if (!os_userland_write_all(1, buffer, bytes)) os_exit(1);
        for (uint64_t index = 0; index < count; ++index)
            if (!os_userland_write_all(descriptors[index], buffer, bytes)) os_exit(1);
    }
    for (uint64_t index = 0; index < count; ++index)
        if (os_close(descriptors[index]) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}

