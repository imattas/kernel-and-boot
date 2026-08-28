#include "../../lib/runtime.h"

static int before(const char *left, uint16_t left_length,
                  const char *right, uint16_t right_length) {
    uint16_t limit = left_length < right_length ? left_length : right_length;
    for (uint16_t index = 0; index < limit; ++index) {
        if (left[index] < right[index]) return 1;
        if (left[index] > right[index]) return 0;
    }
    return left_length < right_length;
}

static void swap_lines(char *left, uint16_t *left_length,
                       char *right, uint16_t *right_length) {
    char buffer[128];
    for (uint16_t index = 0; index < 128; ++index) {
        buffer[index] = left[index];
        left[index] = right[index];
        right[index] = buffer[index];
    }
    uint16_t length = *left_length;
    *left_length = *right_length;
    *right_length = length;
}

int sort_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 1 && argc != 2) os_exit(2);
    uint64_t descriptor = 0;
    int close_descriptor = 0;
    if (argc == 2) {
        descriptor = os_open(argv[1], os_userland_length(argv[1]), 1);
        close_descriptor = 1;
        if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    }
    char lines[64][128];
    uint16_t lengths[64] = {0};
    uint64_t count = 0;
    uint64_t line_length = 0;
    char input[256];
    for (;;) {
        uint64_t bytes = os_read(descriptor, input, sizeof(input));
        if (bytes == OS_SYSCALL_ERROR) os_exit(1);
        if (bytes == 0) break;
        for (uint64_t index = 0; index < bytes; ++index) {
            if (input[index] == '\n') {
                if (count < 64U) {
                    lengths[count++] = (uint16_t)line_length;
                    line_length = 0;
                }
            } else if (count < 64U && line_length + 1U < 128U) {
                lines[count][line_length++] = input[index];
            }
        }
    }
    if (line_length != 0 && count < 64U) lengths[count++] = (uint16_t)line_length;
    for (uint64_t index = 1; index < count; ++index) {
        uint64_t position = index;
        while (position != 0 && before(lines[position], lengths[position],
                                       lines[position - 1U], lengths[position - 1U])) {
            swap_lines(lines[position], &lengths[position],
                       lines[position - 1U], &lengths[position - 1U]);
            --position;
        }
    }
    for (uint64_t index = 0; index < count; ++index)
        if (!os_userland_write_all(1, lines[index], lengths[index]) ||
            !os_userland_write_all(1, "\r\n", 2)) os_exit(1);
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}

