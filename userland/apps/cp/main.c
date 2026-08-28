#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

static int same_text(const char *left, const char *right) {
    uint64_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == 0 && right[index] == 0;
}

int cp_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3 || !argv[1] || !argv[2] || same_text(argv[1], argv[2]))
        os_exit(2);
    uint64_t source = os_open(argv[1], length(argv[1]), 1);
    if (source == OS_SYSCALL_ERROR) os_exit(1);
    os_stat_t metadata;
    if (os_fstat(source, &metadata) == OS_SYSCALL_ERROR) {
        (void)os_close(source);
        os_exit(1);
    }
    uint64_t destination = os_create(argv[2], length(argv[2]), 3);
    if (destination == OS_SYSCALL_ERROR) {
        (void)os_close(source);
        os_exit(1);
    }
    char buffer[256];
    for (;;) {
        uint64_t count = os_file_read(source, buffer, sizeof(buffer));
        if (count == OS_SYSCALL_ERROR) {
            (void)os_close(source);
            (void)os_close(destination);
            os_exit(1);
        }
        if (count == 0) break;
        if (os_file_write(destination, buffer, count) != count) {
            (void)os_close(source);
            (void)os_close(destination);
            os_exit(1);
        }
    }
    if (os_close(source) == OS_SYSCALL_ERROR ||
        os_close(destination) == OS_SYSCALL_ERROR ||
        os_chmod(argv[2], length(argv[2]), metadata.mode) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}
