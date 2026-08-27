#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "kernel/fs/btrfs/zstd.h"

void *kmalloc(uint64_t size) { return malloc((size_t)size); }
void kfree(void *pointer) { free(pointer); }

int main(void) {
    uint8_t raw[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x29, 0x00, 0x00,
                     'h', 'e', 'l', 'l', 'o'};
    uint8_t rle[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x2b, 0x00, 0x00, 'A'};
    uint8_t compressed[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x3d, 0x00, 0x00,
                            0x28, 'h', 'e', 'l', 'l', 'o', 0x00};
    uint8_t checksummed[] = {0x28, 0xb5, 0x2f, 0xfd, 0x24, 0x05, 0x29, 0x00, 0x00,
                             'h', 'e', 'l', 'l', 'o', 0xa3, 0x6d, 0x9f, 0x88};
    uint8_t huffman[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x02, 0x3d, 0x00, 0x00,
                         0x22, 0xc0, 0x00, 0x80, 0x10, 0x05, 0x00};
    uint8_t long_size[] = {0x28, 0xb5, 0x2f, 0xfd, 0x21, 0x00, 0x05, 0x00,
                           0x2b, 0x08, 0x00, 'A'};
    uint8_t output[512] = {0};
    uint32_t size = 0;
    btrfs_zstd_sequence_header_t sequence;
    btrfs_fse_table_t sequence_tables[3];
    btrfs_fse_table_t sequence_previous[3];
    uint32_t tables_consumed = 0;
    btrfs_zstd_sequence_t values;
    btrfs_zstd_sequence_t sequence_list[2];
    uint8_t literals[] = {'a', 'b', 'c'};
    uint8_t sequence_output[16] = {0};
    uint32_t literal_offset = 0, sequence_output_size = 0;
    uint32_t block_output_size = 0;
    uint8_t match[16] = {'a', 'b', 'c'};
    uint32_t match_size = 3;
    btrfs_fse_table_t sequence_table;
    uint32_t sequence_state = 0;
    int64_t sequence_bits = 0;
    assert(btrfs_zstd_decompress(raw, sizeof(raw), output, sizeof(output), &size));
    assert(size == 5 && memcmp(output, "hello", 5) == 0);
    assert(btrfs_zstd_decompress(huffman, sizeof(huffman), output, sizeof(output), &size));
    assert(size == 2 && memcmp(output, "\0\1", 2) == 0);
    assert(btrfs_zstd_decompress(long_size, sizeof(long_size), output, 261, &size));
    assert(btrfs_zstd_decompress(rle, sizeof(rle), output, sizeof(output), &size));
    assert(size == 5 && output[0] == 'A' && output[4] == 'A');
    assert(btrfs_zstd_decompress(compressed, sizeof(compressed), output, sizeof(output), &size));
    assert(size == 5 && memcmp(output, "hello", 5) == 0);
    assert(btrfs_zstd_decompress(checksummed, sizeof(checksummed), output, sizeof(output), &size));
    checksummed[sizeof(checksummed) - 1U] ^= 1U;
    assert(!btrfs_zstd_decompress(checksummed, sizeof(checksummed), output, sizeof(output), &size));
    assert(!btrfs_zstd_decompress(raw, sizeof(raw) - 1, output, sizeof(output), &size));
    assert(!btrfs_zstd_decompress(raw, sizeof(raw), output, 4, &size));
    assert(btrfs_zstd_read_sequence_header((const uint8_t[]){0}, 1, &sequence));
    assert(sequence.count == 0 && sequence.header_size == 1);
    assert(btrfs_zstd_read_sequence_header((const uint8_t[]){5, 0x90}, 2, &sequence));
    assert(sequence.count == 5 && sequence.literal_length_mode == 2 &&
           sequence.offset_mode == 1 && sequence.match_length_mode == 0 &&
           sequence.header_size == 2);
    assert(btrfs_zstd_read_sequence_header((const uint8_t[]){255, 0, 1, 0}, 4, &sequence));
    assert(sequence.count == 0x8000 && sequence.header_size == 4);
    assert(btrfs_zstd_read_sequence_header((const uint8_t[]){5, 0x00}, 2, &sequence));
    assert(btrfs_zstd_prepare_sequence_tables((const uint8_t[]){5, 0x00}, 2,
                                               &sequence, 0, sequence_tables,
                                               &tables_consumed));
    assert(tables_consumed == 2 && sequence_tables[0].size == 64 &&
           sequence_tables[1].size == 32 && sequence_tables[2].size == 64);
    assert(btrfs_zstd_read_sequence_header((const uint8_t[]){5, 0x54, 1, 2, 3}, 5,
                                           &sequence));
    assert(btrfs_zstd_prepare_sequence_tables((const uint8_t[]){5, 0x54, 1, 2, 3}, 5,
                                               &sequence, 0, sequence_tables,
                                               &tables_consumed));
    assert(tables_consumed == 5 && sequence_tables[0].symbols[0] == 1 &&
           sequence_tables[1].symbols[0] == 2 && sequence_tables[2].symbols[0] == 3);
    sequence_previous[0] = sequence_tables[0]; sequence_previous[1] = sequence_tables[1];
    sequence_previous[2] = sequence_tables[2];
    assert(btrfs_zstd_read_sequence_header((const uint8_t[]){5, 0xfc}, 2, &sequence));
    assert(btrfs_zstd_prepare_sequence_tables((const uint8_t[]){5, 0xfc}, 2,
                                               &sequence, sequence_previous,
                                               sequence_tables, &tables_consumed));
    assert(btrfs_zstd_copy_match(match, sizeof(match), &match_size, 3, 6));
    assert(match_size == 9 && memcmp(match, "abcabcabc", 9) == 0);
    assert(!btrfs_zstd_copy_match(match, sizeof(match), &match_size, 0, 1));
    assert(!btrfs_zstd_copy_match(match, 8, &match_size, 3, 6));
    assert(btrfs_zstd_expand_sequence(20, 2, 35, 1, 3, 5, &values));
    assert(values.literal_length == 26 && values.match_length == 42 && values.offset == 13);
    assert(!btrfs_zstd_expand_sequence(36, 0, 0, 0, 0, 0, &values));
    assert(!btrfs_zstd_expand_sequence(0, 1, 0, 0, 0, 0, &values));
    values.literal_length = 3; values.match_length = 6; values.offset = 3;
    assert(btrfs_zstd_execute_sequence(sequence_output, sizeof(sequence_output),
                                       &sequence_output_size, literals, sizeof(literals),
                                       &literal_offset, &values));
    assert(sequence_output_size == 9 && memcmp(sequence_output, "abcabcabc", 9) == 0);
    values.literal_length = 1;
    assert(!btrfs_zstd_execute_sequence(sequence_output, sizeof(sequence_output),
                                        &sequence_output_size, literals, sizeof(literals),
                                        &literal_offset, &values));
    values.literal_length = 3; values.match_length = 6; values.offset = 3;
    assert(btrfs_zstd_execute_sequences(sequence_output, sizeof(sequence_output),
                                        &block_output_size, literals, sizeof(literals),
                                        &values, 1));
    assert(block_output_size == 9 && memcmp(sequence_output, "abcabcabc", 9) == 0);
    assert(!btrfs_zstd_execute_sequences(sequence_output, 8, &block_output_size,
                                         literals, sizeof(literals), &values, 1));
    assert(btrfs_fse_build(&sequence_table, (const int16_t[]){4}, 1, 2));
    values.literal_length = values.match_length = values.offset = 0;
    assert(btrfs_zstd_decode_sequence(&sequence_table, &sequence_table, &sequence_table,
                                      &sequence_state, &sequence_state, &sequence_state,
                                      (const uint8_t[]){0x04}, &sequence_bits, 1, &values));
    assert(values.literal_length == 0 && values.match_length == 3 && values.offset == 1);
    sequence_state = 0; sequence_bits = 0;
    assert(btrfs_zstd_decode_sequences(&sequence_table, &sequence_table, &sequence_table,
                                       &sequence_state, &sequence_state, &sequence_state,
                                       (const uint8_t[]){0x04}, &sequence_bits, 1,
                                       sequence_list, 2));
    assert(sequence_list[0].match_length == 3 && sequence_list[0].offset == 1);
    assert(!btrfs_zstd_decode_sequences(&sequence_table, &sequence_table, &sequence_table,
                                        &sequence_state, &sequence_state, &sequence_state,
                                        (const uint8_t[]){0x04}, &sequence_bits, 2,
                                        sequence_list, 1));
    FILE *compressed_file = fopen("build/tests/zstd_real.zst", "rb");
    FILE *raw_file = fopen("build/tests/zstd_real.raw", "rb");
    assert(compressed_file && raw_file);
    fseek(compressed_file, 0, SEEK_END); long compressed_length = ftell(compressed_file); rewind(compressed_file);
    fseek(raw_file, 0, SEEK_END); long raw_length = ftell(raw_file); rewind(raw_file);
    assert(compressed_length > 0 && compressed_length < 65536 && raw_length == 256);
    uint8_t *compressed_bytes = malloc((size_t)compressed_length);
    uint8_t *raw_bytes = malloc((size_t)raw_length);
    assert(compressed_bytes && raw_bytes);
    assert(fread(compressed_bytes, 1, (size_t)compressed_length, compressed_file) == (size_t)compressed_length);
    assert(fread(raw_bytes, 1, (size_t)raw_length, raw_file) == (size_t)raw_length);
    fclose(compressed_file); fclose(raw_file);
    assert(btrfs_zstd_decompress(compressed_bytes, (uint32_t)compressed_length,
                                 output, sizeof(output), &size));
    assert(size == (uint32_t)raw_length && memcmp(output, raw_bytes, (size_t)raw_length) == 0);
    free(compressed_bytes); free(raw_bytes);
    return 0;
}
