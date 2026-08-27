#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/fs/btrfs/zstd.h"

int main(void) {
    uint8_t raw[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x29, 0x00, 0x00,
                     'h', 'e', 'l', 'l', 'o'};
    uint8_t rle[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x2b, 0x00, 0x00, 'A'};
    uint8_t compressed[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x3d, 0x00, 0x00,
                            0x28, 'h', 'e', 'l', 'l', 'o', 0x00};
    uint8_t huffman[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x02, 0x3d, 0x00, 0x00,
                         0x22, 0xc0, 0x00, 0x80, 0x10, 0x05, 0x00};
    uint8_t long_size[] = {0x28, 0xb5, 0x2f, 0xfd, 0x21, 0x00, 0x05, 0x00,
                           0x2b, 0x08, 0x00, 'A'};
    uint8_t output[261] = {0};
    uint32_t size = 0;
    btrfs_zstd_sequence_header_t sequence;
    assert(btrfs_zstd_decompress(raw, sizeof(raw), output, sizeof(output), &size));
    assert(size == 5 && memcmp(output, "hello", 5) == 0);
    assert(btrfs_zstd_decompress(huffman, sizeof(huffman), output, sizeof(output), &size));
    assert(size == 2 && memcmp(output, "\0\1", 2) == 0);
    assert(btrfs_zstd_decompress(long_size, sizeof(long_size), output, 261, &size));
    assert(btrfs_zstd_decompress(rle, sizeof(rle), output, sizeof(output), &size));
    assert(size == 5 && output[0] == 'A' && output[4] == 'A');
    assert(btrfs_zstd_decompress(compressed, sizeof(compressed), output, sizeof(output), &size));
    assert(size == 5 && memcmp(output, "hello", 5) == 0);
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
    return 0;
}
