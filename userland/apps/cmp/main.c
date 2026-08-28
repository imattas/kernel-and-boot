#include "../../lib/runtime.h"

static int read_byte(uint64_t descriptor, char *value, int *end) {
    uint64_t result = os_read(descriptor, value, 1);
    if (result == OS_SYSCALL_ERROR) return 0;
    *end = result == 0;
    return 1;
}

int cmp_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3) os_exit(2);
    uint64_t left = os_open(argv[1], os_userland_length(argv[1]), 1);
    uint64_t right = os_open(argv[2], os_userland_length(argv[2]), 1);
    if (left == OS_SYSCALL_ERROR || right == OS_SYSCALL_ERROR) os_exit(2);
    int different = 0;
    for (;;) {
        char left_byte = 0;
        char right_byte = 0;
        int left_end = 0;
        int right_end = 0;
        if (!read_byte(left, &left_byte, &left_end) ||
            !read_byte(right, &right_byte, &right_end)) os_exit(2);
        if (left_end || right_end) {
            different = left_end != right_end;
            break;
        }
        if (left_byte != right_byte) {
            different = 1;
            break;
        }
    }
    if (os_close(left) == OS_SYSCALL_ERROR ||
        os_close(right) == OS_SYSCALL_ERROR) os_exit(2);
    os_exit(different ? 1 : 0);
}
