#include "../../lib/runtime.h"

int head_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 1 && argc != 2) os_exit(2);
    uint64_t descriptor = 0;
    int close_descriptor = 0;
    if (argc == 2) {
        descriptor = os_open(argv[1], os_userland_length(argv[1]), 1);
        close_descriptor = 1;
        if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    }

    char buffer[256];
    uint64_t lines = 0;
    for (;;) {
        uint64_t count = os_read(descriptor, buffer, sizeof(buffer));
        if (count == OS_SYSCALL_ERROR) {
            if (close_descriptor) (void)os_close(descriptor);
            os_exit(1);
        }
        if (count == 0) break;
        uint64_t write_count = count;
        for (uint64_t index = 0; index < count; ++index) {
            if (buffer[index] == '\n') {
                ++lines;
                if (lines == 10) {
                    write_count = index + 1U;
                    if (!os_userland_write_all(1, buffer, write_count) ||
                        (close_descriptor &&
                         os_close(descriptor) == OS_SYSCALL_ERROR))
                        os_exit(1);
                    os_exit(0);
                }
            }
        }
        if (!os_userland_write_all(1, buffer, count)) {
            if (close_descriptor) (void)os_close(descriptor);
            os_exit(1);
        }
    }
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}
