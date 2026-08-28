#include "../../lib/runtime.h"

static void two_digits(uint8_t value) {
    char digits[2] = {(char)('0' + value / 10U),
                      (char)('0' + value % 10U)};
    if (os_write(1, digits, sizeof(digits)) != sizeof(digits)) os_exit(1);
}

static void four_digits(uint16_t value) {
    char digits[4] = {(char)('0' + value / 1000U),
                      (char)('0' + (value / 100U) % 10U),
                      (char)('0' + (value / 10U) % 10U),
                      (char)('0' + value % 10U)};
    if (os_write(1, digits, sizeof(digits)) != sizeof(digits)) os_exit(1);
}

int date_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    if (argc != 1) os_exit(2);
    os_datetime_t datetime;
    if (os_clock_realtime(&datetime) == OS_SYSCALL_ERROR) os_exit(1);
    four_digits(datetime.year);
    if (os_write(1, "-", 1) != 1) os_exit(1);
    two_digits(datetime.month);
    if (os_write(1, "-", 1) != 1) os_exit(1);
    two_digits(datetime.day);
    if (os_write(1, " ", 1) != 1) os_exit(1);
    two_digits(datetime.hour);
    if (os_write(1, ":", 1) != 1) os_exit(1);
    two_digits(datetime.minute);
    if (os_write(1, ":", 1) != 1) os_exit(1);
    two_digits(datetime.second);
    if (os_write(1, "\r\n", 2) != 2) os_exit(1);
    os_exit(0);
}
