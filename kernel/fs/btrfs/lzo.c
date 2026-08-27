#include "lzo.h"

static int extend_length(const uint8_t **input, const uint8_t *end,
                         uint32_t *length, uint32_t base) {
    uint32_t zeros = 0;
    while (*input < end && **input == 0) { ++*input; ++zeros; }
    if (*input >= end || zeros > 0x00ffffffU / 255U ||
        *length > 0xffffffffU - base - zeros * 255U - **input) return 0;
    *length += base + zeros * 255U + *(*input)++;
    return 1;
}

int btrfs_lzo1x_decompress(const uint8_t *input, uint32_t input_size,
                           uint8_t *output, uint32_t output_capacity,
                           uint32_t *output_size) {
    const uint8_t *ip = input, *end = input + input_size, *match;
    uint8_t *op = output, *out_end = output + output_capacity;
    uint32_t t, next, state = 0;
    if (!input || !output || !output_size || input_size < 3 || output_capacity == 0) return 0;
    if (*ip > 17) {
        t = *ip++ - 17U;
        if (t < 4) next = t;
        else goto copy_literals;
    } else {
        t = *ip++;
        goto first_match;
    }
    for (;;) {
        if (ip >= end) return 0;
        t = *ip++;
        if (t < 16) {
            if (state == 0) {
                if (t == 0 && !extend_length(&ip, end, &t, 15)) return 0;
                t += 3;
                goto copy_literals;
            }
            if (state != 4) {
                if (ip >= end) return 0;
                next = t & 3U;
                match = op - 1U - (t >> 2) - ((uint32_t)*ip++ << 2);
                if (match < output || (uint32_t)(out_end - op) < 2) return 0;
                *op++ = *match++; *op++ = *match++;
                goto match_next;
            }
            next = t & 3U;
            match = op - 1U - 0x4000U - (t >> 2);
            if (ip >= end) return 0;
            match -= (uint32_t)*ip++ << 2;
            t = 3;
        } else if (t >= 64) {
            next = t & 3U;
            if (ip >= end) return 0;
            match = op - 1U - ((t >> 2) & 7U) - ((uint32_t)*ip++ << 3);
            t = (t >> 5) - 1U + 2U;
        } else if (t >= 32) {
            t = (t & 31U) + 2U;
            if (t == 2 && !extend_length(&ip, end, &t, 31)) return 0;
            if (ip + 1 >= end) return 0;
            next = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8); ip += 2;
            match = op - 1U - (next >> 2); next &= 3U;
        } else {
            if (ip + 1 >= end) return 0;
            next = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8);
            if ((next & 0xfffcU) == 0xfffcU && (t & 0xf8U) == 0x18U) return 0;
            match = op - ((t & 8U) << 11); t = (t & 7U) + 2U;
            if (t == 2 && !extend_length(&ip, end, &t, 7)) return 0;
            if (ip + 1 >= end) return 0;
            next = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8); ip += 2;
            match -= next >> 2; next &= 3U;
            if (match == op) goto done;
            match -= 0x4000U;
        }
        if (match < output || (uint32_t)(out_end - op) < t) return 0;
        while (t--) *op++ = *match++;
match_next:
        state = next; t = next;
        if (t == 0) continue;
        if ((uint32_t)(end - ip) < t || (uint32_t)(out_end - op) < t) return 0;
        while (t--) *op++ = *ip++;
        continue;
first_match:
        state = 0;
        if (t < 4) { next = t; goto match_next; }
copy_literals:
        if ((uint32_t)(end - ip) < t || (uint32_t)(out_end - op) < t) return 0;
        while (t--) *op++ = *ip++;
        state = 4;
    }
done:
    *output_size = (uint32_t)(op - output);
    return t == 3U && ip == end;
}
