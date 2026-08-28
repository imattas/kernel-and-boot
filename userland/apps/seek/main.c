#include "../../lib/runtime.h"



static void print_number(uint64_t value) {
    char digits[20];
    uint64_t count = 0;
    if (value == 0) {
        (void)os_write(1, "0", 1);
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count != 0) (void)os_write(1, &digits[--count], 1);
}

int seek_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t offset;
    if (argc != 3 || !argv[1] || !argv[2] ||
        !os_userland_parse_u64(argv[2], &offset)) os_exit(2);
    uint64_t descriptor = os_open(argv[1], 0, 1);
    if (descriptor == OS_SYSCALL_ERROR ||
        os_seek(descriptor, offset) == OS_SYSCALL_ERROR) {
        if (descriptor != OS_SYSCALL_ERROR) (void)os_close(descriptor);
        os_exit(1);
    }
    unsigned char value;
    uint64_t result = os_read(descriptor, &value, 1);
    if (os_close(descriptor) == OS_SYSCALL_ERROR || result != 1)
        os_exit(1);
    (void)os_write(1, "byte=", 5);
    print_number(value);
    (void)os_write(1, "\r\n", 2);
    os_exit(0);
}
