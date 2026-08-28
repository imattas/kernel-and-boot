#include "../../lib/runtime.h"

static void print_number(uint64_t value) {
    char digits[20];
    uint64_t length = 0;
    if (value == 0) {
        os_userland_write_all(1, "0", 1);
        return;
    }
    while (value != 0) {
        digits[length++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (length != 0) os_userland_write_all(1, &digits[--length], 1);
}

int wc_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 1 && argc != 2) os_exit(2);
    uint64_t descriptor = 0;
    int close_descriptor = 0;
    if (argc == 2) {
        descriptor = os_open(argv[1], os_userland_length(argv[1]), 1);
        close_descriptor = 1;
        if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    }
    char buffer[256];
    uint64_t bytes = 0, lines = 0, words = 0;
    int in_word = 0;
    for (;;) {
        uint64_t count = os_read(descriptor, buffer, sizeof(buffer));
        if (count == OS_SYSCALL_ERROR) {
            if (close_descriptor) (void)os_close(descriptor);
            os_exit(1);
        }
        if (count == 0) break;
        bytes += count;
        for (uint64_t index = 0; index < count; ++index) {
            char value = buffer[index];
            if (value == '\n') ++lines;
            if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
                in_word = 0;
            else if (!in_word) {
                in_word = 1;
                ++words;
            }
        }
    }
    if (close_descriptor && os_close(descriptor) == OS_SYSCALL_ERROR)
        os_exit(1);
    print_number(lines);
    os_userland_write_all(1, " ", 1);
    print_number(words);
    os_userland_write_all(1, " ", 1);
    print_number(bytes);
    os_userland_write_all(1, "\r\n", 2);
    os_exit(0);
}
