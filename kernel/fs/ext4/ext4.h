#ifndef OS_KERNEL_FS_EXT4_H
#define OS_KERNEL_FS_EXT4_H
#include <stdint.h>
#include "../../core/sync/spinlock.h"

typedef struct {
    uint32_t device;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t inodes_per_group;
    uint32_t descriptor_size;
    uint64_t descriptor_block;
    uint64_t groups_count;
    uint64_t block_count;
    spinlock_t write_lock;
    uint8_t has_64bit;
    uint8_t mounted;
} ext4_fs_t;

int ext4_mount(ext4_fs_t *fs, uint32_t device);
int ext4_lookup(ext4_fs_t *fs, uint32_t directory_inode, const char *name,
                uint32_t *inode_number);
int ext4_inode_size(ext4_fs_t *fs, uint32_t inode_number, uint64_t *size);
int ext4_inode_mode(ext4_fs_t *fs, uint32_t inode_number, uint32_t *mode);
int ext4_set_mode(ext4_fs_t *fs, uint32_t inode_number, uint16_t mode);
int ext4_read_file(ext4_fs_t *fs, uint32_t inode_number, uint64_t offset,
                   void *buffer, uint32_t size);
int ext4_write_file(ext4_fs_t *fs, uint32_t inode_number, uint64_t offset,
                    const void *buffer, uint32_t size);
int ext4_truncate_file(ext4_fs_t *fs, uint32_t inode_number, uint64_t size);

#endif
