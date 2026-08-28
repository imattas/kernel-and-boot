#include "../../lib/runtime.h"

static void number(uint64_t value) {
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

int id_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc > 2 || (argc == 2 &&
                     (argv[1][0] != '-' ||
                      (argv[1][1] != 'u' && argv[1][1] != 'g') ||
                      argv[1][2] != 0)))
        os_exit(2);
    if (argc == 2) {
        uint64_t value = argv[1][1] == 'u' ? os_getuid() : os_getgid();
        number(value);
        if (os_write(1, "\r\n", 2) != 2) os_exit(1);
        os_exit(0);
    }
    (void)os_write(1, "pid=", 4); number(os_getpid());
    (void)os_write(1, " ppid=", 6); number(os_getppid());
    (void)os_write(1, " uid=", 5); number(os_getuid());
    (void)os_write(1, " gid=", 5); number(os_getgid());
    if (os_write(1, "\r\n", 2) != 2) os_exit(1);
    os_exit(0);
}
