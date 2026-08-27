#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../kernel/fs/btrfs/btrfs.h"
#include "../../../kernel/drivers/storage/storage.h"
static uint8_t image[512 * 264];
static int rd(uint64_t lba, uint32_t n, void *p) { if (lba + n > 264) return 0; memcpy(p, image + lba * 512, n * 512U); return 1; }
static int wr(uint64_t lba, uint32_t n, const void *p) { if (lba + n > 264) return 0; memcpy(image + lba * 512, p, n * 512U); return 1; }
static void p16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void p32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void p64(uint8_t *p, uint64_t v) { p32(p, v); p32(p + 4, v >> 32); }
static uint32_t crc32c(const uint8_t *data, uint32_t length) { uint32_t crc = ~0U; for (uint32_t i = 0; i < length; ++i) { crc ^= data[i]; for (uint32_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0x82f63b78U & (uint32_t)-(int32_t)(crc & 1U)); } return ~crc; }
static uint32_t name_hash(const char *name) { uint32_t crc = ~1U; for (uint32_t i = 0; name[i]; ++i) { crc ^= (uint8_t)name[i]; for (uint32_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0x82f63b78U & (uint32_t)-(int32_t)(crc & 1U)); } return ~crc; }
int main(void) {
    memset(image, 0, sizeof(image)); uint8_t *s = &image[64 * 1024];
    p64(&s[0x40], 0x4d5f53665248425fULL); p32(&s[0x90], 4096); p32(&s[0x94], 4096); p64(&s[0x70], 128 * 1024); p64(&s[0x50], 8192); p64(&s[0x58], 8192);
    for (uint32_t i = 0; i < 16; ++i) s[0x20 + i] = (uint8_t)(i + 1); p32(&s[0xa0], 93);
    uint8_t *chunk = &s[0x2c0]; p64(chunk, 1); chunk[8] = 0x21; p64(&chunk[9], 0); p64(&chunk[17], 128 * 1024); p64(&chunk[25], 0); p64(&chunk[33], 4096); p64(&chunk[41], 0); p32(&chunk[49], 512); p32(&chunk[53], 512); p32(&chunk[57], 512); p16(&chunk[61], 1); p16(&chunk[63], 0); p64(&chunk[65], 1); p64(&chunk[73], 0); p32(s, crc32c(&s[32], 4096 - 32));
    uint8_t *n = &image[4096]; memcpy(&n[32], &s[0x20], 16); p64(&n[48], 4096); p32(&n[96], 2); n[100] = 0; p64(&n[101], 5); n[109] = 1; p64(&n[110], 0); p32(&n[118], 300); p32(&n[122], 5); p64(&n[126], 5); n[134] = 132; p64(&n[135], 5); p32(&n[143], 400); p32(&n[147], 184); memcpy(&n[300], "hello", 5); p64(&n[576], 16384); p32(n, crc32c(&n[32], 4096 - 32));
    uint8_t *root = &image[8192]; memcpy(&root[32], &s[0x20], 16); p64(&root[48], 8192); p32(&root[96], 1); root[100] = 1; p64(&root[101], 5); root[109] = 1; p64(&root[110], 0); p64(&root[118], 4096); p64(&root[126], 1); p32(root, crc32c(&root[32], 4096 - 32));
    uint8_t *tree = &image[16384]; memcpy(&tree[32], &s[0x20], 16); p64(&tree[48], 16384); p32(&tree[96], 4); tree[100] = 0; p64(&tree[101], 256); tree[109] = 1; p64(&tree[110], 0); p32(&tree[118], 600); p32(&tree[122], 160); p64(&tree[126], 256); tree[134] = 84; p64(&tree[135], name_hash("hello")); p32(&tree[143], 800); p32(&tree[147], 35); p64(&tree[151], 256); tree[159] = 108; p64(&tree[160], 0); p32(&tree[168], 400); p32(&tree[172], 53); p64(&tree[176], 256); tree[184] = 108; p64(&tree[185], 5); p32(&tree[193], 500); p32(&tree[197], 53); p64(&tree[600 + 16], 10); p32(&tree[600 + 96], 0x8000); p64(&tree[800], 257); tree[808] = 1; p64(&tree[809], 0); p16(&tree[825], 0); p16(&tree[827], 5); tree[829] = 8; memcpy(&tree[830], "hello", 5); p64(&tree[400], 1); p64(&tree[408], 5); tree[416] = tree[417] = tree[418] = 0; tree[420] = 1; p64(&tree[421], 24576); p64(&tree[429], 512); p64(&tree[437], 0); p64(&tree[445], 5); p64(&tree[500], 1); p64(&tree[508], 5); tree[516] = tree[517] = tree[518] = 0; tree[520] = 1; p64(&tree[521], 25088); p64(&tree[529], 512); p64(&tree[537], 0); p64(&tree[545], 5); p32(tree, crc32c(&tree[32], 4096 - 32)); memcpy(&image[24576], "world", 5); memcpy(&image[25088], "again", 5);
    storage_initialize(); storage_device_t d = {"btrfs", 512, 264, rd, wr}; assert(storage_register(&d)); btrfs_fs_t fs; assert(btrfs_mount(&fs, 0)); assert(fs.node_size == 4096); assert(btrfs_resolve_filesystem_tree(&fs)); assert(fs.fs_root_bytenr == 16384); uint64_t file_size = 0, child = 0; uint32_t mode = 0; assert(btrfs_inode_stat(&fs, 16384, 256, &file_size, &mode) && file_size == 10 && mode == 0x8000); assert(btrfs_lookup_dir(&fs, 16384, 256, "hello", &child) && child == 257); char output[11] = {0}; assert(btrfs_read_item(&fs, 8192, 5, 1, 0, output, 5)); assert(btrfs_read_file(&fs, 16384, 256, 0, output, 10)); assert(memcmp(output, "worldagain", 10) == 0); s[0x40] = 0; assert(!btrfs_mount(&fs, 0)); return 0;
}
