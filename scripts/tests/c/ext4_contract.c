#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../kernel/fs/ext4/ext4.h"
#include "../../../kernel/drivers/storage/storage.h"
static uint8_t image[128 * 512];
static int image_read(uint64_t lba, uint32_t count, void *buffer) { if (lba + count > 128) return 0; memcpy(buffer, &image[lba * 512], count * 512U); return 1; }
static int image_write(uint64_t lba, uint32_t count, const void *buffer) { if (lba + count > 128) return 0; memcpy(&image[lba * 512], buffer, count * 512U); return 1; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
int main(void) {
    memset(image, 0, sizeof(image)); uint8_t *sb = &image[2 * 512];
    put32(&sb[0], 8); put32(&sb[4], 64); put32(&sb[20], 1); put32(&sb[24], 0);
    put32(&sb[32], 32); put32(&sb[40], 8); put16(&sb[56], 0xef53); put16(&sb[88], 128); put16(&sb[254], 32);
    put32(&image[2 * 1024], 3); put32(&image[2 * 1024 + 8], 5); put32(&image[2 * 1024 + 32 + 8], 13);
    for (uint32_t block = 0; block < 15; ++block)
        image[3 * 1024 + block / 8] |= (uint8_t)(1U << (block & 7));
    uint8_t *root_inode = &image[5 * 1024 + 128]; put16(root_inode, 0x41ed); put32(&root_inode[4], 1024); put32(&root_inode[40], 7);
    uint8_t *file_inode = &image[5 * 1024 + 256]; put16(file_inode, 0x81a4); put32(&file_inode[4], 5); put32(&file_inode[40], 8);
    uint8_t *sparse_inode = &image[5 * 1024 + 640]; put16(sparse_inode, 0x81a4); put32(&sparse_inode[4], 1024);
    uint8_t *indirect_inode = &image[5 * 1024 + 384]; put16(indirect_inode, 0x81a4); put32(&indirect_inode[4], 12293); put32(&indirect_inode[88], 9);
    uint8_t *extent_inode = &image[5 * 1024 + 512]; put16(extent_inode, 0x81a4); put32(&extent_inode[4], 1024); put32(&extent_inode[32], 0x00080000); put16(&extent_inode[40], 0xf30a); put16(&extent_inode[42], 1); put16(&extent_inode[44], 4); put16(&extent_inode[46], 1); put32(&extent_inode[52], 0); put32(&extent_inode[56], 11);
    uint8_t *dir = &image[7 * 1024]; put32(dir, 2); put16(&dir[4], 12); dir[6] = 1; dir[7] = 2; dir[8] = '.';
    put32(&dir[12], 2); put16(&dir[16], 12); dir[18] = 2; dir[19] = 2; dir[20] = '.'; dir[21] = '.';
    put32(&dir[24], 3); put16(&dir[28], 1000); dir[30] = 5; dir[31] = 1; memcpy(&dir[32], "hello", 5);
    memcpy(&image[8 * 1024], "world", 5);
    uint8_t *group_file_inode = &image[13 * 1024]; put16(group_file_inode, 0x81a4);
    put32(&group_file_inode[4], 5); put32(&group_file_inode[40], 14);
    memcpy(&image[14 * 1024], "group", 5);
    put32(&image[9 * 1024], 10); memcpy(&image[10 * 1024], "indir", 5);
    put16(&image[11 * 1024], 0xf30a); put16(&image[11 * 1024 + 2], 1); put16(&image[11 * 1024 + 4], 4); put32(&image[11 * 1024 + 12], 0); put16(&image[11 * 1024 + 16], 1); put32(&image[11 * 1024 + 20], 12); memcpy(&image[12 * 1024], "deep", 4);
    storage_initialize(); storage_device_t device = {.name = "ram-ext4", .block_size = 512, .block_count = 128, .read = image_read, .write = image_write}; assert(storage_register(&device));
    ext4_fs_t fs; assert(ext4_mount(&fs, 0)); uint32_t inode = 0; uint64_t size = 0;
    assert(ext4_lookup(&fs, 2, "hello", &inode) && inode == 3);
    char output[6] = {0}; assert(ext4_read_file(&fs, inode, 0, output, 5)); assert(memcmp(output, "world", 5) == 0);
    assert(ext4_write_file(&fs, inode, 1, "a", 1));
    memset(output, 0, sizeof(output));
    assert(ext4_read_file(&fs, inode, 0, output, 5));
    assert(memcmp(output, "warld", 5) == 0);
    assert(ext4_write_file(&fs, inode, 1, "o", 1));
    uint8_t growth[5000]; memset(growth, 'x', sizeof(growth));
    assert(ext4_write_file(&fs, inode, 5, growth, sizeof(growth)));
    assert(ext4_inode_size(&fs, inode, &size) && size == 5005);
    uint8_t grown[5000]; memset(grown, 0, sizeof(grown));
    assert(ext4_read_file(&fs, inode, 5, grown, sizeof(grown)) &&
           memcmp(grown, growth, sizeof(growth)) == 0);
    assert(ext4_truncate_file(&fs, inode, 3));
    assert(ext4_inode_size(&fs, inode, &size) && size == 3);
    memset(output, 0, sizeof(output)); assert(ext4_read_file(&fs, 4, 1024U * 12U, output, 5)); assert(memcmp(output, "indir", 5) == 0);
    memset(output, 0, sizeof(output)); assert(ext4_read_file(&fs, 5, 0, output, 4)); assert(memcmp(output, "deep", 4) == 0);
    uint64_t group_size = 0;
    memset(output, 0, sizeof(output)); assert(ext4_inode_size(&fs, 9, &group_size) && group_size == 5);
    assert(ext4_read_file(&fs, 9, 0, output, 5) && memcmp(output, "group", 5) == 0);
    put32(&sb[96], 0x00000080U); put16(&sb[254], 64);
    put32(&image[2 * 1024 + 40], 0); put32(&image[2 * 1024 + 64 + 8], 13); assert(ext4_mount(&fs, 0));
    put32(&group_file_inode[108], 1); assert(ext4_inode_size(&fs, 9, &group_size)); assert(
                                               group_size == 0x100000005ULL);
    memset(output, 0, sizeof(output)); assert(ext4_read_file(&fs, 9, 0, output, 5) &&
                                            memcmp(output, "group", 5) == 0);
    memset(output, 0xa5, sizeof(output)); assert(ext4_read_file(&fs, 6, 0, output, 5));
    assert(output[0] == 0 && output[1] == 0 && output[2] == 0 && output[3] == 0 && output[4] == 0);
    image[2 * 512 + 56] = 0; assert(!ext4_mount(&fs, 0)); return 0;
}
