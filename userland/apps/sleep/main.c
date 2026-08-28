#include "../../lib/runtime.h"

int sleep_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t milliseconds;
    uint64_t now = os_clock_monotonic();
    if (argc != 2 || !argv[1] ||
        !os_userland_parse_u64(argv[1], &milliseconds) ||
        milliseconds > (UINT64_MAX - now) / 1000000U)
        os_exit(2);
    uint64_t deadline = now + milliseconds * 1000000U;
    while ((now = os_clock_monotonic()) < deadline) (void)os_yield();
    os_exit(0);
}
