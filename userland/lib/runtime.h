#ifndef OS_USERLAND_RUNTIME_H
#define OS_USERLAND_RUNTIME_H

#include "os.h"

static inline uint64_t os_userland_length(const char *text) {
    uint64_t length = 0;
    if (!text) return 0;
    while (text[length]) ++length;
    return length;
}

static inline int os_userland_write_all(uint64_t descriptor,
                                        const void *buffer, uint64_t length) {
    return os_write(descriptor, buffer, length) == length;
}

static inline int os_userland_parse_u64(const char *text, uint64_t *value) {
    if (!text || !value || !text[0]) return 0;
    uint64_t result = 0;
    for (uint64_t index = 0; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9' ||
            result > (UINT64_MAX - (uint64_t)(text[index] - '0')) / 10U)
            return 0;
        result = result * 10U + (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

static inline int os_userland_parse_octal(const char *text, uint64_t *value,
                                          uint64_t max_digits) {
    if (!text || !value || !text[0]) return 0;
    uint64_t length = os_userland_length(text);
    if (length > max_digits) return 0;
    uint64_t result = 0;
    for (uint64_t index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '7') return 0;
        result = (result << 3) | (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

#endif
