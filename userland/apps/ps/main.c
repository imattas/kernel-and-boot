#include "../../lib/os.h"

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

int ps_main(uint64_t argc, char **argv, char **environment) {
    (void)argc;
    (void)argv;
    (void)environment;
    uint64_t ids[64];
    uint64_t count = os_process_list(ids, 64);
    if (count == OS_SYSCALL_ERROR) os_exit(1);
    for (uint64_t index = 0; index < count; ++index) {
        os_process_info_t info;
        if (os_process_status(ids[index], &info) == OS_SYSCALL_ERROR)
            continue;
        (void)os_write(1, "pid=", 4); number(info.id);
        (void)os_write(1, " ppid=", 6); number(info.parent_id);
        (void)os_write(1, " state=", 7); number(info.state);
        (void)os_write(1, " exit=", 6); number((uint32_t)info.exit_status);
        (void)os_write(1, "\r\n", 2);
    }
    os_exit(0);
}
