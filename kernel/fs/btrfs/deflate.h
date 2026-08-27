#ifndef OS_KERNEL_FS_BTRFS_DEFLATE_H
#define OS_KERNEL_FS_BTRFS_DEFLATE_H

#include <stdint.h>

int btrfs_zlib_inflate(const uint8_t *input, uint32_t input_size,
                       uint8_t *output, uint32_t output_capacity,
                       uint32_t *output_size);

#endif
