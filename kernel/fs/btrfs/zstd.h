#ifndef OS_KERNEL_FS_BTRFS_ZSTD_H
#define OS_KERNEL_FS_BTRFS_ZSTD_H

#include <stdint.h>

typedef struct {
    uint32_t count;
    uint8_t literal_length_mode;
    uint8_t offset_mode;
    uint8_t match_length_mode;
    uint32_t header_size;
} btrfs_zstd_sequence_header_t;

int btrfs_zstd_decompress(const uint8_t *input, uint32_t input_size,
                          uint8_t *output, uint32_t output_capacity,
                          uint32_t *output_size);
int btrfs_zstd_read_sequence_header(const uint8_t *input, uint32_t input_size,
                                    btrfs_zstd_sequence_header_t *header);
int btrfs_zstd_copy_match(uint8_t *output, uint32_t capacity,
                          uint32_t *output_size, uint32_t offset,
                          uint32_t length);

#endif
