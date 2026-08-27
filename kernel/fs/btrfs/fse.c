#include "fse.h"

static uint32_t highest_bit(uint32_t value) {
    uint32_t bit = 0;
    while (value > 1U) { value >>= 1; ++bit; }
    return bit;
}

static uint32_t stream_bits(const uint8_t *source, uint32_t bits, int64_t *offset) {
    int64_t start = *offset - (int64_t)bits;
    uint32_t result = 0, shift = 0;
    *offset = start;
    while (bits && start >= 0) {
        uint32_t available = 8U - (uint32_t)(start & 7);
        uint32_t count = bits < available ? bits : available;
        result |= (((uint32_t)source[start >> 3] >> (start & 7)) &
                   ((1U << count) - 1U)) << shift;
        shift += count; bits -= count; start += count;
    }
    return result;
}

int btrfs_fse_build(btrfs_fse_table_t *table, const int16_t *normalized,
                    uint32_t symbol_count, uint32_t accuracy_log) {
    uint32_t size, high_threshold, position, step, mask;
    uint16_t state_desc[256];
    if (!table || !normalized || !symbol_count || symbol_count > 256U ||
        accuracy_log == 0 || accuracy_log > 10U) return 0;
    size = 1U << accuracy_log;
    high_threshold = size;
    for (uint32_t i = 0; i < symbol_count; ++i) state_desc[i] = 0;
    for (uint32_t symbol = 0; symbol < symbol_count; ++symbol) {
        if (normalized[symbol] < -1 || normalized[symbol] > (int16_t)size)
            return 0;
        if (normalized[symbol] == -1) {
            if (!high_threshold) return 0;
            table->symbols[--high_threshold] = (uint8_t)symbol;
            state_desc[symbol] = 1;
        }
    }
    step = (size >> 1) + (size >> 3) + 3U; mask = size - 1U; position = 0;
    for (uint32_t symbol = 0; symbol < symbol_count; ++symbol) {
        if (normalized[symbol] <= 0) continue;
        state_desc[symbol] = (uint16_t)normalized[symbol];
        for (int16_t count = 0; count < normalized[symbol]; ++count) {
            if (position >= high_threshold) return 0;
            table->symbols[position] = (uint8_t)symbol;
            do { position = (position + step) & mask; } while (position >= high_threshold);
        }
    }
    if (position != 0) return 0;
    for (uint32_t state = 0; state < size; ++state) {
        uint8_t symbol = table->symbols[state];
        uint16_t descriptor = state_desc[symbol]++;
        uint32_t bit_count = accuracy_log - highest_bit(descriptor);
        table->bits[state] = (uint8_t)bit_count;
        table->new_state[state] = (uint16_t)((descriptor << bit_count) - size);
    }
    table->size = size; table->accuracy_log = accuracy_log;
    return 1;
}

int btrfs_fse_decode(const btrfs_fse_table_t *table, const uint8_t *stream,
                     uint32_t stream_size, uint8_t *symbols,
                     uint32_t symbol_count) {
    int64_t offset;
    uint32_t state;
    if (!table || !stream || !stream_size || !symbols || !symbol_count ||
        !table->size || table->size > BTRFS_FSE_TABLE_MAX ||
        table->accuracy_log > 10U || !stream[stream_size - 1U]) return 0;
    offset = (int64_t)stream_size * 8;
    state = stream_bits(stream, table->accuracy_log, &offset);
    for (uint32_t i = 0; i < symbol_count; ++i) {
        uint32_t bits;
        if (state >= table->size) return 0;
        symbols[i] = table->symbols[state];
        bits = table->bits[state];
        state = table->new_state[state] + stream_bits(stream, bits, &offset);
        if (state >= table->size) return 0;
    }
    return 1;
}
