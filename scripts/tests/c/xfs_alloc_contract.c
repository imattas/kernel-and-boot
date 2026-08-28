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
static int rd(uint64_t lba, uint32_t count, void *buffer) {
    memcpy(buffer, &image[lba * 512U], count * 512U); return 1;
}
static int wr(uint64_t lba, uint32_t count, const void *buffer) {
    memcpy(&image[lba * 512U], buffer, count * 512U); return 1;
}

int main(void) {
    memset(image, 0, sizeof(image));
    p32(&image[0], 0x58465342U); p32(&image[4], 4096); p64(&image[8], 16);
    p16(&image[102], 512); p32(&image[84], 16); p32(&image[88], 1);
    p16(&image[100], 4); p16(&image[104], 256); p16(&image[106], 16);
    image[108] = 12; image[110] = 8; image[111] = 4; image[112] = 4; p64(&image[56], 128);
    uint8_t *agf = &image[4096];
    p32(&agf[0], 0x58414746U); p32(&agf[16], 2); p32(&agf[24], 1);
    p32(&agf[40], 3); p32(&agf[44], 3);
    uint8_t *bno = &image[8192];
    p32(&bno[0], 0x58414254U); p32(&bno[4], 0); p32(&bno[8], 1);
    p32(&bno[16], 10); p32(&bno[20], 3);
    storage_initialize();
    storage_device_t device = {"xfs-alloc", 512, 128, rd, wr, 0, 0, 0, 0};
    assert(storage_register(&device));
    xfs_fs_t fs;
    assert(xfs_mount(&fs, 0));
    uint64_t start = 0;
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&bno[8]) == 1 && g32(&bno[16]) == 12 && g32(&bno[20]) == 1);
    assert(g32(&agf[40]) == 1 && g32(&agf[44]) == 1);
    assert(xfs_free_extent(&fs, 10, 2));
    assert(g32(&bno[8]) == 1 && g32(&bno[16]) == 10 && g32(&bno[20]) == 3);
    assert(g32(&agf[40]) == 3 && g32(&agf[44]) == 3);
    assert(xfs_allocate_extent(&fs, 0, 3, &start) && start == 10);
    assert(g32(&bno[8]) == 0 && g32(&agf[40]) == 0 && g32(&agf[44]) == 0);
    p32(&bno[8], 1); p32(&bno[16], 10); p32(&bno[20], 3); p32(&agf[40], 2);
    assert(!xfs_allocate_extent(&fs, 0, 1, &start));
    assert(g32(&bno[8]) == 1 && g32(&bno[16]) == 10 && g32(&bno[20]) == 3 &&
           g32(&agf[40]) == 2);
    p32(&agf[4], 1); p32(&agf[24], 0); p32(&agf[28], 1);
    p32(&bno[0], 0x41425442U); p16(&bno[4], 0); p16(&bno[6], 1);
    p32(&bno[16], 10); p32(&bno[20], 3); p32(&agf[40], 3); p32(&agf[44], 3);
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(((uint16_t)bno[6] << 8 | bno[7]) == 1 && g32(&bno[16]) == 12 &&
           g32(&bno[20]) == 1 && g32(&agf[40]) == 1);
    assert(xfs_free_extent(&fs, 10, 2) && g32(&bno[16]) == 10 &&
           g32(&bno[20]) == 3 && g32(&agf[40]) == 3);
    uint8_t *real_root = &image[2U * 4096U];
    uint8_t *real_leaf = &image[3U * 4096U];
    memset(real_root, 0, 4096); memset(real_leaf, 0, 4096);
    p32(&agf[16], 2); p32(&agf[28], 2); p32(&agf[40], 3); p32(&agf[44], 3);
    p32(real_root, 0x41425442U); p16(&real_root[4], 1); p16(&real_root[6], 1);
    p32(&real_root[16], 10); p32(&real_root[20], 3);
    p32(&real_root[2736], 3);
    p32(real_leaf, 0x41425442U); p16(&real_leaf[4], 0); p16(&real_leaf[6], 1);
    p32(&real_leaf[16], 10); p32(&real_leaf[20], 3);
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&real_leaf[16]) == 12 && g32(&real_leaf[20]) == 1 &&
           g32(&real_root[16]) == 12 && g32(&real_root[20]) == 1 &&
           g32(&agf[40]) == 1);
    assert(xfs_free_extent(&fs, 10, 2) && g32(&real_leaf[16]) == 10 &&
           g32(&real_leaf[20]) == 3 && g32(&real_root[16]) == 10 &&
           g32(&real_root[20]) == 3 && g32(&agf[40]) == 3);
    uint8_t *real_leaf2 = &image[4U * 4096U];
    memset(real_root, 0, 4096); memset(real_leaf, 0, 4096);
    memset(real_leaf2, 0, 4096);
    p32(&agf[16], 2); p32(&agf[28], 2); p32(&agf[40], 4); p32(&agf[44], 2);
    p32(real_root, 0x41425442U); p16(&real_root[4], 1); p16(&real_root[6], 2);
    p32(&real_root[16], 10); p32(&real_root[20], 2);
    p32(&real_root[24], 14); p32(&real_root[28], 2);
    p32(&real_root[2736], 3); p32(&real_root[2740], 4);
    p32(real_leaf, 0x41425442U); p16(&real_leaf[4], 0); p16(&real_leaf[6], 1);
    p32(&real_leaf[16], 10); p32(&real_leaf[20], 2);
    p32(real_leaf2, 0x41425442U); p16(&real_leaf2[4], 0); p16(&real_leaf2[6], 1);
    p32(&real_leaf2[16], 14); p32(&real_leaf2[20], 2);
    assert(xfs_free_extent(&fs, 12, 2));
    assert(((uint16_t)real_root[6] << 8 | real_root[7]) == 1 &&
           g32(&real_root[16]) == 10 && g32(&real_root[20]) == 6 &&
           g32(&real_root[2736]) == 3 && g32(&real_leaf[16]) == 10 &&
           g32(&real_leaf[20]) == 6 && g32(&agf[40]) == 6);
    uint8_t *deep_index = &image[4U * 4096U];
    uint8_t *deep_index2 = &image[5U * 4096U];
    uint8_t *deep_leaf = &image[6U * 4096U];
    memset(real_root, 0, 4096); memset(real_leaf, 0, 4096);
    memset(deep_index, 0, 4096); memset(deep_index2, 0, 4096);
    memset(deep_leaf, 0, 4096);
    p32(&agf[16], 2); p32(&agf[28], 3); p32(&agf[40], 3); p32(&agf[44], 3);
    p32(real_root, 0x41425442U); p16(&real_root[4], 3); p16(&real_root[6], 1);
    p32(&real_root[16], 10); p32(&real_root[20], 3); p32(&real_root[2736], 4);
    p32(deep_index, 0x41425442U); p16(&deep_index[4], 2); p16(&deep_index[6], 1);
    p32(&deep_index[16], 10); p32(&deep_index[20], 3); p32(&deep_index[2736], 5);
    p32(deep_index2, 0x41425442U); p16(&deep_index2[4], 1); p16(&deep_index2[6], 1);
    p32(&deep_index2[16], 10); p32(&deep_index2[20], 3); p32(&deep_index2[2736], 6);
    p32(deep_leaf, 0x41425442U); p16(&deep_leaf[4], 0); p16(&deep_leaf[6], 1);
    p32(&deep_leaf[16], 10); p32(&deep_leaf[20], 3);
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&deep_leaf[16]) == 12 && g32(&deep_leaf[20]) == 1 &&
           g32(&deep_index[16]) == 12 && g32(&deep_index2[16]) == 12 &&
           g32(&real_root[16]) == 12 && g32(&agf[40]) == 1 &&
           g32(&agf[44]) == 1);
    assert(xfs_free_extent(&fs, 10, 2) && g32(&deep_leaf[16]) == 10 &&
           g32(&deep_leaf[20]) == 3 && g32(&deep_index[16]) == 10 &&
           g32(&deep_index2[16]) == 10 && g32(&real_root[16]) == 10 &&
           g32(&agf[40]) == 3 && g32(&agf[44]) == 3);
    return 0;
}
