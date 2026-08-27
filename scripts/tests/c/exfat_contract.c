#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../kernel/fs/exfat/exfat.h"
#include "../../../kernel/drivers/storage/storage.h"
static uint8_t image[64 * 512];
static int image_read(uint64_t lba, uint32_t count, void *buffer) { if (lba + count > 64) return 0; memcpy(buffer, &image[lba * 512], count * 512U); return 1; }
static int image_write(uint64_t lba, uint32_t count, const void *buffer) { if (lba + count > 64) return 0; memcpy(&image[lba * 512], buffer, count * 512U); return 1; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static void put64(uint8_t *p, uint64_t v) { put32(p, v); put32(p + 4, v >> 32); }
static uint32_t boot_checksum(void) {
    uint32_t checksum = 0;
    for (uint32_t sector = 0; sector < 11; ++sector)
        for (uint32_t i = 0; i < 512; ++i)
            checksum = ((checksum >> 1) | (checksum << 31)) + image[sector * 512 + i];
    return checksum;
}
static void finish_boot_region(uint32_t start) {
    uint32_t checksum = boot_checksum();
    for (uint32_t i = 0; i < 512; i += 4) put32(&image[(start + 11) * 512 + i], checksum);
}
int main(void) {
    memset(image, 0, sizeof(image)); uint8_t *boot = image;
    boot[0] = 0xeb; boot[2] = 0x90; memcpy(&boot[3], "EXFAT   ", 8);
    put64(&boot[72], 64); put32(&boot[80], 24); put32(&boot[84], 1); put32(&boot[88], 25); put32(&boot[92], 6); put32(&boot[96], 2);
    boot[108] = 9; boot[109] = 0; boot[110] = 1; boot[510] = 0x55; boot[511] = 0xaa;
    memcpy(&image[12 * 512], image, 11 * 512); finish_boot_region(0);
    memcpy(&image[12 * 512], image, 11 * 512); finish_boot_region(12);
    put32(&image[24 * 512 + 2 * 4], 0xfffffff8U); put32(&image[24 * 512 + 3 * 4], 0xffffffffU);
    uint8_t *root = &image[25 * 512]; root[0] = 0x85; root[1] = 2; root[32] = 0xc0; root[33] = 2; root[35] = 9; put32(&root[52], 3); put64(&root[56], 5); root[64] = 0xc1;
    const char *name = "HELLO.TXT"; for (uint32_t i = 0; i < 9; ++i) put16(&root[66 + i * 2], (uint8_t)name[i]); memcpy(&image[26 * 512], "hello", 5);
    storage_initialize(); storage_device_t device = {"ram-exfat", 512, 64, image_read, image_write}; assert(storage_register(&device));
    exfat_fs_t fs; assert(exfat_mount(&fs, 0)); uint32_t cluster = 0; uint64_t size = 0; uint8_t no_fat = 0;
    assert(exfat_lookup(&fs, "hello.txt", &cluster, &size, &no_fat)); assert(cluster == 3 && size == 5 && no_fat);
    char output[6] = {0}; assert(exfat_read_file(&fs, "HELLO.TXT", 0, output, 5)); assert(memcmp(output, "hello", 5) == 0);
    root[96] = 0x85; root[97] = 2; root[128] = 0xc0; root[129] = 2; root[131] = 3; put32(&root[148], 4); put64(&root[152], 0);
    root[160] = 0xc1; put16(&root[162], 'D'); put16(&root[164], 'I'); put16(&root[166], 'R');
    uint8_t *subdir = &image[27 * 512]; subdir[0] = 0x85; subdir[1] = 2;
    subdir[32] = 0xc0; subdir[33] = 2; subdir[35] = 9; put32(&subdir[52], 3); put64(&subdir[56], 5);
    subdir[64] = 0xc1; for (uint32_t i = 0; i < 9; ++i) put16(&subdir[66 + i * 2], (uint8_t)name[i]);
    subdir[96] = 0x85; subdir[97] = 2; subdir[128] = 0xc0; subdir[129] = 2; subdir[131] = 4;
    put32(&subdir[148], 7); put64(&subdir[152], 4); subdir[160] = 0xc1;
    put16(&subdir[162], 'c'); put16(&subdir[164], 'a'); put16(&subdir[166], 'f'); put16(&subdir[168], 0x00e9);
    put32(&image[24 * 512 + 4 * 4], 0xfffffff8U);
    put32(&image[24 * 512 + 7 * 4], 0xfffffff8U); memcpy(&image[30 * 512], "utf8", 4);
    assert(exfat_lookup_in_directory(&fs, 4, "hello.txt", &cluster, &size, &no_fat) &&
           cluster == 3 && size == 5);
    memset(output, 0, sizeof(output)); assert(exfat_read_file_in_directory(&fs, 4, "HELLO.TXT", 0, output, 5) &&
                                             memcmp(output, "hello", 5) == 0);
    assert(exfat_lookup_in_directory(&fs, 4, "caf\xc3\xa9", &cluster, &size, &no_fat) &&
           cluster == 7 && size == 4);
    memset(output, 0, sizeof(output)); assert(exfat_read_file_in_directory(&fs, 4, "caf\xc3\xa9", 0, output, 4) &&
                                             memcmp(output, "utf8", 4) == 0);
    image[11 * 512] ^= 1; assert(!exfat_mount(&fs, 0)); image[11 * 512] ^= 1;
    image[0] = 0; assert(!exfat_mount(&fs, 0)); return 0;
}
