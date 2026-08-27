#include "memory.h"

void *memcpy(void *destination, const void *source, uint64_t size) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint64_t i = 0; i < size; ++i) out[i] = in[i];
    return destination;
}

void *memset(void *destination, int value, uint64_t size) {
    uint8_t *out = (uint8_t *)destination;
    for (uint64_t i = 0; i < size; ++i) out[i] = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, uint64_t size) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (uint64_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}
