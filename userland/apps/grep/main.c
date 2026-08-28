#include "../../lib/runtime.h"

static int contains(const char *line, uint64_t length,
                    const char *pattern, uint64_t pattern_length) {
    if (pattern_length == 0) return 1;
    if (pattern_length > length) return 0;
    for (uint64_t start = 0; start + pattern_length <= length; ++start) {
        uint64_t index = 0;
        while (index < pattern_length &&
               line[start + index] == pattern[index]) ++index;
        if (index == pattern_length) return 1;
    }
    return 0;
}

int grep_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3) os_exit(2);
    const char *pattern = argv[1];
    uint64_t pattern_length = os_userland_length(pattern);
    uint64_t descriptor = os_open(argv[2], os_userland_length(argv[2]), 1);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);

    char input[256];
    char line[256];
    uint64_t line_length = 0;
    int matched = 0;
    for (;;) {
        uint64_t count = os_read(descriptor, input, sizeof(input));
        if (count == OS_SYSCALL_ERROR) {
            (void)os_close(descriptor);
            os_exit(1);
        }
        if (count == 0) break;
        for (uint64_t index = 0; index < count; ++index) {
            char value = input[index];
            if (value == '\n') {
                if (contains(line, line_length, pattern, pattern_length)) {
                    if (!os_userland_write_all(1, line, line_length) ||
                        !os_userland_write_all(1, "\r\n", 2)) {
                        (void)os_close(descriptor);
                        os_exit(1);
                    }
                    matched = 1;
                }
                line_length = 0;
            } else if (line_length + 1U < sizeof(line)) {
                line[line_length++] = value;
            }
        }
    }
    if (line_length != 0 && contains(line, line_length, pattern, pattern_length)) {
        if (!os_userland_write_all(1, line, line_length) ||
            !os_userland_write_all(1, "\r\n", 2)) {
            (void)os_close(descriptor);
            os_exit(1);
        }
        matched = 1;
    }
    if (os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(matched ? 0 : 1);
}
