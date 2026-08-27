#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../kernel/drivers/storage/storage.h"
#include "../../../kernel/fs/fat/fat32.h"
#include "../../../kernel/fs/fat/fat32_vfs.h"

#define TOTAL_SECTORS 66072U
#define FAT_START 32U
#define FAT_SECTORS 512U
#define DATA_START (FAT_START + FAT_SECTORS)
#define DATA_CLUSTERS (TOTAL_SECTORS - DATA_START)
#define IMAGE_BYTES ((size_t)TOTAL_SECTORS * 512U)

static uint8_t *image;
static storage_device_t device;
void *kmalloc(uint64_t size) { return malloc((size_t)size); }
void kfree(void *pointer) { free(pointer); }

static void store16(uint8_t *p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
static void store32(uint8_t *p, uint32_t value) {
    store16(p, (uint16_t)value); store16(p + 2, (uint16_t)(value >> 16));
}
static uint32_t load32_test(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int read_image(uint64_t lba, uint32_t count, void *buffer) {
    memcpy(buffer, image + lba * 512U, (size_t)count * 512U); return 1;
}
static int write_image(uint64_t lba, uint32_t count, const void *buffer) {
    memcpy(image + lba * 512U, buffer, (size_t)count * 512U); return 1;
}
void storage_initialize(void) { }
int storage_register(const storage_device_t *input) { device = *input; return 1; }
uint32_t storage_device_count(void) { return 1; }
const storage_device_t *storage_device_at(uint32_t index) { return index == 0 ? &device : 0; }
int storage_read(uint32_t index, uint64_t lba, uint32_t count, void *buffer) {
    return index == 0 && lba + count <= TOTAL_SECTORS && read_image(lba, count, buffer);
}
int storage_write(uint32_t index, uint64_t lba, uint32_t count, const void *buffer) {
    return index == 0 && lba + count <= TOTAL_SECTORS && write_image(lba, count, buffer);
}

static void set_fat(uint32_t cluster, uint32_t value) {
    store32(image + FAT_START * 512U + cluster * 4U, value);
}
static uint8_t short_checksum(const char name[11]) {
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < 11; ++i)
        checksum = (uint8_t)(((checksum & 1U) ? 0x80U : 0U) + (checksum >> 1) + (uint8_t)name[i]);
    return checksum;
}

static int fail(const char *message) { fprintf(stderr, "fat32 contract: %s\n", message); return 1; }

int main(void) {
    image = calloc(1, IMAGE_BYTES);
    if (!image) return fail("allocation failed");
    uint8_t *boot = image;
    store16(boot + 11, 512); boot[13] = 1; store16(boot + 14, FAT_START);
    boot[16] = 1; store32(boot + 32, TOTAL_SECTORS); store32(boot + 36, FAT_SECTORS);
    store16(boot + 42, 0); store32(boot + 44, 2); store16(boot + 48, 1); boot[510] = 0x55; boot[511] = 0xaa;
    store32(image + 512, 0x41615252); store32(image + 512 + 484, 0x61417272);
    store32(image + 512 + 488, DATA_CLUSTERS - 6U); store32(image + 512 + 492, 8);
    store32(image + 512 + 508, 0xaa550000);
    set_fat(0, 0x0ffffff8); set_fat(1, 0x0fffffff); set_fat(2, 0x0fffffff);
    set_fat(3, 4); set_fat(4, 0x0fffffff);
    set_fat(5, 0x0fffffff); set_fat(6, 0x0fffffff); set_fat(7, 0x0fffffff);
    uint8_t *root = image + DATA_START * 512U;
    memcpy(root, "CHAIN   BIN", 11); root[11] = 0x20; store16(root + 26, 3);
    store32(root + 28, 700);
    memcpy(root + 32, "SUBDIR     ", 11); root[43] = 0x10; store16(root + 32 + 26, 5);
    const char long_short[11] = {'L','O','N','G','N','A','~','1','T','X','T'};
    root[64] = 0x41; root[75] = 0x0f; root[77] = short_checksum(long_short);
    const char *long_name = "LongName.txt";
    for (uint32_t i = 0; i < 13; ++i) {
        uint16_t value = i < 12 ? (uint8_t)long_name[i] : 0;
        if (i < 5) store16(&root[65 + i * 2], value);
        else if (i < 11) store16(&root[78 + (i - 5) * 2], value);
        else store16(&root[92 + (i - 11) * 2], value);
    }
    memcpy(root + 96, long_short, 11); root[107] = 0x20; store16(root + 96 + 26, 7);
    store32(root + 96 + 28, 4); root[128] = 0;
    memcpy(image + (DATA_START + 5) * 512U, "long", 4);
    uint8_t *subdir = image + (DATA_START + 3) * 512U;
    memcpy(subdir, "NESTED  TXT", 11); subdir[11] = 0x20; store16(subdir + 26, 6);
    store32(subdir + 28, 5); subdir[32] = 0;
    memcpy(image + (DATA_START + 4) * 512U, "child", 5);
    for (uint32_t i = 0; i < 512; ++i) image[(DATA_START + 1) * 512U + i] = (uint8_t)i;
    for (uint32_t i = 0; i < 188; ++i) image[(DATA_START + 2) * 512U + i] = (uint8_t)(i + 0x80);
    device.name = "ram-fat32"; device.block_size = 512; device.block_count = TOTAL_SECTORS;
    device.read = read_image; device.write = write_image;
    fat32_fs_t fs;
    const char name[11] = {'C','H','A','I','N',' ',' ',' ','B','I','N'};
    uint32_t cluster, size; uint8_t output[1100]; uint8_t is_directory = 0;
    if (!fat32_mount(&fs, 0)) return fail("mount failed");
    if (!fs.fsinfo_valid || fs.fsinfo_free_count != DATA_CLUSTERS - 6U ||
        fs.fsinfo_next_free != 8) return fail("FSInfo mount failed");
    if (!fat32_lookup(&fs, name, &cluster, &size) || cluster != 3 || size != 700)
        return fail("lookup failed");
    if (!fat32_read_file(&fs, name, 0, output, 700) ||
        output[0] != 0 || output[511] != 0xff || output[512] != 0x80 || output[699] != 0x3b) {
        fprintf(stderr, "values %u %u %u %u\n", output[0], output[511], output[512], output[699]);
        return fail("cluster-chain read failed");
    }
    const uint8_t update[] = "update";
    if (!fat32_write_file(&fs, name, 510, update, sizeof(update) - 1) ||
        !fat32_read_file(&fs, name, 510, output, sizeof(update) - 1) ||
        memcmp(output, update, sizeof(update) - 1) != 0)
        return fail("cross-cluster write failed");
    uint8_t appended[400];
    for (uint32_t i = 0; i < sizeof(appended); ++i) appended[i] = (uint8_t)(i ^ 0x3cU);
    if (!fat32_append_file(&fs, name, appended, sizeof(appended)) ||
        !fat32_lookup(&fs, name, &cluster, &size) || size != 1100 ||
        !fat32_read_file(&fs, name, 700, output, sizeof(appended)) ||
        memcmp(output, appended, sizeof(appended)) != 0)
        return fail("append growth failed");
    if (fs.fsinfo_free_count != DATA_CLUSTERS - 7U) return fail("FSInfo allocation failed");
    vfs_node_t *vfs_root = vfs_node_create("fat", VFS_NODE_DIRECTORY, 0, 0, 0555);
    if (!vfs_root || !fat32_vfs_attach_file(&fs, vfs_root, name, "chain.bin"))
        return fail("VFS attach failed");
    vfs_node_t *vfs_file = vfs_lookup_path(vfs_root, "/chain.bin");
    const uint8_t vfs_append[] = "vfs";
    if (!vfs_file || vfs_node_write(vfs_file, 1100, vfs_append, 3) != 3 ||
        !vfs_node_read(vfs_file, 1100, output, 3) ||
        memcmp(output, vfs_append, 3) != 0)
        return fail("VFS append failed");
    if (!vfs_node_truncate(vfs_file, 512) ||
        vfs_node_read(vfs_file, 512, output, 1) != 0)
        return fail("VFS truncate failed");
    vfs_node_release(vfs_file);
    if (!fat32_truncate_file(&fs, name, 512) ||
        !fat32_lookup(&fs, name, &cluster, &size) || size != 512 ||
        !fat32_read_file(&fs, name, 0, output, 512) ||
        output[0] != 0 || output[511] != 'p' ||
        load32_test(image + FAT_START * 512U + 4U * 4U) != 0 ||
        load32_test(image + FAT_START * 512U + 8U * 4U) != 0)
        return fail("truncate shrink failed");
    const char directory_name[11] = {'S','U','B','D','I','R',' ',' ',' ',' ',' '};
    const char nested_name[11] = {'N','E','S','T','E','D',' ',' ','T','X','T'};
    if (!fat32_lookup_in_directory(&fs, 2, directory_name, &cluster, &size, &is_directory) ||
        !is_directory || cluster != 5 || size != 0 ||
        !fat32_read_file_in_directory(&fs, cluster, nested_name, 0, output, 5) ||
        memcmp(output, "child", 5) != 0) return fail("subdirectory traversal failed");
    memset(output, 0, sizeof(output));
    if (!fat32_lookup_name_in_directory(&fs, 2, "LongName.txt", &cluster, &size, &is_directory) ||
        cluster != 7 || size != 4 || is_directory) return fail("long filename lookup failed");
    if (!fat32_read_named_file_in_directory(&fs, 2, "LongName.txt", 0, output, 4) ||
        memcmp(output, "long", 4) != 0) return fail("long filename read failed");
    const char created_name[11] = {'C','R','E','A','T','E','D',' ','T','X','T'};
    if (!fat32_create_file_in_directory(&fs, 2, created_name, 0x20) ||
        !fat32_set_attributes_in_directory(&fs, 2, created_name, 0x21) ||
        !fat32_lookup_in_directory(&fs, 2, created_name, &cluster, &size, &is_directory) ||
        size != 0 || is_directory || !fat32_unlink_file_in_directory(&fs, 2, created_name) ||
        fat32_lookup_in_directory(&fs, 2, created_name, &cluster, &size, &is_directory))
        return fail("directory create/unlink failed");
    set_fat(3, 0x0ffffff7);
    fs.fat_sector_valid = 0;
    if (fat32_read_file(&fs, name, 0, output, sizeof(output))) return fail("bad cluster accepted");
    free(image);
    puts("fat32 contract: PASS");
    return 0;
}
