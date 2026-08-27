#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../../kernel/fs/xfs/xfs.h"
#include "../../../kernel/drivers/storage/storage.h"

static uint8_t image[512 * 128];
static int read_image(uint64_t lba, uint32_t count, void *buffer) {
    if (lba > 128 || count > 128 - lba) return 0;
    memcpy(buffer, &image[lba * 512U], count * 512U);
    return 1;
}
static int write_image(uint64_t lba, uint32_t count, const void *buffer) {
    if (lba > 128 || count > 128 - lba) return 0;
    memcpy(&image[lba * 512U], buffer, count * 512U);
    return 1;
}
static void put16(uint8_t *p, uint16_t value) { p[0] = value >> 8; p[1] = value; }
static void put32(uint8_t *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static void put64(uint8_t *p, uint64_t value) { put32(p, value >> 32); put32(p + 4, value); }

int main(void) {
    memset(image, 0, sizeof(image));
    put32(&image[0], 0x58465342); put32(&image[4], 4096);
    put64(&image[8], 16); put16(&image[102], 512); put32(&image[84], 16);
    put32(&image[88], 1); put16(&image[100], 4); put16(&image[104], 256);
    put16(&image[106], 16); image[108] = 12; image[110] = 8;
    image[111] = 4; image[112] = 4; put64(&image[56], 128);
    uint8_t *inode = &image[8 * 4096]; put16(inode, 0x494e);
    put16(&inode[2], 0x8000); inode[4] = 2; inode[5] = 2;
    put64(&inode[56], 12288); put32(&inode[76], 1);
    put64(&inode[100], 1ULL << 63); put64(&inode[108], (9ULL << 21) | 3ULL);
    storage_initialize();
    storage_device_t device = {.name = "xfs-unwritten", .block_size = 512,
                               .block_count = 128, .read = read_image,
                               .write = write_image};
    assert(storage_register(&device));
    xfs_fs_t fs; assert(xfs_mount(&fs, 0));
    assert(xfs_write_file(&fs, 128, 4096U + 7U, "x", 1));
    assert(image[10 * 4096U + 7U] == 'x');
    assert(image[8 * 4096U + 116U + 6U] == 2);
    uint64_t debug_size = 0; assert(xfs_inode_size(&fs, 128, &debug_size) && debug_size == 12288);
    uint8_t output[16]; memset(output, 0xa5, sizeof(output));
    assert(xfs_read_file(&fs, 128, 4096, output, sizeof(output)));
    assert(output[0] == 0); assert(output[6] == 0); assert(output[7] == 'x'); assert(output[8] == 0);
    memset(output, 0xa5, sizeof(output));
    assert(xfs_read_file(&fs, 128, 8192, output, sizeof(output)));
    for (uint32_t i = 0; i < sizeof(output); ++i) assert(output[i] == 0);
    return 0;
}
