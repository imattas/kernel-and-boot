#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "kernel/fs/xfs/xfs.h"
#include "kernel/drivers/storage/storage.h"

static uint8_t image[512U * 256U];
static uint8_t snapshot[512U * 256U];
static uint32_t flush_calls;
static uint32_t write_calls;
static uint32_t fail_write_call = UINT32_MAX;
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
    if (lba + n > 256U) return 0;
    memcpy(p, image + lba * 512U, n * 512U); return 1;
}
static int wr(uint64_t lba, uint32_t n, const void *p) {
    if (lba + n > 256U) return 0;
    if (write_calls++ == fail_write_call) {
        fail_write_call = UINT32_MAX;
        return 0;
    }
    memcpy(image + lba * 512U, p, n * 512U); return 1;
}
static int flush(void *context) {
    (void)context; ++flush_calls; return 1;
}
static void leaf(uint8_t *p, uint32_t start, uint32_t count) {
    p32(p, 0x41425442U); p16(&p[4], 0); p16(&p[6], 1);
    p32(&p[16], start); p32(&p[20], count);
}

int main(void) {
    memset(image, 0, sizeof(image));
    p32(&image[0], 0x58465342U); p32(&image[4], 4096); p64(&image[8], 32);
    p64(&image[40], 20); p16(&image[102], 512); p32(&image[84], 16); p32(&image[88], 2);
    p32(&image[96], 12);
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
                               .block_count = 256, .read = rd, .write = wr,
                               .flush = flush};
    assert(storage_register(&device));
    xfs_fs_t fs;
    assert(xfs_mount(&fs, 0));
    uint64_t start = 0;
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&image[4096 + 52]) == 1 && g32(&image[4096 + 56]) == 1);
    assert(g32(&image[2U * 4096U + 16]) == 12 &&
           g32(&image[3U * 4096U + 16]) == 12);
    assert(xfs_free_extent(&fs, 10, 2));
    assert(flush_calls >= 2);
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
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&bno_leaf[16]) == 12 && g32(&cnt_leaf[16]) == 12 &&
           g32(&bno_root[16]) == 12 && g32(&cnt_root[16]) == 12 &&
           g32(&agf[52]) == 1 && g32(&agf[56]) == 1);
    assert(xfs_free_extent(&fs, 10, 2));
    assert(g32(&bno_leaf[16]) == 10 && g32(&cnt_leaf[16]) == 10 &&
           g32(&bno_root[16]) == 10 && g32(&cnt_root[16]) == 10 &&
           g32(&agf[52]) == 3 && g32(&agf[56]) == 3);
    assert(xfs_allocate_extent(&fs, 0, 3, &start) && start == 10);
    assert(g32(&bno_leaf[6]) == 0 && g32(&cnt_leaf[6]) == 0 &&
           g32(&agf[52]) == 0 && g32(&agf[56]) == 0);
    assert(xfs_free_extent(&fs, 10, 3));
    assert(bno_leaf[6] == 0 && bno_leaf[7] == 1 &&
           cnt_leaf[6] == 0 && cnt_leaf[7] == 1 &&
           g32(&agf[52]) == 3 && g32(&agf[56]) == 3);
    p32(&bno_root[16], 11);
    assert(!xfs_mount(&fs, 0));
    /* Bounded multi-child level-2 transaction: both indexes contain two
     * leaves, then allocation collapses and release repopulates the root. */
    p32(&bno_root[16], 10);
    memset(bno_root, 0, 4096); memset(cnt_root, 0, 4096);
    memset(&image[5U * 4096U], 0, 4096); memset(&image[6U * 4096U], 0, 4096);
    memset(&image[4U * 4096U], 0, 4096); memset(&image[7U * 4096U], 0, 4096);
    p32(bno_root, 0x41425442U); p16(&bno_root[4], 1); p16(&bno_root[6], 2);
    p32(&bno_root[16], 10); p32(&bno_root[20], 2);
    p32(&bno_root[24], 13); p32(&bno_root[28], 3);
    p32(&bno_root[2736], 5); p32(&bno_root[2740], 6);
    p32(cnt_root, 0x41425442U); p16(&cnt_root[4], 1); p16(&cnt_root[6], 2);
    p32(&cnt_root[16], 10); p32(&cnt_root[20], 2);
    p32(&cnt_root[24], 13); p32(&cnt_root[28], 3);
    p32(&cnt_root[2736], 4); p32(&cnt_root[2740], 7);
    leaf(&image[5U * 4096U], 10, 2); leaf(&image[6U * 4096U], 13, 3);
    leaf(&image[4U * 4096U], 10, 2); leaf(&image[7U * 4096U], 13, 3);
    p32(&agf[52], 5); p32(&agf[56], 3);
    assert(xfs_mount(&fs, 0));
    assert(xfs_allocate_extent(&fs, 0, 2, &start) && start == 10);
    assert(g32(&agf[52]) == 3 && g32(&agf[56]) == 3 &&
           g32(&bno_root[16]) == 13 && g32(&cnt_root[16]) == 13 &&
           g32(&bno_root[2736]) == 5 && g32(&cnt_root[2736]) == 4);
    assert(xfs_free_extent(&fs, 10, 2));
    assert(g32(&agf[52]) == 5 && g32(&agf[56]) == 3 &&
           ((bno_root[6] << 8) | bno_root[7]) == 2 &&
           ((cnt_root[6] << 8) | cnt_root[7]) == 2 &&
           g32(&bno_root[16]) == 10 && g32(&bno_root[24]) == 13);
    /* Four-child fan-out also collapses and restores without losing the
     * retained child pointers. */
    memset(bno_root, 0, 4096); memset(cnt_root, 0, 4096);
    memset(&image[5U * 4096U], 0, 4096); memset(&image[6U * 4096U], 0, 4096);
    memset(&image[8U * 4096U], 0, 4096); memset(&image[9U * 4096U], 0, 4096);
    memset(&image[4U * 4096U], 0, 4096); memset(&image[7U * 4096U], 0, 4096);
    memset(&image[10U * 4096U], 0, 4096); memset(&image[11U * 4096U], 0, 4096);
    p32(bno_root, 0x41425442U); p16(&bno_root[4], 1); p16(&bno_root[6], 4);
    p32(cnt_root, 0x41425442U); p16(&cnt_root[4], 1); p16(&cnt_root[6], 4);
    p32(&bno_root[16], 2); p32(&bno_root[20], 1);
    p32(&bno_root[24], 5); p32(&bno_root[28], 1);
    p32(&bno_root[32], 8); p32(&bno_root[36], 1);
    p32(&bno_root[40], 10); p32(&bno_root[44], 1);
    p32(&cnt_root[16], 2); p32(&cnt_root[20], 1);
    p32(&cnt_root[24], 5); p32(&cnt_root[28], 1);
    p32(&cnt_root[32], 8); p32(&cnt_root[36], 1);
    p32(&cnt_root[40], 10); p32(&cnt_root[44], 1);
    p32(&bno_root[2736], 5); p32(&bno_root[2740], 6);
    p32(&bno_root[2744], 8); p32(&bno_root[2748], 9);
    p32(&cnt_root[2736], 4); p32(&cnt_root[2740], 7);
    p32(&cnt_root[2744], 10); p32(&cnt_root[2748], 11);
    leaf(&image[5U * 4096U], 2, 1); leaf(&image[6U * 4096U], 5, 1);
    leaf(&image[8U * 4096U], 8, 1); leaf(&image[9U * 4096U], 10, 1);
    leaf(&image[4U * 4096U], 2, 1); leaf(&image[7U * 4096U], 5, 1);
    leaf(&image[10U * 4096U], 8, 1); leaf(&image[11U * 4096U], 10, 1);
    p32(&agf[52], 4); p32(&agf[56], 1);
    assert(xfs_mount(&fs, 0));
    assert(xfs_allocate_extent(&fs, 0, 1, &start) && start == 2);
    assert(((bno_root[6] << 8) | bno_root[7]) == 3 &&
           g32(&bno_root[2736]) == 5 && g32(&bno_root[2744]) == 8);
    assert(xfs_free_extent(&fs, 2, 1));
    assert(((bno_root[6] << 8) | bno_root[7]) == 4 &&
           g32(&bno_root[16]) == 2 && g32(&bno_root[40]) == 10);
    assert(xfs_allocate_extent(&fs, 0, 1, &start) && start == 2);
    assert(xfs_allocate_extent(&fs, 0, 1, &start) && start == 5);
    assert(xfs_allocate_extent(&fs, 0, 1, &start) && start == 8);
    assert(xfs_allocate_extent(&fs, 0, 1, &start) && start == 10);
    assert(((bno_root[6] << 8) | bno_root[7]) == 0 && g32(&agf[52]) == 0);
    assert(xfs_free_extent(&fs, 2, 1));
    assert(((bno_root[6] << 8) | bno_root[7]) == 1 &&
           g32(&bno_root[16]) == 2 && g32(&agf[52]) == 1);
    memcpy(snapshot, image, sizeof(image));
    fail_write_call = write_calls + 15U;
    assert(!xfs_allocate_extent(&fs, 0, 1, &start));
    assert(memcmp(snapshot, image, sizeof(image)) == 0);
    /* Simulate a committed journal surviving a crash: mount replays the
     * payload before validating the allocation-group indexes. */
    uint8_t *journal = &image[20U * 4096U];
    uint8_t *payload = &image[21U * 4096U];
    memset(journal, 0, 4096); memset(payload, 0, 4096);
    p32(journal, 0x584A4E4CU); p64(&journal[4], 1); p32(&journal[12], 2);
    p32(&journal[16], 1); p64(&journal[24], 5);
    leaf(payload, 2, 1); memset(&image[5U * 4096U], 0, 4096);
    assert(xfs_mount(&fs, 0));
    assert(g32(&image[5U * 4096U + 16]) == 2 && g32(&image[5U * 4096U + 20]) == 1);
    assert(g32(journal) == 0);
    return 0;
}
