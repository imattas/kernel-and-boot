#include "../../lib/runtime.h"

static int same_line(const char *left, uint64_t left_length,
                     const char *right, uint64_t right_length) {
    if (left_length != right_length) return 0;
    for (uint64_t index = 0; index < left_length; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

static void emit_line(const char *line, uint64_t length) {
    if (!os_userland_write_all(1, line, length) ||
        !os_userland_write_all(1, "\r\n", 2)) os_exit(1);
}

int uniq_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 1 && argc != 2) os_exit(2);
    uint64_t descriptor = 0;
    int close_descriptor = 0;
    if (argc == 2) {
        descriptor = os_open(argv[1], os_userland_length(argv[1]), 1);
        close_descriptor = 1;
        if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    }
    char current[256];
    char previous[256];
    uint64_t current_length = 0;
    uint64_t previous_length = 0;
    int have_previous = 0;
    char input[256];
    for (;;) {
        uint64_t bytes = os_read(descriptor, input, sizeof(input));
        if (bytes == OS_SYSCALL_ERROR) os_exit(1);
        if (bytes == 0) break;
        for (uint64_t index = 0; index < bytes; ++index) {
            if (input[index] != '\n') {
                if (current_length + 1U < sizeof(current))
                    current[current_length++] = input[index];
                continue;
            }
            if (!have_previous || !same_line(current, current_length,
                                               previous, previous_length)) {
                emit_line(current, current_length);
                for (uint64_t copy = 0; copy < current_length; ++copy)
                    previous[copy] = current[copy];
                previous_length = current_length;
                have_previous = 1;
            }
            current_length = 0;
        }
    }
    if (current_length != 0 &&
        (!have_previous || !same_line(current, current_length,
                                      previous, previous_length)))
        emit_line(current, current_length);
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}

