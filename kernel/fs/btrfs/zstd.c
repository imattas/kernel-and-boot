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
        if (flag == 1) return (uint32_t)header[0] | ((uint32_t)header[1] << 8) + 256U;
        if (flag == 2) return le32(header);
        return le64(header);
    }
    if (flag == 0) return 0;
    if (flag == 1) size = (uint32_t)header[0] | ((uint32_t)header[1] << 8);
    else if (flag == 2) size = le32(header);
    else if (flag == 3) return le64(header);
    return size;
}

static int decode_compressed_literals(const uint8_t *input, uint32_t size,
                                      uint8_t *output, uint32_t capacity,
                                      uint32_t *decoded_size) {
    uint32_t header, format, regenerated, header_size;
    if (!input || size < 2 || !output || !decoded_size) return 0;
    header = input[0];
    if ((header & 3U) > 1U) return 0;
    format = (header >> 2) & 3U;
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
    return 1;
}

static int decode_compressed_block(const uint8_t *input, uint32_t size,
                                   uint8_t *output, uint32_t capacity,
                                   uint32_t *decoded_size) {
    uint32_t literals_size, literals_decoded, sequences;
    if (!input || !size || !output || !decoded_size ||
        !decode_compressed_literals(input, size, output, capacity, &literals_decoded)) return 0;
    if (input[0] & 3U) return 0;
    if (((input[0] >> 2) & 3U) == 0U || ((input[0] >> 2) & 3U) == 2U)
        literals_size = 1U + literals_decoded;
    else if (((input[0] >> 2) & 3U) == 1U)
        literals_size = 2U + literals_decoded;
    else
        literals_size = 3U + literals_decoded;
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
