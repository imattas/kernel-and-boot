#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../kernel/fs/xfs/xfs.h"
#include "../../../kernel/drivers/storage/storage.h"

static uint8_t image[512 * 128];
static int rd(uint64_t lba, uint32_t count, void *out) {
    if (lba + count > 128) return 0;
    memcpy(out, image + lba * 512U, count * 512U); return 1;
}
static int wr(uint64_t lba, uint32_t count, const void *in) {
    if (lba + count > 128) return 0;
    memcpy(image + lba * 512U, in, count * 512U); return 1;
}
static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)(v >> 32)); p32(p + 4, (uint32_t)v); }

int main(void) {
    memset(image, 0, sizeof(image));
    p32(&image[0], 0x58465342); p32(&image[4], 4096); p64(&image[8], 16);
    p16(&image[102], 512); p32(&image[84], 16); p32(&image[88], 1);
    p16(&image[100], 4); p16(&image[104], 256); p16(&image[106], 16);
    image[108] = 12; image[110] = 8; image[111] = 4; image[112] = 4;
    p64(&image[56], 128);
    uint8_t *block = &image[8 * 4096];
    p16(block, 0x494e); p16(&block[2], 0x4000); block[4] = 2; block[5] = 1;
    p64(&block[56], 22); block[100] = 1; block[101] = 0; p32(&block[102], 128);
    block[106] = 3; p16(&block[107], 0); p32(&block[109], 129); memcpy(&block[113], "old", 3);
    uint8_t *target = block + 1024;
    p16(target, 0x494e); p16(&target[2], 0x4000); target[4] = 2; target[5] = 1;
    p64(&target[56], 6);
    storage_initialize(); storage_device_t device = {
        .name = "xfs-rename", .block_size = 512, .block_count = 128,
        .read = rd, .write = wr
    };
    assert(storage_register(&device)); xfs_fs_t fs; assert(xfs_mount(&fs, 0));
    assert(xfs_rename_local_entry_between(&fs, 128, "old", 132, "new"));
    uint64_t inode = 0;
    assert(!xfs_lookup(&fs, 128, "old", &inode));
    assert(xfs_lookup(&fs, 132, "new", &inode) && inode == 129);
    return 0;
}
