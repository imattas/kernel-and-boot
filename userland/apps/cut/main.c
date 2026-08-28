#include "../../lib/runtime.h"

static int emit_line(const char *line, uint64_t length,
                     char delimiter, uint64_t wanted) {
    uint64_t field = 1;
    uint64_t start = 0;
    for (uint64_t index = 0; index <= length; ++index) {
        if (index == length || line[index] == delimiter) {
            if (field == wanted &&
                !os_userland_write_all(1, line + start, index - start))
                return 0;
            ++field;
            start = index + 1U;
        }
    }
    return os_userland_write_all(1, "\r\n", 2);
}

int cut_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if ((argc != 5 && argc != 6) || os_userland_length(argv[1]) != 2 ||
        argv[1][0] != '-' || argv[1][1] != 'd' ||
        os_userland_length(argv[2]) != 1 || os_userland_length(argv[3]) != 2 ||
        argv[3][0] != '-' || argv[3][1] != 'f') os_exit(2);
    uint64_t wanted = 0;
    if (!os_userland_parse_u64(argv[4], &wanted) || wanted == 0) os_exit(2);
    uint64_t descriptor = 0;
    int close_descriptor = 0;
    if (argc == 6) {
        descriptor = os_open(argv[5], os_userland_length(argv[5]), 1);
        close_descriptor = 1;
        if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    }
    char line[256];
    uint64_t length = 0;
    char input[256];
    for (;;) {
        uint64_t bytes = os_read(descriptor, input, sizeof(input));
        if (bytes == OS_SYSCALL_ERROR) os_exit(1);
        if (bytes == 0) break;
        for (uint64_t index = 0; index < bytes; ++index) {
            if (input[index] == '\n') {
                if (!emit_line(line, length, argv[2][0], wanted)) os_exit(1);
                length = 0;
            } else if (length + 1U < sizeof(line)) {
                line[length++] = input[index];
            }
        }
    }
    if (length != 0 && !emit_line(line, length, argv[2][0], wanted)) os_exit(1);
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
