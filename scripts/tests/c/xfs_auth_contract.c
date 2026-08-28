#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "kernel/fs/xfs/xfs.h"
#include "kernel/drivers/storage/storage.h"

static uint8_t image[512U * 128U];
static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)(v >> 32)); p32(p + 4, (uint32_t)v); }
static uint32_t g32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static int rd(uint64_t lba, uint32_t n, void *p) {
    if (lba + n > 128U) return 0;
    memcpy(p, image + lba * 512U, n * 512U); return 1;
}
static int wr(uint64_t lba, uint32_t n, const void *p) {
    if (lba + n > 128U) return 0;
    memcpy(image + lba * 512U, p, n * 512U); return 1;
}
static void leaf(uint8_t *p, uint32_t start, uint32_t count) {
    p32(p, 0x41425442U); p16(&p[4], 0); p16(&p[6], 1);
    p32(&p[16], start); p32(&p[20], count);
}

int main(void) {
    memset(image, 0, sizeof(image));
    p32(&image[0], 0x58465342U); p32(&image[4], 4096); p64(&image[8], 16);
    p16(&image[102], 512); p32(&image[84], 16); p32(&image[88], 1);
    p16(&image[100], 4); p16(&image[104], 256); p16(&image[106], 16);
    image[108] = 12; image[110] = 8; image[111] = 4; image[112] = 4;
    p64(&image[56], 128);
    uint8_t *agf = &image[4096];
    p32(&agf[0], 0x58414746U); p32(&agf[4], 1); p32(&agf[12], 16);
    p32(&agf[16], 2); p32(&agf[20], 3); p32(&agf[28], 1); p32(&agf[32], 1);
    p32(&agf[52], 3); p32(&agf[56], 3);
    leaf(&image[2U * 4096U], 10, 3); leaf(&image[3U * 4096U], 10, 3);
    storage_initialize();
    storage_device_t device = {.name = "xfs-auth", .block_size = 512,
                               .block_count = 128, .read = rd, .write = wr};
    assert(storage_register(&device));
    xfs_fs_t fs;
    assert(xfs_mount(&fs, 0));
    uint64_t start = 0;
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&image[4096 + 52]) == 1 && g32(&image[4096 + 56]) == 1);
    assert(g32(&image[2U * 4096U + 16]) == 12 &&
           g32(&image[3U * 4096U + 16]) == 12);
    assert(xfs_free_extent(&fs, 10, 2));
    assert(g32(&image[4096 + 52]) == 3 && g32(&image[4096 + 56]) == 3);
    assert(g32(&image[2U * 4096U + 16]) == 10 &&
           g32(&image[3U * 4096U + 16]) == 10);
    assert(!xfs_free_extent(&fs, 11, 2));
    assert(g32(&image[4096 + 52]) == 3 &&
           g32(&image[2U * 4096U + 20]) == 3);
    assert(xfs_allocate_extent(&fs, 0, 3, &start) && start == 10);
    assert(g32(&image[4096 + 52]) == 0 && g32(&image[4096 + 56]) == 0 &&
           image[2U * 4096U + 6] == 0 && image[3U * 4096U + 6] == 0);
    assert(xfs_free_extent(&fs, 10, 3));
    assert(g32(&image[4096 + 52]) == 3 && g32(&image[4096 + 56]) == 3);
    uint8_t *cnt_root = &image[3U * 4096U];
    uint8_t *cnt_leaf = &image[4U * 4096U];
    memset(cnt_root, 0, 4096); memset(cnt_leaf, 0, 4096);
    p32(&agf[20], 3); p32(&agf[32], 2);
    p32(cnt_root, 0x41425442U); p16(&cnt_root[4], 1); p16(&cnt_root[6], 1);
    p32(&cnt_root[16], 10); p32(&cnt_root[20], 3); p32(&cnt_root[2736], 4);
    leaf(cnt_leaf, 10, 3);
    assert(xfs_mount(&fs, 0));
    p32(&cnt_root[16], 11);
    assert(!xfs_mount(&fs, 0));
    p32(&cnt_root[16], 10);
    uint8_t *bno_root = &image[2U * 4096U];
    uint8_t *bno_leaf = &image[5U * 4096U];
    memset(bno_root, 0, 4096); memset(bno_leaf, 0, 4096);
    p32(&agf[28], 2);
    p32(bno_root, 0x41425442U); p16(&bno_root[4], 1); p16(&bno_root[6], 1);
    p32(&bno_root[16], 10); p32(&bno_root[20], 3); p32(&bno_root[2736], 5);
    leaf(bno_leaf, 10, 3);
    assert(xfs_mount(&fs, 0));
    p32(&bno_root[16], 11);
    assert(!xfs_mount(&fs, 0));
    return 0;
}
