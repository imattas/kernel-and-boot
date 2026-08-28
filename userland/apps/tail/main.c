#include "../../lib/runtime.h"

static void emit_line(const char *line, uint64_t length) {
    if (!os_userland_write_all(1, line, length) ||
        !os_userland_write_all(1, "\r\n", 2)) os_exit(1);
}

int tail_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 1 && argc != 2) os_exit(2);
    uint64_t descriptor = 0;
    int close_descriptor = 0;
    if (argc == 2) {
        descriptor = os_open(argv[1], os_userland_length(argv[1]), 1);
        close_descriptor = 1;
        if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    }

    char lines[10][256];
    uint16_t lengths[10] = {0};
    uint64_t next = 0;
    uint64_t stored = 0;
    char input[256];
    uint64_t line_length = 0;
    for (;;) {
        uint64_t count = os_read(descriptor, input, sizeof(input));
        if (count == OS_SYSCALL_ERROR) {
            if (close_descriptor) (void)os_close(descriptor);
            os_exit(1);
        }
        if (count == 0) break;
        for (uint64_t index = 0; index < count; ++index) {
            char value = input[index];
            if (value == '\n') {
                lengths[next] = (uint16_t)line_length;
                next = (next + 1U) % 10U;
                if (stored < 10U) ++stored;
                line_length = 0;
            } else if (line_length + 1U < sizeof(lines[0])) {
                lines[next][line_length++] = value;
            }
        }
    }
    if (line_length != 0) {
        lengths[next] = (uint16_t)line_length;
        next = (next + 1U) % 10U;
        if (stored < 10U) ++stored;
    }
    uint64_t first = (next + 10U - stored) % 10U;
    for (uint64_t index = 0; index < stored; ++index) {
        uint64_t slot = (first + index) % 10U;
        emit_line(lines[slot], lengths[slot]);
    }
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}

