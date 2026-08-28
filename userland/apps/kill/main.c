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

int kill_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t process_id, signal_number;
    if (argc != 3 || !argv[1] || !argv[2] ||
        !number(argv[1], &process_id) || !number(argv[2], &signal_number) ||
        os_signal_send_to(process_id, signal_number) == OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
