#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

int mv_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3 || !argv[1] || !argv[2] ||
        os_rename(argv[1], length(argv[1]), argv[2], length(argv[2])) ==
            OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
