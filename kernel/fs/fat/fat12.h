#ifndef OS_KERNEL_FS_FAT12_H
#define OS_KERNEL_FS_FAT12_H

#include <stdint.h>

#define FAT12_SECTOR_SIZE 512U
#define FAT12_MAX_FAT_SECTORS 9U

typedef struct {
    uint32_t device;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t sectors_per_fat;
    uint32_t root_start;
    uint32_t data_start;
    uint32_t data_clusters;
    uint8_t fat[FAT12_MAX_FAT_SECTORS * FAT12_SECTOR_SIZE];
    uint8_t mounted;
} fat12_fs_t;

int fat12_mount(fat12_fs_t *fs, uint32_t device);
int fat12_lookup(fat12_fs_t *fs, const char short_name[11],
                 uint16_t *first_cluster, uint32_t *size);
int fat12_read_cluster(fat12_fs_t *fs, uint16_t cluster, void *buffer);
int fat12_read_file(fat12_fs_t *fs, const char short_name[11],
                    uint32_t offset, void *buffer, uint32_t size);

#endif
