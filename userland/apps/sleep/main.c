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

int sleep_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t milliseconds;
    uint64_t now = os_clock_monotonic();
    if (argc != 2 || !argv[1] || !number(argv[1], &milliseconds) ||
        milliseconds > (UINT64_MAX - now) / 1000000U)
        os_exit(2);
    uint64_t deadline = now + milliseconds * 1000000U;
    while ((now = os_clock_monotonic()) < deadline) (void)os_yield();
    os_exit(0);
}
