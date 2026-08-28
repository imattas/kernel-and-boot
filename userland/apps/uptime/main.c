#include "../../lib/runtime.h"

static int write_number(uint64_t value) {
    char digits[20];
    uint64_t length = 0;
    do {
        digits[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    while (length != 0 && os_write(1, &digits[--length], 1) == 1) {}
    return length == 0;
}

int uptime_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    if (argc != 1) os_exit(2);
    uint64_t now = os_clock_monotonic();
    uint64_t seconds = now / 1000000000U;
    uint64_t milliseconds = (now / 1000000U) % 1000U;
    if (!write_number(seconds) || os_write(1, ".", 1) != 1 ||
        (milliseconds < 100U && os_write(1, "0", 1) != 1) ||
        (milliseconds < 10U && os_write(1, "0", 1) != 1) ||
        !write_number(milliseconds) || os_write(1, "s\r\n", 3) != 3)
        os_exit(1);
    os_exit(0);
}
