#include "deflate.h"

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t bit;
} bit_reader_t;

typedef struct {
    uint16_t code[288];
    uint8_t length[288];
    uint16_t count;
} huffman_t;

static uint32_t reverse_bits(uint32_t value, uint32_t count) {
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; ++i) { result = (result << 1) | (value & 1U); value >>= 1; }
    return result;
}

static int read_bits(bit_reader_t *reader, uint32_t count, uint32_t *value) {
    uint32_t result = 0;
    if (!reader || !value || count > 24U || reader->bit > reader->size * 8U ||
        count > reader->size * 8U - reader->bit) return 0;
    for (uint32_t i = 0; i < count; ++i)
        result |= ((reader->data[reader->bit >> 3] >> (reader->bit & 7U)) & 1U) << i, ++reader->bit;
    *value = result;
    return 1;
}

static int huffman_build(huffman_t *tree, const uint8_t *lengths, uint16_t count) {
    uint16_t counts[16] = {0}, next[16] = {0};
    if (!tree || !lengths || count == 0 || count > 288) return 0;
    tree->count = count;
    for (uint16_t i = 0; i < count; ++i) {
        if (lengths[i] > 15) return 0;
        if (lengths[i]) ++counts[lengths[i]];
        tree->length[i] = lengths[i];
    }
    uint32_t code = 0;
    for (uint32_t bits = 1; bits <= 15; ++bits) {
        code = (code + counts[bits - 1]) << 1;
        next[bits] = (uint16_t)code;
        if (code + counts[bits] > (1U << bits)) return 0;
    }
    for (uint16_t i = 0; i < count; ++i)
        tree->code[i] = tree->length[i] ?
                        (uint16_t)reverse_bits(next[tree->length[i]]++, tree->length[i]) : 0;
    return 1;
}

static int huffman_read(bit_reader_t *reader, const huffman_t *tree, uint16_t *symbol) {
    uint32_t code = 0;
    if (!reader || !tree || !symbol) return 0;
    for (uint32_t length = 1; length <= 15; ++length) {
        uint32_t bit;
        if (!read_bits(reader, 1, &bit)) return 0;
        code |= bit << (length - 1U);
        for (uint16_t i = 0; i < tree->count; ++i)
            if (tree->length[i] == length && tree->code[i] == code) { *symbol = i; return 1; }
    }
    return 0;
}

static int read_code_lengths(bit_reader_t *reader, const huffman_t *length_tree,
                             uint8_t *lengths, uint16_t count) {
    uint16_t position = 0;
    while (position < count) {
        uint16_t symbol;
        if (!huffman_read(reader, length_tree, &symbol)) return 0;
        if (symbol < 16) { lengths[position++] = (uint8_t)symbol; continue; }
        uint32_t extra, repeat;
        uint8_t previous = position ? lengths[position - 1] : 0;
        if (symbol == 16) {
            if (!position || !read_bits(reader, 2, &extra)) return 0;
            repeat = extra + 3U;
        } else if (symbol == 17) {
            if (!read_bits(reader, 3, &extra)) return 0;
            repeat = extra + 3U; previous = 0;
        } else if (symbol == 18) {
            if (!read_bits(reader, 7, &extra)) return 0;
            repeat = extra + 11U; previous = 0;
        } else return 0;
        if (repeat > count - position) return 0;
        while (repeat--) lengths[position++] = previous;
    }
    return 1;
}

static int fixed_trees(huffman_t *literal, huffman_t *distance) {
    uint8_t lengths[288], distances[32];
    for (uint16_t i = 0; i < 288; ++i) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    for (uint16_t i = 0; i < 32; ++i) distances[i] = 5;
    return huffman_build(literal, lengths, 288) && huffman_build(distance, distances, 32);
}

static uint32_t adler32(const uint8_t *data, uint32_t size) {
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < size; ++i) { a = (a + data[i]) % 65521U; b = (b + a) % 65521U; }
    return (b << 16) | a;
}

int btrfs_zlib_inflate(const uint8_t *input, uint32_t input_size,
                       uint8_t *output, uint32_t output_capacity,
                       uint32_t *output_size) {
    static const uint16_t length_base[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t length_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t distance_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t distance_extra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    if (!input || !output || !output_size || input_size < 6 || output_capacity == 0 ||
        (input[0] & 0x0fU) != 8 || (input[0] >> 4) > 7 || (input[1] & 0x20U) != 0 ||
        (((uint16_t)input[0] << 8) | input[1]) % 31U != 0) return 0;
    bit_reader_t reader = {input + 2, input_size - 6, 0};
    uint32_t final = 0, type = 0; uint32_t produced = 0;
    do {
        if (!read_bits(&reader, 1, &final) || !read_bits(&reader, 2, &type)) return 0;
        if (type == 0) {
            uint32_t length, inverse;
            reader.bit = (reader.bit + 7U) & ~7U;
            if (!read_bits(&reader, 16, &length) || !read_bits(&reader, 16, &inverse) ||
                ((length ^ inverse) & 0xffffU) != 0xffffU || length > output_capacity - produced ||
                reader.bit > reader.size * 8U - length * 8U) return 0;
            for (uint32_t i = 0; i < length; ++i) {
                uint32_t byte;
                if (!read_bits(&reader, 8, &byte)) return 0;
                output[produced++] = (uint8_t)byte;
            }
        } else {
            huffman_t literal_tree, distance_tree; uint8_t literal_lengths[288] = {0}, distance_lengths[32] = {0};
            if (type == 1) { if (!fixed_trees(&literal_tree, &distance_tree)) return 0; }
            else if (type == 2) {
                uint32_t hlit, hdist, hclen, value; static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t code_lengths[19] = {0}, all_lengths[320] = {0};
                if (!read_bits(&reader, 5, &hlit) || !read_bits(&reader, 5, &hdist) || !read_bits(&reader, 4, &hclen)) return 0;
                hlit += 257; hdist += 1; hclen += 4;
                for (uint32_t i = 0; i < hclen; ++i) if (!read_bits(&reader, 3, &value)) return 0; else code_lengths[order[i]] = (uint8_t)value;
                huffman_t code_tree; if (!huffman_build(&code_tree, code_lengths, 19) ||
                    !read_code_lengths(&reader, &code_tree, all_lengths, (uint16_t)(hlit + hdist))) return 0;
                for (uint32_t i = 0; i < hlit; ++i) literal_lengths[i] = all_lengths[i];
                for (uint32_t i = 0; i < hdist; ++i) distance_lengths[i] = all_lengths[hlit + i];
                if (!huffman_build(&literal_tree, literal_lengths, (uint16_t)hlit) ||
                    !huffman_build(&distance_tree, distance_lengths, (uint16_t)hdist)) return 0;
            } else return 0;
            for (;;) {
                uint16_t symbol; if (!huffman_read(&reader, &literal_tree, &symbol)) return 0;
                if (symbol < 256) { if (produced >= output_capacity) return 0; output[produced++] = (uint8_t)symbol; continue; }
                if (symbol == 256) break;
                if (symbol < 257 || symbol > 285) return 0;
                uint32_t index = symbol - 257U, extra, length = length_base[index];
                if (!read_bits(&reader, length_extra[index], &extra)) return 0; length += extra;
                uint16_t distance_symbol; if (!huffman_read(&reader, &distance_tree, &distance_symbol) || distance_symbol >= 30) return 0;
                uint32_t distance = distance_base[distance_symbol];
                if (!read_bits(&reader, distance_extra[distance_symbol], &extra)) return 0; distance += extra;
                if (distance > produced || length > output_capacity - produced) return 0;
                for (uint32_t i = 0; i < length; ++i) output[produced] = output[produced - distance], ++produced;
            }
        }
    } while (!final);
    uint32_t expected = ((uint32_t)input[input_size - 4] << 24) |
                        ((uint32_t)input[input_size - 3] << 16) |
                        ((uint32_t)input[input_size - 2] << 8) | input[input_size - 1];
    if (adler32(output, produced) != expected) return 0;
    *output_size = produced;
    return 1;
}
