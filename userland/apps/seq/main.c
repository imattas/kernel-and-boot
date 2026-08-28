#include "../../lib/runtime.h"

static int parse_number(const char *text, int64_t *value) {
    if (!text || !text[0] || !value) return 0;
    uint64_t magnitude = 0;
    uint64_t limit = UINT64_C(0x7fffffffffffffff);
    uint32_t offset = 0;
    int negative = text[0] == '-';
    if (negative) {
        offset = 1;
        limit = UINT64_C(0x8000000000000000);
    }
    if (!text[offset]) return 0;
    for (; text[offset]; ++offset) {
        if (text[offset] < '0' || text[offset] > '9' ||
            magnitude > (limit - (uint64_t)(text[offset] - '0')) / 10U)
            return 0;
        magnitude = magnitude * 10U + (uint64_t)(text[offset] - '0');
    }
    if (negative) {
        *value = magnitude == UINT64_C(0x8000000000000000) ?
            INT64_MIN : -(int64_t)magnitude;
    } else {
        *value = (int64_t)magnitude;
    }
    return 1;
}

static int write_number(int64_t value) {
    char text[24];
    uint32_t length = 0;
    uint64_t magnitude = value < 0 ? 0U - (uint64_t)value : (uint64_t)value;
    do {
        text[length++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0 && length < sizeof(text));
    if (length == sizeof(text)) return 0;
    if (value < 0) text[length++] = '-';
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
    int64_t first = 1;
    int64_t last = 0;
    int64_t step = 1;
    if (argc == 2) {
        if (!parse_number(argv[1], &last)) os_exit(2);
    } else {
        if (!parse_number(argv[1], &first) || !parse_number(argv[2], &last))
            os_exit(2);
        if (argc == 3) step = first <= last ? 1 : -1;
        else if (!parse_number(argv[3], &step) || step == 0) os_exit(2);
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
