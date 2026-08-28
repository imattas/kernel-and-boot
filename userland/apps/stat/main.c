#include "../../lib/runtime.h"


static void number(uint64_t value) {
    char digits[20];
    uint32_t count = 0;
    if (value == 0) {
        (void)os_write(1, "0", 1);
        return;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) (void)os_write(1, &digits[--count], 1);
}

int stat_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    uint64_t descriptor = os_open(argv[1], os_userland_length(argv[1]), 1);
    os_stat_t stat;
    if (descriptor == OS_SYSCALL_ERROR || os_fstat(descriptor, &stat) == OS_SYSCALL_ERROR) {
        if (descriptor != OS_SYSCALL_ERROR) (void)os_close(descriptor);
        os_exit(1);
    }
    if (os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    (void)os_write(1, "uid=", 4); number(stat.owner_uid);
    (void)os_write(1, " gid=", 5); number(stat.owner_gid);
    (void)os_write(1, " mode=", 6); number(stat.mode);
    (void)os_write(1, " type=", 6); number(stat.type);
    if (os_write(1, "\r\n", 2) != 2) os_exit(1);
    os_exit(0);
}
