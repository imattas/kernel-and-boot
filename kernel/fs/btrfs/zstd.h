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

typedef struct {
    uint32_t literal_length;
    uint32_t match_length;
    uint32_t offset;
} btrfs_zstd_sequence_t;

int btrfs_zstd_decompress(const uint8_t *input, uint32_t input_size,
                          uint8_t *output, uint32_t output_capacity,
                          uint32_t *output_size);
int btrfs_zstd_read_sequence_header(const uint8_t *input, uint32_t input_size,
                                    btrfs_zstd_sequence_header_t *header);
int btrfs_zstd_copy_match(uint8_t *output, uint32_t capacity,
                          uint32_t *output_size, uint32_t offset,
                          uint32_t length);
int btrfs_zstd_expand_sequence(uint8_t literal_length_code,
                               uint32_t literal_length_extra,
                               uint8_t match_length_code,
                               uint32_t match_length_extra,
                               uint8_t offset_code, uint32_t offset_extra,
                               btrfs_zstd_sequence_t *sequence);
int btrfs_zstd_execute_sequence(uint8_t *output, uint32_t output_capacity,
                                uint32_t *output_size, const uint8_t *literals,
                                uint32_t literal_size, uint32_t *literal_offset,
                                const btrfs_zstd_sequence_t *sequence);

#endif
