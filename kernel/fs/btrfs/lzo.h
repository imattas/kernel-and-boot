#ifndef OS_KERNEL_FS_BTRFS_LZO_H
#define OS_KERNEL_FS_BTRFS_LZO_H

#include <stdint.h>

int btrfs_lzo1x_decompress(const uint8_t *input, uint32_t input_size,
                           uint8_t *output, uint32_t output_capacity,
                           uint32_t *output_size);

#endif
