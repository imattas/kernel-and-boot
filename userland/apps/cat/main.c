#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

int cat_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    uint64_t descriptor = os_open(argv[1], length(argv[1]), 1);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    char buffer[256];
    for (;;) {
        uint64_t count = os_file_read(descriptor, buffer, sizeof(buffer));
        if (count == OS_SYSCALL_ERROR) {
            (void)os_close(descriptor);
            os_exit(1);
        }
        if (count == 0) break;
        if (os_write(1, buffer, count) != count) {
            (void)os_close(descriptor);
            os_exit(1);
        }
    }
    if (os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
