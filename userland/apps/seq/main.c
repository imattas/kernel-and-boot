#include "../../lib/runtime.h"
#include "seq_logic.h"

static int write_number(int64_t value) {
    char text[24];
    uint32_t length = seq_format_number(text, sizeof(text), value);
    if (length == 0) return 0;
    return os_userland_write_all(1, text, length) &&
           os_userland_write_all(1, "\r\n", 2);
}

int seq_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc < 2 || argc > 4) os_exit(2);
    int64_t first = 1;
    int64_t last = 0;
    int64_t step = 1;
    if (argc == 2) {
        if (!seq_parse_number(argv[1], &last)) os_exit(2);
    } else {
        if (!seq_parse_number(argv[1], &first) || !seq_parse_number(argv[2], &last))
            os_exit(2);
        if (argc == 3) step = first <= last ? 1 : -1;
        else if (!seq_parse_number(argv[3], &step) || step == 0) os_exit(2);
    }
    for (int64_t value = first;
         (step > 0 && value <= last) || (step < 0 && value >= last);) {
        if (!write_number(value)) os_exit(1);
        uint64_t gap = step > 0 ? (uint64_t)last - (uint64_t)value :
            (uint64_t)value - (uint64_t)last;
        uint64_t distance = step > 0 ? (uint64_t)step : 0U - (uint64_t)step;
        if (distance > gap) break;
        value += step;
    }
    os_exit(0);
}
