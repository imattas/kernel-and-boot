#ifndef OS_USERLAND_SEQ_LOGIC_H
#define OS_USERLAND_SEQ_LOGIC_H

#include <stdint.h>

static inline int seq_parse_number(const char *text, int64_t *value) {
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
    *value = negative && magnitude == UINT64_C(0x8000000000000000) ?
        INT64_MIN : negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 1;
}

static inline uint32_t seq_format_number(char *text, uint32_t capacity,
                                          int64_t value) {
    if (!text || capacity == 0) return 0;
    uint64_t magnitude = value < 0 ? 0U - (uint64_t)value : (uint64_t)value;
    char reversed[24];
    uint32_t length = 0;
    do {
        if (length == sizeof(reversed)) return 0;
        reversed[length++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0);
    if (value < 0) {
        if (length == sizeof(reversed)) return 0;
        reversed[length++] = '-';
    }
    if (length + 1U > capacity) return 0;
    for (uint32_t index = 0; index < length; ++index)
        text[index] = reversed[length - index - 1U];
    text[length] = 0;
    return length;
}

#endif
