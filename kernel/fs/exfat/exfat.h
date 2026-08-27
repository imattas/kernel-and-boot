#ifndef OS_KERNEL_FS_EXFAT_H
#define OS_KERNEL_FS_EXFAT_H
#include <stdint.h>

typedef struct {
    uint32_t device;
    uint32_t fat_start;
    uint32_t fat_sectors;
    uint32_t heap_start;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint64_t volume_sectors;
    uint8_t mounted;
} exfat_fs_t;

int exfat_mount(exfat_fs_t *fs, uint32_t device);
int exfat_lookup(exfat_fs_t *fs, const char *name, uint32_t *first_cluster,
                 uint64_t *size, uint8_t *no_fat_chain);
int exfat_lookup_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                              const char *name, uint32_t *first_cluster,
                              uint64_t *size, uint8_t *no_fat_chain);
int exfat_read_cluster(exfat_fs_t *fs, uint32_t cluster, void *buffer);
int exfat_read_file(exfat_fs_t *fs, const char *name, uint64_t offset,
                    void *buffer, uint32_t size);
int exfat_read_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                 const char *name, uint64_t offset,
                                 void *buffer, uint32_t size);
int exfat_write_file(exfat_fs_t *fs, const char *name, uint64_t offset,
                     const void *buffer, uint32_t size);
int exfat_write_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                  const char *name, uint64_t offset,
                                  const void *buffer, uint32_t size);
int exfat_truncate_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                     const char *name, uint64_t size);

#endif
