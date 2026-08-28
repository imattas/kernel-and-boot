#include "../../lib/runtime.h"

static int write_number(uint64_t value) {
    char text[24];
    uint32_t length = 0;
    do {
        text[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0 && length < sizeof(text));
    if (length == sizeof(text)) return 0;
    for (uint32_t index = 0; index < length / 2U; ++index) {
        char swap = text[index];
        text[index] = text[length - index - 1U];
        text[length - index - 1U] = swap;
    }
    return os_userland_write_all(1, text, length) &&
           os_userland_write_all(1, "\r\n", 2);
}

int seq_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc < 2 || argc > 4) os_exit(2);
    uint64_t first = 1;
    uint64_t last = 0;
    uint64_t step = 1;
    if (argc == 2) {
        if (!os_userland_parse_u64(argv[1], &last)) os_exit(2);
    } else {
        if (!os_userland_parse_u64(argv[1], &first) ||
            !os_userland_parse_u64(argv[2], &last)) os_exit(2);
        if (argc == 4 && (!os_userland_parse_u64(argv[3], &step) || step == 0))
            os_exit(2);
    }
    for (uint64_t value = first; value <= last;) {
        if (!write_number(value)) os_exit(1);
        if (last - value < step) break;
        value += step;
    }
    os_exit(0);
}
