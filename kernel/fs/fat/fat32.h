#ifndef OS_KERNEL_FS_FAT32_H
#define OS_KERNEL_FS_FAT32_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

#define FAT32_SECTOR_SIZE 512U
#define FAT32_MAX_SECTORS_PER_CLUSTER 128U

typedef struct {
    uint32_t device;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t root_cluster;
    uint32_t data_clusters;
    uint32_t total_sectors;
    uint8_t fat_sector[FAT32_SECTOR_SIZE];
    uint32_t fat_sector_number;
    uint8_t fat_sector_valid;
    spinlock_t fat_lock;
    spinlock_t write_lock;
    uint8_t mounted;
} fat32_fs_t;

int fat32_mount(fat32_fs_t *fs, uint32_t device);
int fat32_lookup(fat32_fs_t *fs, const char short_name[11],
                 uint32_t *first_cluster, uint32_t *size);
int fat32_lookup_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                              const char short_name[11], uint32_t *first_cluster,
                              uint32_t *size, uint8_t *is_directory);
int fat32_lookup_name_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                   const char *name, uint32_t *first_cluster,
                                   uint32_t *size, uint8_t *is_directory);
int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, void *buffer);
int fat32_read_file(fat32_fs_t *fs, const char short_name[11],
                    uint32_t offset, void *buffer, uint32_t size);
int fat32_write_file(fat32_fs_t *fs, const char short_name[11],
                     uint32_t offset, const void *buffer, uint32_t size);
int fat32_append_file(fat32_fs_t *fs, const char short_name[11],
                      const void *buffer, uint32_t size);
int fat32_truncate_file(fat32_fs_t *fs, const char short_name[11],
                        uint32_t size);
int fat32_read_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                 const char short_name[11], uint32_t offset,
                                 void *buffer, uint32_t size);
int fat32_read_named_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                       const char *name, uint32_t offset,
                                       void *buffer, uint32_t size);
int fat32_write_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                  const char short_name[11], uint32_t offset,
                                  const void *buffer, uint32_t size);

#endif
