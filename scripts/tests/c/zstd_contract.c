#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/fs/btrfs/zstd.h"

int main(void) {
    uint8_t raw[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x29, 0x00, 0x00,
                     'h', 'e', 'l', 'l', 'o'};
    uint8_t rle[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x2b, 0x00, 0x00, 'A'};
    uint8_t output[8] = {0};
    uint32_t size = 0;
    assert(btrfs_zstd_decompress(raw, sizeof(raw), output, sizeof(output), &size));
    assert(size == 5 && memcmp(output, "hello", 5) == 0);
    assert(btrfs_zstd_decompress(rle, sizeof(rle), output, sizeof(output), &size));
    assert(size == 5 && output[0] == 'A' && output[4] == 'A');
    assert(!btrfs_zstd_decompress(raw, sizeof(raw) - 1, output, sizeof(output), &size));
    assert(!btrfs_zstd_decompress(raw, sizeof(raw), output, 4, &size));
    return 0;
}
