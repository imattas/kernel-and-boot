#include "zstd.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static uint64_t frame_content_size(const uint8_t *header, uint32_t descriptor,
                                   uint8_t single_segment) {
    uint32_t flag = descriptor & 3U;
    uint32_t size = 0;
    if (single_segment) {
        if (flag == 0) return header[0];
        if (flag == 1) return ((uint32_t)header[0] | ((uint32_t)header[1] << 8)) + 256U;
        if (flag == 2) return le32(header);
        return le64(header);
    }
    if (flag == 0) return 0;
    if (flag == 1) size = (uint32_t)header[0] | ((uint32_t)header[1] << 8);
    else if (flag == 2) size = le32(header);
    else if (flag == 3) return le64(header);
    return size;
}

static uint32_t zstd_highest_bit(uint32_t value) {
    uint32_t bit = 0;
    while (value > 1U) { value >>= 1; ++bit; }
    return bit;
}

int btrfs_zstd_read_sequence_header(const uint8_t *input, uint32_t input_size,
                                    btrfs_zstd_sequence_header_t *header) {
    uint8_t first, modes;
    if (!input || !header || !input_size) return 0;
    first = input[0];
    if (first < 128U) {
        header->count = first; header->header_size = 1;
    } else if (first < 255U) {
        if (input_size < 2U) return 0;
        header->count = ((uint32_t)(first - 128U) << 8) | input[1];
        header->header_size = 2;
    } else {
        if (input_size < 3U) return 0;
        header->count = 0x7f00U + input[1] + ((uint32_t)input[2] << 8);
        header->header_size = 3;
    }
    header->literal_length_mode = header->offset_mode = header->match_length_mode = 0;
    if (!header->count) return 1;
    if (header->header_size >= input_size) return 0;
    modes = input[header->header_size];
    if (modes & 3U) return 0;
    header->literal_length_mode = (uint8_t)(modes >> 6);
    header->offset_mode = (uint8_t)((modes >> 4) & 3U);
    header->match_length_mode = (uint8_t)((modes >> 2) & 3U);
    ++header->header_size;
    return 1;
}

static uint32_t zstd_stream_bits(const uint8_t *source, uint32_t bits,
                                 int64_t *offset) {
    int64_t start = *offset - (int64_t)bits;
    uint32_t result = 0, shift = 0;
    *offset = start;
    while (bits && start >= 0) {
        uint32_t available = 8U - (uint32_t)(start & 7);
        uint32_t count = bits < available ? bits : available;
        uint32_t mask = (1U << count) - 1U;
        result |= (((uint32_t)source[start >> 3] >> (start & 7)) & mask) << shift;
        shift += count; bits -= count; start += count;
    }
    return result;
}

static int zstd_huffman_decode(const uint8_t *source, uint32_t source_size,
                               const uint8_t *weights, uint32_t weight_count,
                               uint8_t *output, uint32_t output_capacity,
                               uint32_t expected_size) {
    uint8_t symbols[2048], bits[2048], lengths[256];
    uint16_t rank_count[17];
    uint32_t rank_index[17], max_bits = 0, table_size, weight_sum = 0;
    int64_t offset;
    if (!source || !source_size || !weights || !weight_count || !output ||
        expected_size > output_capacity || weight_count >= 256U) return 0;
    for (uint32_t i = 0; i < 17; ++i) rank_count[i] = 0;
    for (uint32_t i = 0; i < weight_count; ++i) {
        if (weights[i] > 16U) return 0;
        if (weights[i]) weight_sum += 1U << (weights[i] - 1U);
    }
    if (!weight_sum) return 0;
    max_bits = zstd_highest_bit(weight_sum) + 1U;
    if (max_bits > 16U) return 0;
    uint32_t left = (1U << max_bits) - weight_sum;
    if (!left || (left & (left - 1U))) return 0;
    uint32_t last_weight = zstd_highest_bit(left) + 1U;
    if (max_bits + 1U < last_weight) return 0;
    for (uint32_t i = 0; i < weight_count; ++i) {
        lengths[i] = weights[i] ? (uint8_t)(max_bits + 1U - weights[i]) : 0;
        if (lengths[i]) ++rank_count[lengths[i]];
    }
    lengths[weight_count] = (uint8_t)(max_bits + 1U - last_weight);
    if (!lengths[weight_count]) return 0;
    ++rank_count[lengths[weight_count]];
    table_size = 1U << max_bits;
    rank_index[max_bits] = 0;
    for (uint32_t i = max_bits; i > 0; --i) {
        rank_index[i - 1] = rank_index[i] + rank_count[i] * (1U << (max_bits - i));
        for (uint32_t j = rank_index[i]; j < rank_index[i - 1]; ++j) bits[j] = (uint8_t)i;
    }
    if (rank_index[0] != table_size) return 0;
    for (uint32_t symbol = 0; symbol <= weight_count; ++symbol) {
        uint32_t length = lengths[symbol];
        if (!length) continue;
        uint32_t index = rank_index[length];
        uint32_t span = 1U << (max_bits - length);
        for (uint32_t j = 0; j < span; ++j) symbols[index + j] = (uint8_t)symbol;
        rank_index[length] += span;
    }
    uint8_t last = source[source_size - 1U];
    if (!last) return 0;
    offset = (int64_t)source_size * 8 - (int64_t)(8U - zstd_highest_bit(last));
    uint32_t state = zstd_stream_bits(source, max_bits, &offset), produced = 0;
    while (offset > -(int64_t)max_bits) {
        if (produced == expected_size) return 0;
        uint32_t length = bits[state];
        output[produced++] = symbols[state];
        state = ((state << length) + zstd_stream_bits(source, length, &offset)) & (table_size - 1U);
    }
    return produced == expected_size && offset == -(int64_t)max_bits;
}

static int decode_compressed_literals(const uint8_t *input, uint32_t size,
                                      uint8_t *output, uint32_t capacity,
                                      uint32_t *decoded_size, uint32_t *section_size) {
    uint32_t header, format, regenerated, header_size;
    if (!input || size < 2 || !output || !decoded_size || !section_size) return 0;
    header = input[0];
    if ((header & 3U) > 2U) return 0;
    format = (header >> 2) & 3U;
    if ((header & 3U) == 2U) {
        uint32_t packed, compressed, tree_header, count, tree_bytes;
        uint8_t weights[255];
        if (format != 0U || size < 4U) return 0;
        packed = (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
                 ((uint32_t)input[2] << 16);
        regenerated = (packed >> 4) & 0x3ffU;
        compressed = (packed >> 14) & 0x3ffU;
        if (!regenerated || !compressed || regenerated > capacity || compressed > size - 3U)
            return 0;
        tree_header = input[3];
        if (tree_header < 128U) return 0;
        count = tree_header - 127U;
        tree_bytes = (count + 1U) / 2U;
        if (count >= 256U || tree_bytes > compressed - 1U) return 0;
        for (uint32_t i = 0; i < count; ++i)
            weights[i] = (uint8_t)(i & 1U ? input[4U + i / 2U] & 0x0fU :
                                   input[4U + i / 2U] >> 4);
        if (!zstd_huffman_decode(input + 4U + tree_bytes,
                                 compressed - 1U - tree_bytes, weights,
                                 count, output, capacity, regenerated)) return 0;
        *decoded_size = regenerated; *section_size = 3U + compressed;
        return 1;
    }
    if (format == 0U || format == 2U) {
        regenerated = header >> 3; header_size = 1;
    } else if (format == 1U) {
        if (size < 2) return 0;
        regenerated = (header >> 4) | ((uint32_t)input[1] << 4); header_size = 2;
    } else {
        if (size < 3) return 0;
        regenerated = (header >> 4) | ((uint32_t)input[1] << 4) |
                      ((uint32_t)input[2] << 12); header_size = 3;
    }
    if (!regenerated || regenerated > capacity || header_size > size - regenerated) return 0;
    if ((header & 3U) == 0U) {
        for (uint32_t i = 0; i < regenerated; ++i) output[i] = input[header_size + i];
    } else {
        if (header_size >= size) return 0;
        for (uint32_t i = 0; i < regenerated; ++i) output[i] = input[header_size];
    }
    *decoded_size = regenerated;
    *section_size = header_size + regenerated;
    return 1;
}

static int decode_compressed_block(const uint8_t *input, uint32_t size,
                                   uint8_t *output, uint32_t capacity,
                                   uint32_t *decoded_size) {
    uint32_t literals_size, literals_decoded, sequences;
    if (!input || !size || !output || !decoded_size ||
        !decode_compressed_literals(input, size, output, capacity, &literals_decoded,
                                    &literals_size)) return 0;
    if (literals_size >= size) return 0;
    sequences = input[literals_size];
    if (sequences != 0U) return 0;
    *decoded_size = literals_decoded;
    return literals_size + 1U == size;
}

int btrfs_zstd_decompress(const uint8_t *input, uint32_t input_size,
                          uint8_t *output, uint32_t output_capacity,
                          uint32_t *output_size) {
    uint32_t descriptor, position, output_position = 0;
    uint64_t content_size;
    uint32_t header_size, dict_size;
    uint8_t single_segment, checksum;
    if (!input || !output || !output_size || input_size < 6 || !output_capacity ||
        le32(input) != 0xfd2fb528U) return 0;
    descriptor = input[4];
    if (descriptor & 0x08U) return 0;
    single_segment = (uint8_t)((descriptor >> 5) & 1U);
    checksum = (uint8_t)((descriptor >> 2) & 1U);
    dict_size = (descriptor & 3U) == 0 ? 0 : (descriptor & 3U) == 1 ? 1 :
                (descriptor & 3U) == 2 ? 2 : 4;
    header_size = 1U + (single_segment ? 0U : 1U) + dict_size + (single_segment ?
                  ((descriptor & 3U) == 0 ? 1U : (descriptor & 3U) == 1 ? 2U :
                   (descriptor & 3U) == 2 ? 4U : 8U) :
                  ((descriptor & 3U) == 0 ? 0U : (descriptor & 3U) == 1 ? 2U :
                   (descriptor & 3U) == 2 ? 4U : 8U));
    if (header_size > input_size - 4U)
        return 0;
    position = 4U + header_size;
    content_size = frame_content_size(input + 5U + (single_segment ? 0U : 1U) + dict_size, descriptor,
                                      single_segment);
    if (content_size > output_capacity) return 0;
    for (;;) {
        uint32_t block_header, block_size, stored_block_size, block_type;
        uint8_t last;
        if (position > input_size - 3U) return 0;
        block_header = (uint32_t)input[position] | ((uint32_t)input[position + 1] << 8) |
                       ((uint32_t)input[position + 2] << 16);
        position += 3U;
        last = (uint8_t)(block_header & 1U);
        block_type = (block_header >> 1) & 3U;
        block_size = block_header >> 3;
        stored_block_size = block_size;
        if (block_type == 3U || (block_type != 2U && block_size > output_capacity - output_position) ||
            (block_type != 1U && block_size > input_size - position) ||
            (block_type == 1U && position >= input_size)) return 0;
        if (block_type == 0U) {
            for (uint32_t i = 0; i < block_size; ++i) output[output_position + i] = input[position + i];
        } else if (block_type == 1U) {
            if (!block_size) return 0;
            for (uint32_t i = 0; i < block_size; ++i) output[output_position + i] = input[position];
        } else if (block_type == 2U) {
            uint32_t decoded_block = 0;
            if (!decode_compressed_block(&input[position], block_size, output + output_position,
                                         output_capacity - output_position, &decoded_block)) return 0;
            block_size = decoded_block;
        }
        output_position += block_size;
        position += block_type == 1U ? 1U : stored_block_size;
        if (last) break;
    }
    if (checksum) {
        if (position > input_size - 4U) return 0;
        position += 4U;
    }
    if (position != input_size || (content_size && content_size != output_position)) return 0;
    *output_size = output_position;
    return 1;
}
