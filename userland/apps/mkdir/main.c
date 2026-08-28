#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

int mkdir_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 2 || !argv[1]) os_exit(2);
    uint64_t path_length = length(argv[1]);
    if (os_mkdir(argv[1], path_length, 0755) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}
