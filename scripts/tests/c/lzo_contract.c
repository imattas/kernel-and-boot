#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/fs/btrfs/lzo.h"

int main(void) {
    static const uint8_t stream[] = {
        0x28, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x68,
        0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x68, 0x65, 0x6c,
        0x6c, 0x6f, 0x20, 0x68, 0x65, 0x6c, 0x6c, 0x6f,
        0x11, 0x00, 0x00
    };
    static const char expected[] = "hello hello hello hello";
    uint8_t output[sizeof(expected) - 1];
    uint32_t output_size = 0;

    assert(btrfs_lzo1x_decompress(stream, sizeof(stream), output,
                                  sizeof(output), &output_size));
    assert(output_size == sizeof(output));
    assert(memcmp(output, expected, sizeof(output)) == 0);
    assert(!btrfs_lzo1x_decompress(stream, sizeof(stream), output,
                                   sizeof(output) - 1, &output_size));
    assert(!btrfs_lzo1x_decompress(stream, sizeof(stream) - 1, output,
                                   sizeof(output), &output_size));
    return 0;
}
