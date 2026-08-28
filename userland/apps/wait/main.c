#include "../../lib/os.h"

static int number(const char *text, uint64_t *value) {
    if (!text || !value || !text[0]) return 0;
    uint64_t result = 0;
    for (uint64_t index = 0; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9' ||
            result > (UINT64_MAX - (uint64_t)(text[index] - '0')) / 10U)
            return 0;
        result = result * 10U + (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

static void print_number(int32_t value) {
    char digits[20];
    uint64_t magnitude = value < 0 ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
    uint64_t count = 0;
    if (value < 0) (void)os_write(1, "-", 1);
    if (magnitude == 0) {
        (void)os_write(1, "0", 1);
        return;
    }
    while (magnitude != 0) {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    }
    while (count != 0) (void)os_write(1, &digits[--count], 1);
}

int wait_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t process_id;
    int32_t status = -1;
    if (argc != 2 || !argv[1] || !number(argv[1], &process_id) ||
        os_wait(process_id, &status) == OS_SYSCALL_ERROR ||
        os_reap(process_id) == OS_SYSCALL_ERROR)
        os_exit(2);
    (void)os_write(1, "exit=", 5);
    print_number(status);
    (void)os_write(1, "\r\n", 2);
    os_exit(status);
}
