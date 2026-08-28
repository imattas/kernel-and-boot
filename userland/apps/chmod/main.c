#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

static int mode(const char *text, uint64_t *value) {
    uint64_t result = 0;
    uint64_t size = length(text);
    if (size == 0 || size > 4) return 0;
    for (uint64_t index = 0; index < size; ++index) {
        if (text[index] < '0' || text[index] > '7') return 0;
        result = (result << 3) | (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

int chmod_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t permissions;
    if (argc != 3 || !argv[1] || !argv[2] || !mode(argv[2], &permissions) ||
        os_chmod(argv[1], length(argv[1]), permissions) == OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
