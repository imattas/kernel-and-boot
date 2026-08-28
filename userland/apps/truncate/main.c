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

int truncate_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t size;
    if (argc != 3 || !argv[1] || !argv[2] ||
        !number(argv[2], &size) || size > UINT32_MAX)
        os_exit(2);
    uint64_t descriptor = os_open(argv[1], 0, 2);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    uint64_t result = os_truncate(descriptor, size);
    if (os_close(descriptor) == OS_SYSCALL_ERROR ||
        result == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
