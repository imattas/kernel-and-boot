#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../kernel/drivers/storage/storage.h"
#include "../../../kernel/fs/fat/fat32.h"

#define TOTAL_SECTORS 66072U
#define FAT_START 32U
#define FAT_SECTORS 512U
#define DATA_START (FAT_START + FAT_SECTORS)
#define DATA_CLUSTERS (TOTAL_SECTORS - DATA_START)
#define IMAGE_BYTES ((size_t)TOTAL_SECTORS * 512U)

static uint8_t *image;
static storage_device_t device;

static void store16(uint8_t *p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
static void store32(uint8_t *p, uint32_t value) {
    store16(p, (uint16_t)value); store16(p + 2, (uint16_t)(value >> 16));
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

static int fail(const char *message) { fprintf(stderr, "fat32 contract: %s\n", message); return 1; }

int main(void) {
    image = calloc(1, IMAGE_BYTES);
    if (!image) return fail("allocation failed");
    uint8_t *boot = image;
    store16(boot + 11, 512); boot[13] = 1; store16(boot + 14, FAT_START);
    boot[16] = 1; store32(boot + 32, TOTAL_SECTORS); store32(boot + 36, FAT_SECTORS);
    store16(boot + 42, 0); store32(boot + 44, 2); boot[510] = 0x55; boot[511] = 0xaa;
    set_fat(0, 0x0ffffff8); set_fat(1, 0x0fffffff); set_fat(2, 0x0fffffff);
    set_fat(3, 4); set_fat(4, 0x0fffffff);
    uint8_t *root = image + DATA_START * 512U;
    memcpy(root, "CHAIN   BIN", 11); root[11] = 0x20; store16(root + 26, 3);
    store32(root + 28, 700); root[32] = 0;
    for (uint32_t i = 0; i < 512; ++i) image[(DATA_START + 1) * 512U + i] = (uint8_t)i;
    for (uint32_t i = 0; i < 188; ++i) image[(DATA_START + 2) * 512U + i] = (uint8_t)(i + 0x80);
    device.name = "ram-fat32"; device.block_size = 512; device.block_count = TOTAL_SECTORS;
    device.read = read_image; device.write = write_image;
    fat32_fs_t fs;
    const char name[11] = {'C','H','A','I','N',' ',' ',' ','B','I','N'};
    uint32_t cluster, size; uint8_t output[700];
    if (!fat32_mount(&fs, 0)) return fail("mount failed");
    if (!fat32_lookup(&fs, name, &cluster, &size) || cluster != 3 || size != 700)
        return fail("lookup failed");
    if (!fat32_read_file(&fs, name, 0, output, sizeof(output)) ||
        output[0] != 0 || output[511] != 0xff || output[512] != 0x80 || output[699] != 0x3b) {
        fprintf(stderr, "values %u %u %u %u\n", output[0], output[511], output[512], output[699]);
        return fail("cluster-chain read failed");
    }
    set_fat(3, 0x0ffffff7);
    fs.fat_sector_valid = 0;
    if (fat32_read_file(&fs, name, 0, output, sizeof(output))) return fail("bad cluster accepted");
    free(image);
    puts("fat32 contract: PASS");
    return 0;
}
